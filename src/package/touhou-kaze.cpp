#include "touhou-kaze.h"
#include "touhou-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"

using namespace TouhouUtils;

namespace {

// ---------------------------------------------------------------- kaze001

// Local tenyear already owns "thzhiji", so this one carries a suffix.
class ThZhiji : public TriggerSkillV2
{
public:
    ThZhiji() : TriggerSkillV2("thzhiji_tts")
    {
        events << EventPhaseStart << CardsMoveOneTime << EventPhaseEnd;
        frequency = Frequent;
    }

    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->getPhase() != Player::Discard || !player->hasSkill(objectName(), true))
            return false;
        if (event == EventPhaseStart) {
            player->removeTag(countKey());
        } else if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player
                || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
                return false;
            int count = player->getTag(countKey()).toInt();
            for (int i = 0; i < move.card_ids.length(); ++i)
                if (move.from_places.value(i) == Player::PlaceHand)
                    ++count;
            player->setTag(countKey(), count);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Discard || player->getTag(countKey()).toInt() < 2)
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
        ServerPlayer *player = ctx.owner;
        if (player->isWounded() && room->askForChoice(player, objectName(), "recover+draw") == "recover")
            room->recover(player, RecoverStruct(objectName(), player));
        else
            player->drawCards(2, objectName());
        return false;
    }

private:
    static QString countKey() { return QStringLiteral("ThZhijiTtsCount"); }
};

class ThJiyi : public ViewAsSkillV2
{
public:
    ThJiyi() : ViewAsSkillV2("thjiyi") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThJiyiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        const Card *trick = room->askForCard(target, "TrickCard|.|.|hand", "@thjiyi:" + source->objectName(),
                                             QVariant(), Card::MethodNone);
        if (trick) {
            room->showCard(target, trick->getEffectiveId());
            target->drawCards(1, objectName());
        } else if (!target->isNude() && source->isAlive()) {
            const Card *card = room->askForExchange(target, objectName(), 1, 1, true,
                                                    "@thjiyigive:" + source->objectName(), false);
            if (card && !card->getSubcards().isEmpty())
                room->giveCard(target, source, card, objectName());
        }
        return ContinueEffects;
    }
};

class ThYisi : public WakeSkill
{
public:
    ThYisi() : WakeSkill("thyisi$")
    {
        events << EventPhaseStart;
        waked_skills = "thhuazhi";
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->getHp() == 1; }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->isWounded())
            room->recover(player, RecoverStruct(objectName(), player));
        if (room->changeMaxHpForAwakenSkill(player, 1, objectName()) && player->isLord())
            room->acquireSkillFromEffect(player, "thhuazhi", ctx);
    }
};

class ThHuazhi : public TriggerSkillV2
{
public:
    ThHuazhi() : TriggerSkillV2("thhuazhi$") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasLordSkill(objectName()) || isOwnTurn(player))
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || !move.from_places.contains(Player::PlaceHand))
            return TriggerList();
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (p->getKingdom() == "kaze" && !p->isKongcheng())
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    // Each other kaze character decides for itself; the lord has nothing to answer.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lord = ctx.owner;
        foreach (ServerPlayer *p, room->getOtherPlayers(lord)) {
            if (!lord->isAlive())
                break;
            if (!p->isAlive() || p->getKingdom() != "kaze" || p->isKongcheng())
                continue;
            const Card *card = room->askForCard(p, ".|.|.|hand", "@thhuazhi:" + lord->objectName(), QVariant(),
                                                Card::MethodNone);
            if (!card)
                continue;
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = p;
            log.to << lord;
            log.arg = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(lord, objectName());
            room->broadcastSkillInvoke(objectName());
            room->giveCard(p, lord, card, objectName());
        }
        return false;
    }
};

// ---------------------------------------------------------------- kaze002

class ThJilanwen : public TriggerSkillV2
{
public:
    ThJilanwen() : TriggerSkillV2("thjilanwen") { events << EventPhaseStart << DrawNCards; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (event == EventPhaseStart) {
            if (player->hasSkill(objectName()) && player->getPhase() == Player::Draw)
                return TriggerList{{player, {objectName()}}};
        } else if (event == DrawNCards && player->getMark(reduceMark()) > 0) {
            if (data.value<DrawStruct>().reason == "draw_phase")
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DrawNCards)
            return true;
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == DrawNCards) {
            if (player->getMark(reduceMark()) <= 0)
                return false;
            room->removePlayerMark(player, reduceMark());
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num = qMax(0, draw.num - 1);
            *ctx.original_data = QVariant::fromValue(draw);
            room->sendCompulsoryTriggerLog(player, objectName());
            return false;
        }

        room->addPlayerMark(player, reduceMark());
        JudgeStruct judge;
        judge.pattern = ".";
        judge.play_animation = false;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        if (!judge.card || !player->isAlive())
            return false;

        const Card::Suit suit = judge.card->getSuit();
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            foreach (const Card *card, p->getCards("ej")) {
                if (card->getSuit() != suit) {
                    targets << p;
                    break;
                }
            }
        }
        ServerPlayer *target = targets.isEmpty()
            ? nullptr : room->askForPlayerChosen(player, targets, objectName(), "thjilanwen-choose", true);
        if (target) {
            QList<int> disabled;
            foreach (const Card *card, target->getCards("ej"))
                if (card->getSuit() == suit)
                    disabled << card->getEffectiveId();
            const int id = room->askForCardChosen(player, target, "ej", objectName(), false, Card::MethodNone, disabled);
            if (id >= 0)
                room->obtainCard(player, id);
        } else if (inDiscardPile(room, judge.card)) {
            room->obtainCard(player, judge.card);
        }
        return false;
    }

private:
    static QString reduceMark() { return QStringLiteral("thjilanwen_draw"); }
};

// ---------------------------------------------------------------- kaze003

class ThNianke : public ViewAsSkillV2
{
public:
    ThNianke() : ViewAsSkillV2("thnianke", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isKindOf("Jink");
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThNiankeCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        ServerPlayer *payer = ctx.initiator;
        if (!source || !payer || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->subcardsLength() != 1)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (!payer->handCards().contains(id) || Sanguosha->getCard(id)->hasFlag("using"))
            return ContinueEffects;
        room->giveCard(payer, target, ctx.use_card, objectName(), true);
        if (!source->isAlive() || !target->isAlive() || target->isKongcheng())
            return ContinueEffects;
        const int shown = room->askForCardChosen(source, target, "h", objectName());
        if (shown < 0)
            return ContinueEffects;
        room->showCard(target, shown);
        if (Sanguosha->getCard(shown)->isRed()) {
            QList<ServerPlayer *> players;
            players << source << target;
            room->sortByActionOrder(players);
            room->drawCards(players, 1, objectName());
        }
        return ContinueEffects;
    }
};

class ThJilan : public TriggerSkillV2
{
public:
    ThJilan() : TriggerSkillV2("thjilan") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || discarders(room).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = choose(room, ctx.owner);
        if (!target)
            return false;
        ctx.extra_data = target->objectName();
        return true;
    }

    // One trigger per damage event; each further point asks again and a refusal ends it.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int times = ctx.original_data->value<DamageStruct>().damage;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        for (int i = 0; i < times; ++i) {
            if (i > 0)
                target = player->isAlive() ? choose(room, player) : nullptr;
            if (!target)
                break;
            if (target->isAlive() && target->canDiscard(target, "he")) {
                const int n = qMax(target->getLostHp(), 1);
                room->askForDiscard(target, objectName(), n, n, false, true);
            }
        }
        return false;
    }

