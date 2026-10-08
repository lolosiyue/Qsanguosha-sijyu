#include "ikai-sui.h"
#include "touhou-utils.h"
#include "ikai-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"

#include <QRandomGenerator>

using namespace TouhouUtils;
using namespace IkaiUtils;

// TouhouTripleSha's 异界·水. Skills that match a local one are reused by name; the rest are ported.

namespace {

// ---------------------------------------------------------------- wind005

// Converted 杀 can take a hand card from each target; a converted 闪 draws one.
class IkQizhou : public TriggerSkillV2
{
public:
    IkQizhou() : TriggerSkillV2("ikqizhou") { events << TargetSpecified << CardUsed << CardResponded; }

    static bool converted(const Card *card)
    {
        return card && card->isVirtualCard() && card->subcardsLength() > 0;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == CardResponded) {
            const CardResponseStruct resp = data.value<CardResponseStruct>();
            if (!resp.m_card || !resp.m_card->isKindOf("Jink") || !converted(resp.m_card))
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !converted(use.card))
            return TriggerList();
        if (event == CardUsed)
            return use.card->isKindOf("Jink") ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (!use.card->isKindOf("Slash"))
            return TriggerList();
        foreach (ServerPlayer *p, use.to)
            if (!p->isKongcheng())
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (event == TargetSpecified)
            return true;
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != TargetSpecified) {
            player->drawCards(1, objectName());
            return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        foreach (ServerPlayer *p, use.to) {
            if (!player->isAlive() || !p->isAlive() || p->isKongcheng()
                || !player->askForSkillInvoke(objectName(), QVariant::fromValue(p)))
                continue;
            room->broadcastSkillInvoke(objectName());
            const int id = room->askForCardChosen(player, p, "h", objectName());
            if (id >= 0)
                room->obtainCard(player, id, false);
        }
        return false;
    }
};

// ---------------------------------------------------------------- wind022

class IkShushen : public TriggerSkillV2
{
public:
    IkShushen() : TriggerSkillV2("ikshushen")
    {
        events << HpRecover;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        QStringList times;
        for (int i = 0; i < data.value<RecoverStruct>().recover; ++i)
            times << objectName();
        return times.isEmpty() ? TriggerList() : TriggerList{{player, times}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Someone (and the owner) draws one, or a full-handed character turns over.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        QList<ServerPlayer *> targets = room->getOtherPlayers(player);
        if (player->getHandcardNum() > player->getMaxHp())
            targets << player;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@ikshushen", true);
        if (!target) {
            player->drawCards(1, objectName());
            return false;
        }
        if (target->getHandcardNum() >= target->getMaxHp()
            && (target == player
                || room->askForChoice(player, objectName(), "draw+turnover", QVariant::fromValue(target)) == "turnover")) {
            target->turnOver();
            return false;
        }
        QList<ServerPlayer *> drawers{player, target};
        room->sortByActionOrder(drawers);
        foreach (ServerPlayer *p, drawers)
            if (p->isAlive())
                p->drawCards(1, objectName());
        return false;
    }
};

class IkQiaoxia : public TriggerSkillV2
{
public:
    IkQiaoxia() : TriggerSkillV2("ikqiaoxia") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName())
            || player->isKongcheng())
            return TriggerList();
        foreach (const Card *card, player->getHandcards())
            if (!player->canDiscard(player, card->getEffectiveId()))
                return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        const int n = player->getHandcardNum();
        player->throwAllHandCards();
        if (n < player->getHp() || !player->isAlive())
            return false;
        QStringList choices;
        if (player->isWounded())
            choices << "recover";
        choices << "draw";
        if (room->askForChoice(player, objectName(), choices.join("+")) == "recover")
            room->recover(player, RecoverStruct(objectName(), player));
        else
            player->drawCards(2, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- wind023

class IkXielun : public ViewAsSkillV2
{
public:
    IkXielun() : ViewAsSkillV2("ikxielun", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->isRed()
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        const Player *self = request.initiator;
        if (!self || !to || selected.length() >= qMax(self->getLostHp(), 1))
            return false;
        return self->getMark("ikjiuming") > 0 || self->inMyAttackRange(to, request.selectedCardIds);
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty() && selected.length() <= qMax(request.initiator->getLostHp(), 1);
    }

    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkXielunCard"; }

    // Chain every target, then burn one of them.
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || targets.isEmpty())
            return FinishSkill;
        Room *room = source->getRoom();
        QList<ServerPlayer *> alive;
        foreach (ServerPlayer *p, targets) {
            if (!p->isAlive())
                continue;
            if (!p->isChained())
                room->setPlayerChained(p, true, source);
            alive << p;
        }
        if (alive.isEmpty() || !source->isAlive())
            return FinishSkill;
        ServerPlayer *victim = room->askForPlayerChosen(source, alive, objectName(), "@ikxielun");
        if (victim)
            room->damage(DamageStruct(objectName(), source, victim, 1, DamageStruct::Fire));
        return FinishSkill;
    }
};

class IkCanyue : public TriggerSkillV2
{
public:
    IkCanyue() : TriggerSkillV2("ikcanyue")
    {
        events << Damage;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.to
            || !damage.to->isAlive() || damage.nature != DamageStruct::Fire)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The burnt character draws one and can be slashed freely this turn.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        target->drawCards(1, objectName());
        room->setPlayerMark(player, "ikcanyue_" + target->objectName() + "-Clear", 1);
        if (target == player && isOwnTurn(player))
            room->setPlayerMark(player, "ikcanyue_drink-Clear", 1);
        return false;
    }
};

class IkCanyueTargetMod : public TargetModSkillV2
{
public:
    IkCanyueTargetMod() : TargetModSkillV2("#ikcanyue", "Slash,Analeptic") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.primary || !ctx.card)
            return CorrectSkillResult::noEffect();
        if (ctx.card->isKindOf("Analeptic"))
            return ctx.primary->getMark("ikcanyue_drink-Clear") > 0 ? CorrectSkillResult::unlimitedResidue()
                                                                      : CorrectSkillResult::noEffect();
        if (ctx.card->isKindOf("Slash") && ctx.secondary
            && ctx.primary->getMark("ikcanyue_" + ctx.secondary->objectName() + "-Clear") > 0)
            return CorrectSkillResult::unlimitedResidue();
        return CorrectSkillResult::noEffect();
    }
};

class IkJiuming : public WakeSkill
{
public:
    IkJiuming() : WakeSkill("ikjiuming") { events << EventPhaseStart << PreDamageDone; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != PreDamageDone)
            return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && damage.from->hasSkill(objectName()) && isOwnTurn(damage.from))
            room->addPlayerMark(damage.from, "ikjiuming_damage-Clear", damage.damage);
        return false;
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Finish;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->getMark("ikjiuming_damage-Clear") >= 3; }

    void awaken(Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!room->changeMaxHpForAwakenSkill(player, 1, objectName()) || !player->isAlive())
            return;
        room->recover(player, RecoverStruct(objectName(), player));
        if (player->hasSkill("ikcanyue", true))
            room->detachSkillFromPlayer(player, "ikcanyue");
    }
};

// ---------------------------------------------------------------- wind040

class IkYanyu : public TriggerSkillV2
{
public:
    IkYanyu() : TriggerSkillV2("ikyanyu") { events << EventPhaseStart << BeforeCardsMove; }

    static QString markFor(const Card *card) { return "ikyanyu_" + QString::number(static_cast<int>(card->getTypeId())) + "-Clear"; }

    static QList<int> keepable(ServerPlayer *owner, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        foreach (int id, move.card_ids) {
            const Card *card = Sanguosha->getCard(id);
            if ((card->isKindOf("Slash") || card->isKindOf("Jink")) && owner->getMark(markFor(card)) > 0)
                ids << id;
        }
        return ids;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == EventPhaseStart) {
            if (!player->isAlive() || player->getPhase() != Player::Play)
                return result;
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->hasSkill(objectName()) && owner->canDiscard(owner, "he")
                    && (owner == player || owner->inMyAttackRange(player)))
                    result[owner] << objectName();
            return result;
        }
        ServerPlayer *current = room->getCurrent();
        if (!player->isAlive() || !player->hasSkill(objectName()) || !current
            || (current->getPhase() != Player::Play && current->getPhase() != Player::Discard))
            return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::DiscardPile && !keepable(player, move).isEmpty())
            result[player] << objectName();
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == BeforeCardsMove)
            return true;
        const Card *card = room->askForCard(ctx.owner, "Slash,Jink", "@ikyanyu-discard", QVariant(), objectName());
        if (!card)
            return false;
        room->broadcastSkillInvoke(objectName());
        room->addPlayerMark(ctx.owner, markFor(card), 3);
        return false;
    }

    // Up to three 杀 or 闪 of the paid kind are redirected to a chosen character this turn.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> ids = keepable(owner, move);
        while (!ids.isEmpty() && owner->isAlive()) {
            QList<int> disabled;
            foreach (int id, move.card_ids)
                if (!ids.contains(id))
                    disabled << id;
            room->fillAG(move.card_ids, owner, disabled);
            const int id = room->askForAG(owner, ids, true, objectName());
            room->clearAG(owner);
            if (id == -1)
                break;
            const Card *card = Sanguosha->getCard(id);
            ServerPlayer *target = room->askForPlayerChosen(
                owner, room->getAlivePlayers(), objectName(),
                QString("@ikyanyu-give:::%1:%2\\%3").arg(card->objectName(), card->getSuitString() + "_char", card->getNumberString()),
                true, true);
            if (!target)
                break;
            room->removePlayerMark(owner, markFor(card));
            move.removeCardIds(QList<int>() << id);
            *ctx.original_data = QVariant::fromValue(move);
            room->obtainCard(target, card, move.reason);
            ids = keepable(owner, move);
            QList<int> still;
            foreach (int i, ids)
                if (room->getCardPlace(i) == move.from_places.value(move.card_ids.indexOf(i)))
                    still << i;
            ids = still;
        }
        return false;
    }
};

// ---------------------------------------------------------------- wind041

class IkWuyue : public TriggerSkillV2
{
public:
    IkWuyue() : TriggerSkillV2("ikwuyue")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::NotActive
            || !move.from || !move.from->isAlive() || move.from == player
            || !(move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
            return TriggerList();
        foreach (int id, move.card_ids)
            if (Sanguosha->getCard(id)->getTypeId() == Card::TypeBasic)
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->drawCards(1, objectName());
        if (player->isAlive() && player->getHandcardNum() > 4)
            room->askForDiscard(player, objectName(), 1, 1, false, true);
        return false;
    }
};

// ---------------------------------------------------------------- wind043

class IkXinhui : public TriggerSkillV2
{
public:
    IkXinhui() : TriggerSkillV2("ikxinhui") { events << PreDamageDone << EventPhaseEnd; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != PreDamageDone)
            return false;
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (from && from->getPhase() == Player::Play && from->getMark("ikxinhui_dealt-PlayClear") == 0)
            room->setPlayerMark(from, "ikxinhui_dealt-PlayClear", 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Play
            || !player->hasSkill(objectName()) || player->getMark("ikxinhui_dealt-PlayClear") > 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QList<ServerPlayer *> chosen = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0, 2,
                                                                       "@ikxinhui", true);
        if (chosen.length() != 2)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = QStringList{chosen.first()->objectName(), chosen.last()->objectName()};
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        foreach (const QString &name, ctx.extra_data.toStringList())
            if (ServerPlayer *p = room->findPlayerByObjectName(name))
                p->drawCards(1, objectName());
        return false;
    }
};

class IkYongji : public TriggerSkillV2
{
public:
    IkYongji() : TriggerSkillV2("ikyongji") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getMark("ikyongji" + currentPhaseClearSuffix(player->getRoom())) >= 3 || !move.from
            || !move.from->isAlive() || move.from->getPhase() != Player::NotActive
            || !move.from_places.contains(Player::PlaceHand) || !move.is_last_handcard)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *from = qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().from);
        if (!from || !player->askForSkillInvoke(objectName(), QVariant::fromValue(from)))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->addPlayerMark(player, "ikyongji" + currentPhaseClearSuffix(room));
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *from = qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().from);
        if (from && from->isAlive())
            from->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- wind044

void moqiDraw(Room *room, ServerPlayer *player)
{
    room->removePlayerMark(player, "@moqi");
    room->doSuperLightbox(player, "ikmoqi");
    room->setPlayerMark(player, "ikmoqi_finish-Clear", 1);
    player->drawCards(2, "ikmoqi");
}

class IkMoqiViewAs : public ViewAsSkillV2
{
public:
    IkMoqiViewAs() : ViewAsSkillV2("ikmoqi") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("@moqi") > 0;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkMoqiCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getMark("@moqi") > 0)
            moqiDraw(ctx.invoker->getRoom(), ctx.invoker);
        return FinishSkill;
    }
};

// Draw two now (start or play phase) and two more at the end phase.
class IkMoqi : public TriggerSkillV2
{
public:
    IkMoqi() : TriggerSkillV2("ikmoqi")
    {
        events << EventPhaseStart;
        frequency = Limited;
        limit_mark = "@moqi";
        view_as_skill = new IkMoqiViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (player->getPhase() == Player::Finish && player->getMark("ikmoqi_finish-Clear") > 0)
            return TriggerList{{player, {objectName()}}};
        if (player->getPhase() == Player::Start && player->hasSkill(objectName()) && player->getMark(limit_mark) > 0)
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (player->getPhase() == Player::Finish)
            return true;
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (player->getPhase() == Player::Finish) {
            room->setPlayerMark(player, "ikmoqi_finish-Clear", 0);
            player->drawCards(2, objectName());
        } else {
            moqiDraw(room, player);
        }
        return false;
    }
};

class IkTianbei : public ViewAsSkillV2
{
public:
    IkTianbei() : ViewAsSkillV2("iktianbei")
    {
        frequency = Limited;
        limit_mark = "@tianbei";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *to) const override
    {
        return selected.isEmpty() && to;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkTianbeiCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }

    // Give up 魔器 and 天杯 to heal and hand someone 安神.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target)
            return ContinueEffects;
        Room *room = source->getRoom();
        const QStringList given{"ikmoqi", "iktianbei"};
        foreach (const QString &name, given)
            if (source->hasSkill(name, true))
                room->detachSkillFromPlayer(source, name);
        if (source->isAlive() && source->isWounded())
            room->recover(source, RecoverStruct(objectName(), source));
        if (!target->isAlive())
            return ContinueEffects;
        room->addPlayerMark(target, "@anshen");
        room->acquireSkillFromEffect(target, "ikanshen", ctx);
        if (target != source)
            target->drawCards(2, objectName());
        return ContinueEffects;
    }
};

// A play phase's opening 杀 may go back to its user.
class IkAnshen : public TriggerSkillV2
{
public:
    IkAnshen() : TriggerSkillV2("ikanshen") { events << BeforeCardsMove; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || move.to_place != Player::DiscardPile
            || !move.from_places.contains(Player::PlaceTable)
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE)
            return TriggerList();
        const Card *used = move.reason.m_extraData.value<const Card *>();
        ServerPlayer *user = room->findPlayerByObjectName(move.reason.m_playerId);
        if (!used || !used->isKindOf("Slash") || used->tag.value("ikai_playcount").toInt() != 1 || !user
            || !user->isAlive() || user->getPhase() != Player::Play)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *user = room->findPlayerByObjectName(ctx.original_data->value<CardsMoveOneTimeStruct>().reason.m_playerId);
        if (!user || !player->askForSkillInvoke(objectName(), QVariant::fromValue(user)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        ServerPlayer *user = room->findPlayerByObjectName(move.reason.m_playerId);
        if (!user || !user->isAlive())
            return false;
        const QList<int> ids = move.card_ids;
        move.removeCardIds(ids);
        *ctx.original_data = QVariant::fromValue(move);
        DummyCard dummy(ids);
        room->obtainCard(user, &dummy);
        return false;
    }
};

// ---------------------------------------------------------------- wind046

class IkDuanmeng : public ViewAsSkillV2
{
public:
    IkDuanmeng() : ViewAsSkillV2("ikduanmeng", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->isKindOf("Slash")
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkDuanmengCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target)
            return ContinueEffects;
        Room *room = source->getRoom();
        const QString kingdom = room->askForKingdom(source, objectName());
        source->setTag("IkDuanmeng", QStringList{target->objectName(), kingdom});
        room->addPlayerMark(source, "@dream");
        room->addPlayerMark(target, "@dream");
        return ContinueEffects;
    }
};

