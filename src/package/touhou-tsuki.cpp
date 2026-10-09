#include "touhou-tsuki.h"
#include "touhou-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"

#include <QScopedPointer>

using namespace TouhouUtils;

namespace {

// ---------------------------------------------------------------- tsuki001

class ThSuoming : public TriggerSkillV2
{
public:
    ThSuoming() : TriggerSkillV2("thsuoming") { events << Damaged << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || times(event, player, data) <= 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // One chance per point of damage or per red basic card; a refusal ends the rest.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int n = times(event, player, *ctx.original_data);
        for (int i = 0; i < n && player->isAlive(); ++i) {
            ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@thsuoming",
                                                            true, true);
            if (!target)
                break;
            room->broadcastSkillInvoke(objectName());
            room->setPlayerChained(target, !target->isChained(), player);
        }
        return false;
    }

private:
    static int times(TriggerEvent event, ServerPlayer *player, const QVariant &data)
    {
        if (event == Damaged)
            return data.value<DamageStruct>().damage;
        // Anyone's red basic cards reaching the discard pile during the owner's turn. The move
        // is dispatched to every character; only the owner's own dispatch counts it.
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!isOwnTurn(player) || move.to_place != Player::DiscardPile)
            return 0;
        int n = 0;
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const Player::Place from = move.from_places.value(i);
            const Card *card = Sanguosha->getCard(move.card_ids.at(i));
            if ((from == Player::PlaceHand || from == Player::PlaceEquip || from == Player::PlaceDelayedTrick
                 || from == Player::PlaceTable)
                && card->getTypeId() == Card::TypeBasic && card->isRed())
                ++n;
        }
        return n;
    }
};

class ThChiwu : public TriggerSkillV2
{
public:
    ThChiwu() : TriggerSkillV2("thchiwu")
    {
        events << CardEffected;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!player || effect.to != player || !player->isAlive() || !player->hasSkill(objectName()) || !player->isChained()
            || !effect.card || effect.nullified
            || !(effect.card->isKindOf("Duel") || (effect.card->isKindOf("Slash") && !effect.card->isKindOf("NatureSlash"))))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        effect.nullified = true;
        *ctx.original_data = QVariant::fromValue(effect);
        return false;
    }
};

class ThYejunViewAs : public ViewAsSkillV2
{
public:
    ThYejunViewAs() : ViewAsSkillV2("thyejunv")
    {
        setPhaseName("Play");
        attached_lord_skill = true;
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *lord = attachedParentOwner(request, "thyejun");
        if (!lord || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || request.initiator->getKingdom() != "tsuki"
            || request.initiator->isChained())
            return false;
        foreach (const Player *p, request.initiator->getAliveSiblings())
            if (p->isChained())
                return false;
        return true;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return selected.isEmpty() && candidate && candidate == attachedParentOwner(request, "thyejun");
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1 && canSelectTarget(request, {}, selected.first());
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThYejunCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *lord) const override
    {
        if (!lord || !lord->isAlive() || lord->isChained())
            return ContinueEffects;
        Room *room = lord->getRoom();
        room->broadcastSkillInvoke("thyejun");
        room->notifySkillInvoked(lord, "thyejun");
        room->setPlayerChained(lord, true, ctx.initiator);
        return ContinueEffects;
    }
};

class ThYejun : public TriggerSkillV2
{
public:
    ThYejun() : TriggerSkillV2("thyejun$")
    {
        events << GameStart << EventPhaseStart << EventAcquireSkill << EventLoseSkill << Death << GeneralShown
               << GeneralHidden;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        syncAttachedChildren(room, objectName(), "thyejunv", true);
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- tsuki002

class ThJinguoViewAs : public ViewAsSkillV2
{
public:
    ThJinguoViewAs() : ViewAsSkillV2("thjinguo") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->getSuit() == Card::Heart
            && !request.initiator->isJilei(card) && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    // No card: 血呓 for the turn. A heart: discard it and raid another character.
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty())
            return true;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return request.selectedCardIds.size() == 1
            && canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && !request.selectedCardIds.isEmpty() && selected.isEmpty() && to
            && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return request.selectedCardIds.isEmpty() ? selected.isEmpty() : selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThJinguoCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        room->setPlayerMark(source, "thjinguo", 1);
        if (ctx.use_card && ctx.use_card->subcardsLength() == 0) {
            if (!source->hasSkill("thxueyi", true)) {
                room->setPlayerFlag(source, "thjinguo");
                room->acquireSkillFromEffect(source, "thxueyi", ctx);
            }
            return FinishSkill;
        }
        return ContinueEffects;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        const QList<int> ids = room->askForCardsChosen(source, target, "he", objectName(), qMin(2, target->getCardCount()),
                                                       2, false, Card::MethodNone, QList<int>(), false);
        if (!ids.isEmpty()) {
            DummyCard dummy(ids);
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName(), objectName(), QString());
            room->obtainCard(source, &dummy, reason, false);
        }
        if (target->isAlive() && target->isWounded())
            room->recover(target, RecoverStruct(objectName(), source));
        return ContinueEffects;
    }
};

class ThJinguo : public TriggerSkillV2
{
public:
    ThJinguo() : TriggerSkillV2("thjinguo")
    {
        events << EventPhaseEnd << EventPhaseChanging;
        view_as_skill = new ThJinguoViewAs;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && player->hasFlag("thjinguo")
            && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            room->setPlayerFlag(player, "-thjinguo");
            room->detachSkillFromPlayer(player, "thxueyi", false, true);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Play
            || player->getMark("thjinguo") <= 0 || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // Whoever used 禁果 shows the hand and loses its hearts as the phase ends.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->setPlayerMark(player, "thjinguo", 0);
        if (player->isKongcheng())
            return false;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->showAllCards(player);
        QList<int> hearts;
        foreach (const Card *card, player->getHandcards())
            if (card->getSuit() == Card::Heart && player->canDiscard(player, card->getEffectiveId()))
                hearts << card->getEffectiveId();
        if (!hearts.isEmpty()) {
            DummyCard dummy(hearts);
            room->throwCard(&dummy, player);
        }
        return false;
    }
};

