#include "touhou-shin.h"
#include "touhou-utils.h"
#include "engine.h"
#include "exppattern.h"
#include "general.h"
#include "h-strategic-advantage.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"

#include <QScopedPointer>

using namespace TouhouUtils;

namespace {

bool someonesTurn(Room *room)
{
    const ServerPlayer *current = room ? room->getCurrent() : nullptr;
    return current && current->getPhase() != Player::NotActive;
}

bool ownsCard(const Player *self, const Card *card)
{
    if (!self || !card || card->hasFlag("using"))
        return false;
    const int id = card->getEffectiveId();
    return self->handCards().contains(id) || self->getEquipsId().contains(id);
}

bool isDiscardMove(const CardsMoveOneTimeStruct &move)
{
    return (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD;
}

// Any number (between the bounds) of the initiator's cards, chosen as an @@ answer.
class CardPicker : public ViewAsSkillV2
{
public:
    CardPicker(const QString &name, int minCards, int maxCards, bool handOnly, bool discard)
        : ViewAsSkillV2(name, maxCards), m_min(minCards), m_max(maxCards), m_handOnly(handOnly), m_discard(discard)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@" + objectName()
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || card->hasFlag("using") || request.selectedCardIds.size() >= m_max)
            return false;
        const int id = card->getEffectiveId();
        if (!self->handCards().contains(id) && (m_handOnly || !self->getEquipsId().contains(id)))
            return false;
        return (!m_discard || self->canDiscard(self, id)) && extraFilter(request, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const int n = request.selectedCardIds.size();
        if (n < m_min || n > m_max)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        foreach (int id, request.selectedCardIds) {
            if (!canSelectCard(selection, Sanguosha->getCard(id)))
                return false;
            selection.selectedCardIds << id;
        }
        return finalCheck(request);
    }

    bool willThrowSelectedCards() const override { return m_discard; }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

protected:
    virtual bool extraFilter(const ActiveSkillRequest &, const Card *) const { return true; }
    virtual bool finalCheck(const ActiveSkillRequest &) const { return true; }

private:
    int m_min;
    int m_max;
    bool m_handOnly;
    bool m_discard;
};

// ---------------------------------------------------------------- shin001

class ThLuanshen : public ViewAsSkillV2
{
public:
    ThLuanshen() : ViewAsSkillV2("thluanshen") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        return self && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !self->isKongcheng()
            && self->getHandcardNum() >= self->getHp();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsHandCard(request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty())
            return false;
        foreach (int id, request.selectedCardIds)
            if (!ownsHandCard(request.initiator, Sanguosha->getCard(id)))
                return false;
        return true;
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

    QString historyKey(const ActiveSkillRequest &) const override { return "ThLuanshenCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !ctx.use_card)
            return ContinueEffects;
        Room *room = source->getRoom();
        QList<int> ids;
        foreach (int id, ctx.use_card->getSubcards())
            if (room->getCardOwner(id) == source && room->getCardPlace(id) == Player::PlaceHand)
                ids << id;
        if (ids.isEmpty())
            return ContinueEffects;
        room->showCard(source, ids);
        QStringList choices;
        if (target->canDiscard(target, "he") && target->getCardCount() >= ids.length())
            choices << "discard";
        choices << "turnover";
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant(ListI2V(ids))) == "discard") {
            DummyCard shown(ids);
            room->throwCard(&shown, source, target);
            if (target->isAlive())
                room->askForDiscard(target, objectName(), ids.length(), ids.length(), false, true);
        } else {
            DummyCard shown(ids);
            room->obtainCard(target, &shown, false);
            if (target->getHandcardNum() < target->getMaxHp())
                target->drawCards(target->getMaxHp() - target->getHandcardNum(), objectName());
            target->turnOver();
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- shin002

class ThFeiman : public TriggerSkillV2
{
public:
    ThFeiman() : TriggerSkillV2("thfeiman") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@thfeiman", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return false;
        room->damage(DamageStruct(objectName(), player, target));
        if (!target->isAlive())
            return false;
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getOtherPlayers(target))
            if (target->inMyAttackRange(p))
                victims << p;
        QList<ServerPlayer *> holders;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (!p->getCards("ej").isEmpty())
                holders << p;
        QStringList choices;
        if (!victims.isEmpty())
            choices << "damage";
        if (!holders.isEmpty())
            choices << "obtain";
        if (choices.isEmpty())
            return false;
        if (room->askForChoice(target, objectName(), choices.join("+")) == "damage") {
            ServerPlayer *victim = room->askForPlayerChosen(target, victims, "thfeiman-damage", "@thfeiman-damage");
            if (victim)
                room->damage(DamageStruct(objectName(), target, victim, 1, DamageStruct::Fire));
        } else {
            ServerPlayer *holder = room->askForPlayerChosen(target, holders, "thfeiman-obtain", "@thfeiman-obtain");
            if (holder) {
                const int id = room->askForCardChosen(target, holder, "ej", objectName());
                if (id >= 0)
                    room->obtainCard(target, id);
            }
        }
        return false;
    }
};

class ThGuaiqi : public TriggerSkillV2
{
public:
    ThGuaiqi() : TriggerSkillV2("thguaiqi") { events << PreDamageDone << EventPhaseEnd; }

    // Someone took two or more damage during the current play phase.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != PreDamageDone || !player)
            return false;
        ServerPlayer *current = room->getCurrent();
        if (!current || current->getPhase() != Player::Play)
            return false;
        room->addPlayerMark(player, "thguaiqi_damage-PlayClear", data.value<DamageStruct>().damage);
        if (player->getMark("thguaiqi_damage-PlayClear") > 1 && !current->hasFlag("thguaiqi_invoke"))
            room->setPlayerFlag(current, "thguaiqi_invoke");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Play
            || !player->hasFlag("thguaiqi_invoke"))
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

    bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        if (player->isAlive())
            player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- shin003

class ThJingtao : public TriggerSkillV2
{
public:
    ThJingtao() : TriggerSkillV2("thjingtao")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !move.from_places.contains(Player::PlaceHand) || !move.is_last_handcard
            || !player->isAlive() || !player->hasSkill(objectName()))
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
        ctx.owner->drawCards(2, objectName());
        return false;
    }
};

// The draw phase is spent as a play phase instead; its end costs at least one hand card.
class ThZongni : public TriggerSkillV2
{
public:
    ThZongni() : TriggerSkillV2("thzongni") { events << EventPhaseStart << EventPhaseEnd; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (event == EventPhaseEnd) {
            if (player->getPhase() == Player::Play && player->hasFlag("thzongni"))
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        if (!player->hasSkill(objectName()) || player->getPhase() != Player::Draw)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd)
            return true;
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == EventPhaseEnd) {
            room->setPlayerFlag(player, "-thzongni");
            room->sendCompulsoryTriggerLog(player, objectName());
            if (player->canDiscard(player, "h"))
                room->askForDiscard(player, objectName(), 998, 1, false, false, "@thzongni-discard");
            return false;
        }
        room->setPlayerFlag(player, "thzongni");
        player->insertPhase(Player::Play);
        return true;
    }
};

// ---------------------------------------------------------------- shin004

bool lanzouCards(const CardsMoveOneTimeStruct &move)
{
    if (!isDiscardMove(move))
        return false;
    for (int i = 0; i < move.card_ids.length(); ++i) {
        const Player::Place from = move.from_places.value(i);
        if (from != Player::PlaceHand && from != Player::PlaceEquip)
            continue;
        const Card *card = Sanguosha->getCard(move.card_ids.at(i));
        if ((card->getTypeId() == Card::TypeBasic && !card->isKindOf("Slash")) || card->isKindOf("Nullification"))
            return true;
    }
    return false;
}

class ThLanzou : public TriggerSkillV2
{
public:
    ThLanzou() : TriggerSkillV2("thlanzou") { events << CardsMoveOneTime; }

    // The owner's discard lets someone draw; another's discard may let the owner draw.
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !player->isAlive() || !lanzouCards(move))
            return result;
        if (player->hasSkill(objectName()))
            result[player] << objectName();
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (ctx.owner == player) {
            ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
                                                            "@thlanzou", true, true);
            if (!target)
                return false;
            ctx.extra_data = target->objectName();
        } else {
            if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.owner)))
                return false;
            room->notifySkillInvoked(ctx.owner, objectName());
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = player;
            log.to << ctx.owner;
            log.arg = objectName();
            room->sendLog(log);
            ctx.extra_data = ctx.owner->objectName();
        }
        room->broadcastSkillInvoke(objectName());
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

// ---------------------------------------------------------------- shin005

class ThXinqi : public TriggerSkillV2
{
public:
    ThXinqi() : TriggerSkillV2("thxinqi")
    {
        events << TargetSpecifying;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || !use.card->isNDTrick() || !use.card->isBlack())
            return result;
        foreach (ServerPlayer *owner, use.to)
            if (owner != player && owner->isAlive() && owner->hasSkill(objectName()) && !result.contains(owner))
                result[owner] << objectName();
        return result;
    }

    // The user shows its hand to the owner, or the trick does nothing to the owner.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        room->sendCompulsoryTriggerLog(owner, objectName());
        room->broadcastSkillInvoke(objectName());
        owner->drawCards(1, objectName());
        if (!player->isAlive() || player->isKongcheng())
            return false;
        if (room->askForChoice(player, objectName(), "show+cancel", QVariant::fromValue(owner)) == "show") {
            room->showAllCards(player, owner);
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains(owner->objectName()))
            use.nullified_list << owner->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class ThNengwu : public TriggerSkillV2
{
public:
    ThNengwu() : TriggerSkillV2("thnengwu") { events << EventPhaseStart << CardsMoveOneTime << EventPhaseChanging << Death; }

    // The shown card stays the only one the target may use, until it leaves the hand or the turn ends.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        QVariantList ids = player->getTag("ThNengwuIds").toList();
        if (ids.isEmpty())
            return false;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            const int reason = move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON;
            if (move.from != player
                || (reason != CardMoveReason::S_REASON_USE && reason != CardMoveReason::S_REASON_RESPONSE
                    && reason != CardMoveReason::S_REASON_DISCARD))
                return false;
            for (int i = 0; i < move.card_ids.length(); ++i) {
                const int id = move.card_ids.at(i);
                if (move.from_places.value(i) == Player::PlaceHand && ids.contains(id)) {
                    ids.removeAll(id);
                    room->removePlayerCardLimitationByReason(player, "thnengwu_" + QString::number(id));
                }
            }
            player->setTag("ThNengwuIds", ids);
            return false;
        }
        const bool end = (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            || (event == Death && data.value<DeathStruct>().who == player);
        if (!end)
            return false;
        foreach (const QVariant &id, ids)
            room->removePlayerCardLimitationByReason(player, "thnengwu_" + QString::number(id.toInt()));
        player->removeTag("ThNengwuIds");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Start
            || player->isKongcheng())
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && !owner->isKongcheng())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->isKongcheng() || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (player->isKongcheng())
            return false;
        const Card *card = room->askForCardShow(player, owner, objectName());
        if (!card || card->getEffectiveId() < 0)
            card = player->getRandomHandCard();
        if (!card)
            return false;
        const QString choice = room->askForChoice(owner, objectName(), "basic+equip+trick", QVariant::fromValue(player));
        LogMessage log;
        log.type = "#ThNengwu";
        log.from = owner;
        log.arg = choice;
        room->sendLog(log);
        const int id = card->getEffectiveId();
        room->showCard(player, id);
        if (card->getType() == choice)
            room->loseHp(player, 1, true, owner, objectName());
        else if (owner->canDiscard(owner, "he"))
            room->askForDiscard(owner, objectName(), 1, 1, false, true);
        if (!player->isAlive())
            return false;
        QVariantList ids = player->getTag("ThNengwuIds").toList();
        ids << id;
        player->setTag("ThNengwuIds", ids);
        room->setPlayerCardLimitation(player, "use", "^" + QString::number(id), false, "thnengwu_" + QString::number(id));
        return false;
    }
};

// ---------------------------------------------------------------- shin006