class IkDuanmengEffect : public TriggerSkillV2
{
public:
    IkDuanmengEffect() : TriggerSkillV2("#ikduanmeng")
    {
        events << TargetSpecified << EventPhaseStart;
        global = true;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::RoundStart)
            return false;
        const QStringList info = player->getTag("IkDuanmeng").toStringList();
        if (info.size() != 2)
            return false;
        player->removeTag("IkDuanmeng");
        room->removePlayerMark(player, "@dream");
        if (ServerPlayer *target = room->findPlayerByObjectName(info.first(), true))
            room->removePlayerMark(target, "@dream");
        return false;
    }

    // The protected pair for black cards of the named faction.
    static QStringList guarded(Room *room, ServerPlayer *user)
    {
        QStringList names;
        foreach (ServerPlayer *owner, room->getAllPlayers()) {
            const QStringList info = owner->getTag("IkDuanmeng").toStringList();
            if (info.size() == 2 && info.last() == user->getKingdom() && owner != user)
                names << owner->objectName() << info.first();
        }
        return names;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified || !player)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill || !use.card->isBlack())
            return TriggerList();
        const QStringList names = guarded(room, player);
        foreach (ServerPlayer *to, use.to)
            if (to != player && to->isAlive() && names.contains(to->objectName()))
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const QStringList names = guarded(room, player);
        foreach (ServerPlayer *to, use.to)
            if (to != player && to->isAlive() && names.contains(to->objectName())) {
                room->sendCompulsoryTriggerLog(to, "ikduanmeng");
                to->drawCards(1, "ikduanmeng");
            }
        return false;
    }
};

// ---------------------------------------------------------------- wind052

// Recognised in Player::isWounded (the 归命 coupling).
class IkGuiming : public TriggerSkillV2
{
public:
    IkGuiming() : TriggerSkillV2("ikguiming") { frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- wind024

// At HP 3/2/1 or lower the owner holds 挑衅/狂气/神佑, attached under this instance.
class IkXiewang : public TriggerSkillV2
{
public:
    IkXiewang() : TriggerSkillV2("ikxiewang")
    {
        events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill;
        frequency = Compulsory;
    }

    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner != player || !player->isAlive() || !ctx.activationRef.isValid())
            return;
        if (event == EventAcquireSkill
            && (!ctx.original_data || ctx.original_data->value<SkillChangeStruct>().skillName != objectName()))
            return;
        const QStringList names{"thshenyou", "thkuangqi", "tiaoxin"};
        for (int i = 0; i < names.size(); ++i) {
            const QString name = names.at(i);
            const bool active = player->getHp() <= i + 1;
            bool attached = false;
            for (const SkillInstance &entry : player->getSkillInstances()) {
                if (entry.skillName != name || entry.parentRef != ctx.activationRef)
                    continue;
                attached = true;
                if (!active)
                    room->detachAttachedSkill(SkillInstanceRef(player->objectName(), entry.key()));
            }
            if (active && !attached) {
                room->attachSkillToPlayer(player, name, ctx.activationRef);
                room->notifySkillInvoked(player, objectName());
                if (player->getHp() == i + 1)
                    room->broadcastSkillInvoke(objectName(), 3 - i);
            }
        }
    }
};

// ---------------------------------------------------------------- wind053

// 绝彻 is 困奋; waking flips it through the shared "fengliang" mark.
class IkHewu : public WakeSkill
{
public:
    IkHewu() : WakeSkill("ikhewu") { events << Dying; }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == Dying && data.value<DyingStruct>().who == player;
    }

    bool canAwaken(Room *, ServerPlayer *) const override { return true; }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->addPlayerMark(player, "fengliang");
        if (!room->changeMaxHpForAwakenSkill(player, -1, objectName()) || !player->isAlive())
            return;
        if (player->getHp() < 2)
            room->recover(player, RecoverStruct(player, nullptr, 2 - player->getHp(), objectName()));
        Sanguosha->addTranslationEntry(":kunfen", Sanguosha->translate(":kunfen2").toStdString().c_str());
        room->doNotify(player, QSanProtocol::S_COMMAND_UPDATE_SKILL, QVariant("kunfen"));
        room->acquireSkillFromEffect(player, "bazhen", ctx);
        room->acquireSkillFromEffect(player, "tiaoxin", ctx);
    }
};

// ---------------------------------------------------------------- wind054

class IkYuzhi : public TriggerSkillV2
{
public:
    IkYuzhi() : TriggerSkillV2("ikyuzhi")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Reveal three; an equipment discard buys the tricks among them.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        const QList<int> ids = room->getNCards(3, false);
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        room->getThread()->delay();
        QList<int> tricks, rest;
        foreach (int id, ids)
            (Sanguosha->getCard(id)->isKindOf("TrickCard") ? tricks : rest) << id;
        if (!tricks.isEmpty()) {
            if (player->isAlive() && player->canDiscard(player, "he")
                && room->askForCard(player, ".Equip", "@ikyuzhi", QVariant(), objectName())) {
                DummyCard get(tricks);
                room->obtainCard(player, &get);
            } else {
                rest << tricks;
            }
        }
        QList<int> left;
        foreach (int id, rest)
            if (room->getCardPlace(id) == Player::PlaceTable)
                left << id;
        if (!left.isEmpty()) {
            DummyCard thro(left);
            room->throwCard(&thro, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, QString(), objectName(), QString()),
                            nullptr);
        }
        return false;
    }
};

// Without a treasure the owner holds 集智, attached under this instance.
class IkLinglong : public TriggerSkillV2
{
public:
    IkLinglong() : TriggerSkillV2("iklinglong")
    {
        events << GameStart << EventAcquireSkill << CardsMoveOneTime;
        frequency = Compulsory;
    }

    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner != player || !player->isAlive() || !ctx.activationRef.isValid())
            return;
        if (event == EventAcquireSkill
            && (!ctx.original_data || ctx.original_data->value<SkillChangeStruct>().skillName != objectName()))
            return;
        const bool active = !player->getTreasure();
        bool attached = false;
        for (const SkillInstance &entry : player->getSkillInstances()) {
            if (entry.skillName != "jizhi" || entry.parentRef != ctx.activationRef)
                continue;
            attached = true;
            if (!active)
                room->detachAttachedSkill(SkillInstanceRef(player->objectName(), entry.key()));
        }
        if (active && !attached) {
            room->attachSkillToPlayer(player, "jizhi", ctx.activationRef);
            room->notifySkillInvoked(player, objectName());
        }
    }
};

class IkLinglongArmor : public ViewAsEquipSkill
{
public:
    IkLinglongArmor() : ViewAsEquipSkill("#iklinglong-armor") {}

    QString viewAsEquip(const Player *target) const override
    {
        return target->hasEquipArea(1) && !target->getArmor() ? "eight_diagram" : QString();
    }
};

class IkLinglongMax : public MaxCardsSkillV2
{
public:
    IkLinglongMax() : MaxCardsSkillV2("#iklinglong-horse") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getDefensiveHorse() || ctx.primary->getOffensiveHorse())
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(1);
    }
};

// ---------------------------------------------------------------- wind059

class IkFengyuan : public TriggerSkillV2
{
public:
    IkFengyuan() : TriggerSkillV2("ikfengyuan") { events << EventPhaseEnd; }

    static QList<ServerPlayer *> uneven(Room *room)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getHp() != p->getHandcardNum())
                targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || !player->hasSkill(objectName())
            || uneven(room).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, uneven(room), objectName(), "@ikfengyuan", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    // Nudge the target one card toward its HP; landing on it pays the owner.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return false;
        QStringList choices;
        if (target->canDiscard(target, "h"))
            choices << "discard";
        choices << "draw";
        if (room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target)) == "discard")
            room->askForDiscard(target, objectName(), 1, 1);
        else
            target->drawCards(1, objectName());
        if (!target->isAlive() || target->getHp() != target->getHandcardNum() || !player->isAlive())
            return false;
        player->drawCards(1, objectName());
        if (player == target || player->isNude() || !target->isAlive())
            return false;
        const Card *gift = room->askForExchange(player, objectName(), 1, 1, true,
                                                "@ikfengyuan-give:" + target->objectName(), true);
        if (gift) {
            room->giveCard(player, target, gift, objectName());
            delete gift;
        }
        return false;
    }
};

class IkMianlai : public TriggerSkillV2
{
public:
    IkMianlai() : TriggerSkillV2("ikmianlai") { events << TargetConfirming; }

    static QList<ServerPlayer *> extras(Room *room, ServerPlayer *player, const CardUseStruct &use)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (use.to.contains(p) || use.from->isProhibited(p, use.card))
                continue;
            if (use.card->targetFixed() || use.card->targetFilter(QList<const Player *>(), p, use.from))
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card || !use.from
            || !use.to.contains(player) || !use.card->isNDTrick() || use.card->isKindOf("Collateral")
            || use.card->isKindOf("HBurningCamps")
            || extras(room, player, use).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *target = room->askForPlayerChosen(player, extras(room, player, use), objectName(), "@ikmianlai", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!target || !target->isAlive() || use.to.contains(target))
            return false;
        LogMessage log;
        log.type = "#BecomeTarget";
        log.from = target;
        log.card_str = use.card->toString();
        room->sendLog(log);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, use.from->objectName(), target->objectName());
        use.to.append(target);
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

// ---------------------------------------------------------------- wind060

class IkChunsu : public TriggerSkillV2
{
public:
    IkChunsu() : TriggerSkillV2("ikchunsu") { events << DrawNCards << AfterDrawNCards << DamageCaused; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == DamageCaused) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from != player || damage.nature == DamageStruct::Thunder || player->getPile("sincerity").isEmpty())
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        const DrawStruct draw = data.value<DrawStruct>();
        if (draw.who != player || draw.reason != "draw_phase")
            return TriggerList();
        if (event == AfterDrawNCards && (player->getMark("ikchunsu_put-Clear") == 0 || player->isKongcheng()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (event != DrawNCards)
            return true;
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Draw two more and set a 愫 aside; while one is there, non-thunder damage is withheld.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == DamageCaused) {
            room->sendCompulsoryTriggerLog(player, objectName());
            return true;
        }
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += 2;
            *ctx.original_data = QVariant::fromValue(draw);
            room->setPlayerMark(player, "ikchunsu_put-Clear", 1);
            return false;
        }
        room->setPlayerMark(player, "ikchunsu_put-Clear", 0);
        const Card *card = room->askForExchange(player, objectName(), 1, 1, false, "@ikchunsu-put");
        if (!card)
            return false;
        player->addToPile("sincerity", card);
        delete card;
        return false;
    }
};

class IkYingzhi : public TriggerSkillV2
{
public:
    IkYingzhi() : TriggerSkillV2("ikyingzhi") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !player->isAlive() || !move.from_places.contains(Player::PlaceHand)
            || !move.is_last_handcard)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->getMark("ikyingzhi-Clear") == 0
                && !owner->getPile("sincerity").isEmpty())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        const QList<int> pile = owner->getPile("sincerity");
        room->fillAG(pile, owner);
        const int id = room->askForAG(owner, pile, false, objectName());
        room->clearAG(owner);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString());
        room->throwCard(Sanguosha->getCard(id), reason, nullptr);
        room->setPlayerMark(owner, "ikyingzhi-Clear", 1);
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        if (player->isAlive())
            player->drawCards(2, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- wind061

class IkQixin : public ViewAsSkillV2
{
public:
    IkQixin() : ViewAsSkillV2("ikqixin") {}

    static int sum(const ActiveSkillRequest &request)
    {
        int total = 0;
        foreach (int id, request.selectedCardIds)
            total += Sanguosha->getCard(id)->getNumber();
        return total;
    }

    static bool open(const Player *to) { return to->getMark("ikqixin_target-Clear") == 0; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || self->isKongcheng())
            return false;
        foreach (const Player *p, self->getAliveSiblings())
            if (open(p))
                return true;
        return false;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsHandCard(request.initiator, card) && sum(request) + card->getNumber() <= 13;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && sum(request) == 13 && selectionValid(this, request);
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && open(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkQixinCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !ctx.use_card)
            return ContinueEffects;
        Room *room = source->getRoom();
        room->setPlayerMark(target, "ikqixin_target-Clear", 1);
        room->setPlayerMark(source, "ikqixin_used-PlayClear", 1);
        QList<int> ids;
        foreach (int id, ctx.use_card->getSubcards())
            if (room->getCardOwner(id) == source && room->getCardPlace(id) == Player::PlaceHand)
                ids << id;
        if (!ids.isEmpty() && target->isAlive())
            room->giveCard(source, target, ids, objectName());
        if (target->isAlive() && target->isWounded())
            room->recover(target, RecoverStruct(objectName(), source));
        return ContinueEffects;
    }
};

class IkGuangyou : public TriggerSkillV2
{
public:
    IkGuangyou() : TriggerSkillV2("ikguangyou")
    {
        events << EventPhaseEnd;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || !player->hasSkill(objectName())
            || player->getMark("ikqixin_used-PlayClear") > 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (room->askForChoice(player, objectName(), "add+draw") == "add")
            room->setPlayerMark(player, "ikguangyou-Clear", 1);
        else
            player->drawCards(player->getMaxHp(), objectName());
        return false;
    }
};

class IkGuangyouMax : public MaxCardsSkillV2
{
public:
    IkGuangyouMax() : MaxCardsSkillV2("#ikguangyou") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("ikguangyou-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(ctx.primary->getMaxHp());
    }
};

// ---------------------------------------------------------------- bloom024

class IkXinban : public ViewAsSkillV2
{
public:
    IkXinban() : ViewAsSkillV2("ikxinban", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    static int slotOf(const ActiveSkillRequest &request)
    {
        if (request.selectedCardIds.size() != 1)
            return -1;
        const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(request.selectedCardIds.first())->getRealCard());
        return equip ? static_cast<int>(equip->location()) : -1;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isNude();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->getTypeId() == Card::TypeEquip;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        const int slot = slotOf(request);
        return selected.isEmpty() && to && slot >= 0 && to->hasEquipArea(slot) && !to->getEquip(slot);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkXinbanCard"; }

    // Equip the target, then discard near it, let it draw, or trade the equipment for a heal.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
        if (room->getCardOwner(id) != source || !equip || !target->hasEquipArea(static_cast<int>(equip->location()))
            || target->getEquip(static_cast<int>(equip->location())))
            return ContinueEffects;
        room->moveCardTo(Sanguosha->getCard(id), source, target, Player::PlaceEquip,
                         CardMoveReason(CardMoveReason::S_REASON_PUT, source->objectName(), objectName(), QString()));
        if (!source->isAlive() || !target->isAlive())
            return ContinueEffects;
        QList<ServerPlayer *> near;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if ((p == target || target->inMyAttackRange(p)) && source->canDiscard(p, "hej"))
                near << p;
        QStringList choices;
        if (!near.isEmpty())
            choices << "discard";
        choices << "draw";
        const bool equipped = room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceEquip;
        if (equipped && source->canDiscard(target, id))
            choices << "recover";
        const QString choice = room->askForChoice(source, objectName(), choices.join("+"), QVariant::fromValue(target));
        if (choice == "discard") {
            ServerPlayer *victim = room->askForPlayerChosen(source, near, objectName(), "@ikxinban-discard:" + target->objectName());
            const int cid = victim ? room->askForCardChosen(source, victim, "hej", objectName(), false, Card::MethodDiscard) : -1;
            if (cid >= 0)
                room->throwCard(cid, victim, source);
        } else if (choice == "recover") {
            room->throwCard(id, target, source);
            if (target->isAlive() && target->isWounded())
                room->recover(target, RecoverStruct(objectName(), source));
        } else {
            target->drawCards(1, objectName());
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- bloom028

class IkHuyin : public ViewAsSkillV2
{
public:
    IkHuyin() : ViewAsSkillV2("ikhuyin", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isNude();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkHuyinCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != source)
            return ContinueEffects;
        source->addToPile("rhythm", id);
        room->addPlayerMark(target, "@arc");
        room->addPlayerMark(target, "ikhuyin_" + source->objectName());
        return ContinueEffects;
    }
};

// The marked character pays for matching the 律's type: one HP, and it takes the 律.
class IkHuyinEffect : public TriggerSkillV2
{
public:
    IkHuyinEffect() : TriggerSkillV2("#ikhuyin")
    {
        events << CardUsed << CardResponded << CardsMoveOneTime << EventPhaseStart;
        frequency = Compulsory;
        global = true;
    }

    // The owner reclaims every 律 at its round start and the marks lapse.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::RoundStart)
            return false;
        const QString mark = "ikhuyin_" + player->objectName();
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            const int n = p->getMark(mark);
            if (n == 0)
                continue;
            room->setPlayerMark(p, mark, 0);
            room->removePlayerMark(p, "@arc", n);
        }
        const QList<int> pile = player->getPile("rhythm");
        if (!pile.isEmpty() && player->isAlive()) {
            DummyCard dummy(pile);
            room->obtainCard(player, &dummy);
        }
        return false;
    }