class ThXueyi : public ViewAsSkillV2
{
public:
    ThXueyi() : ViewAsSkillV2("thxueyi", 1) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return false;
        Indulgence card(Card::NoSuit, 0);
        return card.isAvailable(request.initiator);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using") || card->getSuit() != Card::Heart)
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Indulgence *card = new Indulgence(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Indulgence"; }
};

class ThLianmi : public WakeSkill
{
public:
    ThLianmi() : WakeSkill("thlianmi")
    {
        events << EventPhaseStart;
        waked_skills = "tenyearkuanggu";
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override
    {
        return player->getEquips().length() > player->getHp();
    }

    // Upstream grants its own 狂骨 (recover or draw); the local tenyear 狂骨 is that rule.
    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (room->changeMaxHpForAwakenSkill(player, -1, objectName()))
            room->acquireSkillFromEffect(player, "tenyearkuanggu", ctx);
    }
};

// ---------------------------------------------------------------- tsuki003

class ThKuangqiViewAs : public ViewAsSkillV2
{
public:
    ThKuangqiViewAs() : ViewAsSkillV2("thkuangqi", 1) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || self->getHandcardNum() < self->getHp())
            return false;
        Duel card(Card::NoSuit, 0);
        return card.isAvailable(self);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using") || !card->isKindOf("Slash"))
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Duel *card = new Duel(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Duel"; }
};

class ThKuangqi : public TriggerSkillV2
{
public:
    ThKuangqi() : TriggerSkillV2("thkuangqi")
    {
        events << DamageCaused;
        view_as_skill = new ThKuangqiViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getHandcardNum() >= player->getHp() || damage.chain || damage.transfer || !damage.card
            || !damage.card->isKindOf("Slash"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#ThKuangqi";
        log.from = ctx.owner;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(++damage.damage);
        room->sendLog(log);
        room->notifySkillInvoked(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

// ---------------------------------------------------------------- tsuki004

class ThKaiyun : public TriggerSkillV2
{
public:
    ThKaiyun() : TriggerSkillV2("thkaiyun") { events << AskForRetrial; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + (judge && judge->who == ctx.owner ? 3 : 1));
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge)
            return false;
        QList<int> ids = room->getNCards(2, false);
        room->fillAG(ids, player);
        const int id = room->askForAG(player, ids, false, objectName());
        room->clearAG(player);
        const int chosen = ids.contains(id) ? id : ids.first();
        ids.removeOne(chosen);
        room->retrial(Sanguosha->getCard(chosen), player, judge, objectName());
        if (!ids.isEmpty()) {
            room->returnToTopDrawPile(ids);
            room->obtainCard(player, ids.first(), false);
        }
        return false;
    }
};

class ThJiaotu : public TriggerSkillV2
{
public:
    ThJiaotu() : TriggerSkillV2("thjiaotu") { events << Damaged << EventPhaseChanging; }

    // 无谋 lasts until the end of the damage source's next turn.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || player->getMark("@jiaotu") <= 0
            || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        if (player->hasFlag("thjiaotu")) {
            room->setPlayerFlag(player, "-thjiaotu");
            return false;
        }
        room->setPlayerMark(player, "@jiaotu", 0);
        room->detachSkillFromPlayer(player, "wumou", false, true);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (event != Damaged || !player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName())
            || !damage.from || !damage.from->isAlive())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // One judgement per point of damage; a refusal ends the rest.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        for (int i = 0; i < damage.damage; ++i) {
            ServerPlayer *from = damage.from;
            if (!player->isAlive() || !from || !from->isAlive()
                || !player->askForSkillInvoke(objectName(), QVariant::fromValue(from)))
                break;
            room->broadcastSkillInvoke(objectName());
            JudgeStruct judge;
            judge.pattern = ".|heart";
            judge.good = true;
            judge.negative = true;
            judge.reason = objectName();
            judge.who = from;
            room->judge(judge);
            if (!judge.isBad() || !from->isAlive())
                continue;
            if (isOwnTurn(from))
                room->setPlayerFlag(from, "thjiaotu");
            if (from->getMark("@jiaotu") <= 0) {
                room->addPlayerMark(from, "@jiaotu");
                // Upstream gives its own 无谋 (炽 marks); the local 无谋 is the same rule.
                room->acquireSkillFromEffect(from, "wumou", ctx);
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------- tsuki005

class ThShouye : public TriggerSkillV2
{
public:
    ThShouye() : TriggerSkillV2("thshouye")
    {
        events << DrawNCards << EventPhaseStart;
        markOwnerOnly(this);
    }

    // At the invoker's turn end each chosen character runs a draw phase without becoming
    // current (as 明鉴 does). The award outlives the skill, so this is a record.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart)
            return false;
        if (!player || player->getPhase() != Player::NotActive)
            return true;
        const QStringList targets = player->getTag("ThShouyeTargets").toStringList();
        player->removeTag("ThShouyeTargets"); // Consume before the phases can recurse.
        foreach (const QString &name, targets) {
            ServerPlayer *target = room->findPlayerByObjectName(name);
            if (!target || !target->isAlive())
                continue;
            target->changePhase(target->getPhase(), Player::Draw);
            if (target->getPhase() != Player::NotActive)
                target->changePhase(target->getPhase(), Player::NotActive);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DrawStruct draw = data.value<DrawStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || draw.reason != "draw_phase" || draw.num < 1
            || room->getOtherPlayers(player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@thshouye", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets << target;
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num = qMax(0, draw.num - 1);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QStringList targets = ctx.owner->getTag("ThShouyeTargets").toStringList();
        targets << target->objectName();
        ctx.owner->setTag("ThShouyeTargets", targets);
        return false;
    }
};

class ThXushi : public TriggerSkillV2
{
public:
    ThXushi() : TriggerSkillV2("thxushi") { events << CardAsked; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const QStringList asked = data.toStringList();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || isOwnTurn(player) || asked.isEmpty())
            return TriggerList();
        const QString pattern = asked.first();
        if (pattern != "slash" && pattern != "jink")
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Reveal the top card: a basic card is kept (and may then be played), anything else is discarded.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const QList<int> ids = room->getNCards(1, false);
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        const Card *card = Sanguosha->getCard(ids.first());
        if (card->getTypeId() == Card::TypeBasic && player->isAlive()) {
            room->obtainCard(player, card);
        } else {
            CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
            room->throwCard(card, toPile, nullptr);
        }
        return false;
    }
};

// ---------------------------------------------------------------- tsuki006

class ThFengxiang : public TriggerSkillV2
{
public:
    ThFengxiang() : TriggerSkillV2("thfengxiang")
    {
        events << ChainStateChanged;
        frequency = Frequent;
    }

    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 3; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx))
            return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->drawCards(1, objectName());
        return false;
    }
};

// Nullification is never asked for this owner's tricks on other unwounded characters.
class ThKuaiqing : public TriggerSkillV2
{
public:
    ThKuaiqing() : TriggerSkillV2("thkuaiqing")
    {
        events << CardEffect;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!player || !effect.card || !effect.card->isNDTrick() || !effect.from || !effect.from->isAlive()
            || !effect.from->hasSkill(objectName()) || !effect.to || effect.to == effect.from || effect.to->isWounded()
            || effect.no_respond)
            return TriggerList();
        return TriggerList{{effect.from, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        effect.no_respond = true;
        *ctx.original_data = QVariant::fromValue(effect);
        return false;
    }
};

class ThYuhuo : public TriggerSkillV2
{
public:
    ThYuhuo() : TriggerSkillV2("thyuhuo")
    {
        events << DamageInflicted;
        frequency = Limited;
        limit_mark = "@yuhuo";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getMark(limit_mark) <= 0 || !damage.from || !damage.from->isAlive() || damage.from == player)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(damage.from)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || ctx.owner->getMark(limit_mark) <= 0)
            return false;
        addUsage(ctx);
        room->removePlayerMark(ctx.owner, limit_mark);
        room->doSuperLightbox(ctx.owner, objectName());
        return true;
    }