// The generic "HandPile:%currency" flag makes the lent pile the borrower's hand pile
// (Player::getHandPileNames) until this play phase ends.
class ThBaochui : public TriggerSkillV2
{
public:
    ThBaochui() : TriggerSkillV2("thbaochui") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || player->isKongcheng())
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && !owner->isKongcheng() && owner->getHandcardNum() <= 3)
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.invoker)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner, *player = ctx.invoker;
        if (!player || !player->isAlive())
            return false;
        if (!player->isKongcheng()) {
            const Card *card = room->askForExchange(player, objectName(), 1, 1, false,
                                                    "@thbaochui:" + owner->objectName(), false);
            if (card && !card->getSubcards().isEmpty())
                room->giveCard(player, owner, card, objectName());
        }
        if (!owner->isAlive() || owner->isKongcheng())
            return false;
        owner->addToPile("currency", owner->handCards());
        owner->setTag("ThBaochuiBorrower", player->objectName());
        room->setPlayerFlag(player, "HandPile:%currency");
        return false;
    }
};

// The pile returns when that play phase ends, even if the lender lost 宝锤.
class ThBaochuiReturn : public TriggerSkillV2
{
public:
    ThBaochuiReturn() : TriggerSkillV2("#thbaochui")
    {
        events << EventPhaseEnd;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->getPhase() == Player::Play && player->hasFlag("HandPile:%currency"))
            room->setPlayerFlag(player, "-HandPile:%currency");
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Play)
            return result;
        foreach (ServerPlayer *lender, room->getOtherPlayers(player))
            if (lender->getTag("ThBaochuiBorrower").toString() == player->objectName())
                result[lender] << objectName();
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lender = ctx.owner;
        lender->removeTag("ThBaochuiBorrower");
        const QList<int> pile = lender->getPile("currency");
        if (!pile.isEmpty()) {
            DummyCard dummy(pile);
            CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, lender->objectName(), "thbaochui",
                                  QString());
            room->obtainCard(lender, &dummy, reason);
        }
        lender->drawCards(1, "thbaochui");
        return false;
    }
};

// ---------------------------------------------------------------- shin007

class ThMoju : public TriggerSkillV2
{
public:
    ThMoju() : TriggerSkillV2("thmoju")
    {
        events << DrawNCards << TargetSpecified;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == DrawNCards) {
            if (player->getWeapon() && data.value<DrawStruct>().reason == "draw_phase")
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from == player && player->getArmor() && use.card && use.card->isKindOf("Slash")
            && player->getPhase() == Player::Play)
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            const Weapon *weapon = player->getWeapon() ? qobject_cast<const Weapon *>(player->getWeapon()->getRealCard())
                                                       : nullptr;
            draw.num = qMax(weapon ? weapon->getRange() : 1, 2);
            *ctx.original_data = QVariant::fromValue(draw);
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        foreach (ServerPlayer *p, use.to) {
            if (use.no_respond_list.contains(p->objectName()))
                continue;
            use.no_respond_list << p->objectName();
            LogMessage log;
            log.type = "#NoJink";
            log.from = p;
            room->sendLog(log);
        }
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class ThMojuMaxCards : public MaxCardsSkillV2
{
public:
    ThMojuMaxCards() : MaxCardsSkillV2("#thmoju") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasSkill("thmoju"))
            return CorrectSkillResult::noEffect();
        const int horses = (ctx.primary->getOffensiveHorse() ? 1 : 0) + (ctx.primary->getDefensiveHorse() ? 1 : 0);
        if (horses == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(horses);
    }
};

class ThGuzhen : public TriggerSkillV2
{
public:
    ThGuzhen() : TriggerSkillV2("thguzhen")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart
            && player->getMark("thguzhen_active") > 0)
            room->setPlayerMark(player, "thguzhen_active", 0);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getMark("thguzhen_active") > 0)
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if ((move.from == player && move.from_places.contains(Player::PlaceEquip))
            || (move.to == player && move.to_place == Player::PlaceEquip))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        room->setPlayerMark(ctx.owner, "thguzhen_active", 1);
        return false;
    }
};

class ThGuzhenLimit : public CardLimitSkill
{
public:
    ThGuzhenLimit() : CardLimitSkill("#thguzhen") {}

    QString limitList(const Player *) const override { return "discard"; }

    QString limitPattern(const Player *target, const Card *card) const override
    {
        if (card->isKindOf("Treasure"))
            return QString();
        foreach (const Player *p, target->getAliveSiblings())
            if (p->getMark("thguzhen_active") > 0 && p->getEquipsId().contains(card->getId())
                && p->hasSkill("thguzhen"))
                return card->toString();
        return QString();
    }
};

// ---------------------------------------------------------------- shin008

class ThLianyingViewAs : public ViewAsSkillV2
{
public:
    ThLianyingViewAs() : ViewAsSkillV2("thlianying", 2) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getCardCount() > 1;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.size() < 2 && ownsCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2)
            return false;
        foreach (int id, request.selectedCardIds)
            if (!ownsCard(request.initiator, Sanguosha->getCard(id)))
                return false;
        return true;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThLianyingCard"; }

    // 赤莲 and 疾步 (the local 马术) until the turn ends.
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        grantTracked(room, source, "thlianying_grants", "ikchilian");
        grantTracked(room, source, "thlianying_grants", "mashu");
        return FinishSkill;
    }
};

class ThLianying : public TriggerSkillV2
{
public:
    ThLianying() : TriggerSkillV2("thlianying")
    {
        events << EventPhaseChanging;
        view_as_skill = new ThLianyingViewAs;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && data.value<PhaseChangeStruct>().to == Player::NotActive)
            revokeTracked(room, player, "thlianying_grants");
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThYuanxiao : public TriggerSkillV2
{
public:
    ThYuanxiao() : TriggerSkillV2("thyuanxiao") { events << TargetSpecified; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
            || !use.card->isKindOf("Slash"))
            return TriggerList();
        foreach (ServerPlayer *to, use.to)
            if (to->getHandcardNum() > player->getHandcardNum())
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    // Asked target by target; a red card taken means that target cannot dodge.
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        bool changed = false;
        foreach (ServerPlayer *to, use.to) {
            if (!player->isAlive() || !to->isAlive() || to->getHandcardNum() <= player->getHandcardNum()
                || !player->askForSkillInvoke(objectName(), QVariant::fromValue(to)))
                continue;
            room->broadcastSkillInvoke(objectName());
            const int id = room->askForCardChosen(player, to, "h", objectName());
            if (id < 0)
                continue;
            room->obtainCard(player, id);
            if (room->getCardOwner(id) == player)
                room->showCard(player, id);
            if (Sanguosha->getCard(id)->isRed() && !use.no_respond_list.contains(to->objectName())) {
                LogMessage log;
                log.type = "#NoJink";
                log.from = to;
                room->sendLog(log);
                use.no_respond_list << to->objectName();
                changed = true;
            }
        }
        if (changed)
            *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

// ---------------------------------------------------------------- shin009

class ThWuyi : public TriggerSkillV2
{
public:
    ThWuyi() : TriggerSkillV2("thwuyi") { events << CardsMoveOneTime << EventPhaseStart << TargetSpecified; }

    // Types the character discarded this turn.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player)
            return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || !isDiscardMove(move) || move.to_place != Player::DiscardPile)
            return false;
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const Player::Place from = move.from_places.value(i);
            if (from != Player::PlaceHand && from != Player::PlaceEquip)
                continue;
            const QString type = Sanguosha->getCard(move.card_ids.at(i))->getType();
            if (player->getMark("thwuyi_" + type + "-Clear") == 0)
                room->setPlayerMark(player, "thwuyi_" + type + "-Clear", 1);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && use.card->isKindOf("Slash") && use.card->getSkillName() == objectName()
                && player->getMark("thwuyi_equip-Clear") > 0)
                result[player] << objectName();
            return result;
        }
        if (event != EventPhaseStart || player->getPhase() != Player::Finish)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->getMark("thwuyi_basic-Clear") > 0 && !victims(room, owner).isEmpty())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == TargetSpecified)
            return true;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, victims(room, ctx.owner), objectName(), "@thwuyi", true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == TargetSpecified) {
            room->sendCompulsoryTriggerLog(owner, objectName());
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            foreach (ServerPlayer *p, use.to)
                p->addQinggangTag(use.card);
            return false;
        }
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || !owner->isAlive())
            return false;
        QList<ServerPlayer *> targets{target};
        // A trick among the discards allows one more target.
        if (owner->getMark("thwuyi_trick-Clear") > 0) {
            QList<ServerPlayer *> more = victims(room, owner);
            more.removeAll(target);
            if (!more.isEmpty()) {
                ServerPlayer *second = room->askForPlayerChosen(owner, more, objectName(), "@thwuyi-extra", true);
                if (second)
                    targets << second;
            }
        }
        room->sortByActionOrder(targets);
        auto *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        CardUseStruct use(slash, owner, targets);
        use.m_addHistory = false;
        use.setOwnedCard(slash);
        room->useCardFromSkillEffect(use, ctx);
        return false;
    }

private:
    static QList<ServerPlayer *> victims(Room *room, ServerPlayer *owner)
    {
        QList<ServerPlayer *> result;
        Slash probe(Card::NoSuit, 0);
        probe.setSkillName("thwuyi");
        foreach (ServerPlayer *p, room->getOtherPlayers(owner))
            if (owner->canSlash(p, &probe, false))
                result << p;
        return result;
    }
};

class ThMumiPicker : public CardPicker
{
public:
    ThMumiPicker() : CardPicker("thmumi", 2, 2, false, true) {}
    QString historyKey(const ActiveSkillRequest &) const override { return "ThMumiCard"; }
};

class ThMumi : public TriggerSkillV2
{
public:
    ThMumi() : TriggerSkillV2("thmumi")
    {
        events << CardAsked;
        view_as_skill = new ThMumiPicker;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const QStringList asked = data.toStringList();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || asked.isEmpty() || asked.first() != "jink"
            || player->getCardCount() < 2 || !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForUseCard(ctx.owner, "@@thmumi", "@thmumi", -1, Card::MethodDiscard) != nullptr;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        Jink *jink = new Jink(Card::NoSuit, 0);
        jink->setSkillName("_thmumi");
        room->provide(jink);
        return true;
    }
};

// ---------------------------------------------------------------- shin010

class ThWangyu : public TriggerSkillV2
{
public:
    ThWangyu() : TriggerSkillV2("thwangyu") { events << Damage; }

    // The owner's 杀 goes to a character with few cards; another's 杀 may go to the owner.
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !damage.card || !damage.card->isKindOf("Slash")
            || room->getCardPlace(damage.card->getEffectiveId()) != Player::PlaceTable)
            return result;
        if (player->hasSkill(objectName())) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (p->getHandcardNum() <= p->getMaxHp()) {
                    result[player] << objectName();
                    break;
                }
        }
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->getHandcardNum() <= owner->getMaxHp())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.card || room->getCardPlace(damage.card->getEffectiveId()) != Player::PlaceTable)
            return false;
        if (ctx.owner == player) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (p->getHandcardNum() <= p->getMaxHp())
                    targets << p;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@thwangyu", true, true);
            if (!target)
                return false;
            ctx.extra_data = target->objectName();
        } else {
            if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.owner)))
                return false;
            room->notifySkillInvoked(ctx.owner, objectName());
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = player;
            log.to << ctx.owner;
            log.arg = objectName();
            room->sendLog(log);
            ctx.extra_data = ctx.owner->objectName();
        }
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target && target->isAlive() && damage.card
            && room->getCardPlace(damage.card->getEffectiveId()) == Player::PlaceTable)
            room->obtainCard(target, damage.card);
        return false;
    }
};