    static QStringList types(TriggerEvent event, ServerPlayer *player, const QVariant &data)
    {
        QStringList list;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill)
                list << use.card->getType();
        } else if (event == CardResponded) {
            const CardResponseStruct resp = data.value<CardResponseStruct>();
            if (resp.m_card && resp.m_card->getTypeId() != Card::TypeSkill)
                list << resp.m_card->getType();
        } else if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player
                || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
                return list;
            for (int i = 0; i < move.card_ids.size(); ++i) {
                const Player::Place from = move.from_places.value(i);
                if (from == Player::PlaceHand || from == Player::PlaceEquip)
                    list << Sanguosha->getCard(move.card_ids.at(i))->getType();
            }
        }
        return list;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart || !player || !player->isAlive())
            return result;
        const QStringList list = types(event, player, data);
        if (list.isEmpty())
            return result;
        foreach (ServerPlayer *owner, room->getAllPlayers()) {
            if (player->getMark("ikhuyin_" + owner->objectName()) == 0)
                continue;
            foreach (int id, owner->getPile("rhythm"))
                if (list.contains(Sanguosha->getCard(id)->getType())) {
                    result[owner] << objectName();
                    break;
                }
        }
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        const QString mark = "ikhuyin_" + owner->objectName();
        if (player->getMark(mark) == 0)
            return false;
        room->sendCompulsoryTriggerLog(owner, "ikhuyin");
        room->removePlayerMark(player, mark);
        room->removePlayerMark(player, "@arc");
        const QList<int> pile = owner->getPile("rhythm");
        if (!pile.isEmpty()) {
            DummyCard dummy(pile);
            room->obtainCard(player, &dummy);
        }
        if (player->isAlive())
            room->loseHp(player, 1, true, owner, "ikhuyin");
        return false;
    }
};

class IkHongcai : public TriggerSkillV2
{
public:
    IkHongcai() : TriggerSkillV2("ikhongcai") { events << TargetSpecified; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || !use.card->isKindOf("TrickCard") || use.to.length() < 2)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Draw one; a targeted owner is spared the trick.
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        owner->drawCards(1, objectName());
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.to.contains(owner) && !use.nullified_list.contains(owner->objectName())) {
            use.nullified_list << owner->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        }
        return false;
    }
};

// ---------------------------------------------------------------- bloom033

// Hands up to three characters 飞影, 灵卫 or 智迟 until the owner's next turn.
class IkXunyuyouli : public TriggerSkillV2
{
public:
    IkXunyuyouli() : TriggerSkillV2("ikxunyuyouli") { events << EventPhaseChanging << EventPhaseStart << Death; }

    static QString key(const ServerPlayer *owner) { return "IkXunyuyouli_" + owner->objectName(); }

    static void revokeAll(Room *room, ServerPlayer *owner)
    {
        foreach (ServerPlayer *p, room->getAllPlayers())
            revokeTracked(room, p, key(owner));
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if ((event == EventPhaseStart && player->getPhase() == Player::RoundStart)
            || (event == Death && data.value<DeathStruct>().who == player))
            revokeAll(room, player);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QList<ServerPlayer *> chosen = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0, 3,
                                                                       "@ikxunyuyouli", true);
        if (chosen.isEmpty())
            return false;
        room->broadcastSkillInvoke(objectName());
        QStringList names;
        foreach (ServerPlayer *p, chosen)
            names << p->objectName();
        ctx.extra_data = names;
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QStringList pool{"feiying", "rhlingwei", "zhichi"};
        foreach (const QString &name, ctx.extra_data.toStringList()) {
            ServerPlayer *target = room->findPlayerByObjectName(name);
            if (!target || !target->isAlive())
                continue;
            QStringList choices;
            foreach (const QString &skill, pool)
                if (!target->hasSkill(skill, true))
                    choices << skill;
            if (choices.isEmpty())
                continue;
            const QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target));
            grantTracked(room, target, key(player), choice);
        }
        return false;
    }
};

// ---------------------------------------------------------------- bloom040

// The kill reward/penalty skip lives in GameRule's BuryVictim.
class IkTianzuoyounai : public TriggerSkillV2
{
public:
    IkTianzuoyounai() : TriggerSkillV2("iktianzuoyounai")
    {
        events << Death;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const ServerPlayer *dead = data.value<DeathStruct>().who;
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !dead || dead == player)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        player->drawCards(3, objectName());
        return false;
    }
};

class IkShenji : public TriggerSkillV2
{
public:
    IkShenji() : TriggerSkillV2("ikshenji")
    {
        events << EventPhaseStart;
        frequency = Limited;
        limit_mark = "@shenji";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName())
            || player->getMark(limit_mark) == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->removePlayerMark(player, limit_mark);
        room->doSuperLightbox(player, objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->drawCards(3, objectName());
        const QList<int> judges = player->getJudgingAreaID();
        if (!judges.isEmpty() && player->isAlive()) {
            DummyCard dummy(judges);
            room->throwCard(&dummy, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName()), nullptr);
        }
        return false;
    }
};

class IkKuanglu : public TriggerSkillV2
{
public:
    IkKuanglu() : TriggerSkillV2("ikkuanglu") { events << EventPhaseStart << PreDamageDone; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != PreDamageDone)
            return false;
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (from && from->hasSkill(objectName()) && isOwnTurn(from))
            room->addPlayerMark(from, "ikkuanglu_damage-Clear", data.value<DamageStruct>().damage);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play
            || !player->hasSkill(objectName()) || !player->isWounded())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->drawCards(1, objectName());
        room->setPlayerMark(player, "ikkuanglu-Clear", 1);
        return false;
    }
};

class IkKuangluMax : public MaxCardsSkillV2
{
public:
    IkKuangluMax() : MaxCardsSkillV2("#ikkuanglu") { frequency = Compulsory; }

    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("ikkuanglu-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(qMin(ctx.primary->getMark("ikkuanglu_damage-Clear"), 4));
    }
};

// ---------------------------------------------------------------- bloom041

class IkBenhua : public TriggerSkillV2
{
public:
    IkBenhua() : TriggerSkillV2("ikbenhua") { events << TargetConfirmed; }

    static QList<ServerPlayer *> near(ServerPlayer *owner, const CardUseStruct &use)
    {
        QList<ServerPlayer *> list;
        foreach (ServerPlayer *to, use.to) {
            const int dis = owner->distanceTo(to);
            if (to->isAlive() && dis != -1 && dis <= 1 && !list.contains(to))
                list << to;
        }
        return list;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !use.card || !use.card->isKindOf("Slash") || use.to.isEmpty() || use.to.first() != player)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && !near(owner, use).isEmpty())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }

    // Per nearby target: hand it a card it reveals (equipment may go straight on), then draw.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        foreach (ServerPlayer *to, near(owner, use)) {
            if (!owner->isAlive() || !owner->hasSkill(objectName()) || !to->isAlive()
                || !owner->askForSkillInvoke(objectName(), QVariant::fromValue(to)))
                continue;
            room->broadcastSkillInvoke(objectName());
            if (owner != to && !owner->isNude()) {
                const Card *gift = room->askForExchange(owner, objectName(), 1, 1, true, "@ikbenhua-give:" + to->objectName());
                const int id = gift && !gift->getSubcards().isEmpty() ? gift->getSubcards().first() : -1;
                delete gift;
                if (id >= 0) {
                    room->giveCard(owner, to, QList<int>() << id, objectName(), true);
                    const Card *card = Sanguosha->getCard(id);
                    if (room->getCardOwner(id) == to && room->getCardPlace(id) == Player::PlaceHand) {
                        room->showCard(to, id);
                        if (card->getTypeId() == Card::TypeEquip && !to->isLocked(card)
                            && room->askForSkillInvoke(to, "ikbenhua_use", "use"))
                            room->useCard(CardUseStruct(card, to, to));
                    }
                }
            }
            if (owner->isAlive())
                owner->drawCards(1, objectName());
        }
        return false;
    }
};

// ---------------------------------------------------------------- bloom043

static int equipSlot(int id)
{
    const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
    return equip ? static_cast<int>(equip->location()) : -1;
}

class IkYaocheng : public TriggerSkillV2
{
public:
    IkYaocheng() : TriggerSkillV2("ikyaocheng") { events << EventPhaseStart; }

    static QList<ServerPlayer *> openSlots(Room *room, int id)
    {
        QList<ServerPlayer *> targets;
        const int slot = equipSlot(id);
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (slot >= 0 && p->hasEquipArea(slot) && !p->getEquip(slot))
                targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Discard || !player->hasSkill(objectName())
            || player->isNude())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const Card *card = room->askForExchange(player, objectName(), 1, 1, true, "@ikyaocheng-equip", true, "EquipCard");
        if (!card)
            return false;
        const int id = card->getSubcards().value(0, -1);
        delete card;
        if (id < 0 || openSlots(room, id).isEmpty())
            return false;
        ServerPlayer *target = room->askForPlayerChosen(player, openSlots(room, id), objectName(), "@ikyaocheng-target", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = QVariantList{id, target->objectName()};
        return true;
    }

    // Put the equipment on someone, then discard from a character two steps from it.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QVariantList info = ctx.extra_data.toList();
        const int id = info.value(0).toInt();
        ServerPlayer *target = room->findPlayerByObjectName(info.value(1).toString());
        const int slot = equipSlot(id);
        if (!target || !target->isAlive() || room->getCardOwner(id) != player || slot < 0 || target->getEquip(slot))
            return false;
        room->moveCardTo(Sanguosha->getCard(id), player, target, Player::PlaceEquip,
                         CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(), QString()));
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (target->isAlive() && target->distanceTo(p) == 2 && player->canDiscard(p, "he"))
                victims << p;
        if (victims.isEmpty() || !player->isAlive())
            return false;
        ServerPlayer *victim = room->askForPlayerChosen(player, victims, objectName(), "@ikyaocheng-discard:" + target->objectName());
        const int cid = victim ? room->askForCardChosen(player, victim, "he", objectName(), false, Card::MethodDiscard) : -1;
        if (cid >= 0)
            room->throwCard(cid, victim, player);
        return false;
    }
};

// A run of seats including the owner holds 飞影 until the owner's next turn ends.
class IkHaobi : public TriggerSkillV2
{
public:
    IkHaobi() : TriggerSkillV2("ikhaobi") { events << EventPhaseChanging << Death; }

    static QString key(const ServerPlayer *owner) { return "IkHaobi_" + owner->objectName(); }

    static void revokeAll(Room *room, ServerPlayer *owner)
    {
        foreach (ServerPlayer *p, room->getAllPlayers())
            revokeTracked(room, p, key(owner));
    }

    // The seats from `head` to `tail` along the arc that holds `self`.
    static QList<ServerPlayer *> segment(Room *room, ServerPlayer *self, ServerPlayer *head, ServerPlayer *tail)
    {
        const QList<ServerPlayer *> seats = room->getAlivePlayers();
        const int n = seats.length();
        auto walk = [&](int from, int to) {
            QList<ServerPlayer *> run;
            for (int i = from;; i = (i + 1) % n) {
                run << seats.at(i);
                if (i == to)
                    break;
            }
            return run;
        };
        const int a = seats.indexOf(head), b = seats.indexOf(tail);
        if (a < 0 || b < 0)
            return QList<ServerPlayer *>();
        QList<ServerPlayer *> run = walk(a, b);
        if (head != self && tail != self && !run.contains(self))
            run = walk(b, a);
        return run;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if ((event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            || (event == Death && data.value<DeathStruct>().who == player))
            revokeAll(room, player);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::NotActive || room->alivePlayerCount() < 2)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QList<ServerPlayer *> ends = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0, 2,
                                                                     "@ikhaobi", true);
        if (ends.length() != 2)
            return false;
        QStringList names;
        foreach (ServerPlayer *p, segment(room, player, ends.first(), ends.last()))
            if (p != player)
                names << p->objectName();
        if (names.isEmpty())
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = names;
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        foreach (const QString &name, ctx.extra_data.toStringList()) {
            ServerPlayer *p = room->findPlayerByObjectName(name);
            if (p && p->isAlive())
                grantTracked(room, p, key(player), "feiying");
        }
        return false;
    }
};

// ---------------------------------------------------------------- bloom044

class IkAoxue : public ViewAsSkillV2
{
public:
    IkAoxue() : ViewAsSkillV2("ikaoxue") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && to->inMyAttackRange(request.initiator);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkAoxueCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->addPlayerMark(target, "@snow");
        room->addPlayerMark(target, "ikaoxue_" + source->objectName());
        return ContinueEffects;
    }
};

// Marks lapse at the owner's next round; in the marked character's turn the owner is at 1 and unarmoured to it.
class IkAoxueEffect : public TriggerSkillV2
{
public:
    IkAoxueEffect() : TriggerSkillV2("#ikaoxue")
    {
        events << EventPhaseStart << EventPhaseChanging << Death;
        frequency = Compulsory;
        global = true;
    }

    static QString reason(const ServerPlayer *owner) { return "ikaoxue_" + owner->objectName(); }

    static void unbind(Room *room, ServerPlayer *snowed, ServerPlayer *owner)
    {
        if (!owner->hasFlag("ikaoxue_bound_" + snowed->objectName()))
            return;
        room->setPlayerFlag(owner, "-ikaoxue_bound_" + snowed->objectName());
        room->removeFixedDistance(snowed, owner, 1);
        room->removePlayerEquipsNullified(owner, "Armor|.|.|.|target:" + snowed->objectName(), reason(owner));
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if (event == EventPhaseStart && player->getPhase() == Player::RoundStart) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                const int n = p->getMark(reason(player));
                if (n == 0)
                    continue;
                unbind(room, p, player);
                room->setPlayerMark(p, reason(player), 0);
                room->removePlayerMark(p, "@snow", n);
            }
            if (player->getMark("@snow") == 0)
                return false;
            foreach (ServerPlayer *owner, room->getOtherPlayers(player))
                if (player->getMark(reason(owner)) > 0 && !owner->hasFlag("ikaoxue_bound_" + player->objectName())) {
                    room->setPlayerFlag(owner, "ikaoxue_bound_" + player->objectName());
                    room->setFixedDistance(player, owner, 1);
                    room->setPlayerEquipsNullified(owner, "Armor|.|.|.|target:" + player->objectName(), reason(owner), false);
                }
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            foreach (ServerPlayer *owner, room->getAllPlayers())
                unbind(room, player, owner);
        } else if (event == Death) {
            ServerPlayer *dead = data.value<DeathStruct>().who;
            if (dead != player)
                return false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                unbind(room, p, dead);
                unbind(room, dead, p);
                const int n = p->getMark(reason(dead));
                if (n > 0) {
                    room->setPlayerMark(p, reason(dead), 0);
                    room->removePlayerMark(p, "@snow", n);
                }
            }
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class IkAoxueProhibit : public ProhibitSkill
{
public:
    IkAoxueProhibit() : ProhibitSkill("#ikaoxue-prohibit") {}

    // A snowed character may only slash the owners who marked it.
    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return from && to && card && card->isKindOf("Slash") && from->getMark("@snow") > 0
            && from->getMark("ikaoxue_" + to->objectName()) == 0;
    }
};

class IkLingxue : public TriggerSkillV2
{
public:
    IkLingxue() : TriggerSkillV2("iklingxue") { events << Damaged << EventPhaseChanging; }