private:
    static QList<ServerPlayer *> discarders(Room *room)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->canDiscard(p, "he"))
                targets << p;
        return targets;
    }

    ServerPlayer *choose(Room *room, ServerPlayer *player) const
    {
        const QList<ServerPlayer *> targets = discarders(room);
        if (targets.isEmpty())
            return nullptr;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@thjilan", true, true);
        if (target)
            room->broadcastSkillInvoke(objectName());
        return target;
    }
};

// ---------------------------------------------------------------- kaze004

class ThWangshou : public TriggerSkillV2
{
public:
    ThWangshou() : TriggerSkillV2("thwangshou") { events << Damage; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.to || damage.to == player || damage.to->isDead() || damage.to->hasFlag("Global_DebutFlag"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *to = ctx.original_data->value<DamageStruct>().to;
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(to)))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {to};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        JudgeStruct judge;
        judge.pattern = ".|black";
        judge.good = false;
        judge.negative = true;
        judge.reason = objectName();
        judge.who = target;
        room->judge(judge);
        if (judge.isBad() && player->isAlive() && target->isAlive() && player->canDiscard(target, "he")
            && player->askForSkillInvoke("thwangshou_discard", "yes:" + target->objectName())) {
            const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, objectName(), target, player);
        }
        return false;
    }
};

class ThZhanye : public TriggerSkillV2
{
public:
    ThZhanye() : TriggerSkillV2("thzhanye") { events << FinishJudge; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!player || !player->isAlive() || !judge || !judge->card || !judge->card->isRed())
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
            if (owner->hasSkill(objectName()) && !isOwnTurn(owner) && !owner->isNude() && canZhanye(owner, player))
                result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.invoker;
        if (!room->askForCard(ctx.owner, "..", "@thzhanye:" + target->objectName(), QVariant::fromValue(target),
                              objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner->isAlive() || !target->isAlive() || !canZhanye(owner, target))
            return false;
        auto *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_thzhanye");
        CardUseStruct use(slash, owner, target);
        use.setOwnedCard(slash);
        room->useCardFromSkillEffect(use, ctx);
        return false;
    }

private:
    static bool canZhanye(ServerPlayer *owner, ServerPlayer *target)
    {
        Slash slash(Card::NoSuit, 0);
        slash.setSkillName("_thzhanye");
        return owner->canSlash(target, &slash, false);
    }
};

// ---------------------------------------------------------------- kaze005

class ThEnan : public ViewAsSkillV2
{
public:
    ThEnan() : ViewAsSkillV2("thenan") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to
            && (to == request.initiator || request.initiator->inMyAttackRange(to));
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThEnanCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || !ctx.initiator->isAlive())
            return false;
        room->loseMaxHp(ctx.initiator, 1, objectName());
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive())
            return ContinueEffects;
        ServerPlayer *from = ctx.invoker && ctx.invoker->isAlive() ? ctx.invoker : nullptr;
        target->getRoom()->loseHp(target, 1, true, from, objectName());
        return ContinueEffects;
    }
};

class ThBeiyun : public TriggerSkillV2
{
public:
    ThBeiyun() : TriggerSkillV2("thbeiyun")
    {
        events << Dying;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getHp() >= 1
            || data.value<DyingStruct>().who != player)
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
        ServerPlayer *player = ctx.owner;
        QList<int> ids = room->getNCards(qMax(4 - player->getMaxHp(), 1), false);
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        const CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());

        bool redDone = false;
        bool blackDone = false;
        for (;;) {
            onTable(room, ids);
            QList<int> red;
            QList<int> black;
            foreach (int id, ids) {
                const Card *card = Sanguosha->getCard(id);
                if (card->isKindOf("TrickCard"))
                    continue;
                if (card->isRed())
                    red << id;
                else if (card->isBlack())
                    black << id;
            }
            QStringList choices;
            if (!redDone && !red.isEmpty())
                choices << "red";
            if (!blackDone && !black.isEmpty())
                choices << "black";
            if (choices.isEmpty() || !player->isAlive())
                break;
            choices << "cancel";
            const QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant(ListI2V(ids)));
            if (choice == "red") {
                redDone = true;
                DummyCard dummy(red);
                room->throwCard(&dummy, toPile, nullptr);
                if (player->isAlive() && player->isWounded())
                    room->recover(player, RecoverStruct(objectName(), player));
            } else if (choice == "black") {
                blackDone = true;
                DummyCard dummy(black);
                room->throwCard(&dummy, toPile, nullptr);
                if (player->isAlive())
                    room->gainMaxHp(player, 1, objectName());
            } else {
                break;
            }
        }

        onTable(room, ids);
        if (ids.isEmpty())
            return false;
        DummyCard rest(ids);
        if (player->isAlive()
            && room->askForChoice(player, objectName(), "get+discard", QVariant(ListI2V(ids))) == "get")
            room->obtainCard(player, &rest);
        else
            room->throwCard(&rest, toPile, nullptr);
        return false;
    }

private:
    // Callbacks between choices may move the revealed cards; keep only those still shown.
    static void onTable(Room *room, QList<int> &ids)
    {
        QList<int> kept;
        foreach (int id, ids)
            if (room->getCardPlace(id) == Player::PlaceTable)
                kept << id;
        ids = kept;
    }
};


// ---------------------------------------------------------------- kaze006

class ThQiaogong : public ViewAsSkillV2
{
public:
    ThQiaogong() : ViewAsSkillV2("thqiaogong", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using"))
            return false;
        const int id = card->getEffectiveId();
        return (self->handCards().contains(id) || self->getEquipsId().contains(id)) && self->canDiscard(self, id);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        if (!request.initiator || !to || !selected.isEmpty() || request.selectedCardIds.length() != 1)
            return false;
        const int discarded = request.selectedCardIds.first();
        const Card::Color color = Sanguosha->getCard(discarded)->getColor();
        foreach (const Card *equip, to->getEquips())
            if (equip->getEffectiveId() != discarded && equip->getColor() == color)
                return true;
        return false;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThQiaogongCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || !ctx.use_card
            || ctx.use_card->subcardsLength() != 1)
            return ContinueEffects;
        const Card::Color color = Sanguosha->getCard(ctx.use_card->getSubcards().first())->getColor();
        QList<int> disabled;
        bool any = false;
        foreach (const Card *equip, target->getEquips()) {
            if (equip->getColor() == color)
                any = true;
            else
                disabled << equip->getEffectiveId();
        }
        if (!any)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = room->askForCardChosen(source, target, "e", objectName(), false, Card::MethodNone, disabled);
        if (id >= 0)
            room->obtainCard(source, id);
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- kaze007

class ThGuiyu : public TriggerSkillV2
{
public:
    ThGuiyu() : TriggerSkillV2("thguiyu") { events << Damage << CardFinished; }