    // The damage moves to its source, who then turns over.
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *source = damage.from;
        damage.to = source;
        damage.transfer = true;
        damage.transfer_reason = objectName();
        ctx.owner->setTag("TransferDamage", QVariant::fromValue(damage));
        if (source->isAlive())
            source->turnOver();
        return true;
    }
};

// ---------------------------------------------------------------- tsuki007

class ThXingmai : public TriggerSkillV2
{
public:
    ThXingmai() : TriggerSkillV2("thxingmai") { events << EventPhaseStart << PreDamageDone << EventPhaseChanging; }

    // Damage dealt and taken this turn, per character.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                p->setMark("thxingmai_damage", 0);
                p->setMark("thxingmai_damaged", 0);
            }
        } else if (event == PreDamageDone) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.to)
                damage.to->setMark("thxingmai_damaged", damage.to->getMark("thxingmai_damaged") + damage.damage);
            if (damage.from)
                damage.from->setMark("thxingmai_damage", damage.from->getMark("thxingmai_damage") + damage.damage);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            if (!owner->hasSkill(objectName()))
                continue;
            QList<const Player *> group;
            group << owner << owner->getLastAlive() << owner->getNextAlive();
            int dealt = 0;
            int taken = 0;
            QSet<QString> seen;
            foreach (const Player *p, group) {
                if (!p || seen.contains(p->objectName()))
                    continue;
                seen << p->objectName();
                dealt += p->getMark("thxingmai_damage");
                taken += p->getMark("thxingmai_damaged");
            }
            if ((owner->faceUp() && dealt > 1) || (!owner->faceUp() && taken > 1))
                result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->turnOver();
        if (ctx.owner->isAlive() && !ctx.owner->faceUp())
            ctx.owner->drawCards(2, objectName());
        return false;
    }
};

class ThLianhuaViewAs : public ViewAsSkillV2
{
public:
    ThLianhuaViewAs() : ViewAsSkillV2("thlianhua") { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thlianhua"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || card->hasFlag("using") || request.selectedCardIds.length() >= required(self))
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.length() != required(request.initiator))
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        foreach (int id, request.selectedCardIds) {
            if (!canSelectCard(selection, Sanguosha->getCard(id)))
                return false;
            selection.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        return slash;
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator)
            return false;
        room->addPlayerMark(ctx.initiator, "thlianhua-Clear");
        return true;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }

private:
    static int required(const Player *player) { return player->getMark("thlianhua-Clear") + 2; }
};

class ThLianhua : public TriggerSkillV2
{
public:
    ThLianhua() : TriggerSkillV2("thlianhua")
    {
        events << Damage;
        view_as_skill = new ThLianhuaViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->hasFlag("Global_DebutFlag") || player->getCardCount() < player->getMark("thlianhua-Clear") + 2)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The nested @@ response is the whole effect.
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thlianhua", "@thlianhua");
        return false;
    }
};

// ---------------------------------------------------------------- tsuki008

class ThQishu : public TriggerSkillV2
{
public:
    ThQishu() : TriggerSkillV2("thqishu") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::NotActive)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && !owner->faceUp())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        owner->turnOver();
        owner->gainMark("@moment");
        room->scheduleExtraTurn(owner, ctx.sourceRef);
        return false;
    }
};

class ThShiting : public TriggerSkillV2
{
public:
    ThShiting() : TriggerSkillV2("thshiting") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start
            || player->getMark("@moment") > 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        player->turnOver();
        if (player->isAlive() && player->getHandcardNum() < 3)
            player->drawCards(3 - player->getHandcardNum(), objectName());
        player->skip(Player::Judge);
        player->skip(Player::Draw);
        player->skip(Player::Play);
        player->skip(Player::Discard);
        return false;
    }
};

class ThHuanzai : public TriggerSkillV2
{
public:
    ThHuanzai() : TriggerSkillV2("thhuanzai")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
            || player->getMark("@moment") <= 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        ctx.owner->loseAllMarks("@moment");
        return false;
    }
};

// ---------------------------------------------------------------- tsuki009

class ThShennao : public ViewAsSkillV2
{
public:
    ThShennao() : ViewAsSkillV2("thshennao")
    {
        response_or_use = true;
        // ServerPlayer::hasNullification still asks the legacy response probe.
        response_pattern = "nullification";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.initiator->isAlive() && request.pattern == "nullification"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        return self && self->getHandcardNum() >= self->getHp() && request.selectedCardIds.isEmpty()
            && ownsHandCard(self, card);
    }

    // Enough hand cards: one of them. Too few: no material, paid with 1 HP and a draw.
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        if (self->getHandcardNum() < self->getHp())
            return request.selectedCardIds.isEmpty();
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Nullification *card = nullptr;
        if (request.selectedCardIds.isEmpty()) {
            card = new Nullification(Card::NoSuit, 0);
        } else {
            const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
            card = new Nullification(material->getSuit(), material->getNumber());
            card->addSubcard(material);
        }
        card->setSkillName(objectName());
        return card;
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator)
            return false;
        if (request.selectedCardIds.isEmpty()) {
            ctx.initiator->drawCards(1, objectName());
            room->loseHp(ctx.initiator, 1, true, ctx.initiator, objectName());
        }
        return true;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Nullification"; }
};

class ThMiaoyao : public TriggerSkillV2
{
public:
    ThMiaoyao() : TriggerSkillV2("thmiaoyao")
    {
        events << CardUsed << CardResponded;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getHp() != 1
            || !player->isWounded())
            return TriggerList();
        const Card *card = nullptr;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player)
                card = use.card;
        } else {
            card = data.value<CardResponseStruct>().m_card;
        }
        if (!card || !card->isKindOf("Jink"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->recover(ctx.owner, RecoverStruct(objectName(), ctx.owner));
        return false;
    }
};

// ---------------------------------------------------------------- tsuki010