    static QString count(const ServerPlayer *owner) { return "iklingxue_" + owner->objectName() + "-Clear"; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == Damaged) {
            ServerPlayer *current = room->getCurrent();
            if (!player->isAlive() || !player->hasSkill(objectName()) || !current || !current->isAlive()
                || current->getPhase() == Player::NotActive)
                return result;
            for (int i = 0; i < data.value<DamageStruct>().damage; ++i)
                result[player] << objectName();
            return result;
        }
        if (data.value<PhaseChangeStruct>().to != Player::NotActive || player->getMark("iklingxue_discard-Clear") > 0)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && player->getMark(count(owner)) > 0)
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != Damaged)
            return true;
        ServerPlayer *current = room->getCurrent();
        if (!current || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(current)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Shrink the current character's hand limit; if it then keeps its whole hand, the owner profits.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == Damaged) {
            ServerPlayer *current = room->getCurrent();
            if (!current)
                return false;
            room->addPlayerMark(current, "@blizzard");
            room->addPlayerMark(current, count(owner));
            return false;
        }
        const int n = qMin(player->getMark(count(owner)), 2);
        LogMessage log;
        log.type = "#IkLingxueDraw";
        log.from = player;
        log.to << owner;
        log.arg = objectName();
        log.arg2 = "0";
        room->sendLog(log);
        QStringList choices{"obtain", "draw"};
        for (int i = 0; i < n && owner->isAlive(); ++i) {
            if (player->isNude() || !player->isAlive())
                choices.removeOne("obtain");
            if (choices.isEmpty())
                break;
            const QString choice = room->askForChoice(owner, objectName(), choices.join("+"), QVariant::fromValue(player));
            choices.removeOne(choice);
            if (choice == "obtain") {
                const int id = room->askForCardChosen(owner, player, "he", objectName());
                if (id >= 0)
                    room->obtainCard(owner, id, false);
            } else {
                owner->drawCards(1, objectName());
            }
        }
        return false;
    }
};

class IkLingxueRecord : public TriggerSkillV2
{
public:
    IkLingxueRecord() : TriggerSkillV2("#iklingxue-record")
    {
        events << CardsMoveOneTime << EventPhaseChanging;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::NotActive && player->getMark("@blizzard") > 0)
                room->setPlayerMark(player, "@blizzard", 0);
            return false;
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == player && player->getPhase() == Player::Discard
            && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
            && player->getMark("iklingxue_discard-Clear") == 0)
            room->setPlayerMark(player, "iklingxue_discard-Clear", 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class IkLingxueMax : public MaxCardsSkillV2
{
public:
    IkLingxueMax() : MaxCardsSkillV2("#iklingxue-max")
    {
        frequency = Compulsory;
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("@blizzard") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(-ctx.primary->getMark("@blizzard"));
    }
};

// ---------------------------------------------------------------- bloom046

class IkGonghu : public TriggerSkillV2
{
public:
    IkGonghu() : TriggerSkillV2("ikgonghu")
    {
        events << Death;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const ServerPlayer *dead = data.value<DeathStruct>().who;
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !dead || dead == player)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        room->gainMaxHp(player, 1, objectName());
        if (player->isAlive() && player->isWounded())
            room->recover(player, RecoverStruct(objectName(), player));
        return false;
    }
};

class IkXuewu : public TriggerSkillV2
{
public:
    IkXuewu() : TriggerSkillV2("ikxuewu")
    {
        events << MaxHpChanged << HpLost;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canDiscard(p, "he"))
                targets << p;
        ServerPlayer *target = targets.isEmpty() ? nullptr
                                                 : room->askForPlayerChosen(player, targets, objectName(), "@ikxuewu", true, true);
        const int id = target ? room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard) : -1;
        if (id >= 0)
            room->throwCard(id, target, player);
        else
            player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- bloom050

class IkQingwei : public TriggerSkillV2
{
public:
    IkQingwei() : TriggerSkillV2("ikqingwei") { events << TargetConfirming << EventPhaseChanging; }

    // The use's cards go back to their user when the turn ends.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            const QList<int> ids = ListV2I(p->getTag("IkQingweiCards").toList());
            if (ids.isEmpty())
                continue;
            p->removeTag("IkQingweiCards");
            QList<int> back;
            foreach (int id, ids)
                if (room->getCardPlace(id) == Player::PlaceSpecial)
                    back << id;
            if (!back.isEmpty() && p->isAlive()) {
                DummyCard dummy(back);
                room->obtainCard(p, &dummy);
            }
        }
        return false;
    }

    static bool transferable(Room *, const CardUseStruct &use, ServerPlayer *owner)
    {
        if (!use.from || use.from == owner || use.card->isKindOf("DelayedTrick") || use.card->isKindOf("Collateral")
            || use.card->isKindOf("FeintAttack") || use.from->isProhibited(owner, use.card))
            return false;
        return !use.card->isKindOf("Slash") || use.from->canSlash(owner, use.card, false);
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetConfirming || !player || !player->isAlive())
            return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.to.length() != 1 || use.to.first() != player
            || !(use.card->isKindOf("Slash") || (use.card->getTypeId() == Card::TypeTrick && use.card->isBlack())))
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->getHp() > player->getHp() && owner->canDiscard(owner, "he"))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, "..", "@ikqingwei", *ctx.original_data, objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Void the card into 忍, or draw and take it in the target's place.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QStringList choices{"null"};
        if (transferable(room, use, owner))
            choices << "draw";
        if (room->askForChoice(owner, objectName(), choices.join("+"), *ctx.original_data) == "null") {
            if (!use.nullified_list.contains("_ALL_TARGETS"))
                use.nullified_list << "_ALL_TARGETS";
            *ctx.original_data = QVariant::fromValue(use);
            QList<int> ids;
            foreach (int id, use.card->isVirtualCard() ? use.card->getSubcards() : QList<int>{use.card->getEffectiveId()})
                if (id >= 0 && room->getCardPlace(id) == Player::PlaceTable)
                    ids << id;
            if (ids.isEmpty() || !owner->isAlive())
                return false;
            owner->addToPile("endure", ids);
            if (use.from) {
                QList<int> held = ListV2I(use.from->getTag("IkQingweiCards").toList());
                held << ids;
                use.from->setTag("IkQingweiCards", ListI2V(held));
            }
            return false;
        }
        owner->drawCards(1, objectName());
        use = ctx.original_data->value<CardUseStruct>();
        if (!owner->isAlive() || !use.to.contains(player) || !transferable(room, use, owner))
            return false;
        use.to.removeOne(player);
        if (!use.to.contains(owner))
            use.to << owner;
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        room->getThread()->trigger(TargetConfirming, room, owner, *ctx.original_data);
        return false;
    }
};

// ---------------------------------------------------------------- bloom051

class IkZhiyu : public ViewAsSkillV2
{
public:
    IkZhiyu() : ViewAsSkillV2("ikzhiyu") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkZhiyuCard"; }

    // Lose 1 HP for this turn's basic conversion, distance-free tricks, or a stripping 杀.
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        room->loseHp(source, 1, true, source, objectName());
        if (!source->isAlive())
            return FinishSkill;
        const QString choice = room->askForChoice(source, objectName(), "basic+trick+equip");
        if (choice == "basic") {
            grantTracked(room, source, "IkZhiyuGrant", "ikzhiyuv");
        } else if (choice == "trick") {
            source->drawCards(2, objectName());
            room->setPlayerMark(source, "ikzhiyu_trick-Clear", 1);
        } else {
            source->drawCards(1, objectName());
            if (source->isAlive() && room->askForCard(source, "EquipCard", "@ikzhiyu-equip", QVariant(), objectName()))
                room->setPlayerMark(source, "ikzhiyu_equip-Clear", 1);
        }
        return FinishSkill;
    }
};

// 智愚's first mode: a basic card as any basic card, for the rest of the turn.
class IkZhiyuViewAs : public ViewAsSkillV2
{
public:
    IkZhiyuViewAs() : ViewAsSkillV2("ikzhiyuv", 1) { setResponseOrUse(true); }

    static bool isBasicName(const QString &name)
    {
        const Card *card = Sanguosha->cloneCard(name);
        const bool basic = card && card->getTypeId() == Card::TypeBasic;
        delete card;
        return basic;
    }

    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false, false); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->isKongcheng())
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return true;
        const QString pattern = request.pattern.toLower();
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE)
            && (pattern.contains("slash") || pattern.contains("jink") || pattern.contains("peach")
                || pattern.contains("analeptic"));
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && card && card->getTypeId() == Card::TypeBasic
            && (ownsHandCard(request.initiator, card) || request.initiator->getHandPile().contains(card->getEffectiveId()));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

protected:
    bool allowDeclaration(const Player *, const QString &name) const override { return isBasicName(name); }
};

class IkZhiyuEffect : public TriggerSkillV2
{
public:
    IkZhiyuEffect() : TriggerSkillV2("#ikzhiyu") { events << TargetSpecified << EventPhaseChanging; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive)
            revokeTracked(room, player, "IkZhiyuGrant");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified || !player || !player->isAlive() || player->getMark("ikzhiyu_equip-Clear") == 0)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || !use.card->isKindOf("Slash"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        foreach (ServerPlayer *p, ctx.original_data->value<CardUseStruct>().to) {
            if (!player->isAlive() || !p->isAlive() || !player->canDiscard(p, "he")
                || !player->askForSkillInvoke("ikzhiyu", QVariant::fromValue(p)))
                continue;
            room->broadcastSkillInvoke("ikzhiyu");
            QList<int> ids;
            for (int i = 0; i < 2 && player->canDiscard(p, "he") && ids.length() < p->getCardCount(); ++i) {
                const int id = room->askForCardChosen(player, p, "he", "ikzhiyu", false, Card::MethodDiscard, ids);
                if (id < 0)
                    break;
                ids << id;
            }
            if (!ids.isEmpty())
                room->throwCard(ids, "ikzhiyu", p, player);
        }
        return false;
    }
};

class IkZhiyuTargetMod : public TargetModSkillV2
{
public:
    IkZhiyuTargetMod() : TargetModSkillV2("#ikzhiyu-tar", "TrickCard") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || ctx.primary->getMark("ikzhiyu_trick-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1000);
    }
};

// ---------------------------------------------------------------- bloom059

class IkSuzhong : public TriggerSkillV2
{
public:
    IkSuzhong() : TriggerSkillV2("iksuzhong") { events << TargetSpecified; }

    static QList<ServerPlayer *> outsiders(Room *room, ServerPlayer *player, const CardUseStruct &use)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (!use.to.contains(p) && player->canDiscard(p, "he"))
                targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !isOwnTurn(player)
            || !use.card || use.card->getTypeId() == Card::TypeSkill || use.card->getTypeId() == Card::TypeEquip
            || outsiders(room, player, use).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *target = room->askForPlayerChosen(player, outsiders(room, player, use), objectName(), "@iksuzhong", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || !player->canDiscard(target, "he"))
            return false;
        const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
        if (id < 0)
            return false;
        room->throwCard(id, target, player);
        room->addPlayerMark(player, "iksuzhong-Clear");
        if (target->isAlive())
            target->drawCards(1, objectName());
        return false;
    }
};

class IkYunhua : public TriggerSkillV2
{
public:
    IkYunhua() : TriggerSkillV2("ikyunhua") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->drawCards(2, objectName());
        const int excess = player->getHandcardNum() - player->getMark("iksuzhong-Clear");
        if (excess > 0 && player->isAlive())
            room->askForDiscard(player, objectName(), excess, excess);
        return false;
    }
};

// ---------------------------------------------------------------- bloom060

class IkYongye : public TriggerSkillV2
{
public:
    IkYongye() : TriggerSkillV2("ikyongye") { events << EventPhaseStart << Damaged; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == Damaged) {
            if (player->isNude())
                return TriggerList();
            QStringList times;
            for (int i = 0; i < data.value<DamageStruct>().damage; ++i)
                times << objectName();
            return TriggerList{{player, times}};
        }
        if ((player->getPhase() == Player::Finish && !player->isNude())
            || (player->getPhase() == Player::Start && !player->getPile("page").isEmpty()))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart && player->getPhase() == Player::Start)
            return true;
        const Card *card = room->askForExchange(player, objectName(), 1, 1, true, "@ikyongye", true);
        if (!card)
            return false;
        ctx.extra_data = card->getSubcards().value(0, -1);
        delete card;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Pages pile up; at the start phase they turn into twice as many cards and the judge phase is skipped.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart && player->getPhase() == Player::Start) {
            room->sendCompulsoryTriggerLog(player, objectName());
            const QList<int> ids = player->getPile("page");
            DummyCard dummy(ids);
            room->throwCard(&dummy, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString()),
                            nullptr);
            player->drawCards(2 * ids.length(), objectName());
            player->skip(Player::Judge);
            return false;
        }
        const int id = ctx.extra_data.toInt();
        if (id >= 0 && room->getCardOwner(id) == player)
            player->addToPile("page", id);
        return false;
    }
};

// ---------------------------------------------------------------- bloom062

class IkShemou : public TriggerSkillV2
{
public:
    IkShemou() : TriggerSkillV2("ikshemou")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    static int amount(const Player *player) { return qMin(4, player->getMark("Global_TurnCount2") - 1); }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || !player->hasSkill(objectName())
            || amount(player) <= 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Draw X, discard X; parting with equipment buys a free 杀.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const int n = amount(player);
        player->drawCards(n, objectName());
        if (!player->isAlive())
            return false;
        const QList<int> before = player->getEquipsId();
        room->askForDiscard(player, objectName(), n, n, false, true);
        bool equipLost = false;
        foreach (int id, before)
            if (!player->getEquipsId().contains(id))
                equipLost = true;
        if (!equipLost || !player->isAlive())
            return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canSlash(p, false))
                targets << p;
        ServerPlayer *target = targets.isEmpty() ? nullptr
                                                 : room->askForPlayerChosen(player, targets, objectName(), "@dummy-slash", true);
        if (target)
            useSkillSlash(room, ctx, player, target, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- snow022

class IkFenxun : public ViewAsSkillV2
{
public:
    IkFenxun() : ViewAsSkillV2("ikfenxun", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkFenxunCard"; }

    // Distance 1 to the target this turn; a basic discard lets the phase's next 杀 come back.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        if (source->getTag("IkFenxunTarget").toString().isEmpty()) {
            source->setTag("IkFenxunTarget", target->objectName());
            room->setFixedDistance(source, target, 1);
        }
        if (ctx.use_card && !ctx.use_card->getSubcards().isEmpty()
            && Sanguosha->getCard(ctx.use_card->getSubcards().first())->getTypeId() == Card::TypeBasic)
            room->setPlayerMark(source, "ikfenxun_slash-PlayClear", 1);
        return ContinueEffects;
    }
};

class IkFenxunEffect : public TriggerSkillV2
{
public:
    IkFenxunEffect() : TriggerSkillV2("#ikfenxun") { events << EventPhaseChanging << Death << PreCardUsed << BeforeCardsMove; }

    static void release(Room *room, ServerPlayer *player)
    {
        const QString name = player->getTag("IkFenxunTarget").toString();
        if (name.isEmpty())
            return;
        player->removeTag("IkFenxunTarget");
        if (ServerPlayer *target = room->findPlayerByObjectName(name, true))
            room->removeFixedDistance(player, target, 1);
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if ((event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            || (event == Death && data.value<DeathStruct>().who == player)) {
            release(room, player);
        } else if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && use.card->isKindOf("Slash")
                && player->getMark("ikfenxun_slash-PlayClear") > 0) {
                room->setPlayerMark(player, "ikfenxun_slash-PlayClear", 0);
                use.card->setTag("ikfenxun", true);
            }
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != BeforeCardsMove || !player || !player->isAlive() || !player->hasSkill("ikfenxun"))
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile || !move.from_places.contains(Player::PlaceTable)
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE
            || move.reason.m_playerId != player->objectName())
            return TriggerList();
        const Card *used = move.reason.m_extraData.value<const Card *>();
        if (!used || !used->isKindOf("Slash") || !used->tag.value("ikfenxun").toBool())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke("ikfenxun"))
            return false;
        room->broadcastSkillInvoke("ikfenxun");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = move.card_ids;
        move.removeCardIds(ids);
        *ctx.original_data = QVariant::fromValue(move);
        DummyCard dummy(ids);
        room->obtainCard(player, &dummy);
        return false;
    }
};

// ---------------------------------------------------------------- snow025