    // A slash whose owner chose "no use limit" gives its history entry back once it finishes.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != CardFinished)
            return false;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && use.from && use.m_addHistory && use.card->hasFlag(flagName())) {
            room->setCardFlag(use.card, "-" + flagName());
            room->addPlayerHistory(use.from, use.card->getClassName(), -1);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damage || !player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash") || !damage.by_user || damage.chain || damage.transfer
            || !damage.to || !damage.to->isAlive() || damage.to->hasFlag("Global_DebutFlag"))
            return TriggerList();
        if (room->getCardPlace(damage.card->getEffectiveId()) != Player::PlaceTable)
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

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to && damage.to->isAlive()
            && room->getCardPlace(damage.card->getEffectiveId()) == Player::PlaceTable)
            room->obtainCard(damage.to, damage.card);
        if (!player->isAlive())
            return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canDiscard(p, "he"))
                targets << p;
        ServerPlayer *target = targets.isEmpty()
            ? nullptr : room->askForPlayerChosen(player, targets, objectName(), "@thguiyu", true);
        if (target) {
            const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, objectName(), target, player);
        } else {
            room->setCardFlag(damage.card, flagName());
        }
        return false;
    }

private:
    static QString flagName() { return QStringLiteral("ThGuiyuUsed"); }
};

class ThZhouhua : public TriggerSkillV2
{
public:
    ThZhouhua() : TriggerSkillV2("thzhouhua")
    {
        events << EventPhaseEnd;
        frequency = Limited;
        limit_mark = "@zhouhua";
        waked_skills = "thhuaimie";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw
            || player->getMark(limit_mark) <= 0 || room->getAlivePlayers().length() >= room->getPlayers().length())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return isUsable(ctx) && ctx.owner->askForSkillInvoke(objectName());
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || ctx.owner->getMark(limit_mark) <= 0)
            return false;
        addUsage(ctx);
        room->removePlayerMark(ctx.owner, limit_mark);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(player, objectName());
        if (player->isWounded() && room->askForChoice(player, objectName(), "recover+draw") == "recover")
            room->recover(player, RecoverStruct(objectName(), player));
        else
            player->drawCards(2, objectName());
        if (player->isAlive())
            room->acquireSkillFromEffect(player, "thhuaimie", ctx);
        return false;
    }
};

class ThHuaimieFilter : public FilterSkill
{
public:
    ThHuaimieFilter() : FilterSkill("#thhuaimie") {}

    bool viewFilter(const Card *to_select) const override
    {
        return to_select->getTypeId() == Card::TypeTrick
            && Sanguosha->getCardPlace(to_select->getEffectiveId()) == Player::PlaceHand;
    }

    const Card *viewAs(const Card *originalCard) const override
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName("thhuaimie");
        return slash;
    }
};

class ThHuaimie : public TriggerSkillV2
{
public:
    ThHuaimie() : TriggerSkillV2("thhuaimie") { events << EventPhaseStart << EventPhaseChanging; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || player->getMark(objectName()) <= 0
            || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        room->setPlayerMark(player, objectName(), 0);
        room->detachSkillFromPlayer(player, "#thhuaimie", false, true, false);
        room->filterCards(player, player->getHandcards(), true);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play || player->getMark(objectName()) > 0)
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
        ServerPlayer *player = ctx.owner;
        room->setPlayerMark(player, objectName(), 1);
        if (!player->hasSkill("#thhuaimie", true)) {
            room->acquireSkill(player, "#thhuaimie", false, true, false);
            room->filterCards(player, player->getHandcards(), false);
        }
        return false;
    }
};

// ---------------------------------------------------------------- kaze008

class ThShenzhou : public TriggerSkillV2
{
public:
    ThShenzhou() : TriggerSkillV2("thshenzhou")
    {
        events << TurnedOver << Damaged;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || (event == TurnedOver && !player->faceUp()))
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
        ServerPlayer *player = ctx.owner;
        const QList<int> ids = room->getNCards(qMin(room->alivePlayerCount(), 5));
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        QMap<QString, QList<int>> byType;
        foreach (int id, ids)
            byType[Sanguosha->getCard(id)->getType()] << id;
        const QString type = room->askForChoice(player, objectName(), byType.keys().join("+"), QVariant(ListI2V(ids)));
        ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                                                        "@thshenzhou-give:::" + type, false, true);
        QList<int> given;
        QList<int> rest;
        foreach (int id, ids) {
            if (room->getCardPlace(id) != Player::PlaceTable)
                continue;
            if (byType.value(type).contains(id))
                given << id;
            else
                rest << id;
        }
        if (target && !given.isEmpty()) {
            DummyCard dummy(given);
            CardMoveReason give(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), objectName(), QString());
            room->obtainCard(target, &dummy, give);
        } else {
            rest << given;
        }
        if (!rest.isEmpty()) {
            DummyCard dummy(rest);
            CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
            room->throwCard(&dummy, toPile, nullptr);
        }
        return false;
    }
};

class ThTianliu : public TriggerSkillV2
{
public:
    ThTianliu() : TriggerSkillV2("thtianliu")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // Replaces the normal draw.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        JudgeStruct judge;
        judge.pattern = ".";
        judge.good = true;
        judge.play_animation = false;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        if (!judge.card || !player->isAlive())
            return true;
        int n = 0;
        switch (judge.card->getSuit()) {
        case Card::Heart: n = 3; break;
        case Card::Diamond: n = 2; break;
        case Card::Club: n = 1; break;
        default: break;
        }
        if (n > 0)
            player->drawCards(n, objectName());
        return true;
    }
};

class ThQianyi : public ViewAsSkillV2
{
public:
    ThQianyi() : ViewAsSkillV2("thqianyi")
    {
        frequency = Limited;
        limit_mark = "@qianyi";
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
        return request.initiator && selected.isEmpty() && to;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThQianyiCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        room->doSuperLightbox(ctx.initiator, objectName());
        ctx.initiator->turnOver();
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        if (target->isWounded()
            && room->askForChoice(source, objectName(), "recover+draw", QVariant::fromValue(target)) == "recover")
            room->recover(target, RecoverStruct(objectName(), source));
        else
            target->drawCards(2, objectName());
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- kaze009

class ThHuosui : public TriggerSkillV2
{
public:
    ThHuosui() : TriggerSkillV2("thhuosui") { events << TargetSpecified << TargetConfirmed; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash"))
            return TriggerList();
        if (event == TargetSpecified) {
            if (use.from != player || player->isNude())
                return TriggerList();
            foreach (ServerPlayer *to, use.to)
                if (to != player && to->isAlive())
                    return TriggerList{{player, {objectName()}}};
        } else if (use.to.contains(player) && use.from && use.from != player && use.from->isAlive()
                   && !use.from->isNude()) {
            return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetConfirmed) {
            if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(use.from)))
                return false;
            room->broadcastSkillInvoke(objectName());
            return true;
        }
        // The user may hand one card to each other target in turn.
        QVariantList gifts;
        QList<ServerPlayer *> asked;
        foreach (ServerPlayer *to, use.to) {
            if (to == player || !to->isAlive() || asked.contains(to) || player->isNude())
                continue;
            asked << to;
            const Card *card = room->askForCard(player, "..", "@thhuosui:" + to->objectName(), *ctx.original_data,
                                                Card::MethodNone);
            if (card)
                gifts << QVariant(QVariantMap{{"to", to->objectName()}, {"card", card->getEffectiveId()}});
        }
        if (gifts.isEmpty())
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = gifts;
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetConfirmed) {
            ServerPlayer *from = use.from;
            if (!from || !from->isAlive() || from->isNude())
                return false;
            const Card *card = room->askForExchange(from, objectName(), 1, 1, true,
                                                    "@thhuosui-target:" + player->objectName(), false);
            if (!card || card->getSubcards().isEmpty())
                return false;
            room->giveCard(from, player, card, objectName());
            noJink(room, use, player);
        } else {
            foreach (const QVariant &entry, ctx.extra_data.toList()) {
                const QVariantMap gift = entry.toMap();
                ServerPlayer *to = room->findPlayerByObjectName(gift.value("to").toString());
                const int id = gift.value("card").toInt();
                if (!to || !to->isAlive() || !player->isAlive() || room->getCardOwner(id) != player
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip))
                    continue;
                room->giveCard(player, to, QList<int>() << id, objectName());
                noJink(room, use, to);
            }
        }
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }

private:
    static void noJink(Room *room, CardUseStruct &use, ServerPlayer *target)
    {
        if (!use.to.contains(target) || use.no_respond_list.contains(target->objectName()))
            return;
        use.no_respond_list << target->objectName();
        LogMessage log;
        log.type = "#NoJink";
        log.from = target;
        room->sendLog(log);
    }
};

class ThKunyi : public ViewAsSkillV2
{
public:
    ThKunyi() : ViewAsSkillV2("thkunyi")
    {
        frequency = Limited;
        limit_mark = "@kunyi";
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
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThKunyiCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        room->doSuperLightbox(ctx.initiator, objectName());
        ctx.initiator->turnOver();
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive())
            return ContinueEffects;
        ServerPlayer *from = ctx.invoker && ctx.invoker->isAlive() ? ctx.invoker : nullptr;
        target->getRoom()->damage(DamageStruct(objectName(), from, target));
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- kaze010

class ThCannve : public ViewAsSkillV2
{
public:
    ThCannve() : ViewAsSkillV2("thcannve", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using")
            || card->getSuit() != Card::Diamond)
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThCannveCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        ServerPlayer *payer = ctx.initiator;
        if (!source || !payer || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->subcardsLength() != 1)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != payer || Sanguosha->getCard(id)->hasFlag("using"))
            return ContinueEffects;
        room->giveCard(payer, target, ctx.use_card, objectName(), true);
        if (!source->isAlive() || !target->isAlive())
            return ContinueEffects;

        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getOtherPlayers(target))
            if (target->canSlash(p, true))
                victims << p;
        if (!victims.isEmpty()) {
            ServerPlayer *victim = room->askForPlayerChosen(source, victims, objectName(), "@thcannve-target:" + target->objectName());
            if (victim) {
                LogMessage log;
                log.type = "#CollateralSlash";
                log.from = source;
                log.to << victim;
                room->sendLog(log);
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), victim->objectName());
                if (room->askForUseSlashTo(target, victim, "@thcannve-slash:" + victim->objectName()))
                    return ContinueEffects;
            }
        }
        if (!source->isAlive() || !target->isAlive())
            return ContinueEffects;
        QStringList choices;
        if (!target->isNude())
            choices << "get";
        choices << "hit";
        const QString choice = room->askForChoice(source, objectName(), choices.join("+"), QVariant::fromValue(target));
        if (choice == "get") {
            const int card = room->askForCardChosen(source, target, "he", objectName());
            if (card >= 0)
                room->obtainCard(source, card, false);
        } else {
            room->damage(DamageStruct(objectName(), source, target));
        }
        return ContinueEffects;
    }
};

class ThSibaoViewAs : public ViewAsSkillV2
{
public:
    ThSibaoViewAs() : ViewAsSkillV2("thsibao", 1) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Analeptic::IsAvailable(request.initiator);
        if (request.pattern.startsWith("@"))
            return false;
        Analeptic card(Card::NoSuit, 0);
        return Sanguosha->matchExpPattern(request.pattern, request.initiator, &card);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using")
            || !(card->isKindOf("EquipCard") || card->isKindOf("DelayedTrick")))
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        Analeptic *analeptic = new Analeptic(original->getSuit(), original->getNumber());
        analeptic->setSkillName(objectName());
        analeptic->addSubcard(original->getId());
        return analeptic;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Analeptic"; }
};

class ThSibao : public TriggerSkillV2
{
public:
    ThSibao() : TriggerSkillV2("thsibao")
    {
        events << CardUsed;
        view_as_skill = new ThSibaoViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || use.from != player || !use.card
            || !use.card->isKindOf("Analeptic"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        ctx.owner->drawCards(1, objectName());
        return false;
    }
};


// ---------------------------------------------------------------- kaze011

class ThWangqin : public TriggerSkillV2
{
public:
    ThWangqin() : TriggerSkillV2("thwangqin") { events << CardUsed << CardResponded; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->isChained())
            return result;
        const Card *card = nullptr;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player)
                card = use.card;
        } else {
            card = data.value<CardResponseStruct>().m_card;
        }
        if (!card || card->getTypeId() == Card::TypeSkill || !card->isRed())
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && !owner->faceUp())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->faceUp() || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.invoker)))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {ctx.invoker};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->faceUp())
            ctx.owner->turnOver();
        if (!target->isAlive())
            return false;
        target->turnOver();
        if (target->isChained())
            room->setPlayerChained(target, false, ctx.owner);
        return false;
    }
};

class ThFusuo : public TriggerSkillV2
{
public:
    ThFusuo() : TriggerSkillV2("thfusuo") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (player->getPhase() == Player::Start && !candidates(room, player).isEmpty())
            return TriggerList{{player, {objectName()}}};
        if (player->getPhase() == Player::Finish && player->hasFlag(flagName()))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getPhase() != Player::Start)
            return true;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates(room, ctx.owner), objectName(),
                                                        "@thfusuoinvoke", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->getPhase() == Player::Start) {
            ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
            player->turnOver();
            if (target && target->isAlive() && !target->isChained())
                room->setPlayerChained(target, true, player);
            room->setPlayerFlag(player, flagName());
            return false;
        }
        room->setPlayerFlag(player, "-" + flagName());
        room->sendCompulsoryTriggerLog(player, objectName());
        if (!room->askForCard(player, "^BasicCard", "@thfusuo"))
            room->loseHp(player, 1, true, player, objectName());
        return false;
    }

private:
    static QString flagName() { return QStringLiteral("thfusuo_invoked"); }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (!p->isChained())
                targets << p;
        return targets;
    }
};

// ---------------------------------------------------------------- kaze012