class ThHeiguanViewAs : public ViewAsSkillV2
{
public:
    ThHeiguanViewAs() : ViewAsSkillV2("thheiguan") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isBlack();
    }

    // A black hand card is given away; with no card, one of the target's hand cards is taken.
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty())
            return true;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return request.selectedCardIds.size() == 1
            && canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        if (!request.initiator || !selected.isEmpty() || !to || to == request.initiator)
            return false;
        return !request.selectedCardIds.isEmpty() || !to->isKongcheng();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThHeiguanCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        ServerPlayer *payer = ctx.initiator;
        if (!source || !payer || !target || !target->isAlive() || !ctx.use_card)
            return ContinueEffects;
        Room *room = source->getRoom();
        if (ctx.use_card->subcardsLength() == 0) {
            if (target->isKongcheng() || !source->isAlive())
                return ContinueEffects;
            const int id = room->askForCardChosen(source, target, "h", objectName());
            if (id >= 0)
                room->obtainCard(source, id, false);
            room->setPlayerMark(target, "@heiguan2", 1);
        } else {
            const int id = ctx.use_card->getSubcards().first();
            if (!payer->handCards().contains(id))
                return ContinueEffects;
            room->giveCard(payer, target, ctx.use_card, objectName(), true);
            room->setPlayerMark(target, "@heiguan1", 1);
        }
        return ContinueEffects;
    }
};

class ThHeiguan : public TriggerSkillV2
{
public:
    ThHeiguan() : TriggerSkillV2("thheiguan")
    {
        events << EventPhaseStart << Death << EventLoseSkill;
        view_as_skill = new ThHeiguanViewAs;
    }

    // Everything lasts until the owner's next turn starts.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        bool clear = false;
        if (event == EventPhaseStart)
            clear = player->getPhase() == Player::RoundStart && player->hasSkill(objectName(), true);
        else if (event == Death)
            clear = data.value<DeathStruct>().who == player && player->hasSkill(objectName(), true);
        else
            clear = data.toString() == objectName() || data.toString().startsWith(objectName() + "#");
        if (!clear)
            return false;
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            room->setPlayerMark(p, "@heiguan1", 0);
            room->setPlayerMark(p, "@heiguan2", 0);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThHeiguanProhibit : public ProhibitSkill
{
public:
    ThHeiguanProhibit() : ProhibitSkill("#thheiguan-prohibit") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        if (!card || !to || !card->isKindOf("Slash"))
            return false;
        if (to->getMark("@heiguan2") > 0)
            return true;
        return from && from->getMark("@heiguan1") > 0 && to->hasSkill("thheiguan");
    }
};

class ThAnyue : public FilterSkill
{
public:
    ThAnyue() : FilterSkill("thanyue") {}

    bool viewFilter(const Card *to_select) const override
    {
        if (to_select->getSuit() != Card::Heart)
            return false;
        const Player *owner = Sanguosha->getCardOwner(to_select->getEffectiveId());
        // Upstream lets its 赤秋 (spades as hearts) take precedence.
        return !owner || !owner->hasSkill("ikchiqiu");
    }

    const Card *viewAs(const Card *original) const override
    {
        Card *card = Sanguosha->cloneCard(original->objectName(), Card::Spade, original->getNumber());
        card->setSkillName(objectName());
        return card;
    }
};

// ---------------------------------------------------------------- tsuki011

class ThXiaoyong : public TriggerSkillV2
{
public:
    ThXiaoyong() : TriggerSkillV2("thxiaoyong")
    {
        events << HpChanged;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        JudgeStruct judge;
        judge.pattern = ".|heart";
        judge.good = false;
        judge.reason = objectName();
        judge.who = ctx.owner;
        room->judge(judge);
        if (judge.isGood() && ctx.owner->isAlive())
            ctx.owner->drawCards(1, objectName());
        return false;
    }
};

class ThKanyao : public ViewAsSkillV2
{
public:
    ThKanyao() : ViewAsSkillV2("thkanyao") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        return self && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !self->isKongcheng()
            && self->getHandcardNum() >= self->getHp();
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThKanyaoCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || source->isKongcheng())
            return FinishSkill;
        Room *room = source->getRoom();
        room->showAllCards(source);
        QMap<Card::Suit, int> suits;
        foreach (const Card *card, source->getHandcards())
            ++suits[card->getSuit()];
        if (suits.size() == 1) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(source))
                if (!p->isNude())
                    targets << p;
            if (targets.isEmpty())
                return FinishSkill;
            ServerPlayer *target = room->askForPlayerChosen(source, targets, objectName(), "@thkanyao-get", false, true);
            if (!target)
                return FinishSkill;
            const int id = room->askForCardChosen(source, target, "he", objectName());
            if (id >= 0) {
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
                room->obtainCard(source, Sanguosha->getCard(id), reason, false);
            }
            return FinishSkill;
        }
        foreach (int count, suits)
            if (count > 1)
                return FinishSkill;
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getHp() >= source->getHp())
                victims << p;
        if (victims.isEmpty())
            return FinishSkill;
        ServerPlayer *victim = room->askForPlayerChosen(source, victims, objectName(), "@thkanyao-lose", false, true);
        if (victim)
            room->loseHp(victim, 1, true, source, objectName());
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- tsuki012

class ThZhehui : public TriggerSkillV2
{
public:
    ThZhehui() : TriggerSkillV2("thzhehui")
    {
        events << Damaged << DamageInflicted << EventPhaseStart << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            foreach (ServerPlayer *p, room->getAllPlayers())
                if (p->getMark("@shine") > 0)
                    room->setPlayerMark(p, "@shine", 0);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == Damaged) {
            if (player->isAlive() && player->hasSkill(objectName()) && player->getMark("@shine") < 1)
                result[player] << objectName();
        } else if (event == DamageInflicted) {
            if (data.value<DamageStruct>().to == player && player->isAlive() && player->getMark("@shine") > 0
                && player->hasSkill(objectName()))
                result[player] << objectName();
        } else if (event == EventPhaseStart && player->getPhase() == Player::Finish) {
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->getMark("@shine") > 0 && p->hasSkill(objectName()))
                    result[p] << objectName();
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == Damaged) {
            room->setPlayerMark(owner, "@shine", 1);
            return false;
        }
        room->sendCompulsoryTriggerLog(owner, objectName());
        room->broadcastSkillInvoke(objectName());
        if (event == DamageInflicted)
            return true;
        ServerPlayer *current = ctx.invoker;
        QList<ServerPlayer *> victims;
        Slash probe(Card::NoSuit, 0);
        probe.setSkillName("_thzhehui");
        foreach (ServerPlayer *p, room->getOtherPlayers(owner))
            if (owner->canSlash(p, &probe, false))
                victims << p;
        QStringList choices;
        if (current && current->isAlive() && owner->canDiscard(current, "he"))
            choices << "discard";
        if (!victims.isEmpty())
            choices << "slash";
        if (choices.isEmpty())
            return false;
        if (room->askForChoice(owner, objectName(), choices.join("+")) == "slash") {
            ServerPlayer *victim = room->askForPlayerChosen(owner, victims, objectName(), "@thzhehui-slash");
            if (!victim)
                return false;
            auto *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_thzhehui");
            CardUseStruct use(slash, owner, victim);
            use.setOwnedCard(slash);
            room->useCardFromSkillEffect(use, ctx);
        } else {
            const int id = room->askForCardChosen(owner, current, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, current, owner);
        }
        return false;
    }
};