class IkHongrou : public TriggerSkillV2
{
public:
    IkHongrou() : TriggerSkillV2("ikhongrou") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || room->alivePlayerCount() < 2
            || (player->getPhase() != Player::Start && player->getPhase() != Player::Finish)
            || !player->canDiscard(player, "h"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QList<ServerPlayer *> chosen = room->askForPlayersChosen(player, room->getOtherPlayers(player), objectName(), 0, 2,
                                                                       "@ikhongrou", true);
        if (chosen.isEmpty() || !room->askForDiscard(player, objectName(), 1, 1, true, false, "@ikhongrou-discard"))
            return false;
        room->broadcastSkillInvoke(objectName());
        QStringList names;
        foreach (ServerPlayer *p, chosen)
            names << p->objectName();
        ctx.extra_data = names;
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        foreach (const QString &name, ctx.extra_data.toStringList())
            if (ServerPlayer *p = room->findPlayerByObjectName(name))
                if (p->isAlive())
                    p->drawCards(1, objectName());
        return false;
    }
};

class IkHuaxiao : public TriggerSkillV2
{
public:
    IkHuaxiao() : TriggerSkillV2("ikhuaxiao") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::NotActive
            || move.to_place != Player::DiscardPile)
            return TriggerList();
        const int basic = move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON;
        const bool fromOwner = move.from == player
            || (move.reason.m_playerId == player->objectName()
                && (basic == CardMoveReason::S_REASON_USE || basic == CardMoveReason::S_REASON_RESPONSE));
        if (!fromOwner
            || (basic != CardMoveReason::S_REASON_USE && basic != CardMoveReason::S_REASON_RESPONSE
                && basic != CardMoveReason::S_REASON_DISCARD))
            return TriggerList();
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const Player::Place from = move.from_places.value(i);
            if (Sanguosha->getCard(move.card_ids.at(i))->isRed()
                && (from == Player::PlaceHand || from == Player::PlaceEquip || from == Player::PlaceTable))
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@ikhuaxiao", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (target && target->isAlive())
            target->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- snow032

class IkQiwu : public TriggerSkillV2
{
public:
    IkQiwu() : TriggerSkillV2("ikqiwu") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Discard || !player->hasSkill(objectName())
            || player->isKongcheng())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const Card *card = room->askForExchange(player, objectName(), 1, 1, false, "@ikqiwu", true);
        if (!card)
            return false;
        ctx.extra_data = card->getSubcards().value(0, -1);
        delete card;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // The third 踏 lets loose: 2 damage to a male character and its equipment thrown.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (id < 0 || room->getCardOwner(id) != player)
            return false;
        player->addToPile("step", id);
        if (player->getPile("step").length() < 3 || !player->isAlive())
            return false;
        DummyCard steps(player->getPile("step"));
        room->throwCard(&steps, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString()),
                        nullptr);
        QList<ServerPlayer *> males;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->isMale())
                males << p;
        if (males.isEmpty() || !player->isAlive())
            return false;
        ServerPlayer *target = room->askForPlayerChosen(player, males, objectName(), "@ikqiwu-choose");
        if (!target)
            return false;
        room->damage(DamageStruct(objectName(), player, target, 2));
        if (!player->isAlive() || !target->isAlive())
            return false;
        QList<int> equips;
        foreach (int id, target->getEquipsId())
            if (player->canDiscard(target, id))
                equips << id;
        if (!equips.isEmpty()) {
            DummyCard dummy(equips);
            room->throwCard(&dummy, target, player);
        }
        return false;
    }
};

// While 踏 lie on the character it holds 知惠 and 眩惑 (天香 and 流离 here), attached under this instance.
class IkShendao : public TriggerSkillV2
{
public:
    IkShendao() : TriggerSkillV2("ikshendao")
    {
        events << GameStart << EventAcquireSkill << CardsMoveOneTime;
        frequency = Compulsory;
    }

    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner != player || !player->isAlive() || !ctx.activationRef.isValid())
            return;
        if (event == EventAcquireSkill
            && (!ctx.original_data || ctx.original_data->value<SkillChangeStruct>().skillName != objectName()))
            return;
        const bool active = !player->getPile("step").isEmpty();
        const QStringList names{"tianxiang", "liuli"};
        bool notified = false;
        foreach (const QString &name, names) {
            bool attached = false;
            for (const SkillInstance &entry : player->getSkillInstances()) {
                if (entry.skillName != name || entry.parentRef != ctx.activationRef)
                    continue;
                attached = true;
                if (!active)
                    room->detachAttachedSkill(SkillInstanceRef(player->objectName(), entry.key()));
            }
            if (active && !attached) {
                room->attachSkillToPlayer(player, name, ctx.activationRef);
                if (!notified)
                    room->notifySkillInvoked(player, objectName());
                notified = true;
            }
        }
    }
};

// ---------------------------------------------------------------- snow033

class IkCangwu : public ViewAsSkillV2
{
public:
    IkCangwu() : ViewAsSkillV2("ikcangwu") { setPhaseName("Play"); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("ikcangwu_blocked-Clear") == 0 && !request.initiator->isNude();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsCard(request.initiator, card) && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && selectionValid(this, request);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    // X discarded cards for a target at X HP inside the attack range left after paying.
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        const Player *self = request.initiator;
        return self && selected.isEmpty() && to && to != self && qMax(0, to->getHp()) == request.selectedCardIds.size()
            && self->inMyAttackRange(to, request.selectedCardIds);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkCangwuCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->setPlayerFlag(source, "-IkCangwuDying");
        room->damage(DamageStruct(objectName(), source, target));
        if (!source->hasFlag("IkCangwuDying"))
            return ContinueEffects;
        room->setPlayerFlag(source, "-IkCangwuDying");
        room->setPlayerMark(source, "ikcangwu_blocked-Clear", 1);
        if (source->isAlive())
            room->loseHp(source, 1, true, source, objectName());
        return ContinueEffects;
    }
};

class IkCangwuRecord : public TriggerSkillV2
{
public:
    IkCangwuRecord() : TriggerSkillV2("#ikcangwu-record")
    {
        events << EnterDying;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const DyingStruct dying = data.value<DyingStruct>();
        if (dying.damage && dying.damage->reason == "ikcangwu" && dying.damage->from)
            room->setPlayerFlag(dying.damage->from, "IkCangwuDying");
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- snow040

class IkLingzhou : public TriggerSkillV2
{
public:
    IkLingzhou() : TriggerSkillV2("iklingzhou") { events << EventPhaseStart << Damaged; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == Damaged)
            return player->getPile("summon").isEmpty() ? TriggerList() : TriggerList{{player, {objectName()}}};
        if (player->getPhase() != Player::Discard || player->isNude())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == Damaged)
            return true;
        const Card *cards = room->askForExchange(player, objectName(), player->getCardCount(), 1, true, "@iklingzhou", true);
        if (!cards)
            return false;
        ctx.extra_data = ListI2V(cards->getSubcards());
        delete cards;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == Damaged) {
            room->sendCompulsoryTriggerLog(player, objectName());
            const QList<int> pile = player->getPile("summon");
            room->fillAG(pile, player);
            const int id = room->askForAG(player, pile, false, objectName());
            room->clearAG(player);
            room->throwCard(Sanguosha->getCard(id),
                            CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString()), nullptr);
            return false;
        }
        QList<int> ids;
        foreach (int id, ListV2I(ctx.extra_data.toList()))
            if (room->getCardOwner(id) == player)
                ids << id;
        if (!ids.isEmpty())
            player->addToPile("summon", ids);
        return false;
    }
};

class IkMoqizhou : public TriggerSkillV2
{
public:
    IkMoqizhou() : TriggerSkillV2("ikmoqizhou") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName())
            || player->getPile("summon").isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Hand every 令 with a heal to someone no healthier, or cash them in for cards.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        QList<ServerPlayer *> weaker;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getHp() <= player->getHp())
                weaker << p;
        ServerPlayer *target = weaker.isEmpty() ? nullptr
                                                : room->askForPlayerChosen(player, weaker, objectName(), "@ikmoqizhou", true);
        const QList<int> pile = player->getPile("summon");
        DummyCard dummy(pile);
        if (target) {
            room->obtainCard(target, &dummy);
            if (target->isAlive() && target->isWounded())
                room->recover(target, RecoverStruct(objectName(), player));
            return false;
        }
        room->throwCard(&dummy, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString()),
                        nullptr);
        if (player->isAlive())
            player->drawCards(pile.length(), objectName());
        return false;
    }
};

// ---------------------------------------------------------------- snow043

class IkLingtong : public ViewAsSkillV2
{
public:
    IkLingtong() : ViewAsSkillV2("iklingtong") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkLingtongCard"; }

    // Show the hand; then look at the target's hand (dropping a black card) or its role.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        if (!source->isKongcheng())
            room->showAllCards(source, target);
        QStringList choices;
        if (!target->isKongcheng())
            choices << "handcards";
        if (!target->isLord())
            choices << "role";
        if (choices.isEmpty() || !source->isAlive())
            return ContinueEffects;
        const QString choice = room->askForChoice(source, objectName(), choices.join("+"), QVariant::fromValue(target));
        LogMessage log;
        log.type = "$IkLingtongView";
        log.from = source;
        log.to << target;
        log.arg = "iklingtong:" + choice;
        room->sendLog(log, room->getOtherPlayers(source));
        if (choice == "handcards") {
            QList<int> blacks;
            foreach (const Card *card, target->getHandcards())
                if (card->isBlack() && source->canDiscard(target, card->getEffectiveId()))
                    blacks << card->getEffectiveId();
            const int id = room->doGongxin(source, target, blacks, objectName());
            if (id >= 0)
                room->throwCard(id, target, source);
        } else {
            JsonArray arg;
            arg << target->objectName() << target->getRole();
            room->doNotify(source, QSanProtocol::S_COMMAND_SET_EMOTION, arg);
            LogMessage roleLog;
            roleLog.type = "$ViewRole";
            roleLog.from = source;
            roleLog.to << target;
            roleLog.arg = target->getRole();
            room->sendLog(roleLog, source);
        }
        return ContinueEffects;
    }
};

class IkXuexia : public TriggerSkillV2
{
public:
    IkXuexia() : TriggerSkillV2("ikxuexia") { events << CardEffected; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.card || !effect.card->isKindOf("Slash") || !effect.from || !effect.to || !effect.from->isAlive()
            || !effect.to->isAlive() || effect.offset_num != 1 || !effect.to->isAdjacentTo(effect.from))
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner != effect.to && owner->hasSkill(objectName()) && effect.to->isAdjacentTo(owner)) {
                result[owner] << objectName();
                break;
            }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *to = ctx.original_data->value<CardEffectStruct>().to;
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(to)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        effect.offset_num = 2;
        *ctx.original_data = QVariant::fromValue(effect);
        return false;
    }
};

// ---------------------------------------------------------------- snow044

class IkLunke : public ViewAsSkillV2
{
public:
    IkLunke() : ViewAsSkillV2("iklunke") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return false;
        foreach (const Player *p, request.initiator->getAliveSiblings())
            if (!p->isChained())
                return true;
        return false;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isChained();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkLunkeCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target)
            return ContinueEffects;
        Room *room = source->getRoom();
        if (target->isAlive() && !target->isChained())
            room->setPlayerChained(target, true, source);
        if (source->isAlive() && !source->isChained())
            room->setPlayerChained(source, true, source);
        return ContinueEffects;
    }
};

class IkCangmie : public TriggerSkillV2
{
public:
    IkCangmie() : TriggerSkillV2("ikcangmie") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || !player->hasSkill(objectName())
            || !player->isChained())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!player->isAlive())
                break;
            if (!p->isAlive() || !p->isChained() || !player->canDiscard(p, "he"))
                continue;
            if (p == player) {
                room->askForDiscard(player, objectName(), 1, 1, false, true);
                continue;
            }
            const int id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, p, player);
        }
        return false;
    }
};

// ---------------------------------------------------------------- snow050

class IkYanhuo : public TriggerSkillV2
{
public:
    IkYanhuo() : TriggerSkillV2("ikyanhuo") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || player->getHandcardNum() > 1)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // It draws one and hands over its whole hand; the owner returns as many cards.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        player->drawCards(1, objectName());
        if (!player->isAlive() || player->isKongcheng() || !owner->isAlive())
            return false;
        const int n = player->getHandcardNum();
        if (owner == player)
            return false;
        room->giveCard(player, owner, player->handCards(), objectName(), false);
        if (!owner->isAlive() || !player->isAlive() || owner->isNude())
            return false;
        const int back = qMin(n, owner->getCardCount());
        const Card *cards = room->askForExchange(owner, objectName(), back, back, true,
                                                 QString("@ikyanhuo-return:%1::%2").arg(player->objectName()).arg(back));
        if (cards) {
            room->giveCard(owner, player, cards, objectName(), false);
            delete cards;
        }
        return false;
    }
};

// ---------------------------------------------------------------- snow053

class IkCiyu : public TriggerSkillV2
{
public:
    IkCiyu() : TriggerSkillV2("ikciyu")
    {
        events << HpRecover;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || !isOwnTurn(player))
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Whoever draws is remembered for 青蛇.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        ServerPlayer *drawer = owner;
        if (owner != player && room->askForChoice(owner, objectName(), "draw+letdraw", QVariant::fromValue(player)) == "letdraw")
            drawer = player;
        drawer->drawCards(1, objectName());
        room->setPlayerMark(drawer, "ikciyu_" + owner->objectName(), 1);
        return false;
    }
};

class IkQingshe : public WakeSkill
{
public:
    IkQingshe() : WakeSkill("ikqingshe") { events << EventPhaseStart; }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *room, ServerPlayer *player) const override
    {
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->isWounded() && p->getMark("ikciyu_" + player->objectName()) > 0)
                return true;
        return false;
    }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->changeMaxHpForAwakenSkill(player, 1, objectName()) || !player->isAlive())
            return;
        room->recover(player, RecoverStruct(objectName(), player));
        room->acquireSkillFromEffect(player, "ikbingling", ctx);
    }
};

class IkBingling : public ViewAsSkillV2
{
public:
    IkBingling() : ViewAsSkillV2("ikbingling") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && to->canDiscard(to, "hej");
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkBinglingCard"; }

    // One whole area is thrown and redrawn; the owner's hand limit drops by one this turn.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        QStringList choices;
        const QStringList areas{"h", "e", "j"};
        foreach (const QString &area, areas)
            if (target->canDiscard(target, area))
                choices << area;
        if (choices.isEmpty())
            return ContinueEffects;
        const QString area = room->askForChoice(source, objectName(), choices.join("+"), QVariant::fromValue(target));
        QList<int> ids = area == "h" ? target->handCards() : (area == "e" ? target->getEquipsId() : target->getJudgingAreaID());
        QList<int> thrown;
        foreach (int id, ids)
            if (target->canDiscard(target, id))
                thrown << id;
        if (thrown.isEmpty())
            return ContinueEffects;
        DummyCard dummy(thrown);
        room->throwCard(&dummy, area == "j" ? nullptr : target, area == "j" ? target : nullptr);
        if (target->isAlive())
            target->drawCards(thrown.length(), objectName());
        room->setPlayerMark(source, "ikbingling-Clear", 1);
        return ContinueEffects;
    }
};

class IkBinglingMax : public MaxCardsSkillV2
{
public:
    IkBinglingMax() : MaxCardsSkillV2("#ikbingling-max") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("ikbingling-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(-1);
    }
};

// ---------------------------------------------------------------- snow055

class IkHuangpo : public ViewAsSkillV2
{
public:
    IkHuangpo() : ViewAsSkillV2("ikhuangpo") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    int getMaxUsageLimit(const SkillContext &) const override { return 2; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *to) const override
    {
        return selected.isEmpty() && to && !to->isAllNude();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkHuangpoCard"; }

    // Set a card aside as 荒; its owner takes it back at the owner's end phase.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || target->isAllNude())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = room->askForCardChosen(source, target, "hej", objectName());
        if (id < 0)
            return ContinueEffects;
        source->addToPile("flap", id);
        QList<int> held = ListV2I(target->getTag("IkHuangpoIds").toList());
        held << id;
        target->setTag("IkHuangpoIds", ListI2V(held));
        return ContinueEffects;
    }
};