class ThGuangshi : public TriggerSkillV2
{
public:
    ThGuangshi() : TriggerSkillV2("thguangshi") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DamageStruct>().to != player || !player->isAlive() || !player->hasSkill(objectName()))
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
        if (!player->canDiscard(player, "he")
            || !room->askForCard(player, "..", "@thguangshi", *ctx.original_data, objectName()))
            player->drawCards(1, objectName());
        if (!player->isAlive() || player->isKongcheng())
            return false;
        room->showAllCards(player);
        int red = 0;
        int black = 0;
        foreach (const Card *card, player->getHandcards()) {
            if (card->isRed())
                ++red;
            else if (card->isBlack())
                ++black;
        }
        if (red > black) {
            player->drawCards(1, objectName());
        } else if (black > red) {
            ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
            if (from && from->isAlive() && player->canDiscard(from, "he")) {
                const int id = room->askForCardChosen(player, from, "he", objectName(), false, Card::MethodDiscard);
                if (id >= 0)
                    room->throwCard(id, from, player);
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------- shin011

class ThSunwu : public TriggerSkillV2
{
public:
    ThSunwu() : TriggerSkillV2("thsunwu") { events << CardsMoveOneTime; }

    static QList<int> candidates(Room *room, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        if (move.to_place != Player::DiscardPile)
            return ids;
        foreach (int id, move.card_ids) {
            const Card *card = Sanguosha->getCard(id);
            if (room->getCardPlace(id) == Player::DiscardPile && (card->isKindOf("Weapon") || card->isKindOf("Armor")))
                ids << id;
        }
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->getPile("frost").isEmpty()
            || candidates(room, data.value<CardsMoveOneTimeStruct>()).isEmpty())
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
        const QList<int> ids = candidates(room, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (ids.isEmpty() || !player->getPile("frost").isEmpty())
            return false;
        int id = ids.first();
        if (ids.size() > 1) {
            room->fillAG(ids, player);
            const int chosen = room->askForAG(player, ids, false, objectName());
            room->clearAG(player);
            if (ids.contains(chosen))
                id = chosen;
        }
        player->addToPile("frost", id);
        return false;
    }
};

// 净涅 is the local 八阵.
class ThLiaogan : public TriggerSkillV2
{
public:
    ThLiaogan() : TriggerSkillV2("thliaogan") { events << EventPhaseStart << Death; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        const bool end = (event == EventPhaseStart && player->getPhase() == Player::RoundStart)
            || (event == Death && data.value<DeathStruct>().who == player);
        if (!end)
            return false;
        const QString key = "thliaogan_grants_" + player->objectName();
        foreach (ServerPlayer *p, room->getAllPlayers(true))
            revokeTracked(room, p, key);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || player->getPhase() != Player::Play)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && !owner->getPile("frost").isEmpty())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@thliaogan",
                                                        true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || owner->getPile("frost").isEmpty())
            return false;
        DummyCard dummy(owner->getPile("frost"));
        room->obtainCard(target, &dummy);
        const QString key = "thliaogan_grants_" + owner->objectName();
        grantTracked(room, target, key, "thxiagong");
        grantTracked(room, target, key, "bazhen");
        return false;
    }
};

// ---------------------------------------------------------------- shin012

class ThJianyueViewAs : public ViewAsSkillV2
{
public:
    ThJianyueViewAs() : ViewAsSkillV2("thjianyue") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thjianyue"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    const Card *createCard(const ActiveSkillRequest &) const override
    {
        Dismantlement *card = new Dismantlement(Card::NoSuit, 0);
        card->setSkillName(objectName());
        return card;
    }
};

class ThJianyue : public TriggerSkillV2
{
public:
    ThJianyue() : TriggerSkillV2("thjianyue")
    {
        events << EventPhaseStart << CardsMoveOneTime << EventPhaseChanging;
        view_as_skill = new ThJianyueViewAs;
    }

    // Remembers what the owner's virtual 过河拆桥 knocked off, and lifts the lock at turn end.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::NotActive)
                foreach (ServerPlayer *p, room->getAllPlayers(true))
                    room->removePlayerCardLimitationByReason(p, objectName());
            return false;
        }
        if (event != CardsMoveOneTime || !player || !player->hasFlag("ThJianyueUsing"))
            return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISMANTLE
            || move.reason.m_playerId != player->objectName() || move.card_ids.isEmpty() || !move.from)
            return false;
        player->setTag("ThJianyueHit", QVariantList{move.card_ids.first(), move.from->objectName()});
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || (player->getPhase() != Player::Draw && player->getPhase() != Player::Play))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        player->removeTag("ThJianyueHit");
        room->setPlayerFlag(player, "ThJianyueUsing");
        const Card *used = room->askForUseCard(player, "@@thjianyue", "@thjianyue");
        room->setPlayerFlag(player, "-ThJianyueUsing");
        return used != nullptr;
    }

    // Either the victim takes the card back but may not use it this turn, or it is chained and the phase ends.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const QVariantList hit = player->getTag("ThJianyueHit").toList();
        player->removeTag("ThJianyueHit");
        if (hit.size() != 2 || !player->isAlive())
            return false;
        const int id = hit.first().toInt();
        ServerPlayer *victim = room->findPlayerByObjectName(hit.last().toString());
        if (!victim || !victim->isAlive())
            return false;
        QStringList choices;
        if (room->getCardPlace(id) == Player::DiscardPile)
            choices << "obtain";
        choices << "skip";
        if (room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(victim)) == "obtain") {
            room->obtainCard(victim, id);
            if (room->getCardOwner(id) == victim)
                room->setPlayerCardLimitation(victim, "use,response", QString::number(id), false, objectName());
            return false;
        }
        if (!victim->isChained())
            room->setPlayerChained(victim, true, player);
        return true;
    }
};

// ---------------------------------------------------------------- shin013

class ThHuanjianViewAs : public ViewAsSkillV2
{
public:
    ThHuanjianViewAs() : ViewAsSkillV2("thhuanjian", 1) { expand_pile = "note"; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thhuanjian"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && request.initiator->getPile("note").contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && request.initiator
            && request.initiator->getPile("note").contains(request.selectedCardIds.first());
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThHuanjianCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.use_card || ctx.use_card->subcardsLength() == 0)
            return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (!ctx.invoker->getPile("note").contains(id))
            return FinishSkill;
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, ctx.invoker->objectName(), objectName(), QString());
        room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPile, reason, true);
        return FinishSkill;
    }
};

class ThHuanjian : public TriggerSkillV2
{
public:
    ThHuanjian() : TriggerSkillV2("thhuanjian")
    {
        events << BeforeCardsMove << EventPhaseChanging << EventPhaseStart;
        view_as_skill = new ThHuanjianViewAs;
    }

    // A stale mark from a skipped prompt is dropped when the next turn begins.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart)
            foreach (ServerPlayer *p, room->getAllPlayers(true))
                if (p->getMark("thhuanjian_used") > 0)
                    room->setPlayerMark(p, "thhuanjian_used", 0);
        return false;
    }

    static int fieldCount(const CardsMoveOneTimeStruct &move)
    {
        if (move.to_place != Player::DiscardPile)
            return 0;
        int n = 0;
        foreach (Player::Place place, move.from_places)
            if (place == Player::PlaceEquip || place == Player::PlaceDelayedTrick || place == Player::PlaceJudge)
                ++n;
        return n;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive)
                return result;
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->getMark("thhuanjian_used") > 0 && !p->getPile("note").isEmpty() && p->hasSkill(objectName()))
                    result[p] << objectName();
            return result;
        }
        if (event != BeforeCardsMove || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->isKongcheng() || fieldCount(data.value<CardsMoveOneTimeStruct>()) == 0)
            return result;
        result[player] << objectName();
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == EventPhaseChanging) {
            room->setPlayerMark(player, "thhuanjian_used", 0);
            room->askForUseCard(player, "@@thhuanjian", "@thhuanjian-put", -1, Card::MethodNone);
            return false;
        }
        const int n = fieldCount(ctx.original_data->value<CardsMoveOneTimeStruct>());
        const Card *card = room->askForExchange(player, objectName(), n, 1, false, "@thhuanjian", true);
        if (!card || card->getSubcards().isEmpty())
            return false;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);
        ctx.extra_data = ListI2V(card->getSubcards());
        return true;
    }

    // As many field cards as hand cards set aside become 鉴 together with them.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const QList<int> hands = ListV2I(ctx.extra_data.toList());
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> field;
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const Player::Place place = move.from_places.value(i);
            if (place == Player::PlaceEquip || place == Player::PlaceDelayedTrick || place == Player::PlaceJudge)
                field << move.card_ids.at(i);
        }
        if (hands.isEmpty() || field.length() < hands.length())
            return false;
        QList<int> taken;
        if (field.length() == hands.length()) {
            taken = field;
        } else {
            room->fillAG(field, player);
            while (taken.length() < hands.length() && !field.isEmpty()) {
                int id = room->askForAG(player, field, false, objectName());
                if (!field.contains(id))
                    id = field.first();
                field.removeOne(id);
                room->takeAG(player, id, false, QList<ServerPlayer *>{player});
                taken << id;
            }
            room->clearAG(player);
        }
        move.removeCardIds(taken);
        *ctx.original_data = QVariant::fromValue(move);
        player->addToPile("note", hands + taken, true);
        if (someonesTurn(room))
            room->setPlayerMark(player, "thhuanjian_used", 1);
        return false;
    }
};

// Two 鉴 of one colour as any basic card; two of different colours as 无懈可击.
class ThShenmi : public ViewAsSkillV2
{
public:
    ThShenmi() : ViewAsSkillV2("thshenmi", 2)
    {
        expand_pile = "note";
        setResponseOrUse(true);
    }

    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false, true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        const QList<int> notes = self->getPile("note");
        if (notes.length() < 2)
            return false;
        bool same = false;
        bool different = false;
        for (int i = 0; i < notes.length(); ++i)
            for (int j = i + 1; j < notes.length(); ++j) {
                if (Sanguosha->getCard(notes.at(i))->sameColorWith(Sanguosha->getCard(notes.at(j))))
                    same = true;
                else
                    different = true;
            }
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return same;
        const QString pattern = request.pattern;
        if (pattern.contains("nullification"))
            return different;
        return same && !responseName(pattern).isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || request.selectedCardIds.size() >= 2 || !self->getPile("note").contains(card->getEffectiveId()))
            return false;
        if (request.selectedCardIds.isEmpty())
            return true;
        const bool same = Sanguosha->getCard(request.selectedCardIds.first())->sameColorWith(card);
        return wantsNullification(request) ? !same : same;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        if (!canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first())))
            return false;
        selection.selectedCardIds << request.selectedCardIds.first();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.last()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        QString name;
        if (wantsNullification(request))
            name = "nullification";
        else if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY || !request.userString.isEmpty())
            name = request.userString;
        else
            name = responseName(request.pattern);
        if (name.isEmpty())
            return nullptr;
        Card *card = Sanguosha->cloneCard(name, Card::SuitToBeDecided, -1);
        if (!card)
            return nullptr;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }

protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        QScopedPointer<Card> card(Sanguosha->cloneCard(name));
        return card && card->getTypeId() == Card::TypeBasic;
    }

private:
    static bool wantsNullification(const ActiveSkillRequest &request)
    {
        return request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern.contains("nullification");
    }

    static QString responseName(const QString &pattern)
    {
        if (pattern.contains("slash") || pattern.contains("Slash"))
            return "slash";
        if (pattern.contains("jink") || pattern.contains("Jink"))
            return "jink";
        if (pattern.contains("peach") || pattern.contains("Peach"))
            return "peach";
        if (pattern.contains("analeptic") || pattern.contains("Analeptic"))
            return "analeptic";
        return QString();
    }
};

// ---------------------------------------------------------------- shin014

class ThMuyuViewAs : public ViewAsSkillV2
{
public:
    ThMuyuViewAs() : ViewAsSkillV2("thmuyu", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return self->canDiscard(self, "he") && self->getMark("thmuyu_used-PlayClear") == 0;
        if (self->getPile("prison").isEmpty())
            return false;
        return request.pattern.contains("jink") || request.pattern.contains("nullification");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !ownsCard(request.initiator, card))
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return request.initiator->canDiscard(request.initiator, card->getEffectiveId());
        if (request.pattern.contains("jink"))
            return card->isRed();
        return card->isBlack();
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
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return ViewAsSkillV2::createCard(request);
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = request.pattern.contains("jink")
            ? static_cast<Card *>(new Jink(material->getSuit(), material->getNumber()))
            : static_cast<Card *>(new Nullification(material->getSuit(), material->getNumber()));
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isNude();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThMuyuCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator)
            return false;
        room->addPlayerMark(ctx.initiator, "thmuyu_used-PlayClear");
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || target->isNude())
            return ContinueEffects;
        Room *room = source->getRoom();
        const bool canDiscard = target->canDiscard(target, "he") && target->getCardCount() > 1;
        const Card *card = room->askForCard(target, "..", "@thmuyu-put:" + source->objectName(), QVariant(), Card::MethodNone);
        if (!card && !canDiscard) {
            const QList<const Card *> cards = target->getCards("he");
            card = cards.at(qsanRandomBounded(cards.length()));
        }
        if (card)
            source->addToPile("prison", card);
        else
            room->askForDiscard(target, objectName(), 2, 2, false, true);
        return ContinueEffects;
    }
};