// ---------------------------------------------------------------- tsuki013

class ThChenji : public TriggerSkillV2
{
public:
    ThChenji() : TriggerSkillV2("thchenji") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || isOwnTurn(player))
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player
            || !(move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@thchenji",
                                                        true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setPlayerChained(target, !target->isChained(), ctx.owner);
        return false;
    }
};

// 狂想 adds targets without the usual legality checks. Collateral needs a second victim
// for every added holder, so it is left out here.
class ThKuangxiang : public TriggerSkillV2
{
public:
    ThKuangxiang() : TriggerSkillV2("thkuangxiang") { events << CardUsed << CardFinished; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != CardFinished)
            return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->hasFlag("thkuangxiang"))
            return false;
        room->setCardFlag(use.card, "-thkuangxiang");
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getMark("kuangxiang") <= 0)
                continue;
            room->setPlayerMark(p, "kuangxiang", 0);
            if (p->isChained())
                room->setPlayerChained(p, false);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardUsed || !player)
            return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || !(use.card->isNDTrick() || use.card->isKindOf("BasicCard"))
            || use.card->isKindOf("Jink") || use.card->isKindOf("Nullification") || use.card->isKindOf("Collateral"))
            return result;
        const QList<ServerPlayer *> targets = effectiveTargets(use);
        bool chainedTarget = false;
        foreach (ServerPlayer *to, targets)
            if (to->isChained())
                chainedTarget = true;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            if (!owner->hasSkill(objectName()))
                continue;
            if (!targets.contains(owner)) {
                if (chainedTarget)
                    result[owner] << objectName();
                continue;
            }
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->isChained() && !targets.contains(p)) {
                    result[owner] << objectName();
                    break;
                }
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.to.isEmpty() && use.from)
            use.to << use.from;
        QList<ServerPlayer *> added;
        if (!use.to.contains(owner)) {
            foreach (ServerPlayer *to, use.to)
                if (to->isChained())
                    room->addPlayerMark(to, "kuangxiang");
            added << owner;
        } else {
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->isChained() && !use.to.contains(p)) {
                    room->addPlayerMark(p, "kuangxiang");
                    added << p;
                }
        }
        if (added.isEmpty())
            return false;
        foreach (ServerPlayer *p, added) {
            use.to << p;
            LogMessage log;
            log.type = "#ThYongyeAdd";
            log.from = owner;
            log.to << p;
            log.arg = objectName();
            log.card_str = use.card->toString();
            room->sendLog(log);
            if (use.from)
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, use.from->objectName(), p->objectName());
        }
        room->setCardFlag(use.card, "thkuangxiang");
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }

private:
    static QList<ServerPlayer *> effectiveTargets(const CardUseStruct &use)
    {
        if (!use.to.isEmpty())
            return use.to;
        return use.from ? QList<ServerPlayer *>{use.from} : QList<ServerPlayer *>();
    }
};

// ---------------------------------------------------------------- tsuki014

class ThExiViewAs : public ViewAsSkillV2
{
public:
    ThExiViewAs() : ViewAsSkillV2("thexi") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thexi"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *to) const override
    {
        return to && selected.length() < 2 && !to->isKongcheng();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 2;
    }

    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThExiCard"; }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || targets.length() != 2 || targets.at(0)->isKongcheng() || targets.at(1)->isKongcheng())
            return FinishSkill;
        Room *room = source->getRoom();
        QList<const Card *> cards;
        foreach (ServerPlayer *p, targets)
            cards << room->askForCardShow(p, source, objectName());
        if (cards.at(0) == nullptr || cards.at(1) == nullptr)
            return FinishSkill;
        for (int i = 0; i < 2; ++i) {
            CardMoveReason reason(CardMoveReason::S_REASON_SHOW, targets.at(i)->objectName(), objectName(), QString());
            room->moveCardTo(cards.at(i), targets.at(i), Player::PlaceTable, reason, true);
        }
        auto onTable = [room](const Card *card) { return room->getCardPlace(card->getEffectiveId()) == Player::PlaceTable; };
        if (cards.at(0)->getNumber() == cards.at(1)->getNumber()) {
            source->addMark("exicount");
            room->scheduleExtraTurn(source, ctx.sourceRef);
            for (int i = 0; i < 2; ++i)
                if (targets.at(i)->isAlive() && onTable(cards.at(i)))
                    room->obtainCard(targets.at(i), cards.at(i));
            return FinishSkill;
        }
        const int bigIndex = cards.at(0)->getNumber() > cards.at(1)->getNumber() ? 0 : 1;
        const Card *big = cards.at(bigIndex);
        const Card *small = cards.at(1 - bigIndex);
        ServerPlayer *bigOwner = targets.at(bigIndex);
        const Card *mine = big;
        const Card *theirs = small;
        if (bigOwner != source && room->askForChoice(source, objectName(), "big+small",
                                                     QVariant(ListI2V(QList<int>{big->getEffectiveId(),
                                                                                 small->getEffectiveId()})))
                                      == "small") {
            mine = small;
            theirs = big;
        }
        if (onTable(mine) && source->isAlive())
            room->obtainCard(source, mine);
        ServerPlayer *receiver = bigOwner == source ? source : bigOwner;
        if (onTable(theirs) && receiver->isAlive())
            room->obtainCard(receiver, theirs);
        return FinishSkill;
    }
};

class ThExi : public TriggerSkillV2
{
public:
    ThExi() : TriggerSkillV2("thexi")
    {
        events << EventPhaseStart;
        view_as_skill = new ThExiViewAs;
    }

    // The extra turn earned by 恶戏 cannot use 恶戏 again.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->getPhase() == Player::RoundStart && player->getMark("exicount") > 0) {
            player->setMark("exicount", 0);
            room->setPlayerFlag(player, "ThExiDisabled");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
            || player->hasFlag("ThExiDisabled"))
            return TriggerList();
        int withHand = 0;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (!p->isKongcheng())
                ++withHand;
        if (withHand < 2)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thexi", "@thexi");
        return false;
    }
};

class ThXinglu : public TriggerSkillV2
{
public:
    ThXinglu() : TriggerSkillV2("thxinglu") { events << Dying; }

    LimitScope getLimitScope() const override { return Limit_Turn; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DyingStruct dying = data.value<DyingStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !dying.who || dying.who == player
            || dying.who->isDead() || dying.who->getHp() > 0 || !player->canPindian(dying.who))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(who)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx))
            return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        if (who && who->isAlive() && ctx.owner->canPindian(who) && ctx.owner->pindian(who, objectName()) && who->isAlive())
            room->recover(who, RecoverStruct(objectName(), ctx.owner));
        return false;
    }
};

// ---------------------------------------------------------------- tsuki015