class IkHuangpoReturn : public TriggerSkillV2
{
public:
    IkHuangpoReturn() : TriggerSkillV2("#ikhuangpo")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || player->getPile("flap").isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            QList<int> held = ListV2I(p->getTag("IkHuangpoIds").toList());
            QList<int> back, keep;
            foreach (int id, held)
                (player->getPile("flap").contains(id) ? back : keep) << id;
            if (back.isEmpty())
                continue;
            p->setTag("IkHuangpoIds", ListI2V(keep));
            DummyCard dummy(back);
            room->obtainCard(p, &dummy);
        }
        return false;
    }
};

class IkYixiang : public TriggerSkillV2
{
public:
    IkYixiang() : TriggerSkillV2("ikyixiang")
    {
        events << CardsMoveOneTime << EventPhaseChanging;
        frequency = Compulsory;
    }

    static void release(Room *room, ServerPlayer *owner)
    {
        const QStringList names = owner->getTag("IkYixiangTargets").toStringList();
        if (names.isEmpty())
            return;
        owner->removeTag("IkYixiangTargets");
        foreach (const QString &name, names)
            if (ServerPlayer *p = room->findPlayerByObjectName(name, true))
                room->removeFixedDistance(owner, p, 1);
    }

    // The distance lock is board state, so it is kept in the record step.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::NotActive)
                foreach (ServerPlayer *p, room->getAllPlayers())
                    release(room, p);
            return false;
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        ServerPlayer *from = qobject_cast<ServerPlayer *>(move.from);
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !from || from == player || !from->isAlive()
            || !move.from_places.contains(Player::PlaceHand) || !move.is_last_handcard || !room->getCurrent())
            return false;
        QStringList names = player->getTag("IkYixiangTargets").toStringList();
        if (names.contains(from->objectName()))
            return false;
        names << from->objectName();
        player->setTag("IkYixiangTargets", names);
        room->setFixedDistance(player, from, 1);
        room->sendCompulsoryTriggerLog(player, objectName());
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- snow059

// Equipment suit count 1/2/3/4 grants 夕紫/沉红/绀碧/苍幽 (夕紫/英姿/樵拾/枭姬 here), attached under this instance.
class IkHonglian : public TriggerSkillV2
{
public:
    IkHonglian() : TriggerSkillV2("ikhonglian")
    {
        events << GameStart << EventAcquireSkill << CardsMoveOneTime;
        frequency = Compulsory;
    }

    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner != player || !player->isAlive() || !ctx.activationRef.isValid())
            return;
        if (event == EventAcquireSkill
            && (!ctx.original_data || ctx.original_data->value<SkillChangeStruct>().skillName != objectName()))
            return;
        QSet<Card::Suit> suits;
        foreach (const Card *equip, player->getEquips())
            suits << equip->getSuit();
        const QStringList names{"ikxizi", "yingzi", "qiaoshi", "xiaoji"};
        for (int i = 0; i < names.size(); ++i) {
            const QString name = names.at(i);
            const bool active = suits.size() >= i + 1;
            bool attached = false;
            for (const SkillInstance &entry : player->getSkillInstances()) {
                if (entry.skillName != name || entry.parentRef != ctx.activationRef)
                    continue;
                attached = true;
                if (!active)
                    room->detachAttachedSkill(SkillInstanceRef(player->objectName(), entry.key()));
            }
            if (active && !attached) {
                room->attachSkillToPlayer(player, name, ctx.activationRef);
                room->notifySkillInvoked(player, objectName());
            }
        }
    }
};

class IkCaiyin : public ViewAsSkillV2
{
public:
    IkCaiyin() : ViewAsSkillV2("ikcaiyin", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isRed()
            && card->getTypeId() == Card::TypeBasic && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isKongcheng()
            && request.initiator->inMyAttackRange(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkCaiyinCard"; }

    // Discard one of its hand cards: a 闪 shows the owner that hand, anything else shows it the owner's.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || target->isKongcheng() || !source->canDiscard(target, "h"))
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = room->askForCardChosen(source, target, "h", objectName(), false, Card::MethodDiscard);
        if (id < 0)
            return ContinueEffects;
        const bool jink = Sanguosha->getCard(id)->isKindOf("Jink");
        room->throwCard(id, target, source);
        ServerPlayer *shower = jink ? target : source;
        ServerPlayer *viewer = jink ? source : target;
        if (!shower->isAlive() || !viewer->isAlive() || shower->isKongcheng())
            return ContinueEffects;
        LogMessage log;
        log.type = "$IkLingtongView";
        log.from = viewer;
        log.to << shower;
        log.arg = "iklingtong:handcards";
        room->sendLog(log, room->getOtherPlayers(viewer));
        room->showAllCards(shower, viewer);
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- snow061

class IkPitai : public TriggerSkillV2
{
public:
    IkPitai() : TriggerSkillV2("ikpitai") { events << CardsMoveOneTime; }

    static bool counts(ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        if (move.to == player && move.to_place == Player::PlaceHand && move.card_ids.length() > 1) {
            if (move.from != move.to)
                return true;
            int n = 0;
            foreach (Player::Place place, move.from_places)
                if (place != Player::PlaceJudge && place != Player::PlaceHand && place != Player::PlaceEquip && ++n > 1)
                    return true;
            return false;
        }
        if (move.from != player || move.card_ids.length() < 2)
            return false;
        int n = 0;
        foreach (Player::Place place, move.from_places)
            if ((place == Player::PlaceHand || place == Player::PlaceEquip) && ++n > 1)
                return true;
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || room->getTag("FirstRound").toBool()
            || room->alivePlayerCount() < 2 || !counts(player, data.value<CardsMoveOneTimeStruct>()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@ikpitai", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (target && target->isAlive())
            target->drawCards(1, objectName());
        return false;
    }
};

class IkHuzhan : public ViewAsSkillV2
{
public:
    IkHuzhan() : ViewAsSkillV2("ikhuzhan") { setPhaseName("Play"); }

    // X is the number of living rebels, counted from the mode's role list less the revealed dead.
    static int rebels(const Player *self)
    {
        int n = Sanguosha->getRoleList(self->getGameMode()).count("rebel");
        foreach (const Player *p, self->getSiblings())
            if (p->isDead() && p->getRole() == "rebel")
                --n;
        return n;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        return self && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && self->getMark("ikhuzhan_used-PlayClear") < rebels(self);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *to) const override
    {
        return selected.isEmpty() && to && to->hasEquip();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkHuzhanCard"; }

    // It draws one, then loses an equipment to the owner's discard, or takes all of it back and takes 1 damage.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->addPlayerMark(source, "ikhuzhan_used-PlayClear");
        target->drawCards(1, objectName());
        if (!target->isAlive())
            return ContinueEffects;
        QStringList choices;
        if (source->isAlive() && source->canDiscard(target, "e"))
            choices << "discard";
        if (target->hasEquip())
            choices << "obtain";
        if (choices.isEmpty())
            return ContinueEffects;
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant::fromValue(source)) == "discard") {
            const int id = room->askForCardChosen(source, target, "e", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, target, source);
            return ContinueEffects;
        }
        DummyCard equips(target->getEquipsId());
        room->obtainCard(target, &equips);
        if (target->isAlive())
            room->damage(DamageStruct(objectName(), source->isAlive() ? source : nullptr, target));
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- luna017

class IkChenyan : public TriggerSkillV2
{
public:
    IkChenyan() : TriggerSkillV2("ikchenyan")
    {
        events << DrawNCards << EventPhaseStart;
        frequency = Compulsory;
    }

    static int kingdoms(Room *room)
    {
        QSet<QString> set;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            set << p->getKingdom();
        return qMax(set.size(), 2);
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == DrawNCards) {
            const DrawStruct draw = data.value<DrawStruct>();
            if (draw.who != player || draw.reason != "draw_phase")
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        if (player->getPhase() != Player::Discard || player->isNude())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        const int x = kingdoms(room);
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += x;
            *ctx.original_data = QVariant::fromValue(draw);
            return false;
        }
        room->askForDiscard(player, objectName(), x + 1, x + 1, false, true);
        return false;
    }
};

class IkMoliao : public TriggerSkillV2
{
public:
    IkMoliao() : TriggerSkillV2("ikmoliao")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName()) || !player->isKongcheng()
            || damage.damage <= 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        damage.damage = 1;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

// ---------------------------------------------------------------- luna019

class IkZhange : public ViewAsSkillV2
{
public:
    IkZhange() : ViewAsSkillV2("ikzhange")
    {
        frequency = Limited;
        limit_mark = "@zhange";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    static int cap(const Player *self) { return self->aliveCount() > 5 ? 3 : 2; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return SelectTargets; }

    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && to && selected.length() < cap(request.initiator);
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty() && selected.length() <= cap(request.initiator);
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkZhangeCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source)
            return FinishSkill;
        Room *room = source->getRoom();
        foreach (ServerPlayer *p, targets)
            if (p->isAlive())
                p->drawCards(3, objectName());
        if (targets.length() == 1 && targets.first() == source && source->isAlive() && source->isWounded())
            room->recover(source, RecoverStruct(objectName(), source));
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- luna020

class IkZhizhai : public TriggerSkillV2
{
public:
    IkZhizhai() : TriggerSkillV2("ikzhizhai")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.from
            || !damage.from->isAlive())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The source either softens the blow by one or shows its hand and pays a card.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *from = damage.from;
        QStringList choices{"reduce"};
        if (!from->isNude() && from != player)
            choices << "show";
        if (room->askForChoice(from, objectName(), choices.join("+"), QVariant::fromValue(player)) == "show") {
            if (!from->isKongcheng())
                room->showAllCards(from);
            const Card *card = room->askForExchange(from, objectName(), 1, 1, true, "@ikzhizhai:" + player->objectName());
            if (card) {
                room->giveCard(from, player, card, objectName(), false);
                delete card;
            }
            return false;
        }
        --damage.damage;
        *ctx.original_data = QVariant::fromValue(damage);
        return damage.damage < 1;
    }
};

// 礼惠's distribution is 礼让; this keeps its discard-phase self discard.
class IkLihui : public TriggerSkillV2
{
public:
    IkLihui() : TriggerSkillV2("iklihui") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Discard || !player->hasSkill(objectName())
            || !player->canDiscard(player, "h"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!room->askForDiscard(player, objectName(), player->getHandcardNum(), 1, true, false, "@iklihui"))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return false; }
};

// ---------------------------------------------------------------- luna021

class IkFeishan : public TriggerSkillV2
{
public:
    IkFeishan() : TriggerSkillV2("ikfeishan") { events << EventPhaseStart; }

    static QList<ServerPlayer *> rivals(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canPindian(p))
                targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || !player->hasSkill(objectName())
            || player->isKongcheng() || rivals(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, rivals(room, player), objectName(), "@ikfeishan-card", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    // Win: a free distance-less 杀 at it. Otherwise it is off-limits to the owner's 杀 this turn.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || !player->canPindian(target))
            return false;
        PindianStruct *pindian = player->PinDian(target, objectName());
        if (!pindian)
            return false;
        if (pindian->success) {
            if (player->isAlive() && target->isAlive())
                useSkillSlash(room, ctx, player, target, objectName());
        } else {
            room->setPlayerMark(player, "ikfeishan_" + target->objectName() + "-Clear", 1);
        }
        return false;
    }
};

class IkFeishanProhibit : public ProhibitSkill
{
public:
    IkFeishanProhibit() : ProhibitSkill("#ikfeishan") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return from && to && card && card->isKindOf("Slash") && from->getMark("ikfeishan_" + to->objectName() + "-Clear") > 0;
    }
};

// ---------------------------------------------------------------- luna022

class IkNifa : public TriggerSkillV2
{
public:
    IkNifa() : TriggerSkillV2("iknifa") { events << CardsMoveOneTime; }

    static QList<ServerPlayer *> victims(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canDiscard(p, "he"))
                targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || !((move.from == player && move.from_places.contains(Player::PlaceHand))
                 || (move.to == player && move.to_place == Player::PlaceHand))
            || player->getHandcardNum() >= qMax(player->getLostHp(), 1) || victims(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, victims(room, player), objectName(), "iknifa-invoke", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || !player->canDiscard(target, "he"))
            return false;
        const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
        if (id >= 0)
            room->throwCard(id, target, player);
        return false;
    }
};

class IkGuyi : public TriggerSkillV2
{
public:
    IkGuyi() : TriggerSkillV2("ikguyi")
    {
        events << HpChanged << Death;
        frequency = Compulsory;
    }

    // HpChanged carries a DamageStruct or a lost amount; recoveries do not count.
    static int lost(const QVariant &data)
    {
        if (data.isNull() || data.canConvert<RecoverStruct>())
            return 0;
        if (data.canConvert<DamageStruct>())
            return data.value<DamageStruct>().damage;
        bool ok = false;
        const int n = data.toInt(&ok);
        return ok ? n : 0;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == Death) {
            const ServerPlayer *dead = data.value<DeathStruct>().who;
            if (player->isAlive() && player->hasSkill(objectName()) && dead && dead != player)
                result[player] << objectName();
            return result;
        }
        const int n = lost(data);
        if (!player->isAlive() || n <= 0)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
            if (!owner->hasSkill(objectName()))
                continue;
            const int x = qMax(owner->getLostHp(), 1);
            for (int i = 0; i < qMin(n, x - player->getHp()); ++i)
                result[owner] << objectName();
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        if (event == Death)
            room->loseHp(ctx.owner, 1, true, ctx.owner, objectName());
        else
            ctx.owner->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- luna023

class IkShunqie : public TriggerSkillV2
{
public:
    IkShunqie() : TriggerSkillV2("ikshunqie") { events << Damage; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.to
            || damage.to == player || !damage.to->hasEquip())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *to = ctx.original_data->value<DamageStruct>().to;
        if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(to)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *to = ctx.original_data->value<DamageStruct>().to;
        if (!to || !to->hasEquip())
            return false;
        const int id = room->askForCardChosen(player, to, "e", objectName());
        if (id >= 0)
            room->obtainCard(player, id);
        return false;
    }
};

// ---------------------------------------------------------------- luna024

class IkLongya : public TriggerSkillV2
{
public:
    IkLongya() : TriggerSkillV2("iklongya") { events << TargetSpecified << CardOffset; }

    static QString mark(const Card *card) { return "iklongya_" + card->toString(); }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardOffset) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card || !effect.card->isKindOf("Slash") || !effect.from || !effect.to || !effect.to->isAlive()
                || !effect.from->isAlive() || effect.from != player || effect.to->getMark(mark(effect.card)) == 0
                || !effect.offset_card || !effect.offset_card->isKindOf("Jink"))
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
            || !use.card->isKindOf("Slash") || use.to.isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }

    // Each target: draw or strip it; a 闪 from it then strips the user back.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == CardOffset) {
            const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
            room->setPlayerMark(effect.to, mark(effect.card), 0);
            if (!effect.to->canDiscard(player, "he"))
                return false;
            room->sendCompulsoryTriggerLog(player, objectName());
            const int id = room->askForCardChosen(effect.to, player, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, player, effect.to);
            return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        foreach (ServerPlayer *p, use.to) {
            if (!player->isAlive() || !p->isAlive() || !player->askForSkillInvoke(objectName(), QVariant::fromValue(p)))
                continue;
            room->broadcastSkillInvoke(objectName());
            const QString choice = player->canDiscard(p, "he")
                ? room->askForChoice(player, objectName(), "draw+discard", QVariant::fromValue(p))
                : QString("draw");
            if (choice == "draw") {
                player->drawCards(1, objectName());
            } else {
                const int id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                if (id >= 0)
                    room->throwCard(id, p, player);
            }
            room->setPlayerMark(p, mark(use.card), 1);
        }
        return false;
    }
};

class IkLongyaClear : public TriggerSkillV2
{
public:
    IkLongyaClear() : TriggerSkillV2("#iklongya-clear")
    {
        events << CardFinished;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash"))
            return false;
        const QString mark = IkLongya::mark(use.card);
        foreach (ServerPlayer *p, room->getAllPlayers())
            if (p->getMark(mark) > 0)
                room->setPlayerMark(p, mark, 0);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- luna025

class IkHengmou : public TriggerSkillV2
{
public:
    IkHengmou() : TriggerSkillV2("ikhengmou") { events << TargetConfirming; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card || !use.card->isKindOf("Slash")
            || !use.to.contains(player))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    static void cycle(Room *room, ServerPlayer *p, const QString &reason)
    {
        const int n = qMin(2, p->getCardCount());
        if (n > 0)
            room->askForDiscard(p, reason, n, n, false, true);
        if (p->isAlive())
            p->drawCards(2, reason);
    }

    // Cycle two cards; a lone healthiest other character may follow suit.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        cycle(room, player, objectName());
        int max = -1000;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            max = qMax(max, p->getHp());
        QList<ServerPlayer *> tops;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getHp() == max)
                tops << p;
        if (tops.length() != 1 || tops.first() == player)
            return false;
        ServerPlayer *top = tops.first();
        if (top->askForSkillInvoke(objectName()))
            cycle(room, top, objectName());
        return false;
    }
};