class ThMuyu : public TriggerSkillV2
{
public:
    ThMuyu() : TriggerSkillV2("thmuyu")
    {
        events << EventPhaseStart;
        view_as_skill = new ThMuyuViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start
            || player->getPile("prison").isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        DummyCard dummy(ctx.owner->getPile("prison"));
        room->obtainCard(ctx.owner, &dummy);
        return false;
    }
};

// ---------------------------------------------------------------- shin015

// 转换技: draw three then discard one, or discard three then draw one.
class ThNihui : public ViewAsSkillV2
{
public:
    ThNihui() : ViewAsSkillV2("thnihui", 3) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        return self && self->getMark("thnihui_step") > 0 && request.selectedCardIds.size() < 3 && ownsCard(self, card)
            && self->canDiscard(self, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        if (self->getMark("thnihui_step") == 0)
            return request.selectedCardIds.isEmpty();
        if (request.selectedCardIds.size() != 3)
            return false;
        foreach (int id, request.selectedCardIds)
            if (!ownsCard(self, Sanguosha->getCard(id)))
                return false;
        return true;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThNihuiCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        if (source->getMark("thnihui_step") == 0) {
            source->drawCards(3, objectName());
            if (source->isAlive() && source->canDiscard(source, "he"))
                room->askForDiscard(source, objectName(), 1, 1, false, true);
            room->setPlayerMark(source, "thnihui_step", 1);
        } else {
            source->drawCards(1, objectName());
            room->setPlayerMark(source, "thnihui_step", 0);
        }
        return FinishSkill;
    }
};

class ThTanguan : public TriggerSkillV2
{
public:
    ThTanguan() : TriggerSkillV2("thtanguan") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
            || player->isKongcheng() || candidates(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates(room, ctx.owner), objectName(), "@thtanguan",
                                                        true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    // Each takes the other's pindian card; two reds heal the owner, two blacks feed the target.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || !player->canPindian(target))
            return false;
        PindianStruct *pindian = player->PinDian(target, objectName());
        if (!pindian || !pindian->from_card || !pindian->to_card)
            return false;
        const Card *mine = pindian->from_card;
        const Card *theirs = pindian->to_card;
        if (target->isAlive() && room->getCardPlace(mine->getEffectiveId()) == Player::DiscardPile)
            room->obtainCard(target, mine);
        if (player->isAlive() && room->getCardPlace(theirs->getEffectiveId()) == Player::DiscardPile)
            room->obtainCard(player, theirs);
        if (!player->isAlive() || !mine->sameColorWith(theirs))
            return false;
        if (mine->isRed() && player->isWounded() && player->askForSkillInvoke("thtanguan", "recover"))
            room->recover(player, RecoverStruct(objectName(), player));
        else if (mine->isBlack() && target->isAlive() && player->askForSkillInvoke("thtanguan", "draw:" + target->objectName()))
            target->drawCards(2, objectName());
        return false;
    }

private:
    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canPindian(p))
                result << p;
        return result;
    }
};

// ---------------------------------------------------------------- shin016

class ThYuancui : public TriggerSkillV2
{
public:
    ThYuancui() : TriggerSkillV2("thyuancui")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        if (player->isWounded())
            room->recover(player, RecoverStruct(objectName(), player));
        else
            room->loseHp(player, 1, true, player, objectName());
        return false;
    }
};

class ThHuikuang : public TriggerSkillV2
{
public:
    ThHuikuang() : TriggerSkillV2("thhuikuang") { events << HpLost << HpRecover; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == HpRecover && (!player->canDiscard(player, "he") || player->getCardCount() < 2))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == HpLost) {
            if (!player->askForSkillInvoke(objectName()))
                return false;
        } else {
            const Card *card = room->askForExchange(player, objectName(), 2, 2, true, "@thhuikuang", true);
            if (!card || card->getSubcards().size() != 2)
                return false;
            ctx.extra_data = ListI2V(card->getSubcards());
        }
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Once per phase, a matching pair is followed by 南蛮入侵.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QList<int> ids;
        if (event == HpLost) {
            ids = room->getNCards(2, false);
            CardMoveReason reason(CardMoveReason::S_REASON_DRAW, player->objectName(), objectName(), QString());
            room->moveCardsAtomic(CardsMoveStruct(ids, player, Player::PlaceHand, reason), false);
        } else {
            ids = ListV2I(ctx.extra_data.toList());
            LogMessage log;
            log.type = "$DiscardCardWithSkill";
            log.from = player;
            log.arg = objectName();
            log.card_str = ListI2S(ids).join("+");
            room->sendLog(log);
            DummyCard dummy(ids);
            room->throwCard(&dummy, player);
        }
        if (ids.size() != 2 || !player->isAlive() || usedThisPhase(room, player, objectName()))
            return false;
        if (!player->askForSkillInvoke("thhuikuang_sa", event == HpLost ? "show" : "use"))
            return false;
        markUsedThisPhase(room, player, objectName());
        if (event == HpLost) {
            QList<int> shown;
            foreach (int id, ids)
                if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand)
                    shown << id;
            if (!shown.isEmpty())
                room->showCard(player, shown);
        }
        if (!Sanguosha->getCard(ids.first())->sameColorWith(Sanguosha->getCard(ids.last())))
            return false;
        auto *aoe = new SavageAssault(Card::NoSuit, 0);
        aoe->setSkillName("_thhuikuang");
        if (!aoe->isAvailable(player) || player->isCardLimited(aoe, Card::MethodUse)) {
            delete aoe;
            return false;
        }
        CardUseStruct use(aoe, player, QList<ServerPlayer *>());
        use.setOwnedCard(aoe);
        room->useCardFromSkillEffect(use, ctx);
        return false;
    }
};

// ---------------------------------------------------------------- shin017

// Three cards face down as 舞; every use, response or discard of hand cards swaps hand and 舞.
class ThXuanman : public TriggerSkillV2
{
public:
    ThXuanman() : TriggerSkillV2("thxuanman")
    {
        events << DrawNCards << CardsMoveOneTime << MarkChanged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == DrawNCards) {
            const DrawStruct draw = data.value<DrawStruct>();
            if (draw.reason == "InitialHandCards" && draw.num > 0)
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        if (event == MarkChanged) {
            const MarkStruct mark = data.value<MarkStruct>();
            if (mark.name == "@flying" && player->getMark("@flying") > 3)
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to == player && move.to_place == Player::PlaceHand && move.reason.m_skillName == "InitialHandCards"
            && player->getPile("dance").isEmpty())
            return TriggerList{{player, {objectName()}}};
        const int reason = move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON;
        if (move.from == player && move.from_places.contains(Player::PlaceHand)
            && (reason == CardMoveReason::S_REASON_USE || reason == CardMoveReason::S_REASON_RESPONSE
                || reason == CardMoveReason::S_REASON_DISCARD))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += 2;
            *ctx.original_data = QVariant::fromValue(draw);
            return false;
        }
        if (event == MarkChanged) {
            room->removePlayerMark(player, "@flying", 4);
            player->drawCards(1, objectName());
            return false;
        }
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.reason.m_skillName == "InitialHandCards") {
            const int n = qMin(3, player->getHandcardNum());
            if (n == 0)
                return false;
            const Card *card = room->askForExchange(player, objectName(), n, n, false, "@thxuanman");
            QList<int> ids = card ? card->getSubcards() : QList<int>();
            if (ids.size() != n)
                ids = player->handCards().mid(0, n);
            player->addToPile("dance", ids, false);
            return false;
        }
        const QList<int> hands = player->handCards();
        const QList<int> dances = player->getPile("dance");
        QList<CardsMoveStruct> moves;
        if (!hands.isEmpty()) {
            CardsMoveStruct toPile(hands, player, Player::PlaceSpecial,
                                   CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(), QString()));
            toPile.to_pile_name = "dance";
            moves << toPile;
        }
        if (!dances.isEmpty())
            moves << CardsMoveStruct(dances, player, Player::PlaceHand,
                                     CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, player->objectName(),
                                                    objectName(), QString()));
        if (!moves.isEmpty())
            room->moveCardsAtomic(moves, false);
        room->addPlayerMark(player, "@flying");
        return false;
    }
};

class ThKuangwuViewAs : public ViewAsSkillV2
{
public:
    ThKuangwuViewAs() : ViewAsSkillV2("thkuangwu") { setPhaseName("Play"); }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "ThKuangwuCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        QList<const Card *> thrown;
        for (int round = 0; round < 2 && source->isAlive(); ++round) {
            source->drawCards(1, objectName());
            if (!source->canDiscard(source, "he"))
                continue;
            const Card *card = room->askForExchange(source, objectName(), 1, 1, true, "@thkuangwu", false);
            int id = card && !card->getSubcards().isEmpty() ? card->getSubcards().first() : -1;
            if (id < 0) {
                const QList<const Card *> cards = source->getCards("he");
                if (cards.isEmpty())
                    continue;
                id = cards.at(qsanRandomBounded(cards.length()))->getEffectiveId();
            }
            thrown << Sanguosha->getCard(id);
            room->throwCard(id, source);
        }
        if (thrown.size() == 2 && thrown.first()->sameColorWith(thrown.last()))
            room->setPlayerFlag(source, "ThKuangwuInvoke");
        return FinishSkill;
    }
};

class ThKuangwu : public TriggerSkillV2
{
public:
    ThKuangwu() : TriggerSkillV2("thkuangwu")
    {
        events << EventPhaseChanging;
        view_as_skill = new ThKuangwuViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasFlag("ThKuangwuInvoke")
            || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->setPlayerFlag(ctx.owner, "-ThKuangwuInvoke");
        if (!ctx.owner->askForSkillInvoke(objectName(), "draw"))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        player->drawCards(1, objectName());
        if (!player->isKongcheng()) {
            const Card *card = room->askForExchange(player, objectName(), 1, 1, false, "@thkuangwu-put", false);
            const int id = card && !card->getSubcards().isEmpty() ? card->getSubcards().first()
                                                                  : player->getRandomHandCard()->getEffectiveId();
            player->addToPile("dance", id, false);
        }
        if (player->canDiscard(player, "he"))
            room->askForDiscard(player, objectName(), 1, 1, false, true);
        return false;
    }
};

// ---------------------------------------------------------------- shin018

class ThDieyingViewAs : public ViewAsSkillV2
{
public:
    ThDieyingViewAs() : ViewAsSkillV2("thdieying", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thdieying"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *to) const override
    {
        return selected.isEmpty() && to && !to->isWounded();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThDieyingCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.invoker && target && target->isAlive())
            target->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target));
        return ContinueEffects;
    }
};

class ThDieying : public TriggerSkillV2
{
public:
    ThDieying() : TriggerSkillV2("thdieying")
    {
        events << PreCardUsed << EventPhaseStart;
        view_as_skill = new ThDieyingViewAs;
    }

    // The single type the owner used this turn; a second type disables the skill.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != PreCardUsed || !player || !isOwnTurn(player))
            return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill)
            return false;
        const int type = int(use.card->getTypeId()) + 1;
        const int seen = player->getMark("thdieying_type-Clear");
        if (seen == 0)
            room->setPlayerMark(player, "thdieying_type-Clear", type);
        else if (seen != type)
            room->setPlayerMark(player, "thdieying_mixed-Clear", 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Discard || player->getMark("thdieying_type-Clear") == 0
            || player->getMark("thdieying_mixed-Clear") > 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thdieying", "@thdieying", -1, Card::MethodDiscard);
        return false;
    }
};