class ThGelong : public ViewAsSkillV2
{
public:
    ThGelong() : ViewAsSkillV2("thgelong") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator
            && request.initiator->canPindian(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThGelongCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || !source->canPindian(target))
            return ContinueEffects;
        Room *room = source->getRoom();
        const bool win = source->pindian(target, objectName());
        if (!source->isAlive())
            return ContinueEffects;
        if (win) {
            const Card *card = source->isKongcheng() || !target->isAlive()
                ? nullptr
                : room->askForCard(source, ".|.|.|hand", "@thgelonggive:" + target->objectName(), QVariant(),
                                   Card::MethodNone);
            if (card)
                room->giveCard(source, target, card, objectName());
            else
                room->loseHp(source, 1, true, source, objectName());
            return ContinueEffects;
        }
        if (!target->isAlive())
            return ContinueEffects;
        QStringList choices;
        choices << "damage";
        if (!target->isNude())
            choices << "get";
        if (room->askForChoice(source, objectName(), choices.join("+"), QVariant::fromValue(target)) == "get") {
            const int id = room->askForCardChosen(source, target, "he", objectName());
            if (id >= 0)
                room->obtainCard(source, id, false);
        } else {
            room->damage(DamageStruct(objectName(), source, target));
        }
        return ContinueEffects;
    }
};

class ThYuanzhou : public TriggerSkillV2
{
public:
    ThYuanzhou() : TriggerSkillV2("thyuanzhou") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish)
            return TriggerList();
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (p->getHandcardNum() < player->getHandcardNum())
                return TriggerList();
        if (victims(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, victims(room, ctx.owner), objectName(),
                                                        "@thyuanzhou", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player->isAlive() || !player->canDiscard(target, "hej"))
            return false;
        const int id = room->askForCardChosen(player, target, "hej", objectName(), false, Card::MethodDiscard);
        if (id < 0)
            return false;
        room->throwCard(id, room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : target, player);
        return false;
    }

private:
    // The others holding the most hand cards whose area the owner may discard from.
    static QList<ServerPlayer *> victims(Room *room, ServerPlayer *player)
    {
        int most = -1;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            most = qMax(most, p->getHandcardNum());
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (p->getHandcardNum() == most && player->canDiscard(p, "hej"))
                result << p;
        return result;
    }
};

// ---------------------------------------------------------------- kaze013

class ThDasuiViewAs : public ViewAsSkillV2
{
public:
    ThDasuiViewAs() : ViewAsSkillV2("thdasui") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.length() < 3 && ownsHandCard(request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const int n = request.selectedCardIds.length();
        if (n < 1 || n > 3)
            return false;
        foreach (int id, request.selectedCardIds)
            if (!ownsHandCard(request.initiator, Sanguosha->getCard(id)))
                return false;
        return true;
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThDasuiCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *payer = ctx.initiator;
        if (!payer || !payer->isAlive() || !ctx.use_card)
            return FinishSkill;
        QList<int> ids;
        foreach (int id, ctx.use_card->getSubcards())
            if (payer->handCards().contains(id))
                ids << id;
        if (!ids.isEmpty())
            payer->addToPile("tassel", ids, true);
        return FinishSkill;
    }
};

class ThDasui : public TriggerSkillV2
{
public:
    ThDasui() : TriggerSkillV2("thdasui")
    {
        events << EventPhaseStart;
        view_as_skill = new ThDasuiViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && !owner->getPile("tassel").isEmpty())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.invoker)))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {ctx.invoker};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QList<int> ids = ctx.owner->getPile("tassel");
        if (ids.isEmpty())
            return false;
        room->fillAG(ids, target);
        const int id = room->askForAG(target, ids, false, objectName());
        room->clearAG(target);
        if (id >= 0 && ctx.owner->getPile("tassel").contains(id))
            room->obtainCard(target, id);
        return false;
    }
};

class ThFengren : public TriggerSkillV2
{
public:
    ThFengren() : TriggerSkillV2("thfengren")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start
            || player->getPile("tassel").isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        const QList<int> ids = player->getPile("tassel");
        if (ids.isEmpty())
            return false;
        QStringList choices;
        if (ids.length() >= 2 && player->isWounded())
            choices << "recover";
        choices << "obtain";
        DummyCard dummy(ids);
        if (room->askForChoice(player, objectName(), choices.join("+")) == "recover") {
            CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString());
            room->throwCard(&dummy, reason, nullptr);
            room->recover(player, RecoverStruct(objectName(), player));
        } else {
            CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, player->objectName(), objectName(), QString());
            room->obtainCard(player, &dummy, reason);
        }
        return false;
    }
};

class ThFuli : public TriggerSkillV2
{
public:
    ThFuli() : TriggerSkillV2("thfuli")
    {
        events << BeforeCardsMove;
        frequency = Frequent;
    }

    LimitScope getLimitScope() const override { return Limit_Turn; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPile("tassel").length() >= 3)
            return TriggerList();
        ServerPlayer *current = room->getCurrent();
        if (!current || !isOwnTurn(current))
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.from_places.contains(Player::PlaceTable) || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE)
            return TriggerList();
        const CardUseStruct &use = move.reason.m_useStruct;
        if (!use.card || !use.from || use.from == player || !use.to.contains(player) || !room->CardInTable(use.card)
            || use.card->getNumber() < 2 || use.card->getNumber() > 9)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(move.reason.m_useStruct.card)))
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
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const Card *card = move.reason.m_useStruct.card;
        if (!card || !room->CardInTable(card))
            return false;
        const QList<int> materials = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        QList<int> ids;
        foreach (int id, materials)
            if (move.card_ids.contains(id) && room->getCardPlace(id) == Player::PlaceTable)
                ids << id;
        if (ids.isEmpty())
            return false;
        ctx.owner->addToPile("tassel", ids, true);
        QList<int> intercepted;
        foreach (int id, ids)
            if (room->getCardPlace(id) == Player::PlaceSpecial)
                intercepted << id;
        move.removeCardIds(intercepted);
        *ctx.original_data = QVariant::fromValue(move);
        return false;
    }
};

// ---------------------------------------------------------------- kaze014

class ThKudao : public TriggerSkillV2
{
public:
    ThKudao() : TriggerSkillV2("thkudao") { events << TargetSpecified << CardUsed << CardResponded; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        ServerPlayer *target = victim(event, player, data);
        if (!target || target == player || !target->isAlive() || target->isNude())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = victim(event, ctx.owner, *ctx.original_data);
        if (!target || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(target)))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->isAlive() || target->isNude())
            return false;
        const int id = room->askForCardChosen(ctx.owner, target, "he", objectName());
        if (id >= 0)
            ctx.owner->addToPile("leaf", id, true);
        return false;
    }

private:
    // The other character whose card this red card answers or singles out.
    static ServerPlayer *victim(TriggerEvent event, ServerPlayer *player, const QVariant &data)
    {
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill && use.card->isRed()
                && use.to.length() == 1)
                return use.to.first();
        } else if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.who && use.card && use.card->isRed() && use.card->isKindOf("BasicCard"))
                return use.who;
        } else if (event == CardResponded) {
            const CardResponseStruct resp = data.value<CardResponseStruct>();
            if (resp.m_card && resp.m_card->isRed() && resp.m_card->isKindOf("BasicCard"))
                return resp.m_who;
        }
        return nullptr;
    }
};