class ThAnbing : public TriggerSkillV2
{
public:
    ThAnbing() : TriggerSkillV2("thanbing")
    {
        events << PreCardUsed << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == PreCardUsed && player && player->getPhase() == Player::Play) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && use.card->isKindOf("TrickCard"))
                room->setPlayerFlag(player, "ThAnbingTrickInPlayPhase");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::Discard || player->hasFlag("ThAnbingTrickInPlayPhase")
            || player->isSkipped(Player::Discard))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        ctx.owner->skip(Player::Discard);
        return false;
    }
};

class ThAnbingMaxCards : public MaxCardsSkillV2
{
public:
    ThAnbingMaxCards() : MaxCardsSkillV2("#thanbing") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasSkill("thanbing"))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(-1);
    }
};

class ThHuilveViewAs : public ViewAsSkillV2
{
public:
    ThHuilveViewAs() : ViewAsSkillV2("thhuilve", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return false;
        const QStringList record = self->property("thhuilve").toString().split("+");
        if (record.size() < 2)
            return false;
        QScopedPointer<Card> card(Sanguosha->cloneCard(record.first(), Card::Suit(record.last().toInt())));
        if (!card)
            return false;
        card->setSkillName(objectName());
        return card->isAvailable(self);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !request.selectedCardIds.isEmpty() || !ownsHandCard(self, card))
            return false;
        const QStringList record = self->property("thhuilve").toString().split("+");
        return record.size() >= 2 && card->getSuit() == Card::Suit(record.last().toInt());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        const QStringList record = request.initiator->property("thhuilve").toString().split("+");
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = Sanguosha->cloneCard(record.first(), material->getSuit(), material->getNumber());
        if (!card)
            return nullptr;
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
};

class ThHuilve : public TriggerSkillV2
{
public:
    ThHuilve() : TriggerSkillV2("thhuilve")
    {
        events << PreCardUsed << EventPhaseChanging;
        view_as_skill = new ThHuilveViewAs;
    }

    // Remembers the last unconverted non-delayed trick of this play phase.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->hasSkill(objectName(), true))
            return false;
        if (event == EventPhaseChanging) {
            if (!player->property("thhuilve").toString().isEmpty())
                room->setPlayerProperty(player, "thhuilve", QString());
            return false;
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || player->getPhase() != Player::Play || !use.card || !use.card->isNDTrick())
            return false;
        if (use.card->isKindOf("Nullification") || !use.card->getSkillName().isEmpty()) {
            room->setPlayerProperty(player, "thhuilve", QString());
            return false;
        }
        room->setPlayerProperty(player, "thhuilve",
                                use.card->objectName() + "+" + QString::number(int(use.card->getSuit())));
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThJizhi : public TargetModSkillV2
{
public:
    ThJizhi() : TargetModSkillV2("thjizhi", "TrickCard") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || !ctx.card || !ctx.card->isKindOf("TrickCard")
            || !ctx.primary->hasSkill(objectName()))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1000);
    }
};

// ---------------------------------------------------------------- tsuki016

class ThShenyou : public TriggerSkillV2
{
public:
    ThShenyou() : TriggerSkillV2("thshenyou") { events << EventPhaseStart << Predamage; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == EventPhaseStart) {
            if (player->getPhase() == Player::Draw && !candidates(room, player).isEmpty())
                return TriggerList{{player, {objectName()}}};
        } else {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from == player && player->getMark(objectName()) > 0 && damage.card && damage.card->isKindOf("Slash"))
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Predamage)
            return ctx.owner->askForSkillInvoke(objectName(), "losehp");
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates(room, ctx.owner), objectName(),
                                                        "@thshenyou-invoke", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == Predamage) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to && damage.to->isAlive())
                room->loseHp(damage.to, damage.damage, true, player, objectName());
            return true;
        }
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return true;
        if (target->getHandcardNum() >= player->getHandcardNum())
            player->drawCards(1, objectName());
        if (player->isAlive() && !target->isAllNude()) {
            const int id = room->askForCardChosen(player, target, "hej", objectName());
            if (id >= 0)
                room->obtainCard(player, id, false);
        }
        if (player->isAlive() && target->isAlive()) {
            room->addPlayerMark(player, objectName());
            room->askForUseSlashTo(player, target, "@thshenyou:" + target->objectName(), false);
            room->removePlayerMark(player, objectName());
        }
        // The draw phase was given up.
        return true;
    }

private:
    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (!p->isAllNude())
                result << p;
        return result;
    }
};

// ---------------------------------------------------------------- tsuki017

class ThTianque : public TriggerSkillV2
{
public:
    ThTianque() : TriggerSkillV2("thtianque")
    {
        events << PreCardUsed << CardUsed;
        frequency = Frequent;
    }

    // Counts the owner's cards of each type this play phase; the first one of a type has count 1.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != PreCardUsed || !player || player->getPhase() != Player::Play)
            return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill)
            room->addPlayerMark(player, "thtianque_" + use.card->getType() + "-PlayClear");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill
            || player->getMark("thtianque_" + use.card->getType() + "-PlayClear") != 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->drawCards(1, objectName());
        return false;
    }
};

static Card::Color colorOf(const QString &name)
{
    return name == "red" ? Card::Red : name == "black" ? Card::Black : Card::Colorless;
}

// Whether `card` in `from`'s judge or equip area can move to a character other than `owner`.
static bool guixuMovable(const Player *owner, const Player *from, const Card *card, bool judge)
{
    QList<const Player *> others = from->getAliveSiblings();
    foreach (const Player *p, others) {
        if (p == owner)
            continue;
        if (judge) {
            if (!p->containsTrick(card->objectName()))
                return true;
        } else {
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            if (equip && !p->getEquip(equip->location()) && p->hasEquipArea(equip->location()))
                return true;
        }
    }
    return false;
}