class ThBiyi : public ViewAsSkillV2
{
public:
    ThBiyi() : ViewAsSkillV2("thbiyi") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *to) const override
    {
        int sum = 0;
        foreach (const Player *p, selected)
            sum += p->getHp();
        return to && sum < 3 && sum + to->getHp() <= 3;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThBiyiCard"; }

    EffectFlow effectOnTarget(SkillContext &, ServerPlayer *target) const override
    {
        if (target && target->isAlive())
            target->drawCards(1, objectName());
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- shin019

class ThTunaPicker : public CardPicker
{
public:
    ThTunaPicker() : CardPicker("thtuna", 1, 13, true, false) {}

    QString historyKey(const ActiveSkillRequest &) const override { return "ThTunaCard"; }

protected:
    bool extraFilter(const ActiveSkillRequest &request, const Card *card) const override
    {
        int sum = 0;
        foreach (int id, request.selectedCardIds)
            sum += Sanguosha->getCard(id)->getNumber();
        return sum < 13 && sum + card->getNumber() <= 13;
    }
};

class ThTuna : public TriggerSkillV2
{
public:
    ThTuna() : TriggerSkillV2("thtuna")
    {
        events << EventPhaseChanging;
        view_as_skill = new ThTunaPicker;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->isKongcheng()
            || player->isSkipped(Player::Play) || data.value<PhaseChangeStruct>().to != Player::Play)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *card = room->askForUseCard(ctx.owner, "@@thtuna", "@thtuna", -1, Card::MethodNone);
        if (!card || card->getSubcards().isEmpty())
            return false;
        ctx.extra_data = ListI2V(card->getSubcards());
        return true;
    }

    // The play phase is given up for the 翕.
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QList<int> ids;
        foreach (int id, ListV2I(ctx.extra_data.toList()))
            if (player->handCards().contains(id))
                ids << id;
        player->skip(Player::Play);
        if (!ids.isEmpty())
            player->addToPile("breath", ids, true);
        return false;
    }
};

class ThNinggu : public ViewAsSkillV2
{
public:
    ThNinggu() : ViewAsSkillV2("thninggu", 1) { expand_pile = "breath"; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->getPile("breath").isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && request.initiator->getPile("breath").contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && request.initiator
            && request.initiator->getPile("breath").contains(request.selectedCardIds.first());
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator
            && to->getMark("thninggu_used-Clear") == 0;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThNingguCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !ctx.use_card || ctx.use_card->subcardsLength() == 0)
            return ContinueEffects;
        Room *room = source->getRoom();
        room->setPlayerMark(target, "thninggu_used-Clear", 1);
        const int id = ctx.use_card->getSubcards().first();
        if (source->getPile("breath").contains(id)) {
            CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString());
            room->throwCard(Sanguosha->getCard(id), reason, nullptr);
        }
        if (!target->isAlive())
            return ContinueEffects;
        const Card *card = room->askForCard(target, "^BasicCard", "@thninggu-give:" + source->objectName(), QVariant(),
                                            Card::MethodNone);
        if (card && source->isAlive())
            room->giveCard(target, source, card, objectName());
        else
            room->loseHp(target, 1, true, source, objectName());
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- shin020

// 贫: its holder has 赤贫 and 哀悯; the owner collects it from the dead and draws when it moves.
class ThQiongfaziyuan : public TriggerSkillV2
{
public:
    ThQiongfaziyuan() : TriggerSkillV2("thqiongfaziyuan")
    {
        events << GameStart << Death << MarkChanged;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != MarkChanged || !player)
            return false;
        if (data.value<MarkStruct>().name != "@poor")
            return false;
        if (player->getMark("@poor") > 0 && player->isAlive()) {
            grantTracked(room, player, "thpoor_grants", "thchipin");
            grantTracked(room, player, "thpoor_grants", "thaimin");
        } else {
            revokeTracked(room, player, "thpoor_grants");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == GameStart) {
            if (player->isAlive() && player->hasSkill(objectName()))
                result[player] << objectName();
        } else if (event == Death) {
            const DeathStruct death = data.value<DeathStruct>();
            if (death.who && death.who->getMark("@poor") > 0)
                foreach (ServerPlayer *owner, room->getAlivePlayers())
                    if (owner != death.who && owner->hasSkill(objectName()))
                        result[owner] << objectName();
        } else {
            const MarkStruct mark = data.value<MarkStruct>();
            if (mark.name == "@poor" && mark.gain > 0 && player->hasFlag("thaimin_receive"))
                foreach (ServerPlayer *owner, room->getAlivePlayers())
                    if (owner->hasSkill(objectName()))
                        result[owner] << objectName();
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        room->sendCompulsoryTriggerLog(owner, objectName());
        if (event == MarkChanged)
            owner->drawCards(1, objectName());
        else
            owner->gainMark("@poor");
        return false;
    }
};

class ThChipin : public TriggerSkillV2
{
public:
    ThChipin() : TriggerSkillV2("thchipin")
    {
        events << DrawNCards;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DrawStruct draw = data.value<DrawStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || draw.reason != "draw_phase" || draw.num < 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num = qMax(0, draw.num - 1);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class ThAimin : public ViewAsSkillV2
{
public:
    ThAimin() : ViewAsSkillV2("thaimin") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return false;
        bool someoneHasCards = false;
        foreach (const Player *p, self->getAliveSiblings()) {
            if (p->getHandcardNum() < self->getHandcardNum())
                return false;
            if (!p->isKongcheng())
                someoneHasCards = true;
        }
        return someoneHasCards;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThAiminCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        bool received = false;
        foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
            if (p->isKongcheng() || !source->isAlive())
                continue;
            const Card *card = room->askForCard(p, ".|.|.|hand", "@thaimin-give:" + source->objectName(), QVariant(),
                                                Card::MethodNone);
            if (card) {
                room->giveCard(p, source, card, objectName());
                received = true;
            }
        }
        if (!received || !source->isAlive() || source->getMark("@poor") == 0)
            return FinishSkill;
        bool fewest = true;
        foreach (ServerPlayer *p, room->getOtherPlayers(source))
            if (p->getHandcardNum() < source->getHandcardNum())
                fewest = false;
        if (fewest)
            return FinishSkill;
        // No longer the poorest: 贫 passes on.
        room->sendCompulsoryTriggerLog(source, objectName());
        ServerPlayer *target = room->askForPlayerChosen(source, room->getOtherPlayers(source), objectName(), "@thaimin");
        if (!target)
            return FinishSkill;
        source->loseMark("@poor");
        room->setPlayerFlag(target, "thaimin_receive");
        target->gainMark("@poor");
        room->setPlayerFlag(target, "-thaimin_receive");
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- shin021

class ThShenhu : public TriggerSkillV2
{
public:
    ThShenhu() : TriggerSkillV2("thshenhu") { events << Damage << Damaged; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->isKongcheng()
            || (event == Damage && damage.from != player) || (event == Damaged && damage.to != player)
            || candidates(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates(room, ctx.owner), objectName(), "@thshenhu",
                                                        true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    // A won pindian is followed by 调虎离山 on the target regardless of legality.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || !player->canPindian(target))
            return false;
        if (player->pindian(target, objectName())) {
            if (!player->isAlive() || !target->isAlive())
                return false;
            Card *lure = new HLureTiger(Card::NoSuit, 0);
            lure->setSkillName("_thshenhu");
            CardUseStruct use(lure, player, target);
            use.setOwnedCard(lure);
            room->useCardFromSkillEffect(use, ctx);
        } else if (target->isAlive()) {
            target->drawCards(1, objectName());
        }
        return false;
    }

private:
    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canPindian(p))
                result << p;
        return result;
    }
};

class ThHouhu : public TriggerSkillV2
{
public:
    ThHouhu() : TriggerSkillV2("thhouhu")
    {
        events << Pindian;
        frequency = Frequent;
    }

    // Once per pindian, answered on the initiator's dispatch.
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        PindianStruct *pindian = data.value<PindianStruct *>();
        if (!pindian || !player || pindian->from != player)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()))
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

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- shin022

class ThGuizhou : public TargetModSkillV2
{
public:
    ThGuizhou() : TargetModSkillV2("thguizhou", "Slash") {}

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::ExtraTarget || !ctx.primary || !ctx.primary->hasSkill(objectName()))
            return CorrectSkillResult::noEffect();
        int n = ctx.primary->getHp() == 1 ? 1 : 0;
        foreach (const Player *p, ctx.primary->getAliveSiblings())
            if (p->getHp() == 1)
                ++n;
        if (n == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(n);
    }
};

class ThRenmo : public ViewAsSkillV2
{
public:
    ThRenmo() : ViewAsSkillV2("threnmo", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isKindOf("EquipCard");
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThRenmoCard"; }

    // Recasts the equipment, then +1 range or +1 杀 for this turn.
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !ctx.use_card || ctx.use_card->subcardsLength() == 0)
            return FinishSkill;
        Room *room = player->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand)
            return FinishSkill;
        CardMoveReason reason(CardMoveReason::S_REASON_RECAST, player->objectName());
        reason.m_skillName = objectName();
        room->moveCardTo(Sanguosha->getCard(id), player, nullptr, Player::DiscardPile, reason);
        LogMessage log;
        log.type = "#UseCard_Recast";
        log.from = player;
        log.card_str = QString::number(id);
        room->sendLog(log);
        player->drawCards(1, "recast");
        if (!player->isAlive())
            return FinishSkill;
        if (room->askForChoice(player, objectName(), "attackrange+slashnum") == "attackrange")
            room->addPlayerMark(player, "threnmo_range-Clear");
        else
            room->addPlayerMark(player, "threnmo_num-Clear");
        return FinishSkill;
    }
};

class ThRenmoRange : public AttackRangeSkillV2
{
public:
    ThRenmoRange() : AttackRangeSkillV2("#threnmo-range") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("threnmo_range-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.primary->getMark("threnmo_range-Clear"));
    }
};

class ThRenmoTargetMod : public TargetModSkillV2
{
public:
    ThRenmoTargetMod() : TargetModSkillV2("#threnmo", "Slash") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.primary || ctx.primary->getMark("threnmo_num-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.primary->getMark("threnmo_num-Clear"));
    }
};

// ---------------------------------------------------------------- shin023

class ThMieyi : public TriggerSkillV2
{
public:
    ThMieyi() : TriggerSkillV2("thmieyi") { events << TargetSpecifying; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || !use.card->isKindOf("Slash"))
            return result;
        foreach (ServerPlayer *to, use.to)
            if (to != player && to->isAlive() && to->hasSkill(objectName()) && !result.contains(to))
                result[to] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // The 杀 is void; its user is taken to have used an unstoppable 决斗 on the owner.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains("_ALL_TARGETS"))
            use.nullified_list << "_ALL_TARGETS";
        *ctx.original_data = QVariant::fromValue(use);
        if (!player->isAlive() || !owner->isAlive())
            return false;
        auto *duel = new Duel(Card::NoSuit, 0);
        duel->setSkillName("_thmieyi");
        if (player->isProhibited(owner, duel)) {
            delete duel;
            return false;
        }
        CardUseStruct duelUse(duel, player, owner);
        duelUse.no_respond_list << "_ALL_TARGETS";
        duelUse.setOwnedCard(duel);
        room->useCardFromSkillEffect(duelUse, ctx);
        return false;
    }
};

class ThShili : public TriggerSkillV2
{
public:
    ThShili() : TriggerSkillV2("thshili") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play)
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
        const QList<int> ids = room->getNCards(2, false, false);
        if (ids.size() != 2)
            return false;
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        const bool same = Sanguosha->getCard(ids.first())->sameColorWith(Sanguosha->getCard(ids.last()));
        CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
        if (same) {
            auto *slash = new Slash(Card::SuitToBeDecided, -1);
            slash->addSubcards(ids);
            slash->setSkillName("_thshili");
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (player->canSlash(p, slash, false))
                    targets << p;
            ServerPlayer *target = targets.isEmpty()
                ? nullptr
                : room->askForPlayerChosen(player, targets, objectName(), "@dummy-slash", true);
            if (target) {
                CardUseStruct use(slash, player, target);
                use.m_addHistory = false;
                use.setOwnedCard(slash);
                room->useCardFromSkillEffect(use, ctx);
                return false;
            }
            delete slash;
            DummyCard dummy(ids);
            room->throwCard(&dummy, toPile, nullptr);
            return false;
        }
        room->fillAG(ids, player);
        int id = room->askForAG(player, ids, false, objectName());
        room->clearAG(player);
        if (!ids.contains(id))
            id = ids.first();
        room->obtainCard(player, id, true);
        const int other = id == ids.first() ? ids.last() : ids.first();
        if (room->getCardPlace(other) == Player::PlaceTable)
            room->throwCard(Sanguosha->getCard(other), toPile, nullptr);
        return false;
    }
};

// ---------------------------------------------------------------- shin024