class ThSuilunViewAs : public ViewAsSkillV2
{
public:
    ThSuilunViewAs() : ViewAsSkillV2("thsuilun") { expand_pile = "leaf"; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thsuilun"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || request.selectedCardIds.length() >= 3)
            return false;
        const int id = card->getEffectiveId();
        int hand = 0;
        QList<Card::Suit> suits;
        foreach (int selected, request.selectedCardIds) {
            if (self->handCards().contains(selected))
                ++hand;
            else
                suits << Sanguosha->getCard(selected)->getSuit();
        }
        if (self->handCards().contains(id))
            return hand == 0 && !self->isJilei(card) && self->canDiscard(self, id);
        return self->getPile("leaf").contains(id) && suits.length() < 2 && !suits.contains(card->getSuit());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 3)
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

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThSuilunCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive())
            ctx.invoker->getRoom()->scheduleExtraTurn(ctx.invoker, ctx.sourceRef);
        return FinishSkill;
    }
};

class ThSuilun : public TriggerSkillV2
{
public:
    ThSuilun() : TriggerSkillV2("thsuilun")
    {
        events << EventPhaseStart;
        view_as_skill = new ThSuilunViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::NotActive
            || player->isKongcheng())
            return TriggerList();
        QSet<int> suits;
        foreach (int id, player->getPile("leaf"))
            suits << int(Sanguosha->getCard(id)->getSuit());
        if (suits.size() < 2)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The nested @@ response pays and schedules the turn itself.
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thsuilun", "@thsuilun", -1, Card::MethodNone);
        return false;
    }
};

// ---------------------------------------------------------------- kaze015

class ThRansangViewAs : public ViewAsSkillV2
{
public:
    ThRansangViewAs() : ViewAsSkillV2("thransang") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator
            && request.initiator->canPindian(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThRansangCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || !source->canPindian(target))
            return ContinueEffects;
        Room *room = source->getRoom();
        if (source->pindian(target, objectName())) {
            if (source->isAlive() && !source->hasSkill("thyanlun", true)) {
                room->setPlayerFlag(source, "thransang");
                room->acquireSkillFromEffect(source, "thyanlun", ctx);
            }
        } else if (source->isAlive()) {
            room->setPlayerCardLimitation(source, "use", "TrickCard|black", true, objectName());
        }
        return ContinueEffects;
    }
};

class ThRansang : public TriggerSkillV2
{
public:
    ThRansang() : TriggerSkillV2("thransang")
    {
        events << EventPhaseChanging;
        view_as_skill = new ThRansangViewAs;
    }

    // "Until the end of this turn": the borrowed 焰轮 leaves when the turn does.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && player->hasFlag("thransang") && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            room->setPlayerFlag(player, "-thransang");
            room->detachSkillFromPlayer(player, "thyanlun", false, true);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThYanlunViewAs : public ViewAsSkillV2
{
public:
    ThYanlunViewAs() : ViewAsSkillV2("thyanlun", 1) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator)
            return false;
        FireAttack card(Card::SuitToBeDecided, -1);
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return card.isAvailable(request.initiator);
        if (request.pattern.startsWith("@"))
            return false;
        return Sanguosha->matchExpPattern(request.pattern, request.initiator, &card);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isRed();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        FireAttack *card = new FireAttack(original->getSuit(), original->getNumber());
        card->addSubcard(original->getId());
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "FireAttack"; }
};

// The color rule for discarding is in FireAttack::onEffect (maneuvering.cpp).
class ThYanlun : public TriggerSkillV2
{
public:
    ThYanlun() : TriggerSkillV2("thyanlun")
    {
        events << Damage;
        view_as_skill = new ThYanlunViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.card
            || !damage.card->isKindOf("FireAttack"))
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


// ---------------------------------------------------------------- kaze016

class ThBazhi : public FilterSkill
{
public:
    ThBazhi() : FilterSkill("thbazhi") {}

    bool viewFilter(const Card *to_select) const override
    {
        if (!to_select->isKindOf("Lightning") && !(to_select->isKindOf("Jink") && to_select->getSuit() == Card::Diamond))
            return false;
        const int id = to_select->getEffectiveId();
        if (Sanguosha->getCardPlace(id) != Player::PlaceHand)
            return false;
        const Player *owner = Sanguosha->getCardOwner(id);
        if (!owner)
            return false;
        foreach (const Player *p, owner->getAliveSiblings())
            if (owner->getHp() > qMax(0, p->getHp()))
                return true;
        return false;
    }

    const Card *viewAs(const Card *originalCard) const override
    {
        FireSlash *slash = new FireSlash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());
        return slash;
    }
};

// Whether 八咫 applies depends on everyone's HP, so refilter whenever it can change.
class ThBazhiRefresh : public TriggerSkillV2
{
public:
    ThBazhiRefresh() : TriggerSkillV2("#thbazhi")
    {
        events << HpChanged << MaxHpChanged << Death << Revived << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->hasSkill("thbazhi", true))
                room->filterCards(p, p->getHandcards(), true);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThYanxingViewAs : public ViewAsSkillV2
{
public:
    ThYanxingViewAs() : ViewAsSkillV2("thyanxing") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThYanxingCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        QStringList choices;
        if (source->getHp() > 0)
            choices << "hp";
        choices << "maxhp";
        if (room->askForChoice(source, objectName(), choices.join("+")) == "hp")
            room->loseHp(source, 1, true, source, objectName());
        else
            room->loseMaxHp(source, 1, objectName());
        if (source->isAlive() && !source->hasSkill("thheyu", true)) {
            room->setPlayerFlag(source, "thyanxing");
            room->acquireSkillFromEffect(source, "thheyu", ctx);
        }
        return FinishSkill;
    }
};

class ThYanxing : public TriggerSkillV2
{
public:
    ThYanxing() : TriggerSkillV2("thyanxing")
    {
        events << EventPhaseChanging;
        view_as_skill = new ThYanxingViewAs;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && player->hasFlag("thyanxing") && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            room->setPlayerFlag(player, "-thyanxing");
            room->detachSkillFromPlayer(player, "thheyu", false, true);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThHeyu : public TriggerSkillV2
{
public:
    ThHeyu() : TriggerSkillV2("thheyu")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<DamageStruct>().nature != DamageStruct::Fire)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ++damage.damage;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

// ---------------------------------------------------------------- kaze017

class ThMaihuo : public TriggerSkillV2
{
public:
    ThMaihuo() : TriggerSkillV2("thmaihuo") { events << TargetSpecified; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill || !use.card->isRed()
            || use.to.length() != 1 || !use.to.first()->isAlive())
            return TriggerList();
        // The accepted use is already in the turn history, so the first card counts as one.
        if (room->countHistoryCards(player, QStringLiteral("turn")) != 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<CardUseStruct>().to.first();
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(target)))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const Card::Suit suit = room->askForSuit(ctx.owner, objectName());
        LogMessage log;
        log.type = "#ChooseSuit";
        log.from = ctx.owner;
        log.arg = Card::Suit2String(suit);
        room->sendLog(log);
        if (target->isKongcheng())
            return false;
        room->showAllCards(target);
        int n = 0;
        foreach (const Card *card, target->getHandcards())
            if (card->getSuit() == suit)
                ++n;
        if (n > 0)
            target->drawCards(qMin(n, 3), objectName());
        return false;
    }
};