class IkXincao : public ViewAsSkillV2
{
public:
    IkXincao() : ViewAsSkillV2("ikxincao") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkXincaoCard"; }

    // Hand over the whole hand; the target duels another by pindian and the winner slashes the loser.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || source->isKongcheng())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->giveCard(source, target, source->handCards(), objectName(), false);
        if (!target->isAlive() || target->isKongcheng() || !source->isAlive())
            return ContinueEffects;
        QList<ServerPlayer *> rivals;
        foreach (ServerPlayer *p, room->getOtherPlayers(target))
            if (target->canPindian(p))
                rivals << p;
        if (rivals.isEmpty())
            return ContinueEffects;
        ServerPlayer *rival = room->askForPlayerChosen(source, rivals, objectName(), "@ikxincao-pindian:" + target->objectName());
        PindianStruct *pindian = rival ? target->PinDian(rival, objectName()) : nullptr;
        if (!pindian || pindian->from_number == pindian->to_number)
            return ContinueEffects;
        ServerPlayer *winner = pindian->success ? target : rival;
        ServerPlayer *loser = pindian->success ? rival : target;
        if (winner->isAlive() && loser->isAlive() && winner->canSlash(loser, false))
            useSkillSlash(room, ctx, winner, loser, objectName());
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- luna026

class IkJiaojin : public ViewAsSkillV2
{
public:
    IkJiaojin() : ViewAsSkillV2("ikjiaojin") { setPhaseName("Play"); }

    static bool open(const Player *self, const Player *to)
    {
        return to->getMark("ikjiaojin_" + self->objectName()) == 0 && to->getHandcardNum() != to->getHp();
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return false;
        if (open(self, self))
            return true;
        foreach (const Player *p, self->getAliveSiblings())
            if (open(self, p))
                return true;
        return false;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && open(request.initiator, to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkJiaojinCard"; }

    // Short-handed characters draw two; over-handed ones discard two. Once per character.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->setPlayerMark(target, "ikjiaojin_" + source->objectName(), 1);
        room->addPlayerMark(target, "@ikjiaojin");
        const int hand = target->getHandcardNum(), hp = target->getHp();
        if (hand > hp)
            room->askForDiscard(target, objectName(), 2, 2, false, true);
        else if (hand < hp)
            target->drawCards(2, objectName());
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- luna040

class IkHongta : public TriggerSkillV2
{
public:
    IkHongta() : TriggerSkillV2("ikhongta")
    {
        events << CardFinished;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || !(use.card->isKindOf("Weapon") || use.card->isKindOf("Horse")))
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && player->inMyAttackRange(owner))
                result[owner] << objectName();
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        ctx.owner->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- luna043

class IkBengying : public TriggerSkillV2
{
public:
    IkBengying() : TriggerSkillV2("ikbengying") { events << Death; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        if (!player || death.who != player || !player->hasSkill(objectName(), true) || player->isNude() || !death.damage
            || !death.damage->from || !death.damage->from->isAlive())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *killer = ctx.original_data->value<DeathStruct>().damage->from;
        if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(killer)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // One judgement per card held; each spade 2-9 burns the killer for 3.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *killer = ctx.original_data->value<DeathStruct>().damage->from;
        const int n = player->getCardCount();
        for (int i = 0; i < n && killer && killer->isAlive(); ++i) {
            JudgeStruct judge;
            judge.pattern = ".|spade|2~9";
            judge.good = false;
            judge.negative = true;
            judge.reason = objectName();
            judge.who = killer;
            room->judge(judge);
            if (judge.isBad() && killer->isAlive())
                room->damage(DamageStruct(objectName(), nullptr, killer, 3, DamageStruct::Fire));
        }
        return false;
    }
};

// ---------------------------------------------------------------- luna044

class IkMingzhen : public TriggerSkillV2
{
public:
    IkMingzhen() : TriggerSkillV2("ikmingzhen") { events << DamageCaused << EventPhaseStart << Death; }

    static QString reason(const ServerPlayer *owner) { return "ikmingzhen_" + owner->objectName(); }

    static void lift(Room *room, ServerPlayer *owner)
    {
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getMark(reason(owner)) == 0)
                continue;
            room->setPlayerMark(p, reason(owner), 0);
            room->removePlayerMark(p, "@mingzhen");
            room->removeSkillInvalidity(p, "all", owner->objectName(), reason(owner));
        }
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if ((event == EventPhaseStart && player->getPhase() == Player::Play)
            || (event == Death && data.value<DeathStruct>().who == player))
            lift(room, player);
        return false;
    }

    static bool canNull(const ServerPlayer *to)
    {
        if (to->hasSkill("thyanmeng"))
            return false;
        foreach (const Skill *skill, to->getVisibleSkillList())
            if (!skill->isAttachedLordSkill())
                return true;
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DamageCaused)
            return TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.to
            || damage.to == player || !(canNull(damage.to) || damage.to->canDiscard(damage.to, "e")))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.original_data->value<DamageStruct>().to)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // The damage is prevented; the victim loses its skills until the owner's next play phase, or its equipment and 1 HP.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *to = ctx.original_data->value<DamageStruct>().to;
        QStringList choices;
        if (canNull(to))
            choices << "null";
        if (to->canDiscard(to, "e"))
            choices << "throw";
        if (choices.isEmpty())
            return true;
        if (room->askForChoice(to, objectName(), choices.join("+"), *ctx.original_data) == "throw") {
            to->throwAllEquips();
            if (to->isAlive())
                room->loseHp(to, 1, true, player, objectName());
        } else if (to->getMark(reason(player)) == 0) {
            room->setPlayerMark(to, reason(player), 1);
            room->addPlayerMark(to, "@mingzhen");
            room->addSkillInvalidity(to, "all", player->objectName(), reason(player));
        }
        return true;
    }
};

class IkPianxiang : public TriggerSkillV2
{
public:
    IkPianxiang() : TriggerSkillV2("ikpianxiang") { events << TargetConfirmed; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !use.card || !use.card->isKindOf("Slash") || !use.from || !use.from->isAlive()
            || !use.to.contains(player) || !player->hasEquip() || !player->isAdjacentTo(use.from)
            || !player->canDiscard(player, "e"))
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner != player && owner->hasSkill(objectName()) && player->isAdjacentTo(owner)) {
                result[owner] << objectName();
                break;
            }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (player->isAlive() && player->canDiscard(player, "e"))
            room->askForDiscard(player, objectName(), 1, 1, false, true, "@ikpianxiang-discard", ".|.|.|equipped");
        return false;
    }
};

// ---------------------------------------------------------------- luna050

class IkTingmai : public TriggerSkillV2
{
public:
    IkTingmai() : TriggerSkillV2("iktingmai") { events << DamageCaused; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || damage.nature != DamageStruct::Thunder || !damage.to
            || damage.to->isChained())
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // The source judges: black adds one damage, red goes to its hand.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        JudgeStruct judge;
        judge.who = player;
        judge.reason = objectName();
        judge.pattern = ".|black";
        judge.good = true;
        judge.play_animation = false;
        room->judge(judge);
        if (!judge.card)
            return false;
        if (judge.card->isRed()) {
            const int id = judge.card->getEffectiveId();
            if (player->isAlive() && room->getCardPlace(id) == Player::DiscardPile)
                room->obtainCard(player, id);
            return false;
        }
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#IkTingmai";
        log.from = player;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(damage.damage + 1);
        room->sendLog(log);
        ++damage.damage;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

// ---------------------------------------------------------------- luna052

class IkHuisuo : public ViewAsSkillV2
{
public:
    IkHuisuo() : ViewAsSkillV2("ikhuisuo", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isNude();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->isKindOf("Slash");
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkHuisuoCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) == source)
            room->giveCard(source, target, QList<int>() << id, objectName(), true);
        if (source->getTag("IkHuisuoTarget").toString().isEmpty()) {
            source->setTag("IkHuisuoTarget", target->objectName());
            room->setFixedDistance(source, target, 1);
        }
        return ContinueEffects;
    }
};

class IkHuisuoClear : public TriggerSkillV2
{
public:
    IkHuisuoClear() : TriggerSkillV2("#ikhuisuo") { events << EventPhaseChanging << Death; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->getTag("IkHuisuoTarget").toString().isEmpty())
            return false;
        if ((event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)
            || (event == Death && data.value<DeathStruct>().who != player))
            return false;
        const QString name = player->getTag("IkHuisuoTarget").toString();
        player->removeTag("IkHuisuoTarget");
        if (ServerPlayer *target = room->findPlayerByObjectName(name, true))
            room->removeFixedDistance(player, target, 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class IkCangliu : public ViewAsSkillV2
{
public:
    IkCangliu() : ViewAsSkillV2("ikcangliu") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkCangliuCard"; }

    // Show a 杀-free hand to conjure 杀 (three a turn) until someone dies.
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || source->isKongcheng())
            return FinishSkill;
        Room *room = source->getRoom();
        room->showAllCards(source);
        foreach (const Card *card, source->getHandcards())
            if (card->isKindOf("Slash"))
                return FinishSkill;
        grantTracked(room, source, "IkCangliuGrant", "ikcangliuv");
        return FinishSkill;
    }
};

class IkCangliuSlash : public ViewAsSkillV2
{
public:
    IkCangliuSlash() : ViewAsSkillV2("ikcangliuv") { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getMark("ikcangliu_count-Clear") >= 3)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(self);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE)
            && request.pattern.contains("slash", Qt::CaseInsensitive);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("ikcangliu");
        return slash;
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.invoker)
            return false;
        room->addPlayerMark(ctx.invoker, "ikcangliu_count-Clear");
        return true;
    }
};

class IkCangliuClear : public TriggerSkillV2
{
public:
    IkCangliuClear() : TriggerSkillV2("#ikcangliu-clear")
    {
        events << Death;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        foreach (ServerPlayer *p, room->getAllPlayers())
            if (!p->getTag("IkCangliuGrant").toMap().isEmpty())
                revokeTracked(room, p, "IkCangliuGrant");
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- luna059

class IkTianxia : public TriggerSkillV2
{
public:
    IkTianxia() : TriggerSkillV2("iktianxia") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Draw || !player->hasSkill(objectName()))
            return TriggerList();
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (p->distanceTo(player) == 1)
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Skip the draw for 遐 marks, each pushing everyone else one step further away.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->gainMark("@far", qMin(room->alivePlayerCount(), 5));
        return true;
    }
};

class IkTianxiaDistance : public DistanceSkill
{
public:
    IkTianxiaDistance() : DistanceSkill("#iktianxia") {}

    int getCorrect(const Player *, const Player *to) const override { return to ? to->getMark("@far") : 0; }
};

class IkYouer : public TriggerSkillV2
{
public:
    IkYouer() : TriggerSkillV2("ikyouer")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && !player->inMyAttackRange(owner))
                result[owner] << objectName();
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        room->sendCompulsoryTriggerLog(owner, objectName());
        room->broadcastSkillInvoke(objectName());
        QStringList choices{"draw"};
        if (player->canDiscard(player, "he"))
            choices << "discard";
        if (room->askForChoice(owner, objectName(), choices.join("+"), QVariant::fromValue(player)) == "discard")
            room->askForDiscard(player, objectName(), 1, 1, false, true);
        else
            owner->drawCards(1, objectName());
        if (owner->isAlive())
            owner->gainMark("@near");
        return false;
    }
};

class IkYouerDistance : public DistanceSkill
{
public:
    IkYouerDistance() : DistanceSkill("#ikyouer") {}

    int getCorrect(const Player *, const Player *to) const override { return to ? -to->getMark("@near") : 0; }
};

// ---------------------------------------------------------------- luna060

class IkChimo : public TriggerSkillV2
{
public:
    IkChimo() : TriggerSkillV2("ikchimo")
    {
        events << Damage << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || damage.nature != DamageStruct::Fire)
            return TriggerList();
        if (event == Damage ? damage.from != player : (damage.to != player || player->getMark("@burn") == 0))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        if (event == Damage)
            player->gainMark("@burn");
        else
            player->loseMark("@burn");
        return false;
    }
};

class IkBaohun : public TriggerSkillV2
{
public:
    IkBaohun() : TriggerSkillV2("ikbaohun")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || damage.nature != DamageStruct::Normal)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // Below X HP the blow grows by one; otherwise it turns to fire.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (player->getHp() < player->getMark("@burn"))
            ++damage.damage;
        else
            damage.nature = DamageStruct::Fire;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class IkBaohunMax : public MaxCardsSkillV2
{
public:
    IkBaohunMax() : MaxCardsSkillV2("#ikbaohun") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("@burn") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(ctx.primary->getMark("@burn"));
    }
};

// ---------------------------------------------------------------- luna061

class IkLianzhen : public ViewAsSkillV2
{
public:
    IkLianzhen() : ViewAsSkillV2("iklianzhen", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isNude();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkLianzhenCard"; }

    // A black gift makes the target pay the owner two cards or discard two.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != source)
            return ContinueEffects;
        const bool black = Sanguosha->getCard(id)->isBlack();
        room->giveCard(source, target, QList<int>() << id, objectName(), true);
        if (!black || !target->isAlive())
            return ContinueEffects;
        QStringList choices{"draw"};
        if (target->canDiscard(target, "he") && target->getCardCount() > 1)
            choices << "discard";
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant::fromValue(source)) == "discard")
            room->askForDiscard(target, objectName(), 2, 2, false, true);
        else if (source->isAlive())
            source->drawCards(2, objectName());
        return ContinueEffects;
    }
};

class IkYouxia : public TriggerSkillV2
{
public:
    IkYouxia() : TriggerSkillV2("ikyouxia") { events << EventPhaseStart << BeforeCardsMove << EventPhaseChanging; }

    static QList<int> blacks(Room *room, ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const int id = move.card_ids.at(i);
            const Player::Place place = move.from_places.value(i);
            if ((place == Player::PlaceHand || place == Player::PlaceEquip) && Sanguosha->getCard(id)->isBlack()
                && room->getCardOwner(id) == player && room->getCardPlace(id) == place)
                ids << id;
        }
        return ids;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        foreach (ServerPlayer *p, room->getAllPlayers())
            if (p->getMark("ikyouxia_locked") > 0) {
                room->setPlayerMark(p, "ikyouxia_locked", 0);
                room->removePlayerCardLimitationByReason(p, "ikyouxia");
            }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == EventPhaseStart) {
            bool black = false;
            foreach (const Card *card, player->getHandcards())
                black = black || card->isBlack();
            if (player->getPhase() != Player::Discard || !black)
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        if (event != BeforeCardsMove)
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || !move.to || move.to == player || move.to_place != Player::PlaceHand
            || blacks(room, player, move).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == BeforeCardsMove) {
            if (!player->askForSkillInvoke(objectName(), *ctx.original_data))
                return false;
            room->broadcastSkillInvoke(objectName());
            return true;
        }
        const Card *shown = room->askForExchange(player, objectName(), player->getHandcardNum(), 1, false, "@ikyouxia", true,
                                                 ".|black|.|hand");
        if (!shown)
            return false;
        ctx.extra_data = ListI2V(shown->getSubcards());
        delete shown;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Shown black cards leave the hand count this discard phase; a taker of black cards is muted for the turn.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            const QList<int> ids = ListV2I(ctx.extra_data.toList());
            room->showCard(player, ids);
            room->ignoreCards(player, ids);
            return false;
        }
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = blacks(room, player, move);
        if (ids.isEmpty())
            return false;
        room->showCard(player, ids);
        ServerPlayer *target = qobject_cast<ServerPlayer *>(move.to);
        if (target && target->getMark("ikyouxia_locked") == 0) {
            room->setPlayerMark(target, "ikyouxia_locked", 1);
            room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", false, "ikyouxia");
        }
        return false;
    }
};

}