class ThYuchi : public TriggerSkillV2
{
public:
    ThYuchi() : TriggerSkillV2("thyuchi") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || player->isKongcheng()
            || !Slash::IsAvailable(player))
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
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

    // A card given (forced when the target holds more) buys a 杀 that counts toward the limit.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (player->isKongcheng() || !owner->isAlive())
            return false;
        const bool forced = player->getHandcardNum() > owner->getHandcardNum();
        const Card *card = room->askForCard(player, ".|.|.|hand", "@thyuchi:" + owner->objectName(), QVariant(),
                                            Card::MethodNone);
        if (!card && forced)
            card = player->getRandomHandCard();
        if (!card)
            return false;
        room->giveCard(player, owner, card, objectName());
        if (!player->isAlive())
            return false;
        auto *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_thyuchi");
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canSlash(p, slash))
                targets << p;
        if (targets.isEmpty()) {
            delete slash;
            return false;
        }
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@dummy-slash");
        if (!target) {
            delete slash;
            return false;
        }
        CardUseStruct use(slash, player, target);
        use.m_addHistory = true;
        use.setOwnedCard(slash);
        room->useCardFromSkillEffect(use, ctx);
        return false;
    }
};

class ThSancai : public TriggerSkillV2
{
public:
    ThSancai() : TriggerSkillV2("thsancai")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start
            || player->getHandcardNum() <= player->getMaxCards())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        auto *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_thsancai");
        QList<ServerPlayer *> users;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (p->canSlash(player, slash, false))
                users << p;
        ServerPlayer *user = users.isEmpty() ? nullptr : room->askForPlayerChosen(player, users, objectName(), "@thsancai");
        if (user) {
            CardUseStruct use(slash, user, player);
            use.m_addHistory = false;
            use.setOwnedCard(slash);
            room->useCardFromSkillEffect(use, ctx);
        } else {
            delete slash;
        }
        if (player->isAlive())
            player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- shin025

class ThRuizhiViewAs : public ViewAsSkillV2
{
public:
    ThRuizhiViewAs() : ViewAsSkillV2("thruizhi", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thruizhi"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *to) const override
    {
        return selected.isEmpty() && to;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThRuizhiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive())
            target->drawCards(3, objectName());
        if (ctx.invoker)
            ctx.invoker->getRoom()->setPlayerMark(ctx.invoker, "thruizhi_step", 1);
        return ContinueEffects;
    }
};

// 转换技 on damage: discard to let someone draw three, then ask everyone to discard.
class ThRuizhi : public TriggerSkillV2
{
public:
    ThRuizhi() : TriggerSkillV2("thruizhi")
    {
        events << Damaged;
        view_as_skill = new ThRuizhiViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DamageStruct>().to != player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (player->getMark("thruizhi_step") == 0 && !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->getMark("thruizhi_step") == 0) {
            room->askForUseCard(player, "@@thruizhi", "@thruizhi", -1, Card::MethodDiscard);
            return false;
        }
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        int n = 0;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isAlive() || !p->canDiscard(p, "he"))
                continue;
            if (room->askForDiscard(p, objectName(), 1, 1, true, true)) {
                ++n;
                if (n == 2 && player->isAlive() && player->isWounded())
                    room->recover(player, RecoverStruct(objectName(), player));
            }
        }
        room->setPlayerMark(player, "thruizhi_step", 0);
        return false;
    }
};

// ---------------------------------------------------------------- shin026

// The 倅 holder may ask the owner for 杀 (the owner discards a hand card) or 闪 (the owner loses
// a max HP) whenever one is requested.
class ThLingwei : public TriggerSkillV2
{
public:
    ThLingwei() : TriggerSkillV2("thlingwei") { events << GameStart << EventPhaseStart << CardAsked; }

    static ServerPlayer *sourceOf(Room *room, const ServerPlayer *player)
    {
        return room->findPlayerByObjectName(player->property("thlingweisource").toString());
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (event == GameStart || (event == EventPhaseStart && player->getPhase() == Player::Play)) {
            if (player->hasSkill(objectName()))
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        if (event != CardAsked || player->getMark("@assistant") == 0)
            return TriggerList();
        ServerPlayer *source = sourceOf(room, player);
        const QStringList asked = data.toStringList();
        if (!source || !source->isAlive() || !source->hasSkill(objectName()) || asked.isEmpty())
            return TriggerList();
        if ((asked.first() == "slash" && source->canDiscard(source, "h")) || asked.first() == "jink")
            return TriggerList{{source, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.owner;
        if (event != CardAsked) {
            ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), objectName(), "@thlingwei",
                                                            true, true);
            if (!target)
                return false;
            room->broadcastSkillInvoke(objectName());
            ctx.extra_data = target->objectName();
            return true;
        }
        if (player != source && !player->askForSkillInvoke(objectName(), QVariant::fromValue(source)))
            return false;
        const QString pattern = ctx.original_data->toStringList().first();
        if (pattern == "slash")
            return room->askForCard(source, ".|.|.|hand", "@thlingwei-slash:" + player->objectName(), QVariant(),
                                    objectName())
                != nullptr;
        if (player != source && !source->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->loseMaxHp(source, 1, objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.owner;
        if (event != CardAsked) {
            ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
            if (!target || !target->isAlive())
                return false;
            foreach (ServerPlayer *p, room->getAllPlayers(true))
                if (p->getMark("@assistant") > 0) {
                    room->setPlayerMark(p, "@assistant", 0);
                    room->setPlayerProperty(p, "thlingweisource", QString());
                }
            room->setPlayerMark(target, "@assistant", 1);
            room->setPlayerProperty(target, "thlingweisource", source->objectName());
            return false;
        }
        if (!player->isAlive())
            return false;
        const QString pattern = ctx.original_data->toStringList().first();
        Card *card = Sanguosha->cloneCard(pattern == "slash" ? "slash" : "jink", Card::NoSuit, 0);
        if (!card)
            return false;
        card->setSkillName("_thlingwei");
        room->provide(card);
        return true;
    }
};

// ---------------------------------------------------------------- shin028

class ThYuguang : public ViewAsSkillV2
{
public:
    ThYuguang() : ViewAsSkillV2("thyuguang") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isKongcheng();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThYuguangCard"; }

    // Shows the hand; one suit held twice or more may be cut down to a single card.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || target->isKongcheng())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->showAllCards(target);
        const QList<int> hand = target->handCards();
        QMap<int, QList<int>> bySuit;
        foreach (int id, hand)
            bySuit[int(Sanguosha->getCard(id)->getSuit())] << id;
        QList<int> choosable;
        foreach (const QList<int> &ids, bySuit)
            if (ids.size() >= 2)
                foreach (int id, ids)
                    if (source->canDiscard(target, id))
                        choosable << id;
        QList<int> thrown;
        int suit = -1;
        while (!choosable.isEmpty()) {
            QList<int> disabled;
            foreach (int id, hand)
                if (!choosable.contains(id))
                    disabled << id;
            room->fillAG(hand, source, disabled);
            int id = room->askForAG(source, choosable, suit >= 0, objectName());
            room->clearAG(source);
            if (!choosable.contains(id))
                break;
            thrown << id;
            suit = int(Sanguosha->getCard(id)->getSuit());
            bySuit[suit].removeOne(id);
            if (bySuit[suit].size() <= 1)
                break;
            choosable.clear();
            foreach (int rest, bySuit[suit])
                if (source->canDiscard(target, rest))
                    choosable << rest;
        }
        if (!thrown.isEmpty()) {
            DummyCard dummy(thrown);
            room->throwCard(&dummy, target, source);
        }
        return ContinueEffects;
    }
};

class ThGuidu : public TriggerSkillV2
{
public:
    ThGuidu() : TriggerSkillV2("thguidu") { events << EventPhaseStart; }

    static QList<ServerPlayer *> needy(Room *room)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getHandcardNum() < p->getHp())
                result << p;
        return result;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
            || needy(room).isEmpty())
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
        const int n = needy(room).size();
        player->drawCards(n, objectName());
        if (!player->isAlive() || player->isKongcheng())
            return false;
        QList<int> ids = player->handCards();
        if (ids.size() > n) {
            const Card *card = room->askForExchange(player, objectName(), n, n, false, "@thguidu", false);
            if (card && card->getSubcards().size() == n)
                ids = card->getSubcards();
            else
                ids = ids.mid(0, n);
        }
        room->showCard(player, ids);
        room->fillAG(ids);
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (ids.isEmpty())
                break;
            if (!p->isAlive() || p->getHandcardNum() >= p->getHp())
                continue;
            int id = room->askForAG(p, ids, false, objectName());
            if (!ids.contains(id))
                id = ids.first();
            ids.removeOne(id);
            room->takeAG(p, id, false);
            if (room->getCardOwner(id) == player)
                room->obtainCard(p, id);
        }
        room->clearAG();
        return false;
    }
};

// ---------------------------------------------------------------- shin029

class ThCanfeiPicker : public CardPicker
{
public:
    ThCanfeiPicker() : CardPicker("thcanfei", 1, 2, true, false) {}

    QString historyKey(const ActiveSkillRequest &) const override { return "ThCanfeiCard"; }

protected:
    bool extraFilter(const ActiveSkillRequest &request, const Card *card) const override
    {
        foreach (int id, request.selectedCardIds)
            if (Sanguosha->getCard(id)->getNumber() == card->getNumber())
                return false;
        return true;
    }
};

class ThCanfei : public TriggerSkillV2
{
public:
    ThCanfei() : TriggerSkillV2("thcanfei")
    {
        events << EventPhaseStart << PreCardUsed;
        view_as_skill = new ThCanfeiPicker;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (event == EventPhaseStart) {
            if (player->hasSkill(objectName()) && player->getPhase() == Player::Play && !player->isKongcheng())
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        // 桃, 酒 and 无中生有 target their user; one more may be added.
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || player->getMark("thcanfei_" + QString::number(use.card->getNumber()) + "-Clear") == 0
            || !(use.card->isKindOf("Peach") || use.card->isKindOf("Analeptic") || use.card->isKindOf("ExNihilo"))
            || extras(room, player, use).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == EventPhaseStart) {
            const Card *card = room->askForUseCard(player, "@@thcanfei", "@thcanfei", -1, Card::MethodNone);
            if (!card || card->getSubcards().isEmpty())
                return false;
            ctx.extra_data = ListI2V(card->getSubcards());
            return true;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *extra = room->askForPlayerChosen(player, extras(room, player, use), objectName(),
                                                       "@thyongye-add:::" + use.card->objectName(), true);
        if (!extra)
            return false;
        ctx.extra_data = extra->objectName();
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == EventPhaseStart) {
            QList<int> ids;
            foreach (int id, ListV2I(ctx.extra_data.toList()))
                if (player->handCards().contains(id))
                    ids << id;
            if (ids.isEmpty())
                return false;
            DummyCard dummy(ids);
            CardMoveReason reason(CardMoveReason::S_REASON_RECAST, player->objectName());
            reason.m_skillName = objectName();
            room->moveCardTo(&dummy, player, nullptr, Player::DiscardPile, reason);
            LogMessage log;
            log.type = "#UseCard_Recast";
            log.from = player;
            log.card_str = ListI2S(ids).join("+");
            room->sendLog(log);
            player->drawCards(ids.size(), "recast");
            foreach (int id, ids)
                room->setPlayerMark(player, "thcanfei_" + QString::number(Sanguosha->getCard(id)->getNumber()) + "-Clear", 1);
            room->setPlayerMark(player, "thcanfei_used-Clear", 1);
            return false;
        }
        ServerPlayer *extra = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!extra || !extra->isAlive())
            return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        use.to << extra;
        room->sortByActionOrder(use.to);
        LogMessage log;
        log.type = "#ThYongyeAdd";
        log.from = player;
        log.to << extra;
        log.arg = objectName();
        log.card_str = use.card->toString();
        room->sendLog(log);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }

private:
    static QList<ServerPlayer *> extras(Room *room, ServerPlayer *player, const CardUseStruct &use)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (use.to.contains(p) || room->isProhibited(player, p, use.card))
                continue;
            if (use.card->isKindOf("Peach") && !p->isWounded())
                continue;
            result << p;
        }
        return result;
    }
};

class ThCanfeiDistance : public DistanceSkill
{
public:
    ThCanfeiDistance() : DistanceSkill("#thcanfei-distance") { frequency = Compulsory; }

    int getCorrect(const Player *from, const Player *) const override
    {
        return from && from->getMark("thcanfei_used-Clear") > 0 ? -1 : 0;
    }
};