class ThWunian : public TriggerSkillV2
{
public:
    ThWunian() : TriggerSkillV2("thwunian")
    {
        events << Predamage << CardEffected;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == Predamage) {
            if (data.value<DamageStruct>().from != player)
                return TriggerList();
        } else {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.to != player || !effect.card || !(effect.card->isNDTrick() || effect.card->isKindOf("Slash"))
                || !effect.from || effect.from->isWounded() || effect.from->getMaxHp() == 1)
                return TriggerList();
        }
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        if (event == Predamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            damage.from = nullptr;
            damage.by_user = false;
            *ctx.original_data = QVariant::fromValue(damage);
        } else {
            CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
            effect.nullified = true;
            *ctx.original_data = QVariant::fromValue(effect);
        }
        return false;
    }
};

// ---------------------------------------------------------------- kaze018

class ThDongxi : public TriggerSkillV2
{
public:
    ThDongxi() : TriggerSkillV2("thdongxi") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start)
            return TriggerList();
        if (player->getTag(currentKey()).toStringList().isEmpty() && candidates(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        // The previous copy always goes first; only then may a new one be taken.
        const QStringList previous = player->getTag(currentKey()).toStringList();
        player->removeTag(currentKey());
        if (!previous.isEmpty()) {
            room->sendCompulsoryTriggerLog(player, objectName());
            foreach (const QString &name, previous)
                room->detachSkillFromPlayer(player, name, false, true);
        }
        player->setTag(lastKey(), previous);
        if (!player->isAlive())
            return false;

        const QMap<ServerPlayer *, QStringList> choices = candidates(room, player);
        if (choices.isEmpty())
            return false;
        ServerPlayer *target = room->askForPlayerChosen(player, choices.keys(), objectName(), "@thdongxi", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        const QString skill = room->askForChoice(player, objectName(), choices.value(target).join("+"),
                                                 QVariant::fromValue(target));
        if (!choices.value(target).contains(skill))
            return false;
        player->setTag(currentKey(), QStringList{skill});
        room->acquireSkillFromEffect(player, skill, ctx);
        return false;
    }

private:
    static QString currentKey() { return QStringLiteral("ThDongxiCurrent"); }
    static QString lastKey() { return QStringLiteral("ThDongxiLast"); }

    static QMap<ServerPlayer *, QStringList> candidates(Room *room, ServerPlayer *player)
    {
        QMap<ServerPlayer *, QStringList> result;
        const QStringList last = player->getTag(currentKey()).toStringList() + player->getTag(lastKey()).toStringList();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            QStringList skills;
            QList<const General *> generals;
            if (p->getGeneral())
                generals << p->getGeneral();
            if (p->getGeneral2())
                generals << p->getGeneral2();
            foreach (const General *general, generals) {
                foreach (const Skill *skill, general->getVisibleSkillList()) {
                    const Skill::Frequency frequency = skill->getFrequency(player);
                    if (skill->isLordSkill() || skill->isAttachedLordSkill() || isOwnerOnlySkill(skill->objectName())
                        || frequency == Skill::Limited || frequency == Skill::Wake || last.contains(skill->objectName())
                        || player->hasSkill(skill->objectName(), true) || skills.contains(skill->objectName()))
                        continue;
                    skills << skill->objectName();
                }
            }
            if (!skills.isEmpty())
                result.insert(p, skills);
        }
        return result;
    }
};

class ThSangzhiViewAs : public ViewAsSkillV2
{
public:
    ThSangzhiViewAs() : ViewAsSkillV2("thsangzhi", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using")
            || !(card->isKindOf("Peach") || card->isKindOf("EquipCard")))
            return false;
        const int id = card->getEffectiveId();
        return (self->handCards().contains(id) || self->getEquipsId().contains(id)) && self->canDiscard(self, id);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThSangzhiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->addSkillInvalidity(target, "all", source->objectName(), objectName());
        room->setPlayerMark(target, "@sangzhi", 1);
        room->filterCards(target, target->getHandcards(), true);
        QStringList victims = source->getTag("ThSangzhiTargets").toStringList();
        if (!victims.contains(target->objectName()))
            victims << target->objectName();
        source->setTag("ThSangzhiTargets", victims);
        return ContinueEffects;
    }
};

class ThSangzhi : public TriggerSkillV2
{
public:
    ThSangzhi() : TriggerSkillV2("thsangzhi")
    {
        events << EventPhaseChanging << Death;
        view_as_skill = new ThSangzhiViewAs;
    }

    // Lasts until the end of the turn it was used in, or until the source dies.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        foreach (ServerPlayer *source, room->getAllPlayers(true)) {
            if (event == Death ? data.value<DeathStruct>().who != source : source != player)
                continue;
            const QStringList victims = source->getTag("ThSangzhiTargets").toStringList();
            if (victims.isEmpty())
                continue;
            source->removeTag("ThSangzhiTargets");
            foreach (const QString &name, victims) {
                ServerPlayer *victim = room->findPlayerByObjectName(name, true);
                if (!victim)
                    continue;
                room->removeSkillInvalidity(victim, "all", source->objectName(), objectName());
                room->setPlayerMark(victim, "@sangzhi", 0);
                room->filterCards(victim, victim->getHandcards(), true);
            }
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThXinhuaViewAs : public ViewAsSkillV2
{
public:
    ThXinhuaViewAs() : ViewAsSkillV2("thxinhuav", 1)
    {
        setPhaseName("Play");
        attached_lord_skill = true;
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.initiator->getKingdom() == "kaze"
            && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using") || !card->isKindOf("Weapon"))
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        if (!request.initiator || !selected.isEmpty() || !candidate || !candidate->isAlive()
            || candidate == request.initiator || !candidate->hasLordSkill("thxinhua"))
            return false;
        const SkillInstance *instance = request.initiator->findSkillInstance(request.activationRef.key.skillName,
                                                                            request.activationRef.key.instanceID);
        return instance && instance->parentRef.key.skillName == "thxinhua"
            && instance->parentRef.ownerObjectName == candidate->objectName();
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1 && canSelectTarget(request, {}, selected.first());
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThXinhuaCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *lord) const override
    {
        ServerPlayer *source = ctx.initiator;
        if (!source || !lord || !lord->isAlive() || !ctx.use_card || ctx.use_card->subcardsLength() != 1)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != source || Sanguosha->getCard(id)->hasFlag("using"))
            return ContinueEffects;
        room->broadcastSkillInvoke("thxinhua");
        room->notifySkillInvoked(lord, "thxinhua");
        room->giveCard(source, lord, QList<int>{id}, "thxinhua", true);
        if (!source->isAlive() || !lord->isAlive())
            return ContinueEffects;
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getOtherPlayers(lord))
            if (!p->isKongcheng())
                victims << p;
        if (victims.isEmpty())
            return ContinueEffects;
        ServerPlayer *victim = room->askForPlayerChosen(source, victims, "thxinhua", "@thxinhua-view:" + lord->objectName());
        if (!victim)
            return ContinueEffects;
        LogMessage log;
        log.type = "#ThXinhuaView";
        log.from = lord;
        log.to << victim;
        room->sendLog(log);
        room->showAllCards(victim, lord);
        return ContinueEffects;
    }
};

class ThXinhua : public TriggerSkillV2
{
public:
    ThXinhua() : TriggerSkillV2("thxinhua$")
    {
        events << GameStart << EventPhaseStart << EventAcquireSkill << EventLoseSkill << Death << GeneralShown
               << GeneralHidden;
        global = true;
    }