class ThGuixuViewAs : public ViewAsSkillV2
{
public:
    ThGuixuViewAs() : ViewAsSkillV2("thguixu") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thguixu"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        if (!request.initiator || !selected.isEmpty() || !to)
            return false;
        const Card::Color color = colorOf(request.initiator->property("thguixu").toString());
        foreach (const Card *card, to->getJudgingArea())
            if (card->getColor() == color && guixuMovable(request.initiator, to, card, true))
                return true;
        foreach (const Card *card, to->getEquips())
            if (card->getColor() == color && guixuMovable(request.initiator, to, card, false))
                return true;
        return false;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThGuixuCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *from) const override
    {
        ServerPlayer *owner = ctx.invoker;
        if (!owner || !from || !from->isAlive())
            return ContinueEffects;
        Room *room = owner->getRoom();
        const Card::Color color = colorOf(owner->property("thguixu").toString());
        QList<int> disabled;
        foreach (const Card *card, from->getJudgingArea())
            if (card->getColor() != color || !guixuMovable(owner, from, card, true))
                disabled << card->getEffectiveId();
        foreach (const Card *card, from->getEquips())
            if (card->getColor() != color || !guixuMovable(owner, from, card, false))
                disabled << card->getEffectiveId();
        const int id = room->askForCardChosen(owner, from, "ej", objectName(), false, Card::MethodNone, disabled);
        if (id < 0 || disabled.contains(id))
            return ContinueEffects;
        const Card *card = Sanguosha->getCard(id);
        const Player::Place place = room->getCardPlace(id);
        QList<ServerPlayer *> receivers;
        foreach (ServerPlayer *p, room->getOtherPlayers(from)) {
            if (p == owner)
                continue;
            if (place == Player::PlaceEquip) {
                const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
                if (equip && !p->getEquip(equip->location()) && p->hasEquipArea(equip->location()))
                    receivers << p;
            } else if (!p->containsTrick(card->objectName()) && !owner->isProhibited(p, card)) {
                receivers << p;
            }
        }
        if (receivers.isEmpty())
            return ContinueEffects;
        ServerPlayer *to = room->askForPlayerChosen(owner, receivers, objectName(), "@thguixu-to:::" + card->objectName());
        if (to)
            room->moveCardTo(card, from, to, place,
                             CardMoveReason(CardMoveReason::S_REASON_TRANSFER, owner->objectName(), objectName(), QString()));
        return ContinueEffects;
    }
};

class ThGuixu : public TriggerSkillV2
{
public:
    ThGuixu() : TriggerSkillV2("thguixu")
    {
        events << PreCardUsed << EventPhaseStart << EventPhaseChanging;
        view_as_skill = new ThGuixuViewAs;
    }

    // Remembers the type and color of the owner's last card this turn.
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->hasSkill(objectName(), true))
            return false;
        if (event == PreCardUsed && isOwnTurn(player)) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill) {
                const QString color = use.card->isRed() ? "red" : use.card->isBlack() ? "black" : "no_color";
                player->setTag("ThGuixuRecord", use.card->getType() + "|" + color);
            }
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            player->removeTag("ThGuixuRecord");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Finish)
            return TriggerList();
        const QStringList record = player->getTag("ThGuixuRecord").toString().split("|");
        if (record.size() != 2 || record.first() != "trick" || record.last() == "no_color")
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QStringList record = ctx.owner->getTag("ThGuixuRecord").toString().split("|");
        room->setPlayerProperty(ctx.owner, "thguixu", record.last());
        room->askForUseCard(ctx.owner, "@@thguixu", "@thguixu", -1, Card::MethodNone);
        room->setPlayerProperty(ctx.owner, "thguixu", QString());
        return false;
    }
};

// ---------------------------------------------------------------- tsuki018

class ThYongye : public TriggerSkillV2
{
public:
    ThYongye() : TriggerSkillV2("thyongye") { events << CardUsed; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play || !use.card || !use.card->isNDTrick() || use.to.isEmpty()
            || !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, ".|black", "@thyongye", *ctx.original_data, objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        // Adding a Collateral holder needs a second victim; only removal is offered for it.
        QList<ServerPlayer *> available;
        if (!use.card->isKindOf("AOE") && !use.card->isKindOf("GlobalEffect") && !use.card->isKindOf("Collateral")) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (use.to.contains(p) || player->isProhibited(p, use.card))
                    continue;
                if (use.card->targetFixed() || use.card->targetFilter(QList<const Player *>(), p, player))
                    available << p;
            }
        }
        QStringList choices;
        if (!available.isEmpty())
            choices << "add";
        choices << "remove";
        if (room->askForChoice(player, objectName(), choices.join("+"), *ctx.original_data) == "add") {
            ServerPlayer *extra = room->askForPlayerChosen(player, available, objectName(),
                                                           "@thyongye-add:::" + use.card->objectName());
            if (!extra)
                return false;
            use.to << extra;
            room->sortByActionOrder(use.to);
            LogMessage log;
            log.type = "#ThYongyeAdd";
            log.from = player;
            log.to << extra;
            log.card_str = use.card->toString();
            log.arg = objectName();
            room->sendLog(log);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), extra->objectName());
        } else {
            ServerPlayer *removed = room->askForPlayerChosen(player, use.to, objectName(),
                                                             "@thyongye-remove:::" + use.card->objectName());
            if (!removed)
                return false;
            LogMessage log;
            log.type = "#ThYongyeRemove";
            log.from = player;
            log.to << removed;
            log.card_str = use.card->toString();
            log.arg = objectName();
            room->sendLog(log);
            room->cancelTarget(use, removed);
        }
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class ThShiming : public TriggerSkillV2
{
public:
    ThShiming() : TriggerSkillV2("thshiming") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Replaces the normal draw.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const QList<int> ids = room->getNCards(4, false);
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        QList<int> red;
        QList<int> nine;
        foreach (int id, ids) {
            if (Sanguosha->getCard(id)->isRed())
                red << id;
            if (Sanguosha->getCard(id)->getNumber() <= 9)
                nine << id;
        }
        QStringList choices;
        if (!red.isEmpty())
            choices << "red";
        if (!nine.isEmpty())
            choices << "nine";
        QList<int> gain;
        if (!choices.isEmpty() && player->isAlive())
            gain = room->askForChoice(player, objectName(), choices.join("+"), QVariant(ListI2V(ids))) == "red" ? red : nine;
        QList<int> rest;
        foreach (int id, ids)
            if (!gain.contains(id) && room->getCardPlace(id) == Player::PlaceTable)
                rest << id;
        QList<int> taken;
        foreach (int id, gain)
            if (room->getCardPlace(id) == Player::PlaceTable)
                taken << id;
        if (!taken.isEmpty()) {
            DummyCard dummy(taken);
            room->obtainCard(player, &dummy);
        }
        if (!rest.isEmpty()) {
            DummyCard dummy(rest);
            CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
            room->throwCard(&dummy, toPile, nullptr);
        }
        return true;
    }
};

class ThShenbaoViewAs : public ViewAsSkillV2
{
public:
    ThShenbaoViewAs() : ViewAsSkillV2("thshenbao")
    {
        frequency = Limited;
        limit_mark = "@shenbao";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isAllNude();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThShenbaoCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || target->isAllNude())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int n = qMin(source->getAttackRange(), 3);
        const int available = target->getCards("hej").length();
        if (n > 0) {
            const QList<int> ids = room->askForCardsChosen(source, target, "hej", objectName(), qMin(n, available), n, false,
                                                           Card::MethodNone, QList<int>(), false);
            if (!ids.isEmpty()) {
                DummyCard dummy(ids);
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName(), objectName(), QString());
                room->obtainCard(source, &dummy, reason, false);
            }
        }
        room->setPlayerFlag(source, "shenbaoused");
        return ContinueEffects;
    }
};