class ThCanfeiTargetMod : public TargetModSkillV2
{
public:
    ThCanfeiTargetMod() : TargetModSkillV2("#thcanfei-tar", "BasicCard,TrickCard") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::ExtraTarget || !ctx.primary || !ctx.card)
            return CorrectSkillResult::noEffect();
        const Card *card = ctx.card;
        if (card->isKindOf("DelayedTrick") || card->isKindOf("Collateral") || card->isKindOf("Peach")
            || card->isKindOf("Analeptic") || card->isKindOf("ExNihilo") || card->isKindOf("AOE")
            || card->isKindOf("GlobalEffect")
            || ctx.primary->getMark("thcanfei_" + QString::number(card->getNumber()) + "-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1);
    }
};

// ---------------------------------------------------------------- shin030

// 穿窬 names, as bits on every character's "thchuanyu" mark.
const QStringList &chuanyuNames()
{
    static const QStringList names{"jink", "analeptic", "nullification", "fire_attack"};
    return names;
}

bool chuanyuNamed(const Player *player, const QString &name)
{
    if (name == "slash")
        return true;
    const int index = chuanyuNames().indexOf(name);
    return index >= 0 && player && (player->getMark("thchuanyu") & (1 << index)) != 0;
}

class ThChuanyu : public TriggerSkillV2
{
public:
    ThChuanyu() : TriggerSkillV2("thchuanyu") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || data.value<DamageStruct>().to != player || !player->isAlive() || !player->hasSkill("thzuoyong"))
            return result;
        bool all = true;
        foreach (const QString &name, chuanyuNames())
            if (!chuanyuNamed(player, name))
                all = false;
        if (all)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()))
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
        QStringList choices;
        foreach (const QString &name, chuanyuNames())
            if (!chuanyuNamed(owner, name))
                choices << name;
        if (choices.isEmpty())
            return false;
        const QString choice = room->askForChoice(owner, objectName(), choices.join("+"));
        LogMessage log;
        log.type = "#ThChuanyu";
        log.from = owner;
        log.arg = choice;
        room->sendLog(log);
        const int index = chuanyuNames().indexOf(choice);
        room->addPlayerMark(owner, "@chuanyu" + QString::number(index + 1));
        const int bits = owner->getMark("thchuanyu") | (1 << index);
        foreach (ServerPlayer *p, room->getAllPlayers(true))
            room->setPlayerMark(p, "thchuanyu", bits);
        return false;
    }
};

class ThZuoyongViewAs : public ViewAsSkillV2
{
public:
    ThZuoyongViewAs() : ViewAsSkillV2("thzuoyong", 1) { setResponseOrUse(true); }

    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, true, true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            foreach (const QString &name, QStringList{"slash"} + chuanyuNames())
                if (allowDeclaration(self, name))
                    return true;
            return false;
        }
        return !responseName(self, request.pattern).isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->isKindOf("EquipCard");
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
        const QString name = request.reason == CardUseStruct::CARD_USE_REASON_PLAY || !request.userString.isEmpty()
            ? request.userString
            : responseName(request.initiator, request.pattern);
        if (name.isEmpty() || !chuanyuNamed(request.initiator, name == "fire_slash" || name == "thunder_slash" ? "slash" : name))
            return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = Sanguosha->cloneCard(name, material->getSuit(), material->getNumber());
        if (!card)
            return nullptr;
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

protected:
    bool allowDeclaration(const Player *player, const QString &name) const override
    {
        if (!chuanyuNamed(player, name))
            return false;
        QScopedPointer<Card> card(Sanguosha->cloneCard(name));
        return card && card->isAvailable(player);
    }

private:
    static QString responseName(const Player *player, const QString &pattern)
    {
        foreach (const QString &name, QStringList{"slash"} + chuanyuNames()) {
            if (!chuanyuNamed(player, name))
                continue;
            if (pattern.contains(name) || (name == "slash" && pattern.contains("Slash")))
                return name;
        }
        return QString();
    }
};

class ThZuoyong : public TriggerSkillV2
{
public:
    ThZuoyong() : TriggerSkillV2("thzuoyong")
    {
        events << CardsMoveOneTime;
        view_as_skill = new ThZuoyongViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !move.from_places.contains(Player::PlaceEquip) || player->hasEquip()
            || !player->isAlive() || !player->hasSkill(objectName()))
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

class ThZaoxing : public TriggerSkillV2
{
public:
    ThZaoxing() : TriggerSkillV2("thzaoxing") { events << EventPhaseEnd << EventPhaseStart << Death; }

    // The lent 作俑 lasts until the owner's next turn begins.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        const bool end = (event == EventPhaseStart && player->getPhase() == Player::RoundStart)
            || (event == Death && data.value<DeathStruct>().who == player);
        if (!end)
            return false;
        const QString key = "thzaoxing_grants_" + player->objectName();
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            if (p->getTag(key).isValid())
                room->setPlayerMark(p, "@zaoxing", 0);
            revokeTracked(room, p, key);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Draw)
            return TriggerList();
        foreach (const Card *card, player->getHandcards())
            if (card->getTypeId() == Card::TypeBasic)
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@thzaoxing", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return false;
        room->showAllCards(player);
        QList<int> ids;
        foreach (const Card *card, player->getHandcards())
            if (card->getTypeId() == Card::TypeBasic)
                ids << card->getEffectiveId();
        if (!ids.isEmpty())
            room->giveCard(player, target, ids, objectName(), true);
        room->addPlayerMark(target, "@zaoxing");
        grantTracked(room, target, "thzaoxing_grants_" + player->objectName(), "thzuoyong");
        return false;
    }
};

// ---------------------------------------------------------------- shin031

class ThGuiyuniu : public ViewAsSkillV2
{
public:
    ThGuiyuniu() : ViewAsSkillV2("thguiyuniu", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *to) const override
    {
        return selected.isEmpty() && to;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThGuiyuniuCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !target || !target->isAlive())
            return ContinueEffects;
        QStringList marked = ctx.invoker->getTag("ThGuiyuniuTargets").toStringList();
        marked << target->objectName();
        ctx.invoker->setTag("ThGuiyuniuTargets", marked);
        target->gainMark("@stable");
        return ContinueEffects;
    }
};

class ThGuiyuniuClear : public TriggerSkillV2
{
public:
    ThGuiyuniuClear() : TriggerSkillV2("#thguiyuniu") { events << EventPhaseStart << Death; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        const bool end = (event == EventPhaseStart && player->getPhase() == Player::RoundStart)
            || (event == Death && data.value<DeathStruct>().who == player);
        if (!end)
            return false;
        const QStringList marked = player->getTag("ThGuiyuniuTargets").toStringList();
        player->removeTag("ThGuiyuniuTargets");
        foreach (const QString &name, marked) {
            ServerPlayer *p = room->findPlayerByObjectName(name, true);
            if (p && p->getMark("@stable") > 0)
                p->loseMark("@stable");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThGuiyuniuDistance : public DistanceSkill
{
public:
    ThGuiyuniuDistance() : DistanceSkill("#thguiyuniu-distance") { frequency = Compulsory; }

    int getCorrect(const Player *from, const Player *to) const override
    {
        return (from ? from->getMark("@stable") : 0) - (to ? to->getMark("@stable") : 0);
    }
};

class ThHaixingViewAs : public ViewAsSkillV2
{
public:
    ThHaixingViewAs() : ViewAsSkillV2("thhaixing", 2)
    {
        expand_pile = "seafood";
        frequency = Limited;
        limit_mark = "@haixing";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return self->getMark(limit_mark) > 0;
        return request.pattern == "@@thhaixing" && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && !self->getPile("seafood").isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.initiator && card
            && request.selectedCardIds.size() < 2 && request.initiator->getPile("seafood").contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return request.selectedCardIds.isEmpty();
        if (request.selectedCardIds.isEmpty() || request.selectedCardIds.size() > 2)
            return false;
        foreach (int id, request.selectedCardIds)
            if (!request.initiator->getPile("seafood").contains(id))
                return false;
        return true;
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.reason != CardUseStruct::CARD_USE_REASON_PLAY && selected.isEmpty() && to;
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? selected.isEmpty() : selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThHaixingCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            if (ctx.initiator->getMark(limit_mark) <= 0)
                return false;
            room->removePlayerMark(ctx.initiator, limit_mark);
            room->doSuperLightbox(ctx.initiator, objectName());
        }
        return true;
    }

    // Everyone lays a card down as 鲜.
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !ctx.use_card || ctx.use_card->subcardsLength() > 0)
            return ContinueEffects;
        Room *room = source->getRoom();
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isNude() || !source->isAlive())
                continue;
            const Card *card = room->askForExchange(p, objectName(), 1, 1, true, "@thhaixing-put:" + source->objectName(), false);
            int id = card && !card->getSubcards().isEmpty() ? card->getSubcards().first() : -1;
            if (id < 0) {
                const QList<const Card *> cards = p->getCards("he");
                id = cards.at(qsanRandomBounded(cards.length()))->getEffectiveId();
            }
            source->addToPile("seafood", id);
        }
        return FinishSkill;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !ctx.use_card)
            return ContinueEffects;
        QList<int> ids;
        foreach (int id, ctx.use_card->getSubcards())
            if (source->getPile("seafood").contains(id))
                ids << id;
        if (ids.isEmpty())
            return ContinueEffects;
        DummyCard dummy(ids);
        source->getRoom()->obtainCard(target, &dummy);
        return ContinueEffects;
    }
};

class ThHaixing : public TriggerSkillV2
{
public:
    ThHaixing() : TriggerSkillV2("thhaixing")
    {
        events << EventPhaseStart;
        view_as_skill = new ThHaixingViewAs;
        frequency = Limited;
        limit_mark = "@haixing";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
            || player->getPile("seafood").isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thhaixing", "@thhaixing", -1, Card::MethodNone);
        return false;
    }
};

// ---------------------------------------------------------------- shin032

class ThZuishengViewAs : public ViewAsSkillV2
{
public:
    ThZuishengViewAs() : ViewAsSkillV2("thzuishengv", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Analeptic::IsAvailable(self);
        return request.pattern.contains("analeptic") || request.pattern.contains("Analeptic");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->isKindOf("BasicCard")
            && card->isRed();
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
        Analeptic *card = new Analeptic(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
};

class ThZuisheng : public TriggerSkillV2
{
public:
    ThZuisheng() : TriggerSkillV2("thzuisheng") { events << EventPhaseStart << EventPhaseChanging; }

    // The effects end with the affected character's turn.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || player->getMark("@zuisheng") == 0
            || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        room->setPlayerMark(player, "@zuisheng", 0);
        revokeTracked(room, player, "thzuisheng_grants");
        const QStringList blocked = player->getTag("ThZuishengBlocked").toStringList();
        player->removeTag("ThZuishengBlocked");
        foreach (const QString &skill, blocked)
            room->removeSkillInvalidity(player, skill, player->objectName(), objectName());
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play
            || !player->canDiscard(player, "h") || player->getMark("@zuisheng") > 0)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player == ctx.owner)
            return room->askForCard(player, ".|.|.|hand", "@thzuisheng", *ctx.original_data, objectName()) != nullptr;
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // A discarded hand card buys 酒 from red basics, at the price of every non-locked skill and short 杀.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player != ctx.owner && !room->askForDiscard(player, objectName(), 1, 1, true, false, "@thzuisheng"))
            return false;
        if (!player->isAlive() || player->getMark("@zuisheng") > 0)
            return false;
        room->setPlayerMark(player, "@zuisheng", 1);
        grantTracked(room, player, "thzuisheng_grants", "thzuishengv");
        QStringList blocked;
        foreach (const Skill *skill, player->getVisibleSkillList()) {
            if (skill->getFrequency() == Skill::Compulsory || skill->objectName() == "thzuishengv"
                || blocked.contains(skill->objectName()))
                continue;
            room->addSkillInvalidity(player, skill->objectName(), player->objectName(), objectName());
            blocked << skill->objectName();
        }
        player->setTag("ThZuishengBlocked", blocked);
        return false;
    }
};

class ThZuishengProhibit : public ProhibitSkill
{
public:
    ThZuishengProhibit() : ProhibitSkill("#thzuisheng") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        if (!from || !to || !card || from->getMark("@zuisheng") == 0 || !card->isKindOf("Slash"))
            return false;
        foreach (const Player *p, from->getAliveSiblings())
            if (from->distanceTo(p) < from->distanceTo(to))
                return true;
        return false;
    }
};