IkXielunCard::IkXielunCard() { setSkillName("ikxielun"); mute = true; }
IkMoqiCard::IkMoqiCard() { setSkillName("ikmoqi"); mute = true; }
IkTianbeiCard::IkTianbeiCard() { setSkillName("iktianbei"); mute = true; }
IkDuanmengCard::IkDuanmengCard() { setSkillName("ikduanmeng"); mute = true; }
IkQixinCard::IkQixinCard() { setSkillName("ikqixin"); mute = true; }
IkXinbanCard::IkXinbanCard() { setSkillName("ikxinban"); mute = true; }
IkHuyinCard::IkHuyinCard() { setSkillName("ikhuyin"); mute = true; }
IkAoxueCard::IkAoxueCard() { setSkillName("ikaoxue"); mute = true; }
IkZhiyuCard::IkZhiyuCard() { setSkillName("ikzhiyu"); mute = true; }
IkFenxunCard::IkFenxunCard() { setSkillName("ikfenxun"); mute = true; }
IkCangwuCard::IkCangwuCard() { setSkillName("ikcangwu"); mute = true; }
IkLingtongCard::IkLingtongCard() { setSkillName("iklingtong"); mute = true; }
IkLunkeCard::IkLunkeCard() { setSkillName("iklunke"); mute = true; }
IkBinglingCard::IkBinglingCard() { setSkillName("ikbingling"); mute = true; }
IkHuangpoCard::IkHuangpoCard() { setSkillName("ikhuangpo"); mute = true; }
IkCaiyinCard::IkCaiyinCard() { setSkillName("ikcaiyin"); mute = true; }
IkHuzhanCard::IkHuzhanCard() { setSkillName("ikhuzhan"); mute = true; }
IkZhangeCard::IkZhangeCard() { setSkillName("ikzhange"); mute = true; }
IkXincaoCard::IkXincaoCard() { setSkillName("ikxincao"); mute = true; }
IkJiaojinCard::IkJiaojinCard() { setSkillName("ikjiaojin"); mute = true; }
IkHuisuoCard::IkHuisuoCard() { setSkillName("ikhuisuo"); mute = true; }
IkCangliuCard::IkCangliuCard() { setSkillName("ikcangliu"); mute = true; }
IkLianzhenCard::IkLianzhenCard() { setSkillName("iklianzhen"); mute = true; }

IkaiSuiPackage::IkaiSuiPackage()
    : Package("ikai-sui")
{
    // 化幻 is 龙胆.
    General *wind005 = new General(this, "wind005", "kaze", 3);
    wind005->addSkill("longdan");
    wind005->addSkill(new IkQizhou);

    General *wind022 = new General(this, "wind022", "kaze", 3, false);
    wind022->addSkill(new IkShushen);
    wind022->addSkill(new IkQiaoxia);

    General *wind023 = new General(this, "wind023", "kaze", 3, false);
    wind023->addSkill(new IkXielun);
    wind023->addSkill(new IkCanyue);
    wind023->addSkill(new IkCanyueTargetMod);
    related_skills.insert("ikcanyue", "#ikcanyue");
    wind023->addSkill(new IkJiuming);

    General *wind040 = new General(this, "wind040", "kaze", 3, false);
    wind040->addSkill(new IkYanyu);

    // 绝冲 is 枪舞.
    General *wind041 = new General(this, "wind041", "kaze", 3, false);
    wind041->addSkill(new IkWuyue);
    wind041->addSkill("qiangwu");

    General *wind043 = new General(this, "wind043", "kaze", 3);
    wind043->addSkill(new IkXinhui);
    wind043->addSkill(new IkYongji);

    General *wind044 = new General(this, "wind044", "kaze", 3, false);
    wind044->addSkill(new IkMoqi);
    wind044->addSkill(new IkTianbei);
    wind044->addRelateSkill("ikanshen");

    // 境穆 is touhou-sp's 境穆.
    General *wind046 = new General(this, "wind046", "kaze", 3);
    wind046->addSkill("ikjingmu");
    wind046->addSkill(new IkDuanmeng);
    wind046->addSkill(new IkDuanmengEffect);
    related_skills.insert("ikduanmeng", "#ikduanmeng");

    // 殇蚀 is 残蚀; 惆海 is 惆海.
    General *wind052 = new General(this, "wind052", "kaze", 5);
    wind052->addSkill("canshi");
    wind052->addSkill("chouhai");
    wind052->addSkill(new IkGuiming);

    skills << new IkAnshen << new IkHuyinEffect << new IkLingxueRecord << new IkZhiyuViewAs << new IkCangwuRecord
           << new IkBingling << new IkBinglingMax << new IkLongyaClear << new IkCangliuSlash << new IkCangliuClear;
    related_skills.insert("ikbingling", "#ikbingling-max");

    // 圣尊 is 伪帝.
    General *wind024 = new General(this, "wind024", "kaze");
    wind024->addSkill(new IkXiewang);
    wind024->addSkill("weidi");
    wind024->addRelateSkill("tiaoxin");
    wind024->addRelateSkill("thkuangqi");
    wind024->addRelateSkill("thshenyou");

    // 绝彻 is 困奋; 净涅 is 八阵.
    General *wind053 = new General(this, "wind053", "kaze");
    wind053->addSkill("kunfen");
    wind053->addSkill(new IkHewu);
    wind053->addRelateSkill("bazhen");
    wind053->addRelateSkill("tiaoxin");

    General *wind054 = new General(this, "wind054", "kaze", 3, false);
    wind054->addSkill(new IkYuzhi);
    wind054->addSkill(new IkLinglong);
    wind054->addSkill(new IkLinglongArmor);
    wind054->addSkill(new IkLinglongMax);
    related_skills.insert("iklinglong", "#iklinglong-armor");
    related_skills.insert("iklinglong", "#iklinglong-horse");
    wind054->addRelateSkill("jizhi");

    General *wind059 = new General(this, "wind059", "kaze", 3);
    wind059->addSkill(new IkFengyuan);
    wind059->addSkill(new IkMianlai);

    General *wind060 = new General(this, "wind060", "kaze", 3);
    wind060->addSkill(new IkChunsu);
    wind060->addSkill(new IkYingzhi);

    General *wind061 = new General(this, "wind061", "kaze", 3);
    wind061->addSkill(new IkQixin);
    wind061->addSkill(new IkGuangyou);
    wind061->addSkill(new IkGuangyouMax);
    related_skills.insert("ikguangyou", "#ikguangyou");

    General *bloom024 = new General(this, "bloom024", "hana");
    bloom024->addSkill(new IkXinban);

    General *bloom028 = new General(this, "bloom028", "hana", 3);
    bloom028->addSkill(new IkHuyin);
    bloom028->addSkill(new IkHongcai);

    General *bloom033 = new General(this, "bloom033", "hana");
    bloom033->addSkill(new IkXunyuyouli);

    General *bloom040 = new General(this, "bloom040", "hana");
    bloom040->addSkill(new IkTianzuoyounai);
    bloom040->addSkill(new IkShenji);
    bloom040->addSkill(new IkKuanglu);
    bloom040->addSkill(new IkKuangluMax);
    related_skills.insert("ikkuanglu", "#ikkuanglu");

    General *bloom041 = new General(this, "bloom041", "hana");
    bloom041->addSkill(new IkBenhua);

    General *bloom043 = new General(this, "bloom043", "hana", 4, true, true);
    bloom043->addSkill(new IkYaocheng);
    bloom043->addSkill(new IkHaobi);
    bloom043->addRelateSkill("feiying");

    General *bloom044 = new General(this, "bloom044", "hana");
    bloom044->addSkill(new IkAoxue);
    bloom044->addSkill(new IkAoxueEffect);
    bloom044->addSkill(new IkAoxueProhibit);
    related_skills.insert("ikaoxue", "#ikaoxue");
    related_skills.insert("ikaoxue", "#ikaoxue-prohibit");
    bloom044->addSkill(new IkLingxue);
    bloom044->addSkill(new IkLingxueMax);
    related_skills.insert("iklingxue", "#iklingxue-max");

    // 崩坏 is 崩坏.
    General *bloom046 = new General(this, "bloom046", "hana", 5);
    bloom046->addSkill(new IkGonghu);
    bloom046->addSkill(new IkXuewu);
    bloom046->addSkill("benghuai");

    General *bloom050 = new General(this, "bloom050", "hana");
    bloom050->addSkill(new IkQingwei);

    General *bloom051 = new General(this, "bloom051", "hana");
    bloom051->addSkill(new IkZhiyu);
    bloom051->addSkill(new IkZhiyuEffect);
    bloom051->addSkill(new IkZhiyuTargetMod);
    related_skills.insert("ikzhiyu", "#ikzhiyu");
    related_skills.insert("ikzhiyu", "#ikzhiyu-tar");

    General *bloom059 = new General(this, "bloom059", "hana", 3);
    bloom059->addSkill(new IkSuzhong);
    bloom059->addSkill(new IkYunhua);

    General *bloom060 = new General(this, "bloom060", "hana");
    bloom060->addSkill(new IkYongye);

    General *bloom062 = new General(this, "bloom062", "hana");
    bloom062->addSkill(new IkShemou);

    // 心渡 is 短兵.
    General *snow022 = new General(this, "snow022", "yuki");
    snow022->addSkill("duanbing");
    snow022->addSkill(new IkFenxun);
    snow022->addSkill(new IkFenxunEffect);
    related_skills.insert("ikfenxun", "#ikfenxun");

    General *snow025 = new General(this, "snow025", "yuki", 3);
    snow025->addSkill(new IkHongrou);
    snow025->addSkill(new IkHuaxiao);

    General *snow032 = new General(this, "snow032", "yuki", 3, false);
    snow032->addSkill(new IkQiwu);
    snow032->addSkill(new IkShendao);
    snow032->addRelateSkill("tianxiang");
    snow032->addRelateSkill("liuli");

    // 天眼 is 傲才.
    General *snow033 = new General(this, "snow033", "yuki", 3);
    snow033->addSkill("aocai");
    snow033->addSkill(new IkCangwu);

    General *snow040 = new General(this, "snow040", "yuki");
    snow040->addSkill(new IkLingzhou);
    snow040->addSkill(new IkMoqizhou);

    General *snow043 = new General(this, "snow043", "yuki");
    snow043->addSkill(new IkLingtong);
    snow043->addSkill(new IkXuexia);

    General *snow044 = new General(this, "snow044", "yuki");
    snow044->addSkill(new IkLunke);
    snow044->addSkill(new IkCangmie);

    // 摇音 is 去疾.
    General *snow050 = new General(this, "snow050", "yuki", 3);
    snow050->addSkill(new IkYanhuo);
    snow050->addSkill("quji");

    General *snow053 = new General(this, "snow053", "yuki", 3, false);
    snow053->addSkill(new IkCiyu);
    snow053->addSkill(new IkQingshe);
    snow053->addRelateSkill("ikbingling");

    General *snow055 = new General(this, "snow055", "yuki");
    snow055->addSkill(new IkHuangpo);
    snow055->addSkill(new IkHuangpoReturn);
    related_skills.insert("ikhuangpo", "#ikhuangpo");
    snow055->addSkill(new IkYixiang);

    General *snow059 = new General(this, "snow059", "yuki");
    snow059->addSkill(new IkHonglian);
    snow059->addSkill(new IkCaiyin);
    snow059->addRelateSkill("ikxizi");
    snow059->addRelateSkill("yingzi");
    snow059->addRelateSkill("qiaoshi");
    snow059->addRelateSkill("xiaoji");

    General *snow061 = new General(this, "snow061", "yuki", 3);
    snow061->addSkill(new IkPitai);
    snow061->addSkill(new IkHuzhan);

    // 圣尊 is 伪帝.
    General *luna017 = new General(this, "luna017", "tsuki");
    luna017->addSkill(new IkChenyan);
    luna017->addSkill(new IkMoliao);
    luna017->addSkill("weidi");

    General *luna019 = new General(this, "luna019", "tsuki");
    luna019->addSkill("thxiagong");
    luna019->addSkill(new IkZhange);

    General *luna020 = new General(this, "luna020", "tsuki", 3);
    luna020->addSkill(new IkZhizhai);
    luna020->addSkill("lirang");
    luna020->addSkill(new IkLihui);

    // 寸阳 is 鹰扬.
    General *luna021 = new General(this, "luna021", "tsuki");
    luna021->addSkill("heg_yingyang");
    luna021->addSkill(new IkFeishan);
    luna021->addSkill(new IkFeishanProhibit);
    related_skills.insert("ikfeishan", "#ikfeishan");

    General *luna022 = new General(this, "luna022", "tsuki");
    luna022->addSkill(new IkNifa);
    luna022->addSkill(new IkGuyi);

    // 疾步 is 马术.
    General *luna023 = new General(this, "luna023", "tsuki");
    luna023->addSkill("mashu");
    luna023->addSkill(new IkShunqie);

    General *luna024 = new General(this, "luna024", "tsuki");
    luna024->addSkill("mashu");
    luna024->addSkill(new IkLongya);

    General *luna025 = new General(this, "luna025", "tsuki", 3);
    luna025->addSkill(new IkHengmou);
    luna025->addSkill(new IkXincao);

    // 叩诛 is 笔伐.
    General *luna026 = new General(this, "luna026", "tsuki", 3);
    luna026->addSkill("bifa");
    luna026->addSkill(new IkJiaojin);

    // 逆蹴 is 逆乱.
    General *luna040 = new General(this, "luna040", "tsuki", 3);
    luna040->addSkill(new IkHongta);
    luna040->addSkill("niluan");

    // 宙毒 is 鸩毒; 狂谛 is 戚乱.
    General *luna043 = new General(this, "luna043", "tsuki", 3, false);
    luna043->addSkill("zhendu");
    luna043->addSkill("qiluan");
    luna043->addSkill(new IkBengying);

    General *luna044 = new General(this, "luna044", "tsuki");
    luna044->addSkill(new IkMingzhen);
    luna044->addSkill(new IkPianxiang);

    // 煌震 is 符箓.
    General *luna050 = new General(this, "luna050", "tsuki", 4, false);
    luna050->addSkill(new IkTingmai);
    luna050->addSkill("fulu");

    General *luna052 = new General(this, "luna052", "tsuki");
    luna052->addSkill(new IkHuisuo);
    luna052->addSkill(new IkHuisuoClear);
    related_skills.insert("ikhuisuo", "#ikhuisuo");
    luna052->addSkill(new IkCangliu);

    General *luna059 = new General(this, "luna059", "tsuki", 3);
    luna059->addSkill(new IkTianxia);
    luna059->addSkill(new IkTianxiaDistance);
    related_skills.insert("iktianxia", "#iktianxia");
    luna059->addSkill(new IkYouer);
    luna059->addSkill(new IkYouerDistance);
    related_skills.insert("ikyouer", "#ikyouer");

    General *luna060 = new General(this, "luna060", "tsuki", 5);
    luna060->addSkill(new IkChimo);
    luna060->addSkill(new IkBaohun);
    luna060->addSkill(new IkBaohunMax);
    related_skills.insert("ikbaohun", "#ikbaohun");

    General *luna061 = new General(this, "luna061", "tsuki", 3, false);
    luna061->addSkill(new IkLianzhen);
    luna061->addSkill(new IkYouxia);

    addMetaObject<IkXielunCard>();
    addMetaObject<IkMoqiCard>();
    addMetaObject<IkTianbeiCard>();
    addMetaObject<IkDuanmengCard>();
    addMetaObject<IkQixinCard>();
    addMetaObject<IkXinbanCard>();
    addMetaObject<IkHuyinCard>();
    addMetaObject<IkAoxueCard>();
    addMetaObject<IkZhiyuCard>();
    addMetaObject<IkFenxunCard>();
    addMetaObject<IkCangwuCard>();
    addMetaObject<IkLingtongCard>();
    addMetaObject<IkLunkeCard>();
    addMetaObject<IkBinglingCard>();
    addMetaObject<IkHuangpoCard>();
    addMetaObject<IkCaiyinCard>();
    addMetaObject<IkHuzhanCard>();
    addMetaObject<IkZhangeCard>();
    addMetaObject<IkXincaoCard>();
    addMetaObject<IkJiaojinCard>();
    addMetaObject<IkHuisuoCard>();
    addMetaObject<IkCangliuCard>();
    addMetaObject<IkLianzhenCard>();
}

ADD_PACKAGE(IkaiSui)