class ThShenbao : public TriggerSkillV2
{
public:
    ThShenbao() : TriggerSkillV2("thshenbao")
    {
        events << EventPhaseStart;
        view_as_skill = new ThShenbaoViewAs;
        frequency = Limited;
        limit_mark = "@shenbao";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasFlag("shenbaoused") || player->getPhase() != Player::Finish
            || !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->setPlayerFlag(player, "-shenbaoused");
        room->sendCompulsoryTriggerLog(player, objectName());
        const int n = player->getAttackRange();
        if (n > 0)
            room->askForDiscard(player, objectName(), n, n, false, true);
        return false;
    }
};

class ThYunyin : public TriggerSkillV2
{
public:
    ThYunyin() : TriggerSkillV2("thyunyin$") { events << Damage; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || player->getKingdom() != "tsuki" || !damage.card
            || !damage.card->isKindOf("Slash"))
            return result;
        foreach (ServerPlayer *lord, room->getOtherPlayers(player))
            if (lord->hasLordSkill(objectName()))
                result[lord] << objectName();
        return result;
    }

    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.owner)))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = ctx.invoker;
        log.to << ctx.owner;
        log.arg = objectName();
        room->sendLog(log);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *invoker = ctx.invoker;
        ServerPlayer *lord = ctx.owner;
        JudgeStruct judge;
        judge.pattern = ".|black";
        judge.good = true;
        judge.reason = objectName();
        judge.who = invoker;
        room->judge(judge);
        if (!judge.isGood() || !lord->isAlive() || !invoker->isAlive())
            return false;
        QList<ServerPlayer *> holders;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getWeapon())
                holders << p;
        if (holders.isEmpty())
            return false;
        ServerPlayer *holder = room->askForPlayerChosen(invoker, holders, objectName(), "@thyunyin");
        if (holder && holder->getWeapon())
            room->obtainCard(lord, holder->getWeapon());
        return false;
    }
};

}

ThYejunCard::ThYejunCard() { setSkillName("thyejunv"); mute = true; }
ThJinguoCard::ThJinguoCard() { setSkillName("thjinguo"); mute = true; }
ThHeiguanCard::ThHeiguanCard() { setSkillName("thheiguan"); mute = true; }
ThKanyaoCard::ThKanyaoCard() { setSkillName("thkanyao"); mute = true; }
ThExiCard::ThExiCard() { setSkillName("thexi"); mute = true; }
ThGuixuCard::ThGuixuCard() { setSkillName("thguixu"); mute = true; }
ThShenbaoCard::ThShenbaoCard() { setSkillName("thshenbao"); mute = true; }

TouhouTsukiPackage::TouhouTsukiPackage()
    : Package("touhou-tsuki")
{
    General *tsuki001 = new General(this, "tsuki001$", "tsuki");
    tsuki001->addSkill(new ThSuoming);
    tsuki001->addSkill(new ThChiwu);
    tsuki001->addSkill(new ThYejun);

    General *tsuki002 = new General(this, "tsuki002", "tsuki", 4, false);
    tsuki002->addSkill(new ThJinguo);
    tsuki002->addSkill(new ThLianmi);
    tsuki002->addRelateSkill("thxueyi");
    tsuki002->addRelateSkill("tenyearkuanggu");

    General *tsuki003 = new General(this, "tsuki003", "tsuki");
    tsuki003->addSkill(new ThKuangqi);

    General *tsuki004 = new General(this, "tsuki004", "tsuki", 3, false);
    tsuki004->addSkill(new ThKaiyun);
    tsuki004->addSkill(new ThJiaotu);

    General *tsuki005 = new General(this, "tsuki005", "tsuki", 3);
    tsuki005->addSkill(new ThShouye);
    tsuki005->addSkill(new ThXushi);

    General *tsuki006 = new General(this, "tsuki006", "tsuki", 3);
    tsuki006->addSkill(new ThFengxiang);
    tsuki006->addSkill(new ThKuaiqing);
    tsuki006->addSkill(new ThYuhuo);

    General *tsuki007 = new General(this, "tsuki007", "tsuki");
    tsuki007->addSkill(new ThXingmai);
    tsuki007->addSkill(new ThLianhua);

    General *tsuki008 = new General(this, "tsuki008", "tsuki");
    tsuki008->addSkill(new ThQishu);
    tsuki008->addSkill(new ThShiting);
    tsuki008->addSkill(new ThHuanzai);

    General *tsuki009 = new General(this, "tsuki009", "tsuki", 3);
    tsuki009->addSkill(new ThShennao);
    tsuki009->addSkill(new ThMiaoyao);

    General *tsuki010 = new General(this, "tsuki010", "tsuki");
    tsuki010->addSkill(new ThHeiguan);
    tsuki010->addSkill(new ThHeiguanProhibit);
    related_skills.insert("thheiguan", "#thheiguan-prohibit");
    tsuki010->addSkill(new ThAnyue);

    General *tsuki011 = new General(this, "tsuki011", "tsuki", 3);
    tsuki011->addSkill(new ThXiaoyong);
    tsuki011->addSkill(new ThKanyao);

    General *tsuki012 = new General(this, "tsuki012", "tsuki");
    tsuki012->addSkill(new ThZhehui);

    General *tsuki013 = new General(this, "tsuki013", "tsuki", 3);
    tsuki013->addSkill(new ThChenji);
    tsuki013->addSkill(new ThKuangxiang);

    General *tsuki014 = new General(this, "tsuki014", "tsuki", 3);
    tsuki014->addSkill(new ThExi);
    tsuki014->addSkill(new ThXinglu);

    General *tsuki015 = new General(this, "tsuki015", "tsuki", 3, false);
    tsuki015->addSkill(new ThAnbing);
    tsuki015->addSkill(new ThAnbingMaxCards);
    related_skills.insert("thanbing", "#thanbing");
    tsuki015->addSkill(new ThHuilve);
    tsuki015->addSkill(new ThJizhi);

    General *tsuki016 = new General(this, "tsuki016", "tsuki");
    tsuki016->addSkill(new ThShenyou);

    General *tsuki017 = new General(this, "tsuki017", "tsuki", 3);
    tsuki017->addSkill(new ThTianque);
    tsuki017->addSkill(new ThGuixu);

    General *tsuki018 = new General(this, "tsuki018$", "tsuki", 3);
    tsuki018->addSkill(new ThYongye);
    tsuki018->addSkill(new ThShiming);
    tsuki018->addSkill(new ThShenbao);
    tsuki018->addSkill(new ThYunyin);

    skills << new ThYejunViewAs << new ThXueyi;

    addMetaObject<ThYejunCard>();
    addMetaObject<ThJinguoCard>();
    addMetaObject<ThHeiguanCard>();
    addMetaObject<ThKanyaoCard>();
    addMetaObject<ThExiCard>();
    addMetaObject<ThGuixuCard>();
    addMetaObject<ThShenbaoCard>();
}

ADD_PACKAGE(TouhouTsuki)