class ThMengsi : public TriggerSkillV2
{
public:
    ThMengsi() : TriggerSkillV2("thmengsi") { events << EventPhaseChanging << EventPhaseStart; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart && player->getMark("@mengsi") > 0) {
            room->setPlayerMark(player, "@mengsi", 0);
            revokeTracked(room, player, "thmengsi_grants");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != EventPhaseChanging || !player || !player->isAlive() || !player->isKongcheng()
            || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->getCardCount() > 1)
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const Card *card = room->askForExchange(ctx.owner, objectName(), 2, 2, true, "@thmengsi:" + player->objectName(), true);
        if (!card || card->getSubcards().size() != 2)
            return false;
        room->broadcastSkillInvoke(objectName());
        room->giveCard(ctx.owner, player, card, objectName());
        return true;
    }

    // 梦玄 and 腐生 until the receiver's next turn begins.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->isAlive() || player->getMark("@mengsi") > 0)
            return false;
        room->setPlayerMark(player, "@mengsi", 1);
        grantTracked(room, player, "thmengsi_grants", "thmengxuan");
        grantTracked(room, player, "thmengsi_grants", "ikfusheng");
        return false;
    }
};

class ThMengxuan : public TriggerSkillV2
{
public:
    ThMengxuan() : TriggerSkillV2("thmengxuan")
    {
        events << Damaged;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DamageStruct>().to != player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner->askForSkillInvoke(objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int n = ctx.original_data->value<DamageStruct>().damage;
        for (int i = 0; i < n && player->isAlive(); ++i) {
            if (i > 0 && !player->askForSkillInvoke(objectName()))
                break;
            room->broadcastSkillInvoke(objectName());
            player->drawCards(1, objectName());
        }
        return false;
    }
};

class IkFusheng : public ViewAsSkillV2
{
public:
    IkFusheng() : ViewAsSkillV2("ikfusheng", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Analeptic::IsAvailable(request.initiator);
        return request.pattern.contains("analeptic") || request.pattern.contains("Analeptic");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->getSuit() == Card::Spade;
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
        Analeptic *card = new Analeptic(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
};

class ThLinglu : public TriggerSkillV2
{
public:
    ThLinglu() : TriggerSkillV2("thlinglu") { events << CardFinished << EventPhaseChanging; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !use.card || !use.card->isKindOf("Analeptic"))
                return result;
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->hasSkill(objectName()))
                    result[owner] << objectName();
            return result;
        }
        // A round ends when the lord's next turn begins.
        if (!player->isLord() || data.value<PhaseChangeStruct>().to != Player::RoundStart)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->getMark("@spirits") > 0)
                result[owner] << objectName();
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        room->sendCompulsoryTriggerLog(owner, objectName());
        room->broadcastSkillInvoke(objectName());
        if (event == CardFinished) {
            owner->gainMark("@spirits");
            return false;
        }
        owner->drawCards((qMin(4, owner->getMark("@spirits")) + 2) / 3, objectName());
        room->setPlayerMark(owner, "@spirits", 0);
        return false;
    }
};

}

ThLuanshenCard::ThLuanshenCard() { setSkillName("thluanshen"); mute = true; }
ThLianyingCard::ThLianyingCard() { setSkillName("thlianying"); mute = true; }
ThMumiCard::ThMumiCard() { setSkillName("thmumi"); mute = true; }
ThHuanjianCard::ThHuanjianCard() { setSkillName("thhuanjian"); mute = true; }
ThShenmiCard::ThShenmiCard() { setSkillName("thshenmi"); mute = true; }
ThMuyuCard::ThMuyuCard() { setSkillName("thmuyu"); mute = true; }
ThNihuiCard::ThNihuiCard() { setSkillName("thnihui"); mute = true; }
ThKuangwuCard::ThKuangwuCard() { setSkillName("thkuangwu"); mute = true; }
ThDieyingCard::ThDieyingCard() { setSkillName("thdieying"); mute = true; }
ThBiyiCard::ThBiyiCard() { setSkillName("thbiyi"); mute = true; }
ThTunaCard::ThTunaCard() { setSkillName("thtuna"); mute = true; }
ThNingguCard::ThNingguCard() { setSkillName("thninggu"); mute = true; }
ThAiminCard::ThAiminCard() { setSkillName("thaimin"); mute = true; }
ThRenmoCard::ThRenmoCard() { setSkillName("threnmo"); mute = true; }
ThRuizhiCard::ThRuizhiCard() { setSkillName("thruizhi"); mute = true; }
ThYuguangCard::ThYuguangCard() { setSkillName("thyuguang"); mute = true; }
ThCanfeiCard::ThCanfeiCard() { setSkillName("thcanfei"); mute = true; }
ThGuiyuniuCard::ThGuiyuniuCard() { setSkillName("thguiyuniu"); mute = true; }
ThHaixingCard::ThHaixingCard() { setSkillName("thhaixing"); mute = true; }

TouhouShinPackage::TouhouShinPackage()
    : Package("touhou-shin")
{
    General *shin001 = new General(this, "shin001", "kaze");
    shin001->addSkill(new ThLuanshen);

    General *shin002 = new General(this, "shin002", "hana");
    shin002->addSkill(new ThFeiman);
    shin002->addSkill(new ThGuaiqi);

    General *shin003 = new General(this, "shin003", "yuki", 2);
    shin003->addSkill(new ThJingtao);
    shin003->addSkill(new ThZongni);

    General *shin004 = new General(this, "shin004", "tsuki");
    shin004->addSkill(new ThLanzou);

    General *shin005 = new General(this, "shin005", "kaze", 3, false);
    shin005->addSkill(new ThXinqi);
    shin005->addSkill(new ThNengwu);

    General *shin006 = new General(this, "shin006", "hana", 3, false);
    shin006->addSkill(new ThBaochui);
    shin006->addSkill(new ThBaochuiReturn);
    related_skills.insert("thbaochui", "#thbaochui");
    shin006->addSkill(new PendingSkill("thyishi"));

    General *shin007 = new General(this, "shin007", "yuki");
    shin007->addSkill(new ThMoju);
    shin007->addSkill(new ThMojuMaxCards);
    related_skills.insert("thmoju", "#thmoju");
    shin007->addSkill(new ThGuzhen);
    shin007->addSkill(new ThGuzhenLimit);
    related_skills.insert("thguzhen", "#thguzhen");

    General *shin008 = new General(this, "shin008", "tsuki");
    shin008->addSkill(new ThLianying);
    shin008->addSkill(new ThYuanxiao);
    shin008->addRelateSkill("ikchilian");
    shin008->addRelateSkill("mashu");

    General *shin009 = new General(this, "shin009", "kaze", 3);
    shin009->addSkill(new ThWuyi);
    shin009->addSkill(new ThMumi);

    General *shin010 = new General(this, "shin010", "hana", 3);
    shin010->addSkill(new ThWangyu);
    shin010->addSkill(new ThGuangshi);

    General *shin011 = new General(this, "shin011", "yuki", 4, false);
    shin011->addSkill(new ThSunwu);
    shin011->addSkill(new ThLiaogan);
    shin011->addRelateSkill("thxiagong");
    shin011->addRelateSkill("bazhen");

    General *shin012 = new General(this, "shin012", "tsuki");
    shin012->addSkill(new ThJianyue);

    General *shin013 = new General(this, "shin013", "kaze", 3);
    shin013->addSkill(new ThHuanjian);
    shin013->addSkill(new ThShenmi);

    General *shin014 = new General(this, "shin014", "hana");
    shin014->addSkill(new ThMuyu);

    General *shin015 = new General(this, "shin015", "yuki", 3);
    shin015->addSkill(new ThNihui);
    shin015->addSkill(new ThTanguan);

    General *shin016 = new General(this, "shin016", "tsuki", 3, false);
    shin016->addSkill(new ThYuancui);
    shin016->addSkill(new ThHuikuang);

    General *shin017 = new General(this, "shin017", "kaze", 3);
    shin017->addSkill(new ThXuanman);
    shin017->addSkill(new ThKuangwu);

    General *shin018 = new General(this, "shin018", "hana", 3);
    shin018->addSkill(new ThDieying);
    shin018->addSkill(new ThBiyi);

    General *shin019 = new General(this, "shin019", "yuki");
    shin019->addSkill(new ThTuna);
    shin019->addSkill(new ThNinggu);

    General *shin020 = new General(this, "shin020", "tsuki");
    shin020->addSkill(new ThQiongfaziyuan);
    shin020->addRelateSkill("thchipin");
    shin020->addRelateSkill("thaimin");

    General *shin021 = new General(this, "shin021", "kaze", 3, false);
    shin021->addSkill(new ThShenhu);
    shin021->addSkill(new ThHouhu);

    General *shin022 = new General(this, "shin022", "hana");
    shin022->addSkill(new ThGuizhou);
    shin022->addSkill(new ThRenmo);
    shin022->addSkill(new ThRenmoRange);
    shin022->addSkill(new ThRenmoTargetMod);
    related_skills.insert("threnmo", "#threnmo-range");
    related_skills.insert("threnmo", "#threnmo");

    General *shin023 = new General(this, "shin023", "yuki", 3);
    shin023->addSkill(new ThMieyi);
    shin023->addSkill(new ThShili);

    General *shin024 = new General(this, "shin024", "tsuki", 3);
    shin024->addSkill(new ThYuchi);
    shin024->addSkill(new ThSancai);

    General *shin025 = new General(this, "shin025", "kaze");
    shin025->addSkill(new ThRuizhi);

    General *shin026 = new General(this, "shin026", "hana", 5, 4);
    shin026->addSkill(new ThLingwei);

    General *shin027 = new General(this, "shin027", "yuki", 4, false);
    shin027->addSkill(new PendingSkill("thminwang"));
    shin027->addSkill(new PendingSkill("thlingbo"));

    General *shin028 = new General(this, "shin028", "tsuki", 3);
    shin028->addSkill(new ThYuguang);
    shin028->addSkill(new ThGuidu);

    General *shin029 = new General(this, "shin029", "kaze");
    shin029->addSkill(new ThCanfei);
    shin029->addSkill(new ThCanfeiDistance);
    shin029->addSkill(new ThCanfeiTargetMod);
    related_skills.insert("thcanfei", "#thcanfei-distance");
    related_skills.insert("thcanfei", "#thcanfei-tar");

    General *shin030 = new General(this, "shin030", "hana", 3);
    shin030->addSkill(new ThChuanyu);
    shin030->addSkill(new ThZuoyong);
    shin030->addSkill(new ThZaoxing);

    General *shin031 = new General(this, "shin031", "yuki", 3);
    shin031->addSkill(new ThGuiyuniu);
    shin031->addSkill(new ThGuiyuniuClear);
    shin031->addSkill(new ThGuiyuniuDistance);
    related_skills.insert("thguiyuniu", "#thguiyuniu");
    related_skills.insert("thguiyuniu", "#thguiyuniu-distance");
    shin031->addSkill(new ThHaixing);

    General *shin032 = new General(this, "shin032", "tsuki", 3);
    shin032->addSkill(new ThZuisheng);
    shin032->addSkill(new ThZuishengProhibit);
    related_skills.insert("thzuisheng", "#thzuisheng");
    shin032->addSkill(new ThMengsi);
    shin032->addSkill(new ThLinglu);
    shin032->addRelateSkill("thmengxuan");
    shin032->addRelateSkill("ikfusheng");

    skills << new ThChipin << new ThAimin << new ThZuishengViewAs << new ThMengxuan << new IkFusheng;

    addMetaObject<ThLuanshenCard>();
    addMetaObject<ThLianyingCard>();
    addMetaObject<ThMumiCard>();
    addMetaObject<ThHuanjianCard>();
    addMetaObject<ThShenmiCard>();
    addMetaObject<ThMuyuCard>();
    addMetaObject<ThNihuiCard>();
    addMetaObject<ThKuangwuCard>();
    addMetaObject<ThDieyingCard>();
    addMetaObject<ThBiyiCard>();
    addMetaObject<ThTunaCard>();
    addMetaObject<ThNingguCard>();
    addMetaObject<ThAiminCard>();
    addMetaObject<ThRenmoCard>();
    addMetaObject<ThRuizhiCard>();
    addMetaObject<ThYuguangCard>();
    addMetaObject<ThCanfeiCard>();
    addMetaObject<ThGuiyuniuCard>();
    addMetaObject<ThHaixingCard>();
}

ADD_PACKAGE(TouhouShin)