    // Each lord instance grants one exact "thxinhuav" child to every other living character.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        QList<SkillInstanceRef> parents;
        foreach (ServerPlayer *lord, room->getAlivePlayers())
            if (lord->hasLordSkill(objectName(), true))
                foreach (int id, lord->getSkillInstanceIds(objectName()))
                    parents << SkillInstanceRef(lord->objectName(), SkillInstanceKey(objectName(), id));
        foreach (ServerPlayer *donor, room->getAllPlayers(true)) {
            foreach (const SkillInstance &instance, donor->getSkillInstances()) {
                if (instance.skillName != "thxinhuav" || instance.source != SourceAttached
                    || instance.parentRef.key.skillName != objectName())
                    continue;
                if (!donor->isAlive() || !parents.contains(instance.parentRef)
                    || instance.parentRef.ownerObjectName == donor->objectName())
                    room->detachAttachedSkill(SkillInstanceRef(donor->objectName(), instance.key()));
            }
            if (!donor->isAlive())
                continue;
            foreach (const SkillInstanceRef &parent, parents)
                if (parent.ownerObjectName != donor->objectName())
                    room->attachSkillToPlayer(donor, "thxinhuav", parent);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

}

ThJiyiCard::ThJiyiCard() { setSkillName("thjiyi"); mute = true; }
ThNiankeCard::ThNiankeCard() { setSkillName("thnianke"); mute = true; }
ThEnanCard::ThEnanCard() { setSkillName("thenan"); mute = true; }
ThQiaogongCard::ThQiaogongCard() { setSkillName("thqiaogong"); mute = true; }
ThQianyiCard::ThQianyiCard() { setSkillName("thqianyi"); mute = true; }
ThKunyiCard::ThKunyiCard() { setSkillName("thkunyi"); mute = true; }
ThCannveCard::ThCannveCard() { setSkillName("thcannve"); mute = true; }
ThGelongCard::ThGelongCard() { setSkillName("thgelong"); mute = true; }
ThDasuiCard::ThDasuiCard() { setSkillName("thdasui"); mute = true; }
ThSuilunCard::ThSuilunCard() { setSkillName("thsuilun"); mute = true; }
ThRansangCard::ThRansangCard() { setSkillName("thransang"); mute = true; }
ThYanxingCard::ThYanxingCard() { setSkillName("thyanxing"); mute = true; }
ThSangzhiCard::ThSangzhiCard() { setSkillName("thsangzhi"); mute = true; }
ThXinhuaCard::ThXinhuaCard() { setSkillName("thxinhuav"); mute = true; }

TouhouKazePackage::TouhouKazePackage()
    : Package("touhou-kaze")
{
    General *kaze001 = new General(this, "kaze001$", "kaze", 3);
    kaze001->addSkill(new ThZhiji);
    kaze001->addSkill(new ThJiyi);
    kaze001->addSkill(new ThYisi);
    kaze001->addRelateSkill("thhuazhi");

    General *kaze002 = new General(this, "kaze002", "kaze");
    kaze002->addSkill(new ThJilanwen);

    General *kaze003 = new General(this, "kaze003", "kaze", 3);
    kaze003->addSkill(new ThNianke);
    kaze003->addSkill(new ThJilan);

    General *kaze004 = new General(this, "kaze004", "kaze");
    kaze004->addSkill(new ThWangshou);
    kaze004->addSkill(new ThZhanye);
    kaze004->addSkill(new SlashNoDistanceLimitSkill("thzhanye"));
    related_skills.insert("thzhanye", "#thzhanye-slash-ndl");

    General *kaze005 = new General(this, "kaze005", "kaze", 3, false);
    kaze005->addSkill(new ThEnan);
    kaze005->addSkill(new ThBeiyun);

    General *kaze006 = new General(this, "kaze006", "kaze");
    kaze006->addSkill(new PendingSkill("thmicai"));
    kaze006->addSkill(new ThQiaogong);

    General *kaze007 = new General(this, "kaze007", "kaze");
    kaze007->addSkill(new ThGuiyu);
    kaze007->addSkill(new ThZhouhua);
    kaze007->addRelateSkill("thhuaimie");

    General *kaze008 = new General(this, "kaze008", "kaze", 3);
    kaze008->addSkill(new ThShenzhou);
    kaze008->addSkill(new ThTianliu);
    kaze008->addSkill(new ThQianyi);

    General *kaze009 = new General(this, "kaze009", "kaze", 3, false);
    kaze009->addSkill(new ThHuosui);
    kaze009->addSkill(new ThKunyi);

    General *kaze010 = new General(this, "kaze010", "kaze", 3);
    kaze010->addSkill(new ThCannve);
    kaze010->addSkill(new ThSibao);

    General *kaze011 = new General(this, "kaze011", "kaze");
    kaze011->addSkill(new ThWangqin);
    kaze011->addSkill(new ThFusuo);

    General *kaze012 = new General(this, "kaze012", "kaze");
    kaze012->addSkill(new ThGelong);
    kaze012->addSkill(new ThYuanzhou);

    General *kaze013 = new General(this, "kaze013", "kaze", 3);
    kaze013->addSkill(new ThDasui);
    kaze013->addSkill(new ThFengren);
    kaze013->addSkill(new ThFuli);

    General *kaze014 = new General(this, "kaze014", "kaze", 3);
    kaze014->addSkill(new ThKudao);
    kaze014->addSkill(new ThSuilun);

    General *kaze015 = new General(this, "kaze015", "kaze");
    kaze015->addSkill(new ThRansang);
    kaze015->addRelateSkill("thyanlun");

    General *kaze016 = new General(this, "kaze016", "kaze", 5);
    kaze016->addSkill(new ThBazhi);
    kaze016->addSkill(new ThBazhiRefresh);
    related_skills.insert("thbazhi", "#thbazhi");
    kaze016->addSkill(new ThYanxing);
    kaze016->addRelateSkill("thheyu");

    General *kaze017 = new General(this, "kaze017", "kaze", 3, false);
    kaze017->addSkill(new ThMaihuo);
    kaze017->addSkill(new ThWunian);

    General *kaze018 = new General(this, "kaze018$", "kaze", 3);
    kaze018->addSkill(new ThDongxi);
    kaze018->addSkill(new ThSangzhi);
    kaze018->addSkill(new ThXinhua);

    skills << new ThHuazhi << new ThHuaimie << new ThHuaimieFilter << new ThYanlun << new ThHeyu
           << new ThXinhuaViewAs;

    addMetaObject<ThJiyiCard>();
    addMetaObject<ThNiankeCard>();
    addMetaObject<ThEnanCard>();
    addMetaObject<ThQiaogongCard>();
    addMetaObject<ThQianyiCard>();
    addMetaObject<ThKunyiCard>();
    addMetaObject<ThCannveCard>();
    addMetaObject<ThGelongCard>();
    addMetaObject<ThDasuiCard>();
    addMetaObject<ThSuilunCard>();
    addMetaObject<ThRansangCard>();
    addMetaObject<ThYanxingCard>();
    addMetaObject<ThSangzhiCard>();
    addMetaObject<ThXinhuaCard>();
}

ADD_PACKAGE(TouhouKaze)
