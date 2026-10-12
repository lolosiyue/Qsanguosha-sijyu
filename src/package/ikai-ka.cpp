#include "ikai-ka.h"
#include "touhou-utils.h"
#include "ikai-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "h-strategic-advantage.h"
#include "util.h"

#include <QRandomGenerator>

using namespace TouhouUtils;
using namespace IkaiUtils;

// TouhouTripleSha's 异界·火. Skills that match a local one are reused by name.

namespace {

// ---------------------------------------------------------------- wind025

class IkZhiju : public ViewAsSkillV2
{
public:
    IkZhiju() : ViewAsSkillV2("ikzhiju") { setPhaseName("Play"); }

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
        return request.initiator && selected.isEmpty() && to && request.initiator->canDiscard(to, "ej");
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkZhijuCard"; }

    // Discard a card in play, flip someone's chain, heal; the discard phase then returns two cards.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !source->canDiscard(target, "ej"))
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = room->askForCardChosen(source, target, "ej", objectName(), false, Card::MethodDiscard);
        if (id >= 0)
            room->throwCard(id, room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : target, source);
        if (!source->isAlive())
            return ContinueEffects;
        ServerPlayer *chained = room->askForPlayerChosen(source, room->getAlivePlayers(), objectName(), "@ikzhiju-chain");
        if (chained)
            room->setPlayerChained(chained, !chained->isChained(), source);
        if (source->isAlive() && source->isWounded())
            room->recover(source, RecoverStruct(objectName(), source));
        room->setPlayerMark(source, "ikzhiju_put-Clear", 1);
        return ContinueEffects;
    }
};

class IkZhijuPut : public TriggerSkillV2
{
public:
    IkZhijuPut() : TriggerSkillV2("#ikzhiju")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Discard
            || player->getMark("ikzhiju_put-Clear") == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->setPlayerMark(player, "ikzhiju_put-Clear", 0);
        if (player->isKongcheng())
            return false;
        room->sendCompulsoryTriggerLog(player, "ikzhiju");
        QList<int> ids;
        if (const Card *chosen = room->askForExchange(player, "ikzhiju", 2, 2, false, "@ikzhiju", true)) {
            ids = chosen->getSubcards();
            delete chosen;
        }
        if (ids.isEmpty()) {
            QList<int> hand = player->handCards();
            while (ids.length() < 2 && !hand.isEmpty())
                ids << hand.takeAt(QRandomGenerator::global()->bounded(hand.length()));
        }
        DummyCard dummy(ids);
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), "ikzhiju", QString());
        room->moveCardTo(&dummy, nullptr, Player::DrawPile, reason, false);
        return false;
    }
};

class IkYingqi : public TriggerSkillV2
{
public:
    IkYingqi() : TriggerSkillV2("ikyingqi") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@ikyingqi", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        room->addPlayerMark(ctx.owner, "@arrangement");
        if (target && target->isAlive())
            room->addPlayerMark(target, "@arrangement");
        return false;
    }
};

// The next play phase (ended or skipped) of a marked character tops its hand up toward its HP.
class IkYingqiEffect : public TriggerSkillV2
{
public:
    IkYingqiEffect() : TriggerSkillV2("#ikyingqi")
    {
        events << EventPhaseEnd << EventPhaseSkipped;
        frequency = Compulsory;
        global = true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || player->getMark("@arrangement") == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        bool logged = false;
        while (player->isAlive() && player->getMark("@arrangement") > 0 && player->getHandcardNum() < player->getHp()) {
            if (!logged) {
                room->sendCompulsoryTriggerLog(player, "ikyingqi");
                logged = true;
            }
            room->removePlayerMark(player, "@arrangement");
            player->drawCards(1, "ikyingqi");
        }
        room->setPlayerMark(player, "@arrangement", 0);
        return false;
    }
};

// ---------------------------------------------------------------- wind033

class IkJilun : public ViewAsSkillV2
{
public:
    IkJilun() : ViewAsSkillV2("ikjilun", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isBlack()
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
        return request.initiator && selected.isEmpty() && to && request.initiator->canDiscard(to, "e");
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkJilunCard"; }

    // Strip an equip, then 杀 the owner; a harmless 杀 hands the equip back.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || !source->canDiscard(target, "e"))
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = room->askForCardChosen(source, target, "e", objectName(), false, Card::MethodDiscard);
        if (id < 0)
            return ContinueEffects;
        room->throwCard(id, target, source);
        room->setPlayerFlag(target, "ikjilun_miss");
        useSkillSlash(room, ctx, source, target, objectName());
        if (target->hasFlag("ikjilun_miss")) {
            room->setPlayerFlag(target, "-ikjilun_miss");
            if (target->isAlive() && room->getCardPlace(id) == Player::DiscardPile) {
                room->sendCompulsoryTriggerLog(source, objectName());
                room->obtainCard(target, id);
            }
        }
        return ContinueEffects;
    }
};

class IkJilunRecord : public TriggerSkillV2
{
public:
    IkJilunRecord() : TriggerSkillV2("#ikjilun")
    {
        events << PreDamageDone;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.to && damage.card && damage.card->isKindOf("Slash") && damage.card->getSkillName() == "ikjilun"
            && damage.to->hasFlag("ikjilun_miss"))
            room->setPlayerFlag(damage.to, "-ikjilun_miss");
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- wind034

class IkJiqiao : public TriggerSkillV2
{
public:
    IkJiqiao() : TriggerSkillV2("ikjiqiao") { events << EventPhaseEnd; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Draw || !player->hasSkill(objectName())
            || !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *card = room->askForCard(ctx.owner, "..", "@ikjiqiao", QVariant(), objectName());
        if (!card)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = static_cast<int>(card->getTypeId());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player->isAlive())
            return false;
        switch (static_cast<Card::CardType>(ctx.extra_data.toInt())) {
        case Card::TypeBasic: {
            if (!Slash::IsAvailable(player))
                break;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (player->canSlash(p, false))
                    targets << p;
            if (targets.isEmpty())
                break;
            ServerPlayer *victim = room->askForPlayerChosen(player, targets, objectName(), "@ikjiqiao-basic");
            useSkillSlash(room, ctx, player, victim, objectName(), true);
            break;
        }
        case Card::TypeEquip: {
            ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
                                                            "@ikjiqiao-equip");
            if (target)
                target->drawCards(2, objectName());
            break;
        }
        case Card::TypeTrick: {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (p->isWounded())
                    targets << p;
            if (targets.isEmpty())
                break;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@ikjiqiao-trick");
            if (target)
                room->recover(target, RecoverStruct(objectName(), player));
            break;
        }
        default:
            break;
        }
        return false;
    }
};

// ---------------------------------------------------------------- wind035

class IkKangjin : public ViewAsSkillV2
{
public:
    IkKangjin() : ViewAsSkillV2("ikkangjin", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return false;
        Duel duel(Card::SuitToBeDecided, -1);
        duel.setSkillName(objectName());
        return duel.isAvailable(request.initiator);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !card || card->getTypeId() != Card::TypeBasic || card->hasFlag("using"))
            return false;
        const int id = card->getEffectiveId();
        return request.initiator->handCards().contains(id) || request.initiator->getHandPile().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Duel *duel = new Duel(Card::SuitToBeDecided, -1);
        duel->addSubcards(request.selectedCardIds);
        duel->setSkillName(objectName());
        return duel;
    }
};

// 亢劲's 决斗 may only aim at characters with more HP.
class IkKangjinProhibit : public ProhibitSkill
{
public:
    IkKangjinProhibit() : ProhibitSkill("#ikkangjin") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return from && to && card && card->isKindOf("Duel") && card->getSkillName() == "ikkangjin"
            && to->getHp() <= from->getHp();
    }
};

class IkYunjue : public TriggerSkillV2
{
public:
    IkYunjue() : TriggerSkillV2("ikyunjue")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // Show the hand until it holds a 杀 or 桃, drawing two each time it does not.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        for (int guard = 0; guard < 40 && player->isAlive(); ++guard) {
            if (!player->isKongcheng())
                room->showAllCards(player);
            bool found = false;
            foreach (const Card *card, player->getHandcards())
                if (card->isKindOf("Slash") || card->isKindOf("Peach")) {
                    found = true;
                    break;
                }
            if (found) {
                room->loseHp(player, 1, true, player, objectName());
                break;
            }
            player->drawCards(2, objectName());
        }
        return false;
    }
};

// ---------------------------------------------------------------- wind036

class IkHunkao : public ViewAsSkillV2
{
public:
    IkHunkao() : ViewAsSkillV2("ikhunkao") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!ownsHandCard(self, card) || !self->canDiscard(self, card->getEffectiveId()))
            return false;
        return request.selectedCardIds.isEmpty()
            || Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == card->getSuit();
    }

    // Every hand card of the chosen suit.
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty() || !selectionValid(this, request))
            return false;
        const Card::Suit suit = Sanguosha->getCard(request.selectedCardIds.first())->getSuit();
        int count = 0;
        foreach (const Card *card, request.initiator->getHandcards())
            if (card->getSuit() == suit)
                ++count;
        return count == request.selectedCardIds.size();
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && to && to != request.initiator && selected.length() < 2
            && selected.length() < request.selectedCardIds.size() && request.initiator->inMyAttackRange(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty() && selected.length() <= qMin(2, request.selectedCardIds.size());
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkHunkaoCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (ctx.invoker && !ctx.invoker->isKongcheng())
            room->showAllCards(ctx.invoker);
        return ViewAsSkillV2::pay(room, ctx, request);
    }

    // Each target hands over a card of the suit or takes a 杀.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || !ctx.use_card
            || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        Room *room = source->getRoom();
        const QString suit = Sanguosha->getCard(ctx.use_card->getSubcards().first())->getSuitString();
        const Card *card = nullptr;
        if (!target->isNude())
            card = room->askForCard(target, ".|" + suit, QString("@ikhunkao-give:%1::%2").arg(source->objectName(), suit),
                                    QVariant::fromValue(source), Card::MethodNone);
        if (card) {
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), source->objectName(), objectName(),
                                  QString());
            room->obtainCard(source, card, reason);
        } else {
            useSkillSlash(room, ctx, source, target, objectName());
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- wind045

// Cards go on top at a turn's start; the turn's discard phase pays the owner back.
class IkHualan : public TriggerSkillV2
{
public:
    IkHualan() : TriggerSkillV2("ikhualan") { events << EventPhaseStart << EventPhaseEnd << CardsMoveOneTime; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime)
            return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || player->getPhase() != Player::Discard
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
            return false;
        int n = 0;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceHand || move.from_places.value(i) == Player::PlaceEquip)
                ++n;
        if (n > 0)
            room->addPlayerMark(player, "ikhualan_discarded-Clear", n);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive())
            return result;
        if (event == EventPhaseStart && player->getPhase() == Player::RoundStart) {
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->hasSkill(objectName()) && !owner->isKongcheng())
                    result[owner] << objectName();
        } else if (event == EventPhaseEnd && player->getPhase() == Player::Discard) {
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                for (int i = 0; i < owner->getMark("ikhualan-Clear"); ++i)
                    result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd)
            return true;
        const Card *chosen = room->askForExchange(ctx.owner, objectName(), 3, 1, false, "@ikhualan", true);
        if (!chosen)
            return false;
        ctx.extra_data = ListI2V(chosen->getSubcards());
        delete chosen;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == EventPhaseStart) {
            DummyCard dummy(ListV2I(ctx.extra_data.toList()));
            CardMoveReason reason(CardMoveReason::S_REASON_PUT, owner->objectName(), objectName(), QString());
            room->moveCardTo(&dummy, nullptr, Player::DrawPile, reason, false);
            room->addPlayerMark(owner, "ikhualan-Clear");
            return false;
        }
        room->sendCompulsoryTriggerLog(owner, objectName());
        const int n = player->getMark("ikhualan_discarded-Clear");
        if (n > 0) {
            owner->drawCards(n, objectName());
            return false;
        }
        QStringList choices{"draw"};
        Slash probe(Card::NoSuit, 0);
        probe.setSkillName("_" + objectName());
        if (player != owner && player->isAlive() && owner->canSlash(player, &probe, false)
            && !owner->isCardLimited(&probe, Card::MethodUse))
            choices << "slash";
        if (room->askForChoice(owner, objectName(), choices.join("+"), QVariant::fromValue(player)) == "slash")
            useSkillSlash(room, ctx, owner, player, objectName());
        else
            owner->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- wind047

class IkTianhua : public TriggerSkillV2
{
public:
    IkTianhua() : TriggerSkillV2("iktianhua")
    {
        events << CardsMoveOneTime << EventPhaseChanging;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || isOwnTurn(player) || !move.from_places.contains(Player::PlaceHand)
                || player->getHandcardNum() > 1)
                return TriggerList();
        } else {
            const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (!(change.to == Player::NotActive && player->getHandcardNum() <= 1) && change.to != Player::Draw
                && change.to != Player::Discard)
                return TriggerList();
        }
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        if (event == EventPhaseChanging) {
            const Player::Phase to = ctx.original_data->value<PhaseChangeStruct>().to;
            if (to == Player::Draw || to == Player::Discard) {
                player->skip(to);
                return false;
            }
        }
        room->broadcastSkillInvoke(objectName());
        QList<int> ids = room->getNCards(3, false);
        QList<int> taken;
        room->fillAG(ids, player);
        while (!ids.isEmpty()) {
            const int id = room->askForAG(player, ids, !taken.isEmpty(), objectName());
            if (id == -1)
                break;
            room->takeAG(player, id, false, QList<ServerPlayer *>() << player);
            ids.removeOne(id);
            taken << id;
        }
        room->clearAG(player);
        if (!taken.isEmpty()) {
            DummyCard dummy(taken);
            room->obtainCard(player, &dummy, false);
        }
        if (!ids.isEmpty())
            room->askForGuanxing(player, ids, Room::GuanxingUpOnly);
        return false;
    }
};

class IkHuangshiViewAs : public ViewAsSkillV2
{
public:
    IkHuangshiViewAs() : ViewAsSkillV2("ikhuangshi") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@ikhuangshi"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.size() < 2 && ownsHandCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && request.selectedCardIds.size() <= 2 && selectionValid(this, request);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator
            && !request.initiator->inMyAttackRange(to) && to->canDiscard(to, "he");
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkHuangshiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.use_card)
            return ContinueEffects;
        const int n = ctx.use_card->subcardsLength();
        if (n > 0 && target->canDiscard(target, "he"))
            target->getRoom()->askForDiscard(target, objectName(), n, n, false, true);
        return ContinueEffects;
    }
};

class IkHuangshi : public TriggerSkillV2
{
public:
    IkHuangshi() : TriggerSkillV2("ikhuangshi")
    {
        events << EventPhaseStart << Damaged;
        view_as_skill = new IkHuangshiViewAs;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->canDiscard(player, "h"))
            return TriggerList();
        if (event == EventPhaseStart)
            return player->getPhase() == Player::Play ? TriggerList{{player, {objectName()}}} : TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.to != player)
            return TriggerList();
        QStringList times;
        for (int i = 0; i < damage.damage; ++i)
            times << objectName();
        return TriggerList{{player, times}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@ikhuangshi", "@ikhuangshi", -1, Card::MethodDiscard);
        return false;
    }
};

// ---------------------------------------------------------------- wind048

class IkXizi : public TriggerSkillV2
{
public:
    IkXizi() : TriggerSkillV2("ikxizi") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName())
            || player->isKongcheng())
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

    // Discard every hand card of one colour, then draw two.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->showAllCards(player);
        QStringList choices;
        foreach (const Card *card, player->getHandcards()) {
            const QString colour = card->isBlack() ? "black" : (card->isRed() ? "red" : QString());
            if (!colour.isEmpty() && !choices.contains(colour))
                choices << colour;
        }
        if (choices.isEmpty())
            return false;
        const QString choice = room->askForChoice(player, objectName(), choices.join("+"));
        QList<int> ids;
        foreach (const Card *card, player->getHandcards())
            if (((choice == "black" && card->isBlack()) || (choice == "red" && card->isRed()))
                && player->canDiscard(player, card->getEffectiveId()))
                ids << card->getEffectiveId();
        if (ids.isEmpty())
            return false;
        room->throwCard(ids, objectName(), player);
        if (player->isAlive())
            player->drawCards(2, objectName());
        return false;
    }
};

class IkCiyuan : public TriggerSkillV2
{
public:
    IkCiyuan() : TriggerSkillV2("ikciyuan")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive())
            return result;
        if (player->getPhase() == Player::Finish && player->hasSkill(objectName()) && player->getHandcardNum() < 2)
            result[player] << objectName();
        else if (player->getPhase() == Player::Start)
            foreach (ServerPlayer *owner, room->getOtherPlayers(player))
                if (owner->hasSkill(objectName()) && owner->isKongcheng())
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

    bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player == ctx.owner && player->getPhase() == Player::Finish) {
            if (ctx.owner->getHandcardNum() < 2)
                ctx.owner->drawCards(2 - ctx.owner->getHandcardNum(), objectName());
        } else {
            ctx.owner->drawCards(1, objectName());
        }
        return false;
    }
};

// ---------------------------------------------------------------- wind050

class IkDongzhao : public ViewAsSkillV2
{
public:
    IkDongzhao() : ViewAsSkillV2("ikdongzhao") { setPhaseName("Play"); }

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
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkDongzhaoCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target)
            return ContinueEffects;
        Room *room = source->getRoom();
        room->loseHp(source, 1, true, source, objectName());
        if (!source->isAlive() || !target->isAlive())
            return ContinueEffects;
        source->setTag("IkDongzhaoTarget", target->objectName());
        room->setPlayerMark(source, "ikdongzhao_extra-PlayClear", 1);
        room->setPlayerMark(target, "ikdongzhao_void-PlayClear", 1);
        return ContinueEffects;
    }
};

// The marked character's next card this phase is void; the user's next basic or ordinary
// trick also aims at that character.
class IkDongzhaoEffect : public TriggerSkillV2
{
public:
    IkDongzhaoEffect() : TriggerSkillV2("#ikdongzhao")
    {
        events << PreCardUsed;
        frequency = Compulsory;
        global = true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill
            || (player->getMark("ikdongzhao_void-PlayClear") == 0 && player->getMark("ikdongzhao_extra-PlayClear") == 0))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *current = room->getCurrent();
        bool changed = false;
        if (player->getMark("ikdongzhao_extra-PlayClear") > 0
            && (use.card->getTypeId() == Card::TypeBasic || use.card->isNDTrick())) {
            room->setPlayerMark(player, "ikdongzhao_extra-PlayClear", 0);
            ServerPlayer *extra = room->findPlayerByObjectName(player->getTag("IkDongzhaoTarget").toString());
            player->removeTag("IkDongzhaoTarget");
            if (extra && extra->isAlive() && !use.to.contains(extra) && !use.card->isKindOf("Jink")
                && !use.card->isKindOf("Nullification") && !use.card->isKindOf("Collateral")) {
                room->sendCompulsoryTriggerLog(player, "ikdongzhao");
                use.to << extra;
                room->sortByActionOrder(use.to);
                changed = true;
            }
        }
        if (player->getMark("ikdongzhao_void-PlayClear") > 0) {
            room->setPlayerMark(player, "ikdongzhao_void-PlayClear", 0);
            if (current)
                room->sendCompulsoryTriggerLog(current, "ikdongzhao");
            if (!use.nullified_list.contains("_ALL_TARGETS"))
                use.nullified_list << "_ALL_TARGETS";
            changed = true;
        }
        if (changed)
            *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

// ---------------------------------------------------------------- wind051

class IkElu : public TriggerSkillV2
{
public:
    IkElu() : TriggerSkillV2("ikelu") { events << EventPhaseStart << PreDamageDone << EventPhaseChanging; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == PreDamageDone) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.to && damage.from->hasFlag("ikelu_using") && damage.card
                && damage.card->isKindOf("Slash"))
                room->setPlayerFlag(damage.to, "ikelu_hit");
        } else if (event == EventPhaseChanging && player
                   && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QStringList owners = player->getTag("IkEluRange").toStringList();
            player->removeTag("IkEluRange");
            foreach (const QString &name, owners)
                if (ServerPlayer *owner = room->findPlayerByObjectName(name, true))
                    room->removeAttackRangePair(player, owner);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->canSlash(player, false))
                result[owner] << objectName();
        return result;
    }

    // The 杀 is the cost; whether it hurt decides what follows.
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        room->setPlayerFlag(owner, "ikelu_using");
        room->setPlayerFlag(player, "-ikelu_hit");
        const Card *slash = room->askForUseSlashTo(owner, player, "@ikelu:" + player->objectName(), false);
        room->setPlayerFlag(owner, "-ikelu_using");
        if (!slash)
            return false;
        ctx.extra_data = player->hasFlag("ikelu_hit");
        room->setPlayerFlag(player, "-ikelu_hit");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->isAlive())
            return false;
        if (ctx.extra_data.toBool()) {
            room->setPlayerCardLimitation(player, "use", "Slash,Duel", true, objectName());
        } else if (ctx.owner->isAlive()) {
            room->insertAttackRangePair(player, ctx.owner);
            QStringList owners = player->getTag("IkEluRange").toStringList();
            owners << ctx.owner->objectName();
            player->setTag("IkEluRange", owners);
        }
        return false;
    }
};

// ---------------------------------------------------------------- wind057

class IkYinchou : public TriggerSkillV2
{
public:
    IkYinchou() : TriggerSkillV2("ikyinchou") { events << CardFinished; }

    static bool distinctNames(const Player *player)
    {
        QStringList names;
        foreach (const Card *card, player->getHandcards()) {
            const QString name = card->isKindOf("Slash") ? QStringLiteral("slash") : card->objectName();
            if (names.contains(name))
                return false;
            names << name;
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
            || use.card->getTypeId() != Card::TypeBasic || player->isKongcheng()
            || player->getMark("ikyinchou-Clear") >= 3 || !distinctNames(player))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->addPlayerMark(player, "ikyinchou-Clear");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->isKongcheng())
            room->showAllCards(player);
        player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- bloom032

class IkFengxing : public TriggerSkillV2
{
public:
    IkFengxing() : TriggerSkillV2("ikfengxing") { events << DrawNCards << AfterDrawNCards; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        const DrawStruct draw = data.value<DrawStruct>();
        if (draw.who != player || draw.reason != "draw_phase")
            return TriggerList();
        if (event == DrawNCards) {
            if (!player->hasSkill(objectName()) || draw.num <= 0)
                return TriggerList();
        } else if (!player->hasFlag("ikfengxing_show")) {
            return TriggerList();
        }
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (event == AfterDrawNCards) {
            room->setPlayerFlag(player, "-ikfengxing_show");
            return player->askForSkillInvoke(objectName(), "show");
        }
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Draw one fewer; afterwards show the hand until a 杀 turns up, drawing one each time.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num = qMax(0, draw.num - 1);
            *ctx.original_data = QVariant::fromValue(draw);
            room->setPlayerFlag(player, "ikfengxing_show");
            return false;
        }
        for (int guard = 0; guard < 40 && player->isAlive(); ++guard) {
            room->addPlayerMark(player, "ikfengxing-Clear");
            if (!player->isKongcheng())
                room->showAllCards(player);
            bool slash = false;
            foreach (const Card *card, player->getHandcards())
                if (card->isKindOf("Slash")) {
                    slash = true;
                    break;
                }
            if (slash)
                break;
            player->drawCards(1, objectName());
            if (!player->askForSkillInvoke(objectName(), "show"))
                break;
        }
        return false;
    }
};

class IkFengxingDistance : public DistanceSkill
{
public:
    IkFengxingDistance() : DistanceSkill("#ikfengxing") {}

    int getCorrect(const Player *from, const Player *) const override
    {
        return from ? qMax(from->getMark("ikfengxing-Clear") - 2, 0) : 0;
    }
};

// ---------------------------------------------------------------- bloom034

class IkQizhong : public TriggerSkillV2
{
public:
    IkQizhong() : TriggerSkillV2("ikqizhong")
    {
        events << PreCardUsed << CardUsed << EventPhaseChanging;
        frequency = Frequent;
    }

    // Colour of the last card used this play phase, kept as colour + 1.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if (event == EventPhaseChanging) {
            if (player->getMark("ikqizhong_colour") > 0)
                room->setPlayerMark(player, "ikqizhong_colour", 0);
            return false;
        }
        if (event != PreCardUsed || player->getPhase() != Player::Play)
            return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill)
            return false;
        const int last = player->getMark("ikqizhong_colour");
        if (last > 0 && last - 1 != static_cast<int>(use.card->getColor()))
            room->setCardFlag(use.card, "ikqizhong_invoke");
        room->setPlayerMark(player, "ikqizhong_colour", static_cast<int>(use.card->getColor()) + 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed || !player || !player->isAlive() || player->getPhase() != Player::Play
            || !player->hasSkill(objectName()))
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || !use.card->hasFlag("ikqizhong_invoke"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->setCardFlag(ctx.original_data->value<CardUseStruct>().card, "-ikqizhong_invoke");
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Reveal the top card: a different colour is kept, a matching one may be swapped for a hand card.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const Card *used = ctx.original_data->value<CardUseStruct>().card;
        const QList<int> ids = room->getNCards(1, false);
        if (ids.isEmpty())
            return false;
        const int id = ids.first();
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(id, nullptr, Player::PlaceTable, turnover), true);
        const Card *card = Sanguosha->getCard(id);
        if (!used || card->getColor() != used->getColor()) {
            room->obtainCard(player, card,
                             CardMoveReason(CardMoveReason::S_REASON_DRAW, player->objectName(), objectName(), QString()));
            return false;
        }
        const Card *swap = nullptr;
        if (!player->isKongcheng())
            swap = room->askForCard(player, ".|.|.|hand", "@ikqizhong-exchange:::" + card->objectName(),
                                    QVariant::fromValue(card), Card::MethodNone);
        if (swap && room->getCardPlace(id) == Player::PlaceTable) {
            QList<CardsMoveStruct> moves;
            moves << CardsMoveStruct(swap->getEffectiveId(), player, nullptr, Player::PlaceUnknown, Player::DrawPile,
                                     CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(),
                                                    QString()));
            moves << CardsMoveStruct(id, nullptr, player, Player::PlaceTable, Player::PlaceHand,
                                     CardMoveReason(CardMoveReason::S_REASON_DRAW, player->objectName(), objectName(),
                                                    QString()));
            room->moveCardsAtomic(moves, false);
        } else if (room->getCardPlace(id) == Player::PlaceTable) {
            room->throwCard(card, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(),
                                                 QString()),
                            nullptr);
        }
        return false;
    }
};

// ---------------------------------------------------------------- bloom035

class IkDuduan : public TriggerSkillV2
{
public:
    IkDuduan() : TriggerSkillV2("ikduduan") { events << EventPhaseStart << PreDamageDone; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != PreDamageDone)
            return false;
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (from && from->getPhase() == Player::Play && from->getMark("ikduduan-Clear") > 0)
            room->setPlayerMark(from, "ikduduan-Clear", 0);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Start
            || !player->hasSkill(objectName()))
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
        QList<ServerPlayer *> targets;
        if (player->canDiscard(player, "he"))
            targets << player;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->inMyAttackRange(p) && player->canDiscard(p, "he"))
                targets << p;
        ServerPlayer *target = targets.isEmpty()
            ? nullptr
            : room->askForPlayerChosen(player, targets, objectName(), "@ikduduan", true);
        if (target) {
            const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, target, target == player ? nullptr : player);
        } else {
            player->drawCards(1, objectName());
        }
        room->setPlayerMark(player, "ikduduan-Clear", 1);
        return false;
    }
};

class IkDuduanMaxCards : public MaxCardsSkillV2
{
public:
    IkDuduanMaxCards() : MaxCardsSkillV2("#ikduduan") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("ikduduan-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(-2);
    }
};

// ---------------------------------------------------------------- bloom036

class IkJimu : public ViewAsSkillV2
{
public:
    IkJimu() : ViewAsSkillV2("ikjimu")
    {
        frequency = Limited;
        limit_mark = "@jimu";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkJimuCard"; }

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
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        target->gainMark("@speed");
        source->getRoom()->setFixedDistance(source, target, 1);
        return ContinueEffects;
    }
};

class IkJimuTrigger : public TriggerSkillV2
{
public:
    IkJimuTrigger() : TriggerSkillV2("#ikjimu") { events << CardFinished << Death; }

    static ServerPlayer *speedTarget(Room *room, ServerPlayer *except)
    {
        foreach (ServerPlayer *p, room->getOtherPlayers(except))
            if (p->getMark("@speed") > 0)
                return p;
        return nullptr;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !player->isAlive() || !player->hasSkill("ikjimu") || !use.card
                || !use.card->isKindOf("Slash"))
                return result;
            foreach (ServerPlayer *to, use.to)
                if (to->getMark("@speed") > 0) {
                    result[player] << objectName();
                    break;
                }
        } else if (data.value<DeathStruct>().who == player && player->getMark("@speed") > 0) {
            foreach (ServerPlayer *owner, room->getOtherPlayers(player))
                if (owner->hasSkill("ikjimu"))
                    result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), "ikjimu",
                                                        event == Death ? "@ikjimu-move" : "@ikjimu", event != Death, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke("ikjimu");
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return false;
        if (event == Death) {
            target->gainMark("@speed");
            room->setFixedDistance(owner, target, 1);
            return false;
        }
        ServerPlayer *victim = speedTarget(room, owner);
        bool used = false;
        if (victim && victim != target && target->canSlash(victim, false))
            used = room->askForUseSlashTo(target, victim,
                                          QString("@ikjimu-slash:%1:%2").arg(owner->objectName(), victim->objectName()),
                                          false);
        if (!used && owner->isAlive())
            owner->drawCards(1, "ikjimu");
        return false;
    }
};

// ---------------------------------------------------------------- shared

// Numbers each card a character uses in its play phase (card tag "ikai_playcount").
class IkPlayCount : public TriggerSkillV2
{
public:
    IkPlayCount() : TriggerSkillV2("#ikai-playcount")
    {
        events << PreCardUsed;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || player->getPhase() != Player::Play || !use.card
            || use.card->getTypeId() == Card::TypeSkill)
            return false;
        room->addPlayerMark(player, "ikai_playcount-PlayClear");
        use.card->setTag("ikai_playcount", player->getMark("ikai_playcount-PlayClear"));
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

int playCount(const CardUseStruct &use)
{
    return use.card ? use.card->tag.value("ikai_playcount").toInt() : 0;
}

// ---------------------------------------------------------------- bloom045

class IkDengpo : public ViewAsSkillV2
{
public:
    IkDengpo() : ViewAsSkillV2("ikdengpo") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.size() < 4 && ownsCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && request.selectedCardIds.size() <= 4 && selectionValid(this, request);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator
            && to->getCardCount() >= request.selectedCardIds.size();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkDengpoCard"; }

    // Whether the payment empties the equip area is decided before the cards move.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ServerPlayer *source = ctx.invoker;
        bool lastEquip = source && source->hasEquip();
        if (lastEquip)
            foreach (int id, source->getEquipsId())
                if (!request.selectedCardIds.contains(id))
                    lastEquip = false;
        if (!ViewAsSkillV2::pay(room, ctx, request))
            return false;
        if (source)
            room->setPlayerFlag(source, lastEquip ? "ikdengpo_reach" : "-ikdengpo_reach");
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !ctx.use_card)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int x = ctx.use_card->subcardsLength();
        if (target->isAlive() && target->canDiscard(target, "he"))
            room->askForDiscard(target, objectName(), x, x, false, true);
        source->setTag("IkDengpoTarget", target->objectName());
        room->setPlayerMark(source, "ikdengpo_draw", x);
        if (source->hasFlag("ikdengpo_reach")) {
            room->setPlayerFlag(source, "-ikdengpo_reach");
            if (source->isAlive() && target->isAlive()) {
                room->setFixedDistance(source, target, 1);
                QStringList reached = source->getTag("IkDengpoReach").toStringList();
                reached << target->objectName();
                source->setTag("IkDengpoReach", reached);
            }
        }
        return ContinueEffects;
    }
};

class IkDengpoDraw : public TriggerSkillV2
{
public:
    IkDengpoDraw() : TriggerSkillV2("#ikdengpo")
    {
        events << EventPhaseEnd << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        const QStringList reached = player->getTag("IkDengpoReach").toStringList();
        player->removeTag("IkDengpoReach");
        foreach (const QString &name, reached)
            if (ServerPlayer *target = room->findPlayerByObjectName(name, true))
                room->removeFixedDistance(player, target, 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || player->getPhase() != Player::Play || player->getMark("ikdengpo_draw") == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        const int x = player->getMark("ikdengpo_draw");
        room->setPlayerMark(player, "ikdengpo_draw", 0);
        ServerPlayer *target = room->findPlayerByObjectName(player->getTag("IkDengpoTarget").toString());
        player->removeTag("IkDengpoTarget");
        QList<ServerPlayer *> drawers;
        if (player->isAlive())
            drawers << player;
        if (target && target->isAlive() && target != player)
            drawers << target;
        room->sortByActionOrder(drawers);
        foreach (ServerPlayer *p, drawers)
            p->drawCards(x, "ikdengpo");
        return false;
    }
};

// ---------------------------------------------------------------- bloom047

class IkGuoshang : public TriggerSkillV2
{
public:
    IkGuoshang() : TriggerSkillV2("ikguoshang")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (!player || data.value<DamageStruct>().to != player || player->getMark("drank") == 0 || !current
            || current == player || !current->isAlive() || !isOwnTurn(current) || !current->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{current, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        room->setPlayerMark(player, "drank", 0);
        if (!player->isKongcheng()) {
            const int id = room->askForCardChosen(ctx.owner, player, "h", objectName());
            if (id >= 0)
                room->obtainCard(ctx.owner, id, false);
        }
        return false;
    }
};

class IkZuiyan : public TriggerSkillV2
{
public:
    IkZuiyan() : TriggerSkillV2("ikzuiyan") { events << EventPhaseStart; markOwnerOnly(this); }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || (player->getPhase() != Player::Play && player->getPhase() != Player::Finish))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
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

    // Everyone drinks at the play phase; at the end phase every drinker may slash.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->getPhase() == Player::Play) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                auto *anal = new Analeptic(Card::NoSuit, 0);
                anal->setSkillName("_" + objectName());
                if (p->isProhibited(p, anal) || p->isCardLimited(anal, Card::MethodUse)) {
                    delete anal;
                    continue;
                }
                CardUseStruct use(anal, p, p);
                use.setOwnedCard(anal);
                room->useCardFromSkillEffect(use, ctx, true);
            }
            return false;
        }
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!p->isAlive() || p->getMark("drank") == 0)
                continue;
            room->askForUseCard(p, "slash", "@ikzuiyan-slash:" + player->objectName(), -1, Card::MethodUse, false);
        }
        return false;
    }
};

class IkQihunViewAs : public ViewAsSkillV2
{
public:
    IkQihunViewAs() : ViewAsSkillV2("ikqihunv") { attached_lord_skill = true; }

    static const Player *activeOwner(const Player *self)
    {
        foreach (const Player *p, self->getAliveSiblings())
            if (p->hasSkill("ikqihun") && p->getPhase() != Player::NotActive)
                return p;
        return nullptr;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        return self && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern.contains("slash", Qt::CaseInsensitive) && !self->isKongcheng()
            && self->getMark("ikqihun_failed-Clear") == 0 && activeOwner(self);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        return slash;
    }

    // The current owner may take the whole hand; otherwise the 杀 does not happen.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        ServerPlayer *self = ctx.invoker;
        ServerPlayer *owner = room->getCurrent();
        if (!self || self->isKongcheng() || !owner || owner == self || !owner->isAlive() || !owner->hasSkill("ikqihun"))
            return false;
        if (!owner->askForSkillInvoke("ikqihun", QVariant::fromValue(self))) {
            room->setPlayerMark(self, "ikqihun_failed-Clear", 1);
            return false;
        }
        room->broadcastSkillInvoke("ikqihun");
        room->notifySkillInvoked(owner, "ikqihun");
        DummyCard hand(self->handCards());
        room->obtainCard(owner, &hand, false);
        return true;
    }
};

class IkQihun : public TriggerSkillV2
{
public:
    IkQihun() : TriggerSkillV2("ikqihun")
    {
        events << GameStart << EventAcquireSkill << EventLoseSkill << Death << GeneralShown << GeneralHidden;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        syncAttachedChildren(room, objectName(), "ikqihunv", false);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- bloom048

class IkDiebei : public TriggerSkillV2
{
public:
    IkDiebei() : TriggerSkillV2("ikdiebei")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        if (player->getHandcardNum() != player->getHp()) {
            player->drawCards(2, objectName());
        } else {
            room->loseHp(player, 1, true, player, objectName());
            room->setPlayerMark(player, "ikdiebei-Clear", 1);
        }
        return false;
    }
};

class IkDiebeiMaxCards : public MaxCardsSkillV2
{
public:
    IkDiebeiMaxCards() : MaxCardsSkillV2("#ikdiebei") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("ikdiebei-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(1);
    }
};

// ---------------------------------------------------------------- bloom049

class IkXunfeng : public TriggerSkillV2
{
public:
    IkXunfeng() : TriggerSkillV2("ikxunfeng")
    {
        events << CardFinished;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || player->getPhase() != Player::Play
            || !player->hasSkill(objectName()))
            return TriggerList();
        const int n = playCount(use);
        if (n == 1 || (n == 2 && player->getMark("ikxunfeng_used-PlayClear") > 0))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (playCount(ctx.original_data->value<CardUseStruct>()) == 2) {
            room->sendCompulsoryTriggerLog(player, objectName());
            return true;
        }
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (playCount(ctx.original_data->value<CardUseStruct>()) == 2) {
            room->setPlayerMark(player, "ikxunfeng_used-PlayClear", 0);
            if (player->canDiscard(player, "he"))
                room->askForDiscard(player, objectName(), 1, 1, false, true);
        } else {
            player->drawCards(1, objectName());
            room->setPlayerMark(player, "ikxunfeng_used-PlayClear", 1);
        }
        return false;
    }
};

class IkLuhua : public TriggerSkillV2
{
public:
    IkLuhua() : TriggerSkillV2("ikluhua") { events << CardFinished; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || player->getPhase() != Player::Play)
            return result;
        const int n = playCount(use);
        if (n != 3 && n != 4)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && (n == 3 || owner->getMark("ikluhua_used-PlayClear") > 0))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (playCount(ctx.original_data->value<CardUseStruct>()) == 4) {
            room->sendCompulsoryTriggerLog(ctx.owner, objectName());
            return true;
        }
        if (!player->isAlive() || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (playCount(ctx.original_data->value<CardUseStruct>()) == 4) {
            room->setPlayerMark(ctx.owner, "ikluhua_used-PlayClear", 0);
            ctx.owner->drawCards(1, objectName());
        } else {
            player->drawCards(1, objectName());
            room->setPlayerMark(ctx.owner, "ikluhua_used-PlayClear", 1);
        }
        return false;
    }
};

// ---------------------------------------------------------------- bloom052

class IkLingcha : public ViewAsSkillV2
{
public:
    IkLingcha() : ViewAsSkillV2("iklingcha")
    {
        setResponseOrUse(true);
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, true, false, false); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getPhase() != Player::Play || self->isKongcheng())
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            ExNihilo ex(Card::SuitToBeDecided, -1);
            return Slash::IsAvailable(self) || ex.isAvailable(self);
        }
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern.contains("slash", Qt::CaseInsensitive);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && !card->hasFlag("using")
            && (ownsHandCard(request.initiator, card) || request.initiator->getHandPile().contains(card->getEffectiveId()));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && selectionValid(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkLingchaCard"; }

protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        return name == "slash" || name == "ex_nihilo";
    }
};

class IkLingchaDraw : public TriggerSkillV2
{
public:
    IkLingchaDraw() : TriggerSkillV2("#iklingcha")
    {
        events << CardOffset;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.from || !effect.from->isAlive() || !effect.card || effect.card->getSkillName() != "iklingcha"
            || !effect.offset_card || !(effect.offset_card->isKindOf("Jink") || effect.offset_card->isKindOf("Nullification"))
            || effect.card->subcardsLength() == 0)
            return TriggerList();
        return TriggerList{{effect.from, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, "iklingcha");
        ctx.owner->drawCards(ctx.original_data->value<CardEffectStruct>().card->subcardsLength(), "iklingcha");
        return false;
    }
};

// ---------------------------------------------------------------- bloom053

class IkMingshi : public TriggerSkillV2
{
public:
    IkMingshi() : TriggerSkillV2("ikmingshi") { events << BeforeCardsMove; }

    static QList<int> eligible(const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        foreach (int id, move.card_ids) {
            const Card *card = Sanguosha->getCard(id);
            if (card && (card->isKindOf("Jink") || card->isKindOf("EquipCard")))
                ids << id;
        }
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || move.to_place != Player::DiscardPile || eligible(move).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = eligible(move);
        QList<int> disabled;
        foreach (int id, move.card_ids)
            if (!ids.contains(id))
                disabled << id;
        room->fillAG(move.card_ids, nullptr, disabled);
        const int id = room->askForAG(player, ids, true, objectName());
        if (id == -1) {
            room->clearAG();
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = id;
        return true;
    }

    // Each picked card is handed to someone (leaving the move) or traded for a draw.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> ids = eligible(move);
        int id = ctx.extra_data.toInt();
        while (id != -1 && player->isAlive()) {
            ids.removeOne(id);
            ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
                                                            "@ikmingshi", true, true);
            if (target) {
                move.removeCardIds(QList<int>() << id);
                room->takeAG(target, id, false);
                room->obtainCard(target, id);
            } else {
                room->takeAG(nullptr, id, false);
                player->drawCards(1, objectName());
            }
            id = ids.isEmpty() ? -1 : room->askForAG(player, ids, true, objectName());
        }
        room->clearAG();
        *ctx.original_data = QVariant::fromValue(move);
        return false;
    }
};

// ---------------------------------------------------------------- bloom054

class IkDuanni : public ViewAsSkillV2
{
public:
    IkDuanni() : ViewAsSkillV2("ikduanni") { setPhaseName("Play"); }

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
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isAllNude()
            && to->inMyAttackRange(request.initiator);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkDuanniCard"; }

    // One of the target's cards becomes its 杀 at the user, plus at most one more victim.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || target->isAllNude())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = room->askForCardChosen(source, target, "hej", objectName());
        if (id < 0)
            return ContinueEffects;
        auto *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcard(id);
        slash->setSkillName("_" + objectName());
        if (target->isCardLimited(slash, Card::MethodUse) || !target->canSlash(source, slash, false)) {
            delete slash;
            return ContinueEffects;
        }
        QList<ServerPlayer *> targets{source};
        QList<ServerPlayer *> extras;
        foreach (ServerPlayer *p, room->getOtherPlayers(target))
            if (p != source && target->canSlash(p, slash, false))
                extras << p;
        if (!extras.isEmpty())
            if (ServerPlayer *extra = room->askForPlayerChosen(source, extras, objectName(), "@ikduanni-extra", true))
                targets << extra;
        room->sortByActionOrder(targets);
        CardUseStruct use(slash, target, targets);
        use.m_addHistory = false;
        use.setOwnedCard(slash);
        room->useCardFromSkillEffect(use, ctx);
        return ContinueEffects;
    }
};

class IkMeitong : public TriggerSkillV2
{
public:
    IkMeitong() : TriggerSkillV2("ikmeitong") { events << TargetConfirming; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card || !use.card->isKindOf("Slash")
            || !use.to.contains(player) || !use.from || use.from == player || use.from->isKongcheng())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<CardUseStruct>().from;
        if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(from)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<CardUseStruct>().from;
        if (!from || from->isKongcheng())
            return false;
        const int id = room->askForCardChosen(player, from, "h", objectName(), true, Card::MethodDiscard);
        if (id >= 0 && player->canDiscard(from, id))
            room->throwCard(id, from, player);
        if (from->isAlive())
            from->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- bloom056

class IkSheji : public TriggerSkillV2
{
public:
    IkSheji() : TriggerSkillV2("iksheji") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || player->isKongcheng())
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && !owner->isKongcheng())
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

    // A won pindian names a character every basic or ordinary trick of this turn also hits.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->isKongcheng() || ctx.owner->isKongcheng() || !ctx.owner->pindian(player, objectName()))
            return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@iksheji");
        if (!target)
            return false;
        LogMessage log;
        log.type = "#IkShejiChoose";
        log.from = ctx.owner;
        log.to << target;
        room->sendLog(log);
        QStringList names = player->getTag("IkShejiTargets").toStringList();
        names << target->objectName();
        player->setTag("IkShejiTargets", names);
        room->addPlayerMark(target, "@sheji");
        return false;
    }
};

class IkShejiExtra : public TriggerSkillV2
{
public:
    IkShejiExtra() : TriggerSkillV2("#iksheji")
    {
        events << PreCardUsed << EventPhaseChanging;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        const QStringList names = player->getTag("IkShejiTargets").toStringList();
        if (names.isEmpty())
            return false;
        player->removeTag("IkShejiTargets");
        foreach (const QString &name, names)
            if (ServerPlayer *p = room->findPlayerByObjectName(name, true))
                room->removePlayerMark(p, "@sheji");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != PreCardUsed || !player || player->getTag("IkShejiTargets").toStringList().isEmpty())
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || (use.card->getTypeId() != Card::TypeBasic && !use.card->isNDTrick())
            || use.card->isKindOf("Jink") || use.card->isKindOf("Nullification") || use.card->isKindOf("Collateral")
            || use.to.isEmpty())
            return TriggerList();
        foreach (const QString &name, player->getTag("IkShejiTargets").toStringList()) {
            ServerPlayer *p = room->findPlayerByObjectName(name);
            if (p && p->isAlive() && !use.to.contains(p) && !player->isProhibited(p, use.card))
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        foreach (const QString &name, player->getTag("IkShejiTargets").toStringList()) {
            ServerPlayer *p = room->findPlayerByObjectName(name);
            if (p && p->isAlive() && !use.to.contains(p) && !player->isProhibited(p, use.card))
                use.to << p;
        }
        room->sendCompulsoryTriggerLog(player, "iksheji");
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

// Targets chosen by the cards a character uses in its own turn.
class IkPingweiRecord : public TriggerSkillV2
{
public:
    IkPingweiRecord() : TriggerSkillV2("#ikpingwei")
    {
        events << CardUsed;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (player && use.from == player && isOwnTurn(player) && use.card && use.card->getTypeId() != Card::TypeSkill
            && !use.to.isEmpty())
            room->addPlayerMark(player, "ikpingwei_targets-Clear", use.to.length());
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class IkPingwei : public TriggerSkillV2
{
public:
    IkPingwei() : TriggerSkillV2("ikpingwei")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Finish)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && player->getMark("ikpingwei_targets-Clear") > owner->getHp())
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

// ---------------------------------------------------------------- snow031

class IkLingyun : public TriggerSkillV2
{
public:
    IkLingyun() : TriggerSkillV2("iklingyun") { events << BeforeCardsMove; }

    static QList<int> tableIds(const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceTable)
                ids << move.card_ids.at(i);
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE
            || move.reason.m_playerId != player->objectName() || tableIds(move).isEmpty())
            return TriggerList();
        const Card *used = move.reason.m_extraData.value<const Card *>();
        if (used) {
            if (used->getTypeId() != Card::TypeBasic)
                return TriggerList();
        } else {
            foreach (int id, tableIds(move))
                if (Sanguosha->getCard(id)->getTypeId() != Card::TypeBasic)
                    return TriggerList();
        }
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // The used card goes back on top; with several, the last chosen ends up on top.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> ids = tableIds(move);
        move.removeCardIds(ids);
        *ctx.original_data = QVariant::fromValue(move);
        QList<int> disabled;
        while (!ids.isEmpty()) {
            int id = ids.first();
            if (ids.length() + disabled.length() > 1) {
                room->fillAG(ids + disabled, nullptr, disabled);
                id = room->askForAG(player, ids, false, objectName());
                room->clearAG();
            }
            ids.removeOne(id);
            disabled << id;
            CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(), QString());
            room->moveCardsAtomic(CardsMoveStruct(id, nullptr, Player::DrawPile, reason), true);
        }
        return false;
    }
};

class IkMiyao : public TriggerSkillV2
{
public:
    IkMiyao() : TriggerSkillV2("ikmiyao") { events << EventPhaseChanging; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->getHandcardNum() != player->getHandcardNum())
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

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        const int x = player->getHandcardNum() - owner->getHandcardNum();
        if (x > 0) {
            owner->drawCards(x, objectName());
            if (x >= 2 && owner->isAlive())
                room->loseHp(owner, 1, true, owner, objectName());
        } else if (x < 0) {
            room->askForDiscard(owner, objectName(), -x, -x);
            if (-x >= 2 && owner->isAlive() && owner->isWounded())
                room->recover(owner, RecoverStruct(objectName(), owner));
        }
        return false;
    }
};

// ---------------------------------------------------------------- snow034

// "This phase" for a client-side check: the phase of whoever is acting.
QString actingPhaseClearSuffix(const Player *self)
{
    QList<const Player *> players{self};
    foreach (const Player *p, self->getAliveSiblings())
        players << p;
    foreach (const Player *p, players) {
        switch (p->getPhase()) {
        case Player::RoundStart: return QStringLiteral("-RoundStartClear");
        case Player::Start: return QStringLiteral("-StartClear");
        case Player::Judge: return QStringLiteral("-JudgeClear");
        case Player::Draw: return QStringLiteral("-DrawClear");
        case Player::Play: return QStringLiteral("-PlayClear");
        case Player::Discard: return QStringLiteral("-DiscardClear");
        case Player::Finish: return QStringLiteral("-FinishClear");
        default: break;
        }
    }
    return QStringLiteral("-Clear");
}

class IkLixin : public ViewAsSkillV2
{
public:
    IkLixin() : ViewAsSkillV2("iklixin", 2) { markOwnerOnly(this); }

    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false, false, false); }

    static bool hasRecipient(const Player *self)
    {
        const QString mark = "iklixin_given" + actingPhaseClearSuffix(self);
        foreach (const Player *p, self->getAliveSiblings())
            if (p->getMark(mark) == 0)
                return true;
        return false;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getCardCount() < 2 || !hasRecipient(self))
            return false;
        QSet<int> types;
        foreach (const Card *card, self->getHandcards() + self->getEquips())
            types << static_cast<int>(card->getTypeId());
        if (types.size() < 2)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return true;
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            return false;
        const QString pattern = request.pattern.toLower();
        return pattern.contains("slash") || pattern.contains("jink") || pattern.contains("peach")
            || pattern.contains("analeptic");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (request.selectedCardIds.size() >= 2 || !ownsCard(request.initiator, card))
            return false;
        return request.selectedCardIds.isEmpty()
            || Sanguosha->getCard(request.selectedCardIds.first())->getTypeId() != card->getTypeId();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 2 && selectionValid(this, request);
    }

    bool willThrowSelectedCards() const override { return false; }

    // The two cards go to a recipient chosen after confirming; the basic card is virtual.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ServerPlayer *self = ctx.invoker;
        if (!self || request.selectedCardIds.size() != 2)
            return false;
        const QString mark = "iklixin_given" + currentPhaseClearSuffix(room);
        QList<ServerPlayer *> recipients;
        foreach (ServerPlayer *p, room->getOtherPlayers(self))
            if (p->getMark(mark) == 0)
                recipients << p;
        if (recipients.isEmpty())
            return false;
        ServerPlayer *target = room->askForPlayerChosen(self, recipients, objectName(), "@iklixin");
        if (!target)
            return false;
        room->setPlayerMark(target, mark, 1);
        room->giveCard(self, target, request.selectedCardIds, objectName(), false);
        return true;
    }

protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        return name == "slash" || name == "thunder_slash" || name == "fire_slash" || name == "jink" || name == "peach"
            || name == "analeptic";
    }

    Card *buildCard(const ActiveSkillRequest &, const QString &name) const override
    {
        Card *card = Sanguosha->cloneCard(name, Card::NoSuit, 0);
        if (card) {
            card->setSkillName(objectName());
            card->setCanRecast(false);
        }
        return card;
    }
};

// ---------------------------------------------------------------- snow035

class IkShenshu : public ViewAsSkillV2
{
public:
    IkShenshu() : ViewAsSkillV2("ikshenshu", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            GodSalvation probe(Card::SuitToBeDecided, -1);
            probe.setSkillName(objectName());
            return probe.isAvailable(request.initiator);
        }
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern.contains("god_salvation", Qt::CaseInsensitive);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !card || !card->isKindOf("Peach") || card->hasFlag("using"))
            return false;
        const int id = card->getEffectiveId();
        return request.initiator->handCards().contains(id) || request.initiator->getHandPile().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        const Card *peach = Sanguosha->getCard(request.selectedCardIds.first());
        GodSalvation *card = new GodSalvation(peach->getSuit(), peach->getNumber());
        card->addSubcard(peach);
        card->setSkillName(objectName());
        return card;
    }
};

class IkMingwang : public ViewAsSkillV2
{
public:
    IkMingwang() : ViewAsSkillV2("ikmingwang", 1)
    {
        setResponseOrUse(true);
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !card || card->getSuit() != Card::Spade || card->hasFlag("using"))
            return false;
        const int id = card->getEffectiveId();
        return ownsCard(request.initiator, card) || request.initiator->getHandPile().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        HLureTiger *card = new HLureTiger(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkMingwangCard"; }
};

class IkQiyi : public TriggerSkillV2
{
public:
    IkQiyi() : TriggerSkillV2("ikqiyi")
    {
        events << HpRecover;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (!player || !current || !current->isAlive() || !isOwnTurn(current) || !current->hasSkill(objectName()))
            return TriggerList();
        QStringList times;
        for (int i = 0; i < data.value<RecoverStruct>().recover; ++i)
            times << objectName();
        return times.isEmpty() ? TriggerList() : TriggerList{{current, times}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        QStringList choices{"draw"};
        if (player->isAlive() && owner->canDiscard(player, "he"))
            choices << "discard";
        if (room->askForChoice(owner, objectName(), choices.join("+"), QVariant::fromValue(player)) == "discard") {
            const int id = room->askForCardChosen(owner, player, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, player, owner == player ? nullptr : owner);
        } else {
            owner->drawCards(1, objectName());
        }
        return false;
    }
};

// ---------------------------------------------------------------- snow036

// Two stages under one skill: the play activation makes the target discard, then an
// optional "@@iklinghui" answer pays same-coloured cards for that many draws.
class IkLinghui : public ViewAsSkillV2
{
public:
    IkLinghui() : ViewAsSkillV2("iklinghui") {}

    static int colourMark(const Player *self) { return self ? self->getMark("iklinghui_colour") : 0; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return self->getPhase() == Player::Play && self->getMark("iklinghui_used-PlayClear") == 0
                && colourMark(self) == 0;
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@iklinghui"
            && colourMark(self) > 0;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        const int colour = colourMark(self);
        return colour > 0 && request.selectedCardIds.size() < 3 && ownsHandCard(self, card)
            && static_cast<int>(card->getColor()) + 1 == colour && self->canDiscard(self, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (colourMark(request.initiator) == 0)
            return request.selectedCardIds.isEmpty();
        return !request.selectedCardIds.isEmpty() && request.selectedCardIds.size() <= 3 && selectionValid(this, request);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        const Player *self = request.initiator;
        if (!self || !to)
            return false;
        if (colourMark(self) > 0)
            return selected.length() < request.selectedCardIds.size();
        return selected.isEmpty() && to != self && to->canDiscard(to, "h");
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (colourMark(request.initiator) == 0)
            return selected.length() == 1;
        if (selected.length() != request.selectedCardIds.size())
            return false;
        foreach (const Player *p, selected)
            if (p->getMark("iklinghui_target") > 0)
                return true;
        return false;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkLinghuiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target)
            return ContinueEffects;
        Room *room = source->getRoom();
        if (colourMark(source) > 0) {
            if (target->isAlive())
                target->drawCards(2, objectName());
            return ContinueEffects;
        }
        room->setPlayerMark(source, "iklinghui_used-PlayClear", 1);
        if (!target->isAlive() || !target->canDiscard(target, "h"))
            return ContinueEffects;
        const Card *card = room->askForCard(target, ".", "@iklinghui-discard", QVariant::fromValue(source));
        if (!card) {
            card = target->getRandomHandCard();
            if (!card)
                return ContinueEffects;
            room->throwCard(card, target);
        }
        room->setPlayerMark(source, "iklinghui_colour", static_cast<int>(card->getColor()) + 1);
        room->setPlayerMark(target, "iklinghui_target", 1);
        room->askForUseCard(source, "@@iklinghui", "@iklinghui", -1, Card::MethodDiscard, false);
        room->setPlayerMark(target, "iklinghui_target", 0);
        room->setPlayerMark(source, "iklinghui_colour", 0);
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- snow045

class IkLvyan : public ViewAsSkillV2
{
public:
    IkLvyan() : ViewAsSkillV2("iklvyan") { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getPhase() != Player::Play)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(self);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern.contains("slash", Qt::CaseInsensitive);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!card || card->hasFlag("using"))
            return false;
        const int id = card->getEffectiveId();
        return request.initiator->handCards().contains(id) || request.initiator->getHandPile().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() >= 2 && selectionValid(this, request);
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
};

class IkLvyanDraw : public TriggerSkillV2
{
public:
    IkLvyanDraw() : TriggerSkillV2("#iklvyan") { events << CardOffset; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.from || !effect.from->isAlive() || !effect.from->hasSkill("iklvyan") || !effect.card
            || !effect.card->isKindOf("Slash") || effect.card->getSkillName() != "iklvyan"
            || effect.card->subcardsLength() == 0)
            return TriggerList();
        return TriggerList{{effect.from, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke("iklvyan", *ctx.original_data))
            return false;
        room->broadcastSkillInvoke("iklvyan");
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->drawCards(ctx.original_data->value<CardEffectStruct>().card->subcardsLength(), "iklvyan");
        return false;
    }
};

class IkWuming : public TriggerSkillV2
{
public:
    IkWuming() : TriggerSkillV2("ikwuming")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || !player->hasSkill(objectName())
            || player->getHandcardNum() != 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- snow046

class IkLianxiao : public TriggerSkillV2
{
public:
    IkLianxiao() : TriggerSkillV2("iklianxiao") { events << CardFinished << EventPhaseEnd; }

    // Characters a card used in the acting play phase aimed at.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished || !player || player->getPhase() != Player::Play)
            return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill)
            return false;
        foreach (ServerPlayer *p, use.to)
            if (p->getMark("iklianxiao_aimed-Clear") == 0)
                room->setPlayerMark(p, "iklianxiao_aimed-Clear", 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Discard)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->getMark("iklianxiao_aimed-Clear") == 0
                && (owner == player || owner->inMyAttackRange(player)))
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

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        QStringList choices{"draw"};
        if (player->canDiscard(player, "he"))
            choices << "throw";
        if (room->askForChoice(ctx.owner, objectName(), choices.join("+"), QVariant::fromValue(player)) == "throw")
            room->askForDiscard(player, objectName(), 1, 1, false, true);
        else
            player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- snow048

class IkQile : public TriggerSkillV2
{
public:
    IkQile() : TriggerSkillV2("ikqile") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || !player->hasSkill(objectName())
            || !player->canDiscard(player, "h"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->askForCard(player, "BasicCard", "@ikqile", *ctx.original_data, objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->drawCards(2, objectName());
        room->setPlayerCardLimitation(player, "use", "Slash", true, objectName());
        return false;
    }
};

// After damage the source names a suit; a hand card of another suit is shown and a chosen
// character uses a copy of it.
class IkSaoxiao : public TriggerSkillV2
{
public:
    IkSaoxiao() : TriggerSkillV2("iksaoxiao") { events << DamageComplete; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName()) || player->isKongcheng()
            || !damage.from || !damage.from->isAlive())
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

    static QList<ServerPlayer *> legalTargets(Room *room, ServerPlayer *user, const Card *card)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (card->targetFilter(QList<const Player *>(), p, user) && !user->isProhibited(p, card))
                targets << p;
        return targets;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        if (!from || !from->isAlive() || player->isKongcheng())
            return false;
        const QString suit = Card::Suit2String(room->askForSuit(from, objectName()));
        LogMessage log;
        log.type = "#ChooseSuit";
        log.from = from;
        log.arg = suit;
        room->sendLog(log);
        const Card *hand = room->askForCard(player, ".|^" + suit + "|.|hand", "@iksaoxiao", *ctx.original_data,
                                            Card::MethodNone);
        if (!hand)
            return false;
        room->showCard(player, hand->getEffectiveId());
        const Card *shown = Sanguosha->getCard(hand->getEffectiveId());
        if (shown->isKindOf("Collateral") || shown->getTypeId() == Card::TypeSkill)
            return false;
        QList<ServerPlayer *> users;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            Card *probe = Sanguosha->cloneCard(shown->objectName(), shown->getSuit(), shown->getNumber());
            if (!probe)
                continue;
            probe->setSkillName("_" + objectName());
            if (probe->isAvailable(p) && !p->isCardLimited(probe, Card::MethodUse)
                && (probe->targetFixed() || !legalTargets(room, p, probe).isEmpty()))
                users << p;
            delete probe;
        }
        if (users.isEmpty())
            return false;
        ServerPlayer *user = room->askForPlayerChosen(player, users, objectName(), "@iksaoxiao-choose", true);
        if (!user)
            return false;
        Card *copy = Sanguosha->cloneCard(shown->objectName(), shown->getSuit(), shown->getNumber());
        copy->setSkillName("_" + objectName());
        CardUseStruct use(copy, user, QList<ServerPlayer *>());
        if (!copy->targetFixed()) {
            ServerPlayer *target = room->askForPlayerChosen(user, legalTargets(room, user, copy), objectName(),
                                                            "@iksaoxiao-use:::" + shown->objectName());
            if (!target) {
                delete copy;
                return false;
            }
            use.to << target;
        }
        use.setOwnedCard(copy);
        room->useCardFromSkillEffect(use, ctx, true);
        return false;
    }
};

// ---------------------------------------------------------------- snow049

// A diamond card becomes 乐不思蜀 or a black non-trick 兵粮寸断 on the user itself; then it
// draws, closes in on someone and slashes them through their armour.
class IkXiaowu : public ViewAsSkillV2
{
public:
    IkXiaowu() : ViewAsSkillV2("ikxiaowu", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    static QString trickFor(const Card *card)
    {
        if (card->getSuit() == Card::Diamond)
            return QStringLiteral("indulgence");
        if (card->isBlack() && card->getTypeId() != Card::TypeTrick)
            return QStringLiteral("supply_shortage");
        return QString();
    }

    static bool canPlace(const Player *self, const QString &name)
    {
        if (name.isEmpty() || self->containsTrick(name))
            return false;
        Card *probe = Sanguosha->cloneCard(name);
        const bool ok = probe && !self->isCardLimited(probe, Card::MethodUse);
        delete probe;
        return ok;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        return self && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && (canPlace(self, "indulgence") || canPlace(self, "supply_shortage"));
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card)
            && canPlace(request.initiator, trickFor(card));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkXiaowuCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
            return FinishSkill;
        Room *room = source->getRoom();
        const Card *material = Sanguosha->getCard(ctx.use_card->getSubcards().first());
        const QString name = trickFor(material);
        if (!canPlace(source, name) || room->getCardOwner(material->getEffectiveId()) != source)
            return FinishSkill;
        Card *trick = Sanguosha->cloneCard(name, material->getSuit(), material->getNumber());
        trick->addSubcard(material);
        trick->setSkillName(objectName());
        CardUseStruct use(trick, source, source);
        use.setOwnedCard(trick);
        room->useCardFromSkillEffect(use, ctx);
        if (!source->isAlive())
            return FinishSkill;
        source->drawCards(1, objectName());
        ServerPlayer *victim = room->askForPlayerChosen(source, room->getOtherPlayers(source), objectName(), "@ikxiaowu");
        if (!victim)
            return FinishSkill;
        room->setFixedDistance(source, victim, 1);
        QStringList fixed = source->getTag("IkXiaowuTarget").toStringList();
        fixed << victim->objectName();
        source->setTag("IkXiaowuTarget", fixed);
        useSkillSlash(room, ctx, source, victim, objectName());
        return FinishSkill;
    }
};

class IkXiaowuEffect : public TriggerSkillV2
{
public:
    IkXiaowuEffect() : TriggerSkillV2("#ikxiaowu")
    {
        events << TargetSpecified << EventPhaseChanging << Death;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QStringList fixed = player->getTag("IkXiaowuTarget").toStringList();
            player->removeTag("IkXiaowuTarget");
            foreach (const QString &name, fixed)
                if (ServerPlayer *p = room->findPlayerByObjectName(name, true))
                    room->removeFixedDistance(player, p, 1);
        } else if (event == Death) {
            ServerPlayer *dead = data.value<DeathStruct>().who;
            if (!dead || dead == player)
                return false;
            QStringList fixed = player->getTag("IkXiaowuTarget").toStringList();
            if (fixed.removeAll(dead->objectName()) > 0) {
                room->removeFixedDistance(player, dead, 1);
                player->setTag("IkXiaowuTarget", fixed);
            }
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !use.card || !use.card->isKindOf("Slash")
            || use.card->getSkillName() != "ikxiaowu")
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, "ikxiaowu");
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        foreach (ServerPlayer *p, use.to)
            p->addQinggangTag(use.card);
        return false;
    }
};

// ---------------------------------------------------------------- snow051

class IkWanmi : public TriggerSkillV2
{
public:
    IkWanmi() : TriggerSkillV2("ikwanmi") { events << CardsMoveOneTime; }

    static QList<int> arrived(Room *room, ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        foreach (int id, move.card_ids)
            if (room->getCardOwner(id) == player && room->getCardPlace(id) == move.to_place)
                ids << id;
        return ids;
    }

    static QStringList usableNames(Room *room, ServerPlayer *player, const QList<int> &ids)
    {
        QStringList names;
        const QStringList all{"slash", "thunder_slash", "fire_slash", "analeptic", "peach"};
        foreach (const QString &name, all) {
            Card *card = Sanguosha->cloneCard(name, Card::SuitToBeDecided, -1);
            if (!card)
                continue;
            card->addSubcards(ids);
            card->setSkillName("ikwanmi");
            bool ok = !player->isCardLimited(card, Card::MethodUse);
            if (ok && card->isKindOf("Slash")) {
                ok = false;
                foreach (ServerPlayer *p, room->getOtherPlayers(player))
                    if (player->canSlash(p, card, false)) {
                        ok = true;
                        break;
                    }
            } else if (ok && card->isKindOf("Peach")) {
                ok = player->isWounded() && !player->isProhibited(player, card);
            } else if (ok) {
                ok = !player->isProhibited(player, card);
            }
            delete card;
            if (ok)
                names << name;
        }
        return names;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.to != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getMark("ikwanmi-Clear") >= 2 || move.reason.m_skillName == "InitialHandCards"
            || (move.to_place != Player::PlaceHand && move.to_place != Player::PlaceEquip
                && move.to_place != Player::PlaceDelayedTrick))
            return TriggerList();
        const QList<int> ids = arrived(room, player, move);
        if (ids.isEmpty() || usableNames(room, player, ids).isEmpty())
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

    // The arrived cards are used together as one basic card, unlimited and at any distance.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = arrived(room, player, move);
        const QStringList names = usableNames(room, player, ids);
        if (ids.isEmpty() || names.isEmpty())
            return false;
        const QString name = room->askForChoice(player, objectName(), names.join("+"));
        Card *card = Sanguosha->cloneCard(name, Card::SuitToBeDecided, -1);
        card->addSubcards(ids);
        card->setSkillName(objectName());
        CardUseStruct use(card, player, player);
        if (card->isKindOf("Slash")) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (player->canSlash(p, card, false))
                    targets << p;
            ServerPlayer *target = targets.isEmpty()
                ? nullptr
                : room->askForPlayerChosen(player, targets, objectName(), "@ikwanmi-slash", true);
            if (!target) {
                delete card;
                return false;
            }
            use.to.clear();
            use.to << target;
        }
        room->addPlayerMark(player, "ikwanmi-Clear");
        use.m_addHistory = false;
        use.setOwnedCard(card);
        room->useCardFromSkillEffect(use, ctx);
        return false;
    }
};

class IkGuichan : public TriggerSkillV2
{
public:
    IkGuichan() : TriggerSkillV2("ikguichan")
    {
        events << Death << EventPhaseChanging;
        global = true;
    }

    // The acting character's ban outlives its current turn: it lasts through its next one.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive
            || player->getMark("ikguichan_turns") == 0)
            return false;
        room->removePlayerMark(player, "ikguichan_turns");
        if (player->getMark("ikguichan_turns") == 0)
            room->removePlayerCardLimitationByReason(player, objectName());
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Death || !player || data.value<DeathStruct>().who != player || !player->hasSkill(objectName()))
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
        const QString suit = Card::Suit2String(room->askForSuit(player, objectName()));
        LogMessage log;
        log.type = "#ChooseSuit";
        log.from = player;
        log.arg = suit;
        room->sendLog(log);
        ServerPlayer *current = room->getCurrent();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p == current && isOwnTurn(p)) {
                room->setPlayerCardLimitation(p, "use,response", ".|" + suit, false, objectName());
                room->setPlayerMark(p, "ikguichan_turns", 2);
            } else {
                room->setPlayerCardLimitation(p, "use,response", ".|" + suit, true, objectName());
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------- snow052

class IkHuanlve : public ViewAsSkillV2
{
public:
    IkHuanlve() : ViewAsSkillV2("ikhuanlve", 2)
    {
        setResponseOrUse(true);
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }

    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), false, true, false, false); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getPhase() != Player::Play || self->getCardCount() < 2)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return true;
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern.contains("nullification", Qt::CaseInsensitive);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (request.selectedCardIds.size() >= 2 || !card || card->hasFlag("using"))
            return false;
        const int id = card->getEffectiveId();
        if (!ownsCard(request.initiator, card) && !request.initiator->getHandPile().contains(id))
            return false;
        return !request.selectedCardIds.isEmpty() || card->getTypeId() == Card::TypeTrick;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 2 && selectionValid(this, request);
    }

    // Paying with two tricks earns the end-phase draw.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request))
            return false;
        bool allTricks = request.selectedCardIds.size() == 2;
        foreach (int id, request.selectedCardIds)
            if (Sanguosha->getCard(id)->getTypeId() != Card::TypeTrick)
                allTricks = false;
        if (allTricks && ctx.invoker && ctx.invoker->getMark("@thought") == 0)
            ctx.invoker->gainMark("@thought");
        return true;
    }

protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        Card *card = Sanguosha->cloneCard(name);
        const bool ok = card && card->isNDTrick();
        delete card;
        return ok;
    }
};

class IkHuanlveDraw : public TriggerSkillV2
{
public:
    IkHuanlveDraw() : TriggerSkillV2("#ikhuanlve")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || player->getMark("@thought") == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, "ikhuanlve");
        player->loseMark("@thought");
        player->drawCards(2, "ikhuanlve");
        return false;
    }
};

class IkMuguang : public TriggerSkillV2
{
public:
    IkMuguang() : TriggerSkillV2("ikmuguang")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName())
            || (damage.nature == DamageStruct::Normal) != player->isKongcheng())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // Elemental damage with cards in hand heals instead; plain damage with an empty hand is void.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.nature != DamageStruct::Normal && player->isWounded())
            room->recover(player, RecoverStruct(objectName(), player));
        return true;
    }
};

// ---------------------------------------------------------------- snow054

class IkLihun : public ViewAsSkillV2
{
public:
    IkLihun() : ViewAsSkillV2("iklihun") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator
            && to->getHandcardNum() < request.initiator->getHandcardNum();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkLihunCard"; }

    // The whole hand goes over; the target shows some, the user takes the shown or the rest.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || source->isKongcheng())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->giveCard(source, target, source->handCards(), objectName(), false);
        if (!target->isAlive() || target->isKongcheng() || !source->isAlive())
            return ContinueEffects;
        QList<int> shown;
        if (const Card *chosen = room->askForExchange(target, objectName(), 998, 1, false, "@iklihun-showcard", false)) {
            shown = chosen->getSubcards();
            delete chosen;
        }
        if (shown.isEmpty())
            shown << target->getRandomHandCardId();
        room->showCard(target, shown);
        QList<int> taken = shown;
        if (room->askForChoice(source, objectName(), "showcard+noshowcard", QVariant::fromValue(target)) == "noshowcard") {
            taken = target->handCards();
            foreach (int id, shown)
                taken.removeOne(id);
        }
        if (!taken.isEmpty()) {
            DummyCard dummy(taken);
            room->obtainCard(source, &dummy, false);
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- snow057

// Cards a character discards from hand or equipment in its discard phase.
QList<int> discardedThisPhase(const Player *player)
{
    return ListV2I(player->getTag("IkDiscardPhaseCards").toList());
}

class IkDiscardRecord : public TriggerSkillV2
{
public:
    IkDiscardRecord() : TriggerSkillV2("#ikai-discard-record")
    {
        events << CardsMoveOneTime << EventPhaseChanging;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if (event == EventPhaseChanging) {
            const Player::Phase to = data.value<PhaseChangeStruct>().to;
            if (to == Player::Discard || to == Player::NotActive)
                player->removeTag("IkDiscardPhaseCards");
            return false;
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || player->getPhase() != Player::Discard
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
            return false;
        QVariantList list = player->getTag("IkDiscardPhaseCards").toList();
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceHand || move.from_places.value(i) == Player::PlaceEquip)
                list << move.card_ids.at(i);
        player->setTag("IkDiscardPhaseCards", list);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class IkTianzuo : public TriggerSkillV2
{
public:
    IkTianzuo() : TriggerSkillV2("iktianzuo") { events << EventPhaseStart << EventPhaseEnd; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Discard)
            return TriggerList();
        if (event == EventPhaseStart)
            return player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (player->getMark("iktianzuo-Clear") == 0)
            return TriggerList();
        const int x = discardedThisPhase(player).length();
        const QString spared = player->getTag("IkYewuTarget").toString();
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->isWounded() && p->getHp() < x && p->objectName() != spared)
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (event == EventPhaseEnd)
            return true;
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Draw one or two now; after the discards everyone below X HP heals.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (event == EventPhaseStart) {
            player->drawCards(room->askForChoice(player, objectName(), "draw1+draw2") == "draw1" ? 1 : 2, objectName());
            room->setPlayerMark(player, "iktianzuo-Clear", 1);
            return false;
        }
        room->setPlayerMark(player, "iktianzuo-Clear", 0);
        room->sendCompulsoryTriggerLog(player, objectName());
        const int x = discardedThisPhase(player).length();
        const QString spared = player->getTag("IkYewuTarget").toString();
        foreach (ServerPlayer *p, room->getAllPlayers())
            if (p->isWounded() && p->getHp() < x && p->objectName() != spared)
                room->recover(p, RecoverStruct(objectName(), player));
        return false;
    }
};

class IkYewu : public TriggerSkillV2
{
public:
    IkYewu() : TriggerSkillV2("ikyewu") { events << EventPhaseStart << Death; }

    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Death || !player)
            return false;
        ServerPlayer *dead = data.value<DeathStruct>().who;
        if (dead && player->getTag("IkYewuTarget").toString() == dead->objectName())
            player->removeTag("IkYewuTarget");
        return false;
    }

    static QList<int> inPile(Room *room, const Player *player)
    {
        QList<int> ids;
        foreach (int id, discardedThisPhase(player))
            if (room->getCardPlace(id) == Player::DiscardPile)
                ids << id;
        return ids;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Finish
            || !player->hasSkill(objectName()) || !player->getTag("IkYewuTarget").toString().isEmpty()
            || inPile(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@ikyewu",
                                                        true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return false;
        const QList<int> ids = inPile(room, player);
        if (!ids.isEmpty()) {
            DummyCard dummy(ids);
            room->obtainCard(target, &dummy);
        }
        player->setTag("IkYewuTarget", target->objectName());
        return false;
    }
};

// ---------------------------------------------------------------- luna030

class IkLingcu : public TriggerSkillV2
{
public:
    IkLingcu() : TriggerSkillV2("iklingcu") { events << EventPhaseChanging; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::Play || player->isSkipped(Player::Play))
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

    // Judge into "cluster" until a suit repeats; each cluster card is a 杀 within range.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        player->skip(Player::Play, true);
        player->turnOver();
        QList<Card::Suit> suits;
        for (int guard = 0; guard < 8 && player->isAlive(); ++guard) {
            JudgeStruct judge;
            judge.play_animation = false;
            judge.who = player;
            judge.reason = objectName();
            room->judge(judge);
            if (!judge.card)
                break;
            const Card::Suit suit = judge.card->getSuit();
            if (room->getCardPlace(judge.card->getEffectiveId()) == Player::DiscardPile)
                player->addToPile("cluster", judge.card);
            const bool repeated = suits.contains(suit);
            suits << suit;
            if (repeated)
                break;
        }
        const int n = player->getPile("cluster").length();
        for (int i = 0; i < n && player->isAlive(); ++i) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (player->canSlash(p))
                    targets << p;
            if (targets.isEmpty())
                break;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@dummy-slash");
            if (!target || !useSkillSlash(room, ctx, player, target, objectName()))
                break;
        }
        if (!player->getPile("cluster").isEmpty())
            player->clearOnePrivatePile("cluster");
        return false;
    }
};

// ---------------------------------------------------------------- luna031

// The acting trick's user, kept on each owner as hand count + 1 while it resolves.
class IkQisiRecord : public TriggerSkillV2
{
public:
    IkQisiRecord() : TriggerSkillV2("#ikqisi")
    {
        events << CardEffected << CardFinished;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        int mark = 0;
        if (event == CardEffected) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.card && effect.card->isNDTrick() && effect.from)
                mark = effect.from->getHandcardNum() + 1;
            foreach (ServerPlayer *owner, room->getAlivePlayers()) {
                if (!owner->hasSkill("ikqisi"))
                    continue;
                const int value = effect.from == owner ? 0 : mark;
                if (owner->getMark("ikqisi_from") != value)
                    room->setPlayerMark(owner, "ikqisi_from", value);
            }
        } else {
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->getMark("ikqisi_from") > 0)
                    room->setPlayerMark(owner, "ikqisi_from", 0);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class IkQisi : public ViewAsSkillV2
{
public:
    IkQisi() : ViewAsSkillV2("ikqisi") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            || !request.pattern.contains("nullification", Qt::CaseInsensitive))
            return false;
        const int from = self->getMark("ikqisi_from");
        return from > 0 && from - 1 >= self->getHandcardNum() && self->getHandcardNum() % 2 == 1;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Nullification *card = new Nullification(Card::NoSuit, 0);
        card->setSkillName(objectName());
        return card;
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request))
            return false;
        if (ctx.invoker)
            ctx.invoker->drawCards(1, objectName());
        return true;
    }
};

class IkJilian : public TriggerSkillV2
{
public:
    IkJilian() : TriggerSkillV2("ikjilian") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || data.value<DamageStruct>().to != player || !player->isAlive())
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && player->getHandcardNum() <= owner->getHandcardNum()
                && owner->getHandcardNum() % 2 == 0 && owner->canDiscard(owner, "he"))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, "^BasicCard", "@ikjilian:" + player->objectName(), *ctx.original_data,
                              objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->isAlive() && player->isWounded())
            room->recover(player, RecoverStruct(objectName(), ctx.owner));
        return false;
    }
};

// ---------------------------------------------------------------- luna032

class IkJichang : public TriggerSkillV2
{
public:
    IkJichang() : TriggerSkillV2("ikjichang") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName()))
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

    // Draw one per suit missing from the shown hand; two or more skip judge and draw.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        int n = 4;
        if (!player->isKongcheng()) {
            room->showAllCards(player);
            QSet<int> suits;
            foreach (const Card *card, player->getHandcards())
                suits << static_cast<int>(card->getSuit());
            n -= suits.size();
        }
        if (n <= 0)
            return false;
        player->drawCards(n, objectName());
        if (n >= 2) {
            player->skip(Player::Judge);
            player->skip(Player::Draw);
        }
        return false;
    }
};

class IkManwu : public ViewAsSkillV2
{
public:
    IkManwu() : ViewAsSkillV2("ikmanwu") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkManwuCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || source->isKongcheng() || target->isKongcheng())
            return ContinueEffects;
        const int result = source->pindianInt(target, objectName());
        ServerPlayer *winner = result > 0 ? source : (result < 0 ? target : nullptr);
        if (winner && winner->isAlive() && winner->getHandcardNum() < winner->getHp())
            winner->drawCards(winner->getHp() - winner->getHandcardNum(), objectName());
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- luna033

class IkXianlv : public TriggerSkillV2
{
public:
    IkXianlv() : TriggerSkillV2("ikxianlv") { events << EventPhaseStart << DrawNCards; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive())
            return result;
        if (event == DrawNCards) {
            const DrawStruct draw = data.value<DrawStruct>();
            if (draw.who == player && draw.reason == "draw_phase" && player->getMark("ikxianlv_less-Clear") > 0)
                result[player] << objectName();
            return result;
        }
        if (player->getPhase() == Player::Draw) {
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->hasSkill(objectName()) && !owner->getPile("music").isEmpty())
                    result[owner] << objectName();
        } else if ((player->getPhase() == Player::Start || player->getPhase() == Player::Finish)
                   && player->hasSkill(objectName())) {
            result[player] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == DrawNCards)
            return true;
        if (player->getPhase() == Player::Draw) {
            const QList<int> ids = room->askForExchangeCards(ctx.owner, objectName(), ctx.owner->getPile("music").length(), 0,
                                                             "@ikxianlv:" + player->objectName(), "music");
            if (ids.isEmpty())
                return false;
            ctx.extra_data = ListI2V(ids);
            room->broadcastSkillInvoke(objectName());
            return true;
        }
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num = qMax(0, draw.num - player->getMark("ikxianlv_less-Clear"));
            room->setPlayerMark(player, "ikxianlv_less-Clear", 0);
            *ctx.original_data = QVariant::fromValue(draw);
            return false;
        }
        if (player->getPhase() == Player::Draw) {
            QList<int> ids;
            foreach (int id, ListV2I(ctx.extra_data.toList()))
                if (owner->getPile("music").contains(id))
                    ids << id;
            if (ids.isEmpty())
                return false;
            DummyCard dummy(ids);
            room->obtainCard(player, &dummy,
                             CardMoveReason(CardMoveReason::S_REASON_GIVE, owner->objectName(), player->objectName(),
                                            objectName(), QString()));
            owner->drawCards(1, objectName());
            room->addPlayerMark(player, "ikxianlv_less-Clear");
            return false;
        }
        JudgeStruct judge;
        judge.good = true;
        judge.play_animation = false;
        judge.who = owner;
        judge.reason = objectName();
        room->judge(judge);
        if (!judge.card || room->getCardPlace(judge.card->getEffectiveId()) != Player::DiscardPile)
            return false;
        foreach (int id, owner->getPile("music"))
            if (Sanguosha->getCard(id)->getSuit() == judge.card->getSuit())
                return false;
        owner->addToPile("music", judge.card);
        return false;
    }
};

// ---------------------------------------------------------------- luna036

class IkLianwu : public TriggerSkillV2
{
public:
    IkLianwu() : TriggerSkillV2("iklianwu") { events << EventPhaseStart; }

    static int amount(const Player *player) { return qMax(1, player->getLostHp()); }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName()))
            return TriggerList();
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canDiscard(p, "he"))
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    // Pay with HP or a non-basic card, then knock off one of someone's cards.
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canDiscard(p, "he"))
                targets << p;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@iklianwu", true, true);
        if (!target)
            return false;
        QStringList choices{"losehp"};
        foreach (const Card *card, player->getHandcards() + player->getEquips())
            if (card->getTypeId() != Card::TypeBasic && player->canDiscard(player, card->getEffectiveId())) {
                choices.prepend("discard");
                break;
            }
        if (room->askForChoice(player, objectName(), choices.join("+")) == "discard"
            && room->askForCard(player, "^BasicCard", "@iklianwu-discard", QVariant(), objectName())) {
        } else {
            room->loseHp(player, 1, true, player, objectName());
        }
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return player->isAlive();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || !player->canDiscard(target, "he"))
            return false;
        const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
        if (id < 0)
            return false;
        const Card::CardType type = Sanguosha->getCard(id)->getTypeId();
        room->throwCard(id, target, player);
        if (!player->isAlive())
            return false;
        if (type == Card::TypeTrick) {
            const QList<ServerPlayer *> drawers = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0,
                                                                            amount(player), "@iklianwu-draw", false, true);
            foreach (ServerPlayer *p, drawers)
                if (p->isAlive())
                    p->drawCards(1, objectName());
        } else if (type == Card::TypeBasic) {
            room->setPlayerMark(player, "iklianwu_near-Clear", 1);
        } else if (type == Card::TypeEquip) {
            room->setPlayerMark(player, "iklianwu_slash-Clear", 1);
        }
        return false;
    }
};

class IkLianwuDistance : public DistanceSkill
{
public:
    IkLianwuDistance() : DistanceSkill("#iklianwu-dist") {}

    int getCorrect(const Player *from, const Player *) const override
    {
        return from && from->getMark("iklianwu_near-Clear") > 0 ? -IkLianwu::amount(from) : 0;
    }
};

class IkLianwuTargetMod : public TargetModSkillV2
{
public:
    IkLianwuTargetMod() : TargetModSkillV2("#iklianwu-tar", "Slash") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.primary || ctx.primary->getMark("iklianwu_slash-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(IkLianwu::amount(ctx.primary));
    }
};

// ---------------------------------------------------------------- luna037

class IkMoshanFilter : public FilterSkill
{
public:
    IkMoshanFilter() : FilterSkill("ikmoshan") {}

    bool viewFilter(const Card *to_select) const override
    {
        return to_select->isKindOf("Jink") || to_select->isKindOf("NatureSlash");
    }

    const Card *viewAs(const Card *originalCard) const override
    {
        EightDiagram *armor = new EightDiagram(originalCard->getSuit(), originalCard->getNumber());
        armor->setSkillName(objectName());
        return armor;
    }
};

// Losing an equipped 八卦阵 hands it to someone instead; once a turn the owner then takes a card.
class IkMoshan : public TriggerSkillV2
{
public:
    IkMoshan() : TriggerSkillV2("ikmoshan")
    {
        events << BeforeCardsMove;
        frequency = Compulsory;
        view_as_skill = new IkMoshanFilter;
        markOwnerOnly(this);
    }

    static int armorLeaving(Room *room, ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const int id = move.card_ids.at(i);
            if (move.from_places.value(i) == Player::PlaceEquip && room->getCardOwner(id) == player
                && Sanguosha->getCard(id)->isKindOf("EightDiagram"))
                return id;
        }
        return -1;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->hasFlag("ikmoshan_giving") || armorLeaving(room, player, move) < 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const int id = armorLeaving(room, player, move);
        if (id < 0)
            return false;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        move.removeCardIds(QList<int>() << id);
        *ctx.original_data = QVariant::fromValue(move);
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@ikmoshan");
        if (target) {
            room->setPlayerFlag(player, "ikmoshan_giving");
            room->obtainCard(target, Sanguosha->getCard(id),
                             CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(),
                                            objectName(), QString()));
            room->setPlayerFlag(player, "-ikmoshan_giving");
        }
        if (player->getMark("ikmoshan_took-Clear") > 0 || !player->isAlive())
            return false;
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (!p->isNude())
                victims << p;
        if (victims.isEmpty())
            return false;
        ServerPlayer *victim = room->askForPlayerChosen(player, victims, objectName(), "@ikmoshan-obtain");
        if (!victim)
            return false;
        const int taken = room->askForCardChosen(player, victim, "he", objectName());
        if (taken >= 0) {
            room->obtainCard(player, taken, false);
            room->setPlayerMark(player, "ikmoshan_took-Clear", 1);
        }
        return false;
    }
};

// ---------------------------------------------------------------- luna045

class IkXieke : public ViewAsSkillV2
{
public:
    IkXieke() : ViewAsSkillV2("ikxieke") {}

    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, true, false, false); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getPhase() != Player::Play || self->isChained())
            return false;
        const bool duel = self->aliveCount() == 2;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return !duel || Slash::IsAvailable(self);
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            return false;
        const QString pattern = request.pattern.toLower();
        if (pattern.contains("slash"))
            return true;
        return !duel
            && (pattern.contains("peach") || pattern.contains("analeptic") || pattern.contains("jink")
                || pattern.contains("nullification"));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

protected:
    bool allowDeclaration(const Player *player, const QString &name) const override
    {
        if (player && player->aliveCount() == 2)
            return name == "slash" || name == "thunder_slash" || name == "fire_slash";
        return name == "slash" || name == "thunder_slash" || name == "fire_slash" || name == "jink" || name == "peach"
            || name == "analeptic" || name == "nullification";
    }
};

class IkXiekeChain : public TriggerSkillV2
{
public:
    IkXiekeChain() : TriggerSkillV2("#ikxieke")
    {
        events << CardFinished;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || player->isChained() || !use.card
            || use.card->getSkillName() != "ikxieke")
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->setPlayerChained(player, true, player);
        return false;
    }
};

class IkYunmai : public TriggerSkillV2
{
public:
    IkYunmai() : TriggerSkillV2("ikyunmai") { events << EventPhaseChanging; }

    static QList<ServerPlayer *> candidates(Room *room)
    {
        const int half = (room->alivePlayerCount() + 1) / 2;
        QList<ServerPlayer *> chained, loose;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            (p->isChained() ? chained : loose) << p;
        QList<ServerPlayer *> targets;
        if (chained.length() >= half)
            targets += chained;
        if (loose.length() >= half)
            targets += loose;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::NotActive || candidates(room).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, candidates(room), objectName(), "@ikyunmai", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (target && target->isAlive())
            room->setPlayerChained(target, !target->isChained(), player);
        return false;
    }
};

// ---------------------------------------------------------------- luna046

class IkLunyao : public TriggerSkillV2
{
public:
    IkLunyao() : TriggerSkillV2("iklunyao") { events << BeforeCardsMove; }

    static QList<int> ownIds(const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceHand || move.from_places.value(i) == Player::PlaceEquip)
                ids << move.card_ids.at(i);
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getMark("iklunyao-Clear") > 0 || ownIds(move).isEmpty())
            return TriggerList();
        const bool takenByOther = move.to && move.to != player && move.to_place == Player::PlaceHand;
        const bool discardedByOther = (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
            && !move.reason.m_playerId.isEmpty() && move.reason.m_playerId != player->objectName();
        if (!takenByOther && !discardedByOther)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->setPlayerMark(player, "iklunyao-Clear", 1);
        return true;
    }

    // The draw pile's top cards travel in place of the owner's.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = ownIds(move);
        const QList<int> pile = room->getNCards(ids.length(), false);
        move.removeCardIds(ids);
        *ctx.original_data = QVariant::fromValue(move);
        if (!pile.isEmpty())
            room->moveCardsAtomic(CardsMoveStruct(pile, nullptr, move.to, Player::DrawPile, move.to_place, move.reason),
                                  false);
        return false;
    }
};

class IkQimu : public TriggerSkillV2
{
public:
    IkQimu() : TriggerSkillV2("ikqimu")
    {
        events << PreCardUsed << BeforeCardsMove << EventPhaseChanging;
        frequency = Compulsory;
    }

    // The first card the owner uses in each turn.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::NotActive)
                foreach (ServerPlayer *p, room->getAllPlayers(true))
                    p->removeTag("IkQimuFirst");
            return false;
        }
        if (event != PreCardUsed || !player->hasSkill(objectName()) || player->getTag("IkQimuFirst").isValid())
            return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill)
            return false;
        player->setTag("IkQimuFirst", use.card->subcardsLength() > 0 || !use.card->isVirtualCard()
                                          ? use.card->toString() : QStringLiteral("-"));
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != BeforeCardsMove || !player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE
            || move.card_ids.isEmpty())
            return TriggerList();
        const Card *used = move.reason.m_extraData.value<const Card *>();
        if (!used || used->toString() != player->getTag("IkQimuFirst").toString())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        player->setTag("IkQimuFirst", QStringLiteral("-"));
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = move.card_ids;
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@ikqimu");
        if (!target)
            return false;
        move.removeCardIds(ids);
        *ctx.original_data = QVariant::fromValue(move);
        DummyCard dummy(ids);
        room->obtainCard(target, &dummy);
        return false;
    }
};

// ---------------------------------------------------------------- luna047

class IkYuanji : public TriggerSkillV2
{
public:
    IkYuanji() : TriggerSkillV2("ikyuanji")
    {
        events << GameStart << DrawNCards << Death;
        frequency = Compulsory;
        global = true;
    }

    static ServerPlayer *keeper(Room *room)
    {
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->hasSkill("ikyuanji"))
                return p;
        return nullptr;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return TriggerList();
        if (event == GameStart)
            return player->isAlive() && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (event == DrawNCards) {
            const DrawStruct draw = data.value<DrawStruct>();
            if (draw.who != player || draw.reason != "draw_phase" || player->getMark("@canal") == 0)
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        const DeathStruct death = data.value<DeathStruct>();
        if (death.who != player)
            return TriggerList();
        if (player->hasSkill(objectName(), true))
            return TriggerList{{player, {objectName()}}};
        if (player->getMark("@canal") > 0)
            if (ServerPlayer *owner = keeper(room))
                return TriggerList{{owner, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == GameStart) {
            room->sendCompulsoryTriggerLog(player, objectName());
            player->gainMark("@canal");
        } else if (event == DrawNCards) {
            if (ServerPlayer *owner = keeper(room))
                room->sendCompulsoryTriggerLog(owner, objectName(), false);
            room->notifySkillInvoked(player, objectName());
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            ++draw.num;
            *ctx.original_data = QVariant::fromValue(draw);
        } else if (ctx.owner == player) {
            room->sendCompulsoryTriggerLog(player, objectName());
            foreach (ServerPlayer *p, room->getAllPlayers(true))
                if (p->getMark("@canal") > 0)
                    p->loseAllMarks("@canal");
        } else {
            room->sendCompulsoryTriggerLog(ctx.owner, objectName());
            player->loseAllMarks("@canal");
            ctx.owner->gainMark("@canal");
        }
        return false;
    }
};

// The first 杀 of a "枢" holder's play phase is outside the limit.
class IkYuanjiTargetMod : public TargetModSkillV2
{
public:
    IkYuanjiTargetMod() : TargetModSkillV2("#ikyuanji-tar", "Slash")
    {
        frequency = Compulsory;
        setHolderSelector(CorrectSkill_System); // any "枢" holder
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.primary || ctx.primary->getMark("@canal") == 0
            || ctx.primary->getPhase() != Player::Play)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1);
    }
};

class IkShuluo : public TriggerSkillV2
{
public:
    IkShuluo() : TriggerSkillV2("ikshuluo")
    {
        events << Damaged;
        global = true;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || player->getMark("@canal") == 0 || !damage.from
            || !damage.from->isAlive() || damage.from == player)
            return TriggerList();
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->hasSkill(objectName()))
                return TriggerList{{damage.from, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // The source takes the mark; unless it is the skill's owner, it loses two cards for it.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.owner;
        player->loseAllMarks("@canal");
        source->gainMark("@canal");
        if (source->hasSkill(objectName()) || !player->isAlive() || !player->canDiscard(source, "he"))
            return false;
        QList<int> ids;
        for (int i = 0; i < 2 && player->canDiscard(source, "he"); ++i) {
            const int id = room->askForCardChosen(player, source, "he", objectName(), false, Card::MethodDiscard, ids);
            if (id < 0)
                break;
            ids << id;
            if (ids.length() >= source->getCardCount())
                break;
        }
        if (!ids.isEmpty())
            room->throwCard(ids, objectName(), source, player);
        return false;
    }
};

// ---------------------------------------------------------------- luna048

class IkZhiwang : public TriggerSkillV2
{
public:
    IkZhiwang() : TriggerSkillV2("ikzhiwang")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // A 杀 at the owner or someone in range, whatever its legality.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        QList<ServerPlayer *> targets{player};
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->inMyAttackRange(p))
                targets << p;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@ikzhiwang");
        if (!target)
            return false;
        auto *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_" + objectName());
        CardUseStruct use(slash, player, target);
        use.m_addHistory = false;
        use.setOwnedCard(slash);
        room->useCardFromSkillEffect(use, ctx);
        return false;
    }
};

class IkLianlong : public DistanceSkill
{
public:
    IkLianlong() : DistanceSkill("iklianlong") {}

    int getCorrect(const Player *from, const Player *) const override
    {
        if (!from || !from->hasSkill(objectName()))
            return 0;
        return from->getHandcardNum() % 2 == 1 ? 1 : -1;
    }
};

// ---------------------------------------------------------------- luna049

class IkHuanxian : public TriggerSkillV2
{
public:
    IkHuanxian() : TriggerSkillV2("ikhuanxian") { events << CardAsked; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const QStringList asked = data.toStringList();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || asked.size() < 3
            || asked.first() != "jink" || asked.at(2) != "use")
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

    // Everyone else may hand over a card; if nobody does, a 闪 is used.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        bool given = false;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!player->isAlive())
                break;
            if (p->isNude())
                continue;
            const Card *card = room->askForCard(p, "..", "@ikhuanxian-give:" + player->objectName(), QVariant::fromValue(player),
                                                Card::MethodNone, player);
            if (!card)
                continue;
            room->obtainCard(player, card,
                             CardMoveReason(CardMoveReason::S_REASON_GIVE, p->objectName(), player->objectName(),
                                            objectName(), QString()),
                             false);
            given = true;
        }
        if (given)
            return false;
        Jink *jink = new Jink(Card::NoSuit, 0);
        jink->setSkillName("_" + objectName());
        room->provide(jink);
        return true;
    }
};

// Cards that reached the discard pile during the owner's discard phase.
class IkWuyu : public TriggerSkillV2
{
public:
    IkWuyu() : TriggerSkillV2("ikwuyu")
    {
        events << CardsMoveOneTime << EventPhaseEnd << EventPhaseChanging;
        global = true;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (event == EventPhaseChanging) {
            if (player && player->getTag("IkWuyu").isValid())
                player->removeTag("IkWuyu");
            return false;
        }
        if (event != CardsMoveOneTime || !current || player != current || current->getPhase() != Player::Discard
            || !current->hasSkill(objectName()))
            return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile)
            return false;
        QVariantList list = current->getTag("IkWuyu").toList();
        foreach (int id, move.card_ids)
            if (!list.contains(id))
                list << id;
        current->setTag("IkWuyu", list);
        return false;
    }

    static QList<int> available(Room *room, const Player *owner)
    {
        QList<int> ids;
        foreach (int id, ListV2I(owner->getTag("IkWuyu").toList()))
            if (room->getCardPlace(id) == Player::DiscardPile)
                ids << id;
        return ids;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Discard
            || !player->hasSkill(objectName()) || available(room, player).isEmpty())
            return result;
        foreach (ServerPlayer *p, room->getAllPlayers())
            if (player->inMyAttackRange(p))
                result[p] << objectName();
        return result;
    }

    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QList<int> ids = available(room, player);
        if (ids.isEmpty() || !ctx.owner->askForSkillInvoke("ikwuyu_get", QVariant::fromValue(player)))
            return false;
        room->fillAG(ids, ctx.owner);
        const int id = room->askForAG(ctx.owner, ids, true, objectName());
        room->clearAG(ctx.owner);
        if (id == -1)
            return false;
        ctx.extra_data = id;
        room->broadcastSkillInvoke(objectName());
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = ctx.owner;
        log.to << player;
        log.arg = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (room->getCardPlace(id) != Player::DiscardPile)
            return false;
        const bool red = Sanguosha->getCard(id)->isRed();
        room->obtainCard(ctx.owner, id);
        if (!red || !player->isAlive())
            return false;
        QStringList choices;
        if (player->isWounded())
            choices << "recover";
        choices << "draw";
        if (room->askForChoice(player, objectName(), choices.join("+")) == "recover")
            room->recover(player, RecoverStruct(objectName(), ctx.owner));
        else
            player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- luna051

class IkKezhan : public TriggerSkillV2
{
public:
    IkKezhan() : TriggerSkillV2("ikkezhan")
    {
        events << PreDamageDone << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != PreDamageDone)
            return false;
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (from && isOwnTurn(from) && from->getMark("ikkezhan_dealt-Clear") == 0)
            room->setPlayerMark(from, "ikkezhan_dealt-Clear", 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // One draw per condition met; none met costs a point of HP.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        int n = player->getMark("ikkezhan_dealt-Clear") > 0 ? 1 : 0;
        if (!player->isKongcheng()) {
            const Card *card = room->askForCard(player, "Slash|black|.|hand", "@ikkezhan", QVariant(), Card::MethodNone);
            if (card) {
                room->showCard(player, card->getEffectiveId());
                ++n;
            }
        }
        foreach (ServerPlayer *p, room->getPlayers())
            if (p->isDead() && p->getRole() == "renegade") {
                ++n;
                break;
            }
        if (n > 0)
            player->drawCards(n, objectName());
        else
            room->loseHp(player, 1, true, player, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- luna053

// Per-turn damage counts: dealt in one's own turn, and suffered from each source.
class IkDamageCount : public TriggerSkillV2
{
public:
    IkDamageCount() : TriggerSkillV2("#ikai-damagecount")
    {
        events << DamageCaused << DamageInflicted;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from)
            return false;
        if (event == DamageCaused && player == damage.from && isOwnTurn(damage.from))
            room->addPlayerMark(damage.from, "ikhuwu_count-Clear");
        else if (event == DamageInflicted && player == damage.to)
            room->addPlayerMark(damage.from, "ikmosu_count-Clear");
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class IkHuwu : public TriggerSkillV2
{
public:
    IkHuwu() : TriggerSkillV2("ikhuwu") { events << DamageCaused; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || data.value<DamageStruct>().from != player || !isOwnTurn(player)
            || player->getMark("ikhuwu_count-Clear") != 2)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->canDiscard(owner, "he"))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, "..", "@ikhuwu:" + player->objectName(), *ctx.original_data, objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        if (player->isAlive())
            player->drawCards(2, objectName());
        return false;
    }
};

class IkMosu : public TriggerSkillV2
{
public:
    IkMosu() : TriggerSkillV2("ikmosu") { events << DamageInflicted; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.from
            || !room->getCurrent() || player->getMark("ikmosu_used-Clear") > 0
            || damage.from->getMark("ikmosu_count-Clear") == 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->setPlayerMark(player, "ikmosu_used-Clear", 1);
        return true;
    }

    // The damage is prevented; the owner heals and both sides draw one.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        if (player->isWounded())
            room->recover(player, RecoverStruct(objectName(), player));
        QList<ServerPlayer *> drawers{player};
        if (from && from->isAlive() && from != player)
            drawers << from;
        room->sortByActionOrder(drawers);
        foreach (ServerPlayer *p, drawers)
            if (p->isAlive())
                p->drawCards(1, objectName());
        return true;
    }
};

// ---------------------------------------------------------------- luna054

class IkSuyi : public ViewAsSkillV2
{
public:
    IkSuyi() : ViewAsSkillV2("iksuyi") { setPhaseName("Play"); }

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
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkSuyiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        QStringList choices{"draw"};
        if (source->canDiscard(target, "h"))
            choices << "discard";
        if (room->askForChoice(source, objectName(), choices.join("+"), QVariant::fromValue(target)) == "discard") {
            const int id = room->askForCardChosen(source, target, "h", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, target, source);
        } else {
            target->drawCards(1, objectName());
        }
        return ContinueEffects;
    }
};

class IkYihui : public TriggerSkillV2
{
public:
    IkYihui() : TriggerSkillV2("ikyihui") { events << Death; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DeathStruct>().who != player || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@ikyihui",
                                                        true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return false;
        grantTracked(room, target, "IkYihuiSkills", "iksuyi");
        grantTracked(room, target, "IkYihuiSkills", "ikyihui");
        return false;
    }
};

// ---------------------------------------------------------------- luna055

class IkQianshe : public ViewAsSkillV2
{
public:
    IkQianshe() : ViewAsSkillV2("ikqianshe") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsHandCard(request.initiator, card) && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return selectionValid(this, request); }

    bool willThrowSelectedCards() const override { return false; }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkQiansheCard"; }

    // Both sides discard at once; discarding more deals a point of damage.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !ctx.use_card)
            return ContinueEffects;
        Room *room = source->getRoom();
        QList<int> mine;
        foreach (int id, ctx.use_card->getSubcards())
            if (room->getCardOwner(id) == source && room->getCardPlace(id) == Player::PlaceHand)
                mine << id;
        QList<int> theirs;
        if (target->canDiscard(target, "h"))
            if (const Card *chosen = room->askForExchange(target, objectName(), 998, 1, false, "@ikqianshe-discard", true)) {
                theirs = chosen->getSubcards();
                delete chosen;
            }
        QList<CardsMoveStruct> moves;
        if (!mine.isEmpty())
            moves << CardsMoveStruct(mine, source, nullptr, Player::PlaceHand, Player::DiscardPile,
                                     CardMoveReason(CardMoveReason::S_REASON_THROW, source->objectName(), objectName(),
                                                    QString()));
        if (!theirs.isEmpty())
            moves << CardsMoveStruct(theirs, target, nullptr, Player::PlaceHand, Player::DiscardPile,
                                     CardMoveReason(CardMoveReason::S_REASON_THROW, target->objectName(), objectName(),
                                                    QString()));
        foreach (const CardsMoveStruct &move, moves) {
            LogMessage log;
            log.type = "$DiscardCard";
            log.from = qobject_cast<ServerPlayer *>(move.from);
            log.card_str = ListI2S(move.card_ids).join("+");
            room->sendLog(log);
        }
        if (!moves.isEmpty())
            room->moveCardsAtomic(moves, true);
        if (mine.length() > theirs.length() && target->isAlive())
            room->damage(DamageStruct(objectName(), source, target));
        return ContinueEffects;
    }
};

class IkDaolei : public ViewAsSkillV2
{
public:
    IkDaolei() : ViewAsSkillV2("ikdaolei", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->getSuit() == Card::Spade;
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
        return request.initiator && selected.isEmpty() && to && to != request.initiator
            && to->getHandcardNum() < request.initiator->getHandcardNum();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkDaoleiCard"; }

    // After the spade changes hands, some of the target's cards are shown at random; a spade among
    // them hands the user every unshown card.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int given = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(given) != source)
            return ContinueEffects;
        room->giveCard(source, target, QList<int>() << given, objectName(), true);
        if (!target->isAlive() || target->isKongcheng() || !source->isAlive())
            return ContinueEffects;
        QStringList counts;
        for (int i = 1; i <= target->getHandcardNum(); ++i)
            counts << QString::number(i);
        const int n = room->askForChoice(source, "ikdaolei_show", counts.join("+"), QVariant::fromValue(target)).toInt();
        QList<int> hand = target->handCards();
        QList<int> shown;
        while (shown.length() < qMax(1, n) && !hand.isEmpty())
            shown << hand.takeAt(QRandomGenerator::global()->bounded(hand.length()));
        room->showCard(target, shown);
        bool spade = false;
        foreach (int id, shown)
            if (Sanguosha->getCard(id)->getSuit() == Card::Spade)
                spade = true;
        if (spade && !hand.isEmpty()) {
            DummyCard dummy(hand);
            room->obtainCard(source, &dummy, false);
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- luna056

class IkYuanshou : public TriggerSkillV2
{
public:
    IkYuanshou() : TriggerSkillV2("ikyuanshou") { events << EventPhaseStart << TargetConfirming; }

    static QString protectMark(const Player *owner) { return "ikyuanshou_" + owner->objectName(); }

    // The protection lasts until the owner's next turn begins.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::RoundStart
            || !player->getTag("IkYuanshouTarget").isValid())
            return false;
        ServerPlayer *target = room->findPlayerByObjectName(player->getTag("IkYuanshouTarget").toString(), true);
        player->removeTag("IkYuanshouTarget");
        QList<ServerPlayer *> protectedOnes{player};
        if (target && target != player)
            protectedOnes << target;
        foreach (ServerPlayer *p, protectedOnes) {
            if (p->getMark(protectMark(player)) == 0)
                continue;
            room->setPlayerMark(p, protectMark(player), 0);
            room->removePlayerMark(p, "@protect");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == EventPhaseStart) {
            if (player->isAlive() && player->getPhase() == Player::Start && player->hasSkill(objectName()))
                result[player] << objectName();
            return result;
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash") || !use.to.contains(player)
            || use.nullified_list.contains("_ALL_TARGETS"))
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && player->getMark(protectMark(owner)) > 0)
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@ikyuanshou",
                                                            true, true);
            if (!target)
                return false;
            room->broadcastSkillInvoke(objectName());
            ctx.extra_data = target->objectName();
            return true;
        }
        if (ctx.original_data->value<CardUseStruct>().nullified_list.contains("_ALL_TARGETS")
            || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == EventPhaseStart) {
            ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
            owner->setTag("IkYuanshouTarget", target ? target->objectName() : owner->objectName());
            QList<ServerPlayer *> protectedOnes{owner};
            if (target && target != owner)
                protectedOnes << target;
            foreach (ServerPlayer *p, protectedOnes) {
                room->setPlayerMark(p, protectMark(owner), 1);
                room->addPlayerMark(p, "@protect");
            }
            return false;
        }
        if (!owner->canDiscard(owner, "he")
            || !room->askForCard(owner, "..", "@ikyuanshou-discard", QVariant(), Card::MethodDiscard))
            room->loseHp(owner, 1, true, owner, objectName());
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains("_ALL_TARGETS"))
            use.nullified_list << "_ALL_TARGETS";
        *ctx.original_data = QVariant::fromValue(use);
        if (!use.from || !use.from->isAlive() || use.from == owner || !owner->isAlive())
            return false;
        auto *duel = new Duel(Card::NoSuit, 0);
        duel->setSkillName("_" + objectName());
        if (owner->isProhibited(use.from, duel) || owner->isCardLimited(duel, Card::MethodUse)) {
            delete duel;
            return false;
        }
        CardUseStruct duelUse(duel, owner, use.from);
        duelUse.m_addHistory = false;
        duelUse.setOwnedCard(duel);
        room->useCardFromSkillEffect(duelUse, ctx);
        return false;
    }
};

// ---------------------------------------------------------------- luna057

class IkZhuxue : public TriggerSkillV2
{
public:
    IkZhuxue() : TriggerSkillV2("ikzhuxue")
    {
        events << BeforeCardsMove;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile)
            return TriggerList();
        QStringList times;
        foreach (int id, move.card_ids)
            if (Sanguosha->getCard(id)->isKindOf("Peach"))
                times << objectName();
        return times.isEmpty() ? TriggerList() : TriggerList{{player, times}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName(), false);
        player->gainMark("@blood");
        return false;
    }
};

// The "赫" count unlocks skills at 1, 4 and 7; a reshuffle of the draw pile wipes them.
class IkSheluo : public TriggerSkillV2
{
public:
    IkSheluo() : TriggerSkillV2("iksheluo")
    {
        events << MarkChanged << SwappedPile;
        frequency = Compulsory;
        markOwnerOnly(this);
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == SwappedPile)
            return player->getMark("@blood") > 0 || !player->getTag("IkSheluoSkills").toMap().isEmpty()
                ? TriggerList{{player, {objectName()}}}
                : TriggerList();
        const MarkStruct mark = data.value<MarkStruct>();
        const int n = player->getMark("@blood");
        if (mark.who != player || mark.name != "@blood" || mark.gain <= 0 || (n != 1 && n != 4 && n != 7))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        if (event == SwappedPile) {
            player->loseAllMarks("@blood");
            revokeTracked(room, player, "IkSheluoSkills");
            return false;
        }
        const int n = player->getMark("@blood");
        const QString name = n == 1 ? QStringLiteral("chenqing") : (n == 4 ? QStringLiteral("ikyuanjie")
                                                                               : QStringLiteral("ikhunkao"));
        if (!Sanguosha->getSkill(name) || player->hasSkill(name, true))
            return false;
        grantTracked(room, player, "IkSheluoSkills", name);
        if (player->getMark("ikzhonggui") == 0 || name == "chenqing")
            return false;
        QStringList choices;
        foreach (const Skill *skill, player->getVisibleSkillList())
            if (!isOwnerOnlySkill(skill->objectName()) && !choices.contains(skill->objectName()))
                choices << skill->objectName();
        if (choices.isEmpty())
            return false;
        const QString lost = room->askForChoice(player, objectName(), choices.join("+"));
        if (player->getTag("IkSheluoSkills").toMap().contains(lost))
            revokeTracked(room, player, "IkSheluoSkills", lost);
        else
            room->detachSkillFromPlayer(player, lost);
        return false;
    }
};

class IkZhonggui : public WakeSkill
{
public:
    IkZhonggui() : WakeSkill("ikzhonggui") { events << EventPhaseStart; }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *room, ServerPlayer *) const override { return room->getTag("SwapPile").toInt() > 0; }

    void awaken(Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->drawCards(2, objectName());
        if (player->isAlive())
            room->changeMaxHpForAwakenSkill(player, 1, objectName());
    }
};

// ---------------------------------------------------------------- luna058

class IkJuexiang : public TriggerSkillV2
{
public:
    IkJuexiang() : TriggerSkillV2("ikjuexiang") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (player->getPhase() == Player::RoundStart && player->getMark("ikjuexiang_pending") > 0)
            return TriggerList{{player, {objectName()}}};
        if (player->getPhase() != Player::Finish || !player->hasSkill(objectName()) || !player->canDiscard(player, "h"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (player->getPhase() == Player::RoundStart)
            return true;
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Throw the whole hand; non-杀 basics only buy an unrestricted extra turn.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->getPhase() == Player::RoundStart) {
            room->setPlayerMark(player, "ikjuexiang_pending", 0);
            room->setPlayerFlag(player, "IkJuexiang");
            player->skip(Player::Discard);
            player->skip(Player::Finish);
            return false;
        }
        const QList<const Card *> cards = player->getHandcards();
        QList<int> ids;
        foreach (const Card *card, cards)
            if (player->canDiscard(player, card->getEffectiveId()))
                ids << card->getEffectiveId();
        if (ids.isEmpty())
            return false;
        room->throwCard(ids, objectName(), player);
        if (ids.length() >= player->getHp() && player->isAlive())
            player->drawCards(1, objectName());
        bool allBasic = true;
        foreach (int id, ids) {
            const Card *card = Sanguosha->getCard(id);
            if (card->getTypeId() != Card::TypeBasic || card->isKindOf("Slash"))
                allBasic = false;
        }
        if (allBasic && player->isAlive()) {
            room->setPlayerMark(player, "ikjuexiang_pending", 1);
            room->scheduleExtraTurn(player, ctx.sourceRef);
        }
        return false;
    }
};

class IkJuexiangTargetMod : public TargetModSkillV2
{
public:
    IkJuexiangTargetMod() : TargetModSkillV2("#ikjuexiang-tar", ".") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasFlag("IkJuexiang"))
            return CorrectSkillResult::noEffect();
        if (ctx.modType == TargetModSkill::Residue)
            return CorrectSkillResult::unlimitedResidue();
        if (ctx.modType == TargetModSkill::DistanceLimit)
            return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

}

IkZhijuCard::IkZhijuCard() { setSkillName("ikzhiju"); mute = true; }
IkJilunCard::IkJilunCard() { setSkillName("ikjilun"); mute = true; }
IkHunkaoCard::IkHunkaoCard() { setSkillName("ikhunkao"); mute = true; }
IkHuangshiCard::IkHuangshiCard() { setSkillName("ikhuangshi"); mute = true; }
IkDongzhaoCard::IkDongzhaoCard() { setSkillName("ikdongzhao"); mute = true; }
IkJimuCard::IkJimuCard() { setSkillName("ikjimu"); mute = true; }
IkDengpoCard::IkDengpoCard() { setSkillName("ikdengpo"); mute = true; }
IkLingchaCard::IkLingchaCard() { setSkillName("iklingcha"); mute = true; }
IkDuanniCard::IkDuanniCard() { setSkillName("ikduanni"); mute = true; }
IkLinghuiCard::IkLinghuiCard() { setSkillName("iklinghui"); mute = true; }
IkMingwangCard::IkMingwangCard() { setSkillName("ikmingwang"); mute = true; }
IkXiaowuCard::IkXiaowuCard() { setSkillName("ikxiaowu"); mute = true; }
IkLihunCard::IkLihunCard() { setSkillName("iklihun"); mute = true; }
IkManwuCard::IkManwuCard() { setSkillName("ikmanwu"); mute = true; }
IkSuyiCard::IkSuyiCard() { setSkillName("iksuyi"); mute = true; }
IkQiansheCard::IkQiansheCard() { setSkillName("ikqianshe"); mute = true; }
IkDaoleiCard::IkDaoleiCard() { setSkillName("ikdaolei"); mute = true; }

IkaiKaPackage::IkaiKaPackage()
    : Package("ikai-ka")
{
    General *wind025 = new General(this, "wind025", "kaze", 3);
    wind025->addSkill(new IkZhiju);
    wind025->addSkill(new IkZhijuPut);
    related_skills.insert("ikzhiju", "#ikzhiju");
    wind025->addSkill(new IkYingqi);
    wind025->addSkill(new IkYingqiEffect);
    related_skills.insert("ikyingqi", "#ikyingqi");

    General *wind033 = new General(this, "wind033", "kaze");
    wind033->addSkill(new IkJilun);
    wind033->addSkill(new IkJilunRecord);
    related_skills.insert("ikjilun", "#ikjilun");

    General *wind034 = new General(this, "wind034", "kaze");
    wind034->addSkill(new IkJiqiao);

    General *wind035 = new General(this, "wind035", "kaze");
    wind035->addSkill(new IkKangjin);
    wind035->addSkill(new IkKangjinProhibit);
    related_skills.insert("ikkangjin", "#ikkangjin");
    wind035->addSkill(new IkYunjue);

    General *wind036 = new General(this, "wind036", "kaze");
    wind036->addSkill(new IkHunkao);

    General *wind045 = new General(this, "wind045", "kaze");
    wind045->addSkill(new IkHualan);

    General *wind047 = new General(this, "wind047", "kaze", 3);
    wind047->addSkill(new IkTianhua);
    wind047->addSkill(new IkHuangshi);

    General *wind048 = new General(this, "wind048", "kaze", 3);
    wind048->addSkill(new IkXizi);
    wind048->addSkill(new IkCiyuan);

    General *wind050 = new General(this, "wind050", "kaze", 4, false);
    wind050->addSkill(new IkDongzhao);
    wind050->addSkill(new IkDongzhaoEffect);
    related_skills.insert("ikdongzhao", "#ikdongzhao");

    General *wind051 = new General(this, "wind051", "kaze");
    wind051->addSkill(new IkElu);

    General *wind057 = new General(this, "wind057", "kaze");
    wind057->addSkill(new IkYinchou);

    General *bloom032 = new General(this, "bloom032", "hana");
    bloom032->addSkill(new IkFengxing);
    bloom032->addSkill(new IkFengxingDistance);
    related_skills.insert("ikfengxing", "#ikfengxing");

    General *bloom034 = new General(this, "bloom034", "hana");
    bloom034->addSkill(new IkQizhong);

    General *bloom035 = new General(this, "bloom035", "hana", 4, true, true);
    bloom035->addSkill(new IkDuduan);
    bloom035->addSkill(new IkDuduanMaxCards);
    related_skills.insert("ikduduan", "#ikduduan");

    General *bloom036 = new General(this, "bloom036", "hana");
    bloom036->addSkill(new IkJimu);
    bloom036->addSkill(new IkJimuTrigger);
    related_skills.insert("ikjimu", "#ikjimu");

    General *bloom045 = new General(this, "bloom045", "hana", 4, false);
    bloom045->addSkill(new IkDengpo);
    bloom045->addSkill(new IkDengpoDraw);
    related_skills.insert("ikdengpo", "#ikdengpo");

    General *bloom047 = new General(this, "bloom047", "hana", 3);
    bloom047->addSkill(new IkGuoshang);
    bloom047->addSkill(new IkZuiyan);
    bloom047->addSkill(new IkQihun);

    General *bloom048 = new General(this, "bloom048", "hana");
    bloom048->addSkill(new IkDiebei);
    bloom048->addSkill(new IkDiebeiMaxCards);
    related_skills.insert("ikdiebei", "#ikdiebei");

    General *bloom049 = new General(this, "bloom049", "hana", 3);
    bloom049->addSkill(new IkXunfeng);
    bloom049->addSkill(new IkLuhua);

    General *bloom052 = new General(this, "bloom052", "hana", 4, false);
    bloom052->addSkill(new IkLingcha);
    bloom052->addSkill(new IkLingchaDraw);
    related_skills.insert("iklingcha", "#iklingcha");

    General *bloom053 = new General(this, "bloom053", "hana");
    bloom053->addSkill(new IkMingshi);

    General *bloom054 = new General(this, "bloom054", "hana", 3);
    bloom054->addSkill(new IkDuanni);
    bloom054->addSkill(new IkMeitong);

    General *bloom056 = new General(this, "bloom056", "hana", 3);
    bloom056->addSkill(new IkSheji);
    bloom056->addSkill(new IkShejiExtra);
    related_skills.insert("iksheji", "#iksheji");
    bloom056->addSkill(new IkPingwei);

    General *snow031 = new General(this, "snow031", "yuki", 3);
    snow031->addSkill(new IkLingyun);
    snow031->addSkill(new IkMiyao);

    General *snow034 = new General(this, "snow034", "yuki");
    snow034->addSkill(new IkLixin);

    General *snow035 = new General(this, "snow035", "yuki");
    snow035->addSkill(new IkShenshu);
    snow035->addSkill(new IkMingwang);
    snow035->addSkill(new IkQiyi);

    // 迷途 is 无言.
    General *snow036 = new General(this, "snow036", "yuki", 3, false);
    snow036->addSkill("wuyan");
    snow036->addSkill(new IkLinghui);

    General *snow045 = new General(this, "snow045", "yuki");
    snow045->addSkill(new IkLvyan);
    snow045->addSkill(new IkLvyanDraw);
    related_skills.insert("iklvyan", "#iklvyan");
    snow045->addSkill(new IkWuming);

    General *snow046 = new General(this, "snow046", "yuki");
    snow046->addSkill(new IkLianxiao);

    General *snow048 = new General(this, "snow048", "yuki");
    snow048->addSkill(new IkQile);
    snow048->addSkill(new IkSaoxiao);

    General *snow049 = new General(this, "snow049", "yuki");
    snow049->addSkill(new IkXiaowu);
    snow049->addSkill(new IkXiaowuEffect);
    related_skills.insert("ikxiaowu", "#ikxiaowu");

    General *snow051 = new General(this, "snow051", "yuki", 3);
    snow051->addSkill(new IkWanmi);
    snow051->addSkill(new IkGuichan);

    General *snow052 = new General(this, "snow052", "yuki", 3, false);
    snow052->addSkill(new IkHuanlve);
    snow052->addSkill(new IkHuanlveDraw);
    related_skills.insert("ikhuanlve", "#ikhuanlve");
    snow052->addSkill(new IkMuguang);

    General *snow054 = new General(this, "snow054", "yuki");
    snow054->addSkill(new IkLihun);

    General *snow057 = new General(this, "snow057", "yuki", 3);
    snow057->addSkill(new IkTianzuo);
    snow057->addSkill(new IkYewu);

    General *luna030 = new General(this, "luna030", "tsuki");
    luna030->addSkill(new IkLingcu);

    General *luna031 = new General(this, "luna031", "tsuki", 3);
    luna031->addSkill(new IkQisi);
    luna031->addSkill(new IkQisiRecord);
    related_skills.insert("ikqisi", "#ikqisi");
    luna031->addSkill(new IkJilian);

    General *luna032 = new General(this, "luna032", "tsuki", 3, false);
    luna032->addSkill(new IkJichang);
    luna032->addSkill(new IkManwu);

    General *luna033 = new General(this, "luna033", "tsuki", 3);
    luna033->addSkill(new IkXianlv);

    General *luna036 = new General(this, "luna036", "tsuki", 4, true, true);
    luna036->addSkill(new IkLianwu);
    luna036->addSkill(new IkLianwuDistance);
    luna036->addSkill(new IkLianwuTargetMod);
    related_skills.insert("iklianwu", "#iklianwu-dist");
    related_skills.insert("iklianwu", "#iklianwu-tar");

    // 魇梦 is still pending.
    General *luna037 = new General(this, "luna037", "tsuki");
    luna037->addSkill(new IkMoshan);
    luna037->addSkill("thyanmeng");

    General *luna045 = new General(this, "luna045", "tsuki");
    luna045->addSkill(new IkXieke);
    luna045->addSkill(new IkXiekeChain);
    related_skills.insert("ikxieke", "#ikxieke");
    luna045->addSkill(new IkYunmai);

    General *luna046 = new General(this, "luna046", "tsuki", 3);
    luna046->addSkill(new IkLunyao);
    luna046->addSkill(new IkQimu);

    General *luna047 = new General(this, "luna047", "tsuki");
    luna047->addSkill(new IkYuanji);
    luna047->addSkill(new IkShuluo);

    General *luna048 = new General(this, "luna048", "tsuki");
    luna048->addSkill(new IkZhiwang);
    luna048->addSkill(new IkLianlong);

    General *luna049 = new General(this, "luna049", "tsuki", 3);
    luna049->addSkill(new IkHuanxian);
    luna049->addSkill(new IkWuyu);

    General *luna051 = new General(this, "luna051", "tsuki");
    luna051->addSkill(new IkKezhan);

    General *luna053 = new General(this, "luna053", "tsuki", 3, false);
    luna053->addSkill(new IkHuwu);
    luna053->addSkill(new IkMosu);

    General *luna054 = new General(this, "luna054", "tsuki");
    luna054->addSkill(new IkSuyi);
    luna054->addSkill(new IkYihui);

    General *luna055 = new General(this, "luna055", "tsuki", 3);
    luna055->addSkill(new IkQianshe);
    luna055->addSkill(new IkDaolei);

    General *luna056 = new General(this, "luna056", "tsuki");
    luna056->addSkill(new IkYuanshou);

    // 奢罗's 1st and 4th rewards (陈情, 缘结) belong to packages not ported yet.
    General *luna057 = new General(this, "luna057", "tsuki", 3);
    luna057->addSkill(new IkZhuxue);
    luna057->addSkill(new IkSheluo);
    luna057->addSkill(new IkZhonggui);

    General *luna058 = new General(this, "luna058", "tsuki");
    luna058->addSkill(new IkJuexiang);
    luna058->addSkill(new IkJuexiangTargetMod);
    related_skills.insert("ikjuexiang", "#ikjuexiang-tar");

    skills << new IkPlayCount << new IkQihunViewAs << new IkPingweiRecord << new IkDiscardRecord << new IkYuanjiTargetMod << new IkDamageCount;

    addMetaObject<IkZhijuCard>();
    addMetaObject<IkJilunCard>();
    addMetaObject<IkHunkaoCard>();
    addMetaObject<IkHuangshiCard>();
    addMetaObject<IkDongzhaoCard>();
    addMetaObject<IkJimuCard>();
    addMetaObject<IkDengpoCard>();
    addMetaObject<IkLingchaCard>();
    addMetaObject<IkDuanniCard>();
    addMetaObject<IkLinghuiCard>();
    addMetaObject<IkMingwangCard>();
    addMetaObject<IkXiaowuCard>();
    addMetaObject<IkLihunCard>();
    addMetaObject<IkManwuCard>();
    addMetaObject<IkSuyiCard>();
    addMetaObject<IkQiansheCard>();
    addMetaObject<IkDaoleiCard>();
}

ADD_PACKAGE(IkaiKa)

// Xufa notification metadata: exact compiled dynamic types and declaring slots.
// No correction-family bit or event/activation execution authority is granted.
#include "ai-strategy/native-registry.h"

namespace {
template<class Owner> Owner *xufaExtraOwner(int (Owner::*)(const Player *) const);
template<class Owner> Owner *xufaExtraModeOwner(int (Owner::*)(const Player *, MaxCardsType::MaxCardsCount) const);
template<class Owner> Owner *xufaLimitOneOwner(QString (Owner::*)(const Player *) const);
template<class Owner> Owner *xufaLimitTwoOwner(QString (Owner::*)(const Player *, const Card *) const);
}
void registerAINativeTypes_src_package_ikai_ka_cpp(AINativeTypes &types) {
 types.emplace(std::type_index(typeid(IkZhiju)), 2u);
 types.recordActivateSource<IkZhiju>(4262u); AI_NATIVE_REQUIRE_DECLARATION(IkZhiju, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhiju, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhiju, getLimitScope, IkZhiju); AI_NATIVE_REQUIRE_DECLARATION(IkZhiju, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhiju, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhiju, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkZhijuPut)), 2u);
 types.recordActivateSource<IkZhijuPut>(4263u); AI_NATIVE_REQUIRE_DECLARATION(IkZhijuPut, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhijuPut, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhijuPut, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhijuPut, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhijuPut, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhijuPut, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkYingqi)), 2u);
 types.recordActivateSource<IkYingqi>(4264u); AI_NATIVE_REQUIRE_DECLARATION(IkYingqi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqi, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkYingqiEffect)), 2u);
 types.recordActivateSource<IkYingqiEffect>(4265u); AI_NATIVE_REQUIRE_DECLARATION(IkYingqiEffect, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqiEffect, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqiEffect, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqiEffect, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqiEffect, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYingqiEffect, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkJilun)), 2u);
 types.recordActivateSource<IkJilun>(4266u); AI_NATIVE_REQUIRE_DECLARATION(IkJilun, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilun, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilun, getLimitScope, IkJilun); AI_NATIVE_REQUIRE_DECLARATION(IkJilun, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilun, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilun, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkJilunRecord)), 2u);
 types.recordActivateSource<IkJilunRecord>(4267u); AI_NATIVE_REQUIRE_DECLARATION(IkJilunRecord, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilunRecord, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilunRecord, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilunRecord, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilunRecord, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilunRecord, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkJiqiao)), 2u);
 types.recordActivateSource<IkJiqiao>(4268u); AI_NATIVE_REQUIRE_DECLARATION(IkJiqiao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJiqiao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJiqiao, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJiqiao, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJiqiao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJiqiao, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkKangjin)), 2u);
 types.recordActivateSource<IkKangjin>(4269u); AI_NATIVE_REQUIRE_DECLARATION(IkKangjin, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKangjin, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKangjin, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKangjin, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKangjin, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKangjin, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkKangjinProhibit)), 3u); AI_NATIVE_REQUIRE_DECLARATION(IkKangjinProhibit, isProhibited, IkKangjinProhibit);
 types.recordActivateSource<IkKangjinProhibit>(4270u); AI_NATIVE_REQUIRE_DECLARATION(IkKangjinProhibit, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKangjinProhibit, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKangjinProhibit, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKangjinProhibit, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKangjinProhibit, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(IkYunjue)), 2u);
 types.recordActivateSource<IkYunjue>(4271u); AI_NATIVE_REQUIRE_DECLARATION(IkYunjue, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunjue, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunjue, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunjue, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunjue, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunjue, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkHunkao)), 2u);
 types.recordActivateSource<IkHunkao>(4272u); AI_NATIVE_REQUIRE_DECLARATION(IkHunkao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHunkao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHunkao, getLimitScope, IkHunkao); AI_NATIVE_REQUIRE_DECLARATION(IkHunkao, getMaxUsageLimit, IkHunkao); AI_NATIVE_REQUIRE_DECLARATION(IkHunkao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHunkao, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkHualan)), 2u);
 types.recordActivateSource<IkHualan>(4273u); AI_NATIVE_REQUIRE_DECLARATION(IkHualan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHualan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHualan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHualan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHualan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHualan, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkTianhua)), 2u);
 types.recordActivateSource<IkTianhua>(4274u); AI_NATIVE_REQUIRE_DECLARATION(IkTianhua, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianhua, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianhua, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianhua, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianhua, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianhua, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkHuangshiViewAs)), 2u);
 types.recordActivateSource<IkHuangshiViewAs>(4275u); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshiViewAs, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshiViewAs, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshiViewAs, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshiViewAs, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshiViewAs, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshiViewAs, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkHuangshi)), 2u);
 types.recordActivateSource<IkHuangshi>(4276u); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuangshi, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkXizi)), 2u);
 types.recordActivateSource<IkXizi>(4277u); AI_NATIVE_REQUIRE_DECLARATION(IkXizi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXizi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXizi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXizi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXizi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXizi, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkCiyuan)), 2u);
 types.recordActivateSource<IkCiyuan>(4278u); AI_NATIVE_REQUIRE_DECLARATION(IkCiyuan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkCiyuan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkCiyuan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkCiyuan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkCiyuan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkCiyuan, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkDongzhao)), 2u);
 types.recordActivateSource<IkDongzhao>(4279u); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhao, getLimitScope, IkDongzhao); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhao, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhao, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkDongzhaoEffect)), 2u);
 types.recordActivateSource<IkDongzhaoEffect>(4280u); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhaoEffect, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhaoEffect, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhaoEffect, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhaoEffect, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhaoEffect, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDongzhaoEffect, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkElu)), 2u);
 types.recordActivateSource<IkElu>(4281u); AI_NATIVE_REQUIRE_DECLARATION(IkElu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkElu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkElu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkElu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkElu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkElu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkYinchou)), 2u);
 types.recordActivateSource<IkYinchou>(4282u); AI_NATIVE_REQUIRE_DECLARATION(IkYinchou, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYinchou, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYinchou, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYinchou, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYinchou, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYinchou, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkFengxing)), 2u);
 types.recordActivateSource<IkFengxing>(4283u); AI_NATIVE_REQUIRE_DECLARATION(IkFengxing, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkFengxing, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkFengxing, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkFengxing, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkFengxing, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkFengxing, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkFengxingDistance)), 3u); AI_NATIVE_REQUIRE_DECLARATION(IkFengxingDistance, getCorrect, IkFengxingDistance); AI_NATIVE_REQUIRE_DECLARATION(IkFengxingDistance, getFixed, DistanceSkill);
 types.recordActivateSource<IkFengxingDistance>(4284u); AI_NATIVE_REQUIRE_DECLARATION(IkFengxingDistance, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkFengxingDistance, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkFengxingDistance, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkFengxingDistance, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkFengxingDistance, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(IkQizhong)), 2u);
 types.recordActivateSource<IkQizhong>(4285u); AI_NATIVE_REQUIRE_DECLARATION(IkQizhong, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQizhong, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQizhong, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQizhong, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQizhong, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQizhong, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkDuduan)), 2u);
 types.recordActivateSource<IkDuduan>(4286u); AI_NATIVE_REQUIRE_DECLARATION(IkDuduan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduan, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkDuduanMaxCards)), 3u); static_assert(std::is_same<decltype(xufaExtraOwner(&IkDuduanMaxCards::getExtra)), MaxCardsSkill*>::value, "Xufa getExtra owner changed"); static_assert(std::is_same<decltype(xufaExtraModeOwner(&IkDuduanMaxCards::getExtra)), MaxCardsSkill*>::value, "Xufa getExtra mode owner changed"); AI_NATIVE_REQUIRE_DECLARATION(IkDuduanMaxCards, getFixed, MaxCardsSkill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduanMaxCards, getCorrection, IkDuduanMaxCards); AI_NATIVE_REQUIRE_DECLARATION(IkDuduanMaxCards, getFixedValue, MaxCardsSkillV2);
 types.recordActivateSource<IkDuduanMaxCards>(4287u); AI_NATIVE_REQUIRE_DECLARATION(IkDuduanMaxCards, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduanMaxCards, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduanMaxCards, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduanMaxCards, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduanMaxCards, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuduanMaxCards, getBaseAmount, MaxCardsSkillV2);
 types.emplace(std::type_index(typeid(IkJimu)), 2u);
 types.recordActivateSource<IkJimu>(4288u); AI_NATIVE_REQUIRE_DECLARATION(IkJimu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJimu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJimu, getLimitScope, IkJimu); AI_NATIVE_REQUIRE_DECLARATION(IkJimu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJimu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJimu, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkJimuTrigger)), 2u);
 types.recordActivateSource<IkJimuTrigger>(4289u); AI_NATIVE_REQUIRE_DECLARATION(IkJimuTrigger, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJimuTrigger, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJimuTrigger, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJimuTrigger, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJimuTrigger, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJimuTrigger, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkPlayCount)), 2u);
 types.recordActivateSource<IkPlayCount>(4290u); AI_NATIVE_REQUIRE_DECLARATION(IkPlayCount, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPlayCount, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPlayCount, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPlayCount, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPlayCount, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPlayCount, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkDengpo)), 2u);
 types.recordActivateSource<IkDengpo>(4291u); AI_NATIVE_REQUIRE_DECLARATION(IkDengpo, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDengpo, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDengpo, getLimitScope, IkDengpo); AI_NATIVE_REQUIRE_DECLARATION(IkDengpo, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDengpo, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDengpo, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkDengpoDraw)), 2u);
 types.recordActivateSource<IkDengpoDraw>(4292u); AI_NATIVE_REQUIRE_DECLARATION(IkDengpoDraw, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDengpoDraw, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDengpoDraw, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDengpoDraw, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDengpoDraw, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDengpoDraw, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkGuoshang)), 2u);
 types.recordActivateSource<IkGuoshang>(4293u); AI_NATIVE_REQUIRE_DECLARATION(IkGuoshang, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuoshang, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuoshang, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuoshang, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuoshang, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuoshang, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkZuiyan)), 2u);
 types.recordActivateSource<IkZuiyan>(4294u); AI_NATIVE_REQUIRE_DECLARATION(IkZuiyan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZuiyan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZuiyan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZuiyan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZuiyan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZuiyan, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkQihunViewAs)), 2u);
 types.recordActivateSource<IkQihunViewAs>(4295u); AI_NATIVE_REQUIRE_DECLARATION(IkQihunViewAs, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihunViewAs, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihunViewAs, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihunViewAs, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihunViewAs, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihunViewAs, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkQihun)), 2u);
 types.recordActivateSource<IkQihun>(4296u); AI_NATIVE_REQUIRE_DECLARATION(IkQihun, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihun, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihun, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihun, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihun, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQihun, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkDiebei)), 2u);
 types.recordActivateSource<IkDiebei>(4297u); AI_NATIVE_REQUIRE_DECLARATION(IkDiebei, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebei, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebei, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebei, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebei, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebei, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkDiebeiMaxCards)), 3u); static_assert(std::is_same<decltype(xufaExtraOwner(&IkDiebeiMaxCards::getExtra)), MaxCardsSkill*>::value, "Xufa getExtra owner changed"); static_assert(std::is_same<decltype(xufaExtraModeOwner(&IkDiebeiMaxCards::getExtra)), MaxCardsSkill*>::value, "Xufa getExtra mode owner changed"); AI_NATIVE_REQUIRE_DECLARATION(IkDiebeiMaxCards, getFixed, MaxCardsSkill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebeiMaxCards, getCorrection, IkDiebeiMaxCards); AI_NATIVE_REQUIRE_DECLARATION(IkDiebeiMaxCards, getFixedValue, MaxCardsSkillV2);
 types.recordActivateSource<IkDiebeiMaxCards>(4298u); AI_NATIVE_REQUIRE_DECLARATION(IkDiebeiMaxCards, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebeiMaxCards, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebeiMaxCards, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebeiMaxCards, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebeiMaxCards, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiebeiMaxCards, getBaseAmount, MaxCardsSkillV2);
 types.emplace(std::type_index(typeid(IkXunfeng)), 2u);
 types.recordActivateSource<IkXunfeng>(4299u); AI_NATIVE_REQUIRE_DECLARATION(IkXunfeng, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXunfeng, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXunfeng, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXunfeng, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXunfeng, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXunfeng, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLuhua)), 2u);
 types.recordActivateSource<IkLuhua>(4300u); AI_NATIVE_REQUIRE_DECLARATION(IkLuhua, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLuhua, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLuhua, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLuhua, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLuhua, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLuhua, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLingcha)), 2u);
 types.recordActivateSource<IkLingcha>(4301u); AI_NATIVE_REQUIRE_DECLARATION(IkLingcha, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingcha, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingcha, getLimitScope, IkLingcha); AI_NATIVE_REQUIRE_DECLARATION(IkLingcha, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingcha, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingcha, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkLingchaDraw)), 2u);
 types.recordActivateSource<IkLingchaDraw>(4302u); AI_NATIVE_REQUIRE_DECLARATION(IkLingchaDraw, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingchaDraw, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingchaDraw, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingchaDraw, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingchaDraw, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingchaDraw, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkMingshi)), 2u);
 types.recordActivateSource<IkMingshi>(4303u); AI_NATIVE_REQUIRE_DECLARATION(IkMingshi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMingshi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMingshi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMingshi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMingshi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMingshi, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkDuanni)), 2u);
 types.recordActivateSource<IkDuanni>(4304u); AI_NATIVE_REQUIRE_DECLARATION(IkDuanni, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuanni, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuanni, getLimitScope, IkDuanni); AI_NATIVE_REQUIRE_DECLARATION(IkDuanni, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuanni, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDuanni, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkMeitong)), 2u);
 types.recordActivateSource<IkMeitong>(4305u); AI_NATIVE_REQUIRE_DECLARATION(IkMeitong, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeitong, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeitong, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeitong, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeitong, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeitong, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkSheji)), 2u);
 types.recordActivateSource<IkSheji>(4306u); AI_NATIVE_REQUIRE_DECLARATION(IkSheji, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheji, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheji, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheji, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheji, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheji, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkShejiExtra)), 2u);
 types.recordActivateSource<IkShejiExtra>(4307u); AI_NATIVE_REQUIRE_DECLARATION(IkShejiExtra, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShejiExtra, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShejiExtra, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShejiExtra, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShejiExtra, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShejiExtra, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkPingweiRecord)), 2u);
 types.recordActivateSource<IkPingweiRecord>(4308u); AI_NATIVE_REQUIRE_DECLARATION(IkPingweiRecord, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingweiRecord, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingweiRecord, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingweiRecord, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingweiRecord, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingweiRecord, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkPingwei)), 2u);
 types.recordActivateSource<IkPingwei>(4309u); AI_NATIVE_REQUIRE_DECLARATION(IkPingwei, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingwei, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingwei, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingwei, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingwei, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkPingwei, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLingyun)), 2u);
 types.recordActivateSource<IkLingyun>(4310u); AI_NATIVE_REQUIRE_DECLARATION(IkLingyun, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingyun, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingyun, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingyun, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingyun, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingyun, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkMiyao)), 2u);
 types.recordActivateSource<IkMiyao>(4311u); AI_NATIVE_REQUIRE_DECLARATION(IkMiyao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMiyao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMiyao, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMiyao, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMiyao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMiyao, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLixin)), 2u);
 types.recordActivateSource<IkLixin>(4312u); AI_NATIVE_REQUIRE_DECLARATION(IkLixin, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLixin, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLixin, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLixin, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLixin, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLixin, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkShenshu)), 2u);
 types.recordActivateSource<IkShenshu>(4313u); AI_NATIVE_REQUIRE_DECLARATION(IkShenshu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShenshu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShenshu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShenshu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShenshu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShenshu, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkMingwang)), 2u);
 types.recordActivateSource<IkMingwang>(4314u); AI_NATIVE_REQUIRE_DECLARATION(IkMingwang, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMingwang, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMingwang, getLimitScope, IkMingwang); AI_NATIVE_REQUIRE_DECLARATION(IkMingwang, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMingwang, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMingwang, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkQiyi)), 2u);
 types.recordActivateSource<IkQiyi>(4315u); AI_NATIVE_REQUIRE_DECLARATION(IkQiyi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQiyi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQiyi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQiyi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQiyi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQiyi, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLinghui)), 2u);
 types.recordActivateSource<IkLinghui>(4316u); AI_NATIVE_REQUIRE_DECLARATION(IkLinghui, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLinghui, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLinghui, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLinghui, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLinghui, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLinghui, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkLvyan)), 2u);
 types.recordActivateSource<IkLvyan>(4317u); AI_NATIVE_REQUIRE_DECLARATION(IkLvyan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyan, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkLvyanDraw)), 2u);
 types.recordActivateSource<IkLvyanDraw>(4318u); AI_NATIVE_REQUIRE_DECLARATION(IkLvyanDraw, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyanDraw, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyanDraw, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyanDraw, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyanDraw, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLvyanDraw, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkWuming)), 2u);
 types.recordActivateSource<IkWuming>(4319u); AI_NATIVE_REQUIRE_DECLARATION(IkWuming, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuming, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuming, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuming, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuming, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuming, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLianxiao)), 2u);
 types.recordActivateSource<IkLianxiao>(4320u); AI_NATIVE_REQUIRE_DECLARATION(IkLianxiao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianxiao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianxiao, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianxiao, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianxiao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianxiao, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkQile)), 2u);
 types.recordActivateSource<IkQile>(4321u); AI_NATIVE_REQUIRE_DECLARATION(IkQile, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQile, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQile, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQile, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQile, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQile, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkSaoxiao)), 2u);
 types.recordActivateSource<IkSaoxiao>(4322u); AI_NATIVE_REQUIRE_DECLARATION(IkSaoxiao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSaoxiao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSaoxiao, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSaoxiao, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSaoxiao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSaoxiao, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkXiaowu)), 2u);
 types.recordActivateSource<IkXiaowu>(4323u); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowu, getLimitScope, IkXiaowu); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowu, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkXiaowuEffect)), 2u);
 types.recordActivateSource<IkXiaowuEffect>(4324u); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowuEffect, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowuEffect, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowuEffect, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowuEffect, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowuEffect, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiaowuEffect, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkWanmi)), 2u);
 types.recordActivateSource<IkWanmi>(4325u); AI_NATIVE_REQUIRE_DECLARATION(IkWanmi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWanmi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWanmi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWanmi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWanmi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWanmi, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkGuichan)), 2u);
 types.recordActivateSource<IkGuichan>(4326u); AI_NATIVE_REQUIRE_DECLARATION(IkGuichan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuichan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuichan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuichan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuichan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkGuichan, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkHuanlve)), 2u);
 types.recordActivateSource<IkHuanlve>(4327u); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlve, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlve, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlve, getLimitScope, IkHuanlve); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlve, getMaxUsageLimit, IkHuanlve); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlve, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlve, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkHuanlveDraw)), 2u);
 types.recordActivateSource<IkHuanlveDraw>(4328u); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlveDraw, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlveDraw, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlveDraw, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlveDraw, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlveDraw, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanlveDraw, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkMuguang)), 2u);
 types.recordActivateSource<IkMuguang>(4329u); AI_NATIVE_REQUIRE_DECLARATION(IkMuguang, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMuguang, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMuguang, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMuguang, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMuguang, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMuguang, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLihun)), 2u);
 types.recordActivateSource<IkLihun>(4330u); AI_NATIVE_REQUIRE_DECLARATION(IkLihun, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLihun, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLihun, getLimitScope, IkLihun); AI_NATIVE_REQUIRE_DECLARATION(IkLihun, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLihun, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLihun, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkDiscardRecord)), 2u);
 types.recordActivateSource<IkDiscardRecord>(4331u); AI_NATIVE_REQUIRE_DECLARATION(IkDiscardRecord, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiscardRecord, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiscardRecord, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiscardRecord, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiscardRecord, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDiscardRecord, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkTianzuo)), 2u);
 types.recordActivateSource<IkTianzuo>(4332u); AI_NATIVE_REQUIRE_DECLARATION(IkTianzuo, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianzuo, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianzuo, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianzuo, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianzuo, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkTianzuo, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkYewu)), 2u);
 types.recordActivateSource<IkYewu>(4333u); AI_NATIVE_REQUIRE_DECLARATION(IkYewu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYewu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYewu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYewu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYewu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYewu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLingcu)), 2u);
 types.recordActivateSource<IkLingcu>(4334u); AI_NATIVE_REQUIRE_DECLARATION(IkLingcu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingcu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingcu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingcu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingcu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLingcu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkQisiRecord)), 2u);
 types.recordActivateSource<IkQisiRecord>(4335u); AI_NATIVE_REQUIRE_DECLARATION(IkQisiRecord, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisiRecord, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisiRecord, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisiRecord, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisiRecord, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisiRecord, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkQisi)), 2u);
 types.recordActivateSource<IkQisi>(4336u); AI_NATIVE_REQUIRE_DECLARATION(IkQisi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQisi, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkJilian)), 2u);
 types.recordActivateSource<IkJilian>(4337u); AI_NATIVE_REQUIRE_DECLARATION(IkJilian, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilian, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilian, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilian, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilian, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJilian, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkJichang)), 2u);
 types.recordActivateSource<IkJichang>(4338u); AI_NATIVE_REQUIRE_DECLARATION(IkJichang, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJichang, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJichang, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJichang, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJichang, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJichang, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkManwu)), 2u);
 types.recordActivateSource<IkManwu>(4339u); AI_NATIVE_REQUIRE_DECLARATION(IkManwu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkManwu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkManwu, getLimitScope, IkManwu); AI_NATIVE_REQUIRE_DECLARATION(IkManwu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkManwu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkManwu, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkXianlv)), 2u);
 types.recordActivateSource<IkXianlv>(4340u); AI_NATIVE_REQUIRE_DECLARATION(IkXianlv, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXianlv, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXianlv, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXianlv, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXianlv, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXianlv, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLianwu)), 2u);
 types.recordActivateSource<IkLianwu>(4341u); AI_NATIVE_REQUIRE_DECLARATION(IkLianwu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLianwuDistance)), 3u); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuDistance, getCorrect, IkLianwuDistance); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuDistance, getFixed, DistanceSkill);
 types.recordActivateSource<IkLianwuDistance>(4342u); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuDistance, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuDistance, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuDistance, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuDistance, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuDistance, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(IkLianwuTargetMod)), 3u); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getPattern, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getResidueNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getDistanceLimit, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getExtraTargetNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, requiresShowForUse, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getCorrection, IkLianwuTargetMod); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getFixedValue, TargetModSkillV2);
 types.recordActivateSource<IkLianwuTargetMod>(4343u); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianwuTargetMod, getBaseAmount, TargetModSkillV2);
 types.emplace(std::type_index(typeid(IkMoshanFilter)), 2u);
 types.recordActivateSource<IkMoshanFilter>(4344u); AI_NATIVE_REQUIRE_DECLARATION(IkMoshanFilter, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMoshanFilter, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMoshanFilter, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMoshanFilter, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMoshanFilter, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(IkMoshan)), 2u);
 types.recordActivateSource<IkMoshan>(4345u); AI_NATIVE_REQUIRE_DECLARATION(IkMoshan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMoshan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMoshan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMoshan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMoshan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMoshan, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkXieke)), 2u);
 types.recordActivateSource<IkXieke>(4346u); AI_NATIVE_REQUIRE_DECLARATION(IkXieke, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXieke, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXieke, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXieke, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXieke, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXieke, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkXiekeChain)), 2u);
 types.recordActivateSource<IkXiekeChain>(4347u); AI_NATIVE_REQUIRE_DECLARATION(IkXiekeChain, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiekeChain, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiekeChain, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiekeChain, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiekeChain, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkXiekeChain, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkYunmai)), 2u);
 types.recordActivateSource<IkYunmai>(4348u); AI_NATIVE_REQUIRE_DECLARATION(IkYunmai, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunmai, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunmai, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunmai, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunmai, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYunmai, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLunyao)), 2u);
 types.recordActivateSource<IkLunyao>(4349u); AI_NATIVE_REQUIRE_DECLARATION(IkLunyao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLunyao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLunyao, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLunyao, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLunyao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLunyao, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkQimu)), 2u);
 types.recordActivateSource<IkQimu>(4350u); AI_NATIVE_REQUIRE_DECLARATION(IkQimu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQimu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQimu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQimu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQimu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQimu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkYuanji)), 2u);
 types.recordActivateSource<IkYuanji>(4351u); AI_NATIVE_REQUIRE_DECLARATION(IkYuanji, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanji, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanji, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanji, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanji, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanji, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkYuanjiTargetMod)), 3u); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getPattern, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getResidueNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getDistanceLimit, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getExtraTargetNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, requiresShowForUse, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getCorrection, IkYuanjiTargetMod); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getFixedValue, TargetModSkillV2);
 types.recordActivateSource<IkYuanjiTargetMod>(4352u); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanjiTargetMod, getBaseAmount, TargetModSkillV2);
 types.emplace(std::type_index(typeid(IkShuluo)), 2u);
 types.recordActivateSource<IkShuluo>(4353u); AI_NATIVE_REQUIRE_DECLARATION(IkShuluo, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShuluo, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShuluo, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShuluo, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShuluo, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkShuluo, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkZhiwang)), 2u);
 types.recordActivateSource<IkZhiwang>(4354u); AI_NATIVE_REQUIRE_DECLARATION(IkZhiwang, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhiwang, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhiwang, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhiwang, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhiwang, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhiwang, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkLianlong)), 3u); AI_NATIVE_REQUIRE_DECLARATION(IkLianlong, getCorrect, IkLianlong); AI_NATIVE_REQUIRE_DECLARATION(IkLianlong, getFixed, DistanceSkill);
 types.recordActivateSource<IkLianlong>(4355u); AI_NATIVE_REQUIRE_DECLARATION(IkLianlong, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianlong, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianlong, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianlong, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkLianlong, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(IkHuanxian)), 2u);
 types.recordActivateSource<IkHuanxian>(4356u); AI_NATIVE_REQUIRE_DECLARATION(IkHuanxian, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanxian, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanxian, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanxian, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanxian, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuanxian, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkWuyu)), 2u);
 types.recordActivateSource<IkWuyu>(4357u); AI_NATIVE_REQUIRE_DECLARATION(IkWuyu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuyu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuyu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuyu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuyu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkWuyu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkKezhan)), 2u);
 types.recordActivateSource<IkKezhan>(4358u); AI_NATIVE_REQUIRE_DECLARATION(IkKezhan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKezhan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKezhan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKezhan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKezhan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkKezhan, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkDamageCount)), 2u);
 types.recordActivateSource<IkDamageCount>(4359u); AI_NATIVE_REQUIRE_DECLARATION(IkDamageCount, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDamageCount, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDamageCount, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDamageCount, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDamageCount, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDamageCount, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkHuwu)), 2u);
 types.recordActivateSource<IkHuwu>(4360u); AI_NATIVE_REQUIRE_DECLARATION(IkHuwu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuwu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuwu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuwu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuwu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkHuwu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkMosu)), 2u);
 types.recordActivateSource<IkMosu>(4361u); AI_NATIVE_REQUIRE_DECLARATION(IkMosu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMosu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMosu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMosu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMosu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMosu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkSuyi)), 2u);
 types.recordActivateSource<IkSuyi>(4362u); AI_NATIVE_REQUIRE_DECLARATION(IkSuyi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSuyi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSuyi, getLimitScope, IkSuyi); AI_NATIVE_REQUIRE_DECLARATION(IkSuyi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSuyi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSuyi, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkYihui)), 2u);
 types.recordActivateSource<IkYihui>(4363u); AI_NATIVE_REQUIRE_DECLARATION(IkYihui, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYihui, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYihui, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYihui, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYihui, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYihui, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkQianshe)), 2u);
 types.recordActivateSource<IkQianshe>(4364u); AI_NATIVE_REQUIRE_DECLARATION(IkQianshe, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQianshe, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQianshe, getLimitScope, IkQianshe); AI_NATIVE_REQUIRE_DECLARATION(IkQianshe, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQianshe, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkQianshe, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkDaolei)), 2u);
 types.recordActivateSource<IkDaolei>(4365u); AI_NATIVE_REQUIRE_DECLARATION(IkDaolei, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDaolei, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDaolei, getLimitScope, IkDaolei); AI_NATIVE_REQUIRE_DECLARATION(IkDaolei, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDaolei, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkDaolei, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkYuanshou)), 2u);
 types.recordActivateSource<IkYuanshou>(4366u); AI_NATIVE_REQUIRE_DECLARATION(IkYuanshou, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanshou, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanshou, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanshou, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanshou, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkYuanshou, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkZhuxue)), 2u);
 types.recordActivateSource<IkZhuxue>(4367u); AI_NATIVE_REQUIRE_DECLARATION(IkZhuxue, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhuxue, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhuxue, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhuxue, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhuxue, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhuxue, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkSheluo)), 2u);
 types.recordActivateSource<IkSheluo>(4368u); AI_NATIVE_REQUIRE_DECLARATION(IkSheluo, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheluo, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheluo, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheluo, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheluo, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkSheluo, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkZhonggui)), 2u);
 types.recordActivateSource<IkZhonggui>(4369u); AI_NATIVE_REQUIRE_DECLARATION(IkZhonggui, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhonggui, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhonggui, getLimitScope, WakeSkill); AI_NATIVE_REQUIRE_DECLARATION(IkZhonggui, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhonggui, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkZhonggui, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkJuexiang)), 2u);
 types.recordActivateSource<IkJuexiang>(4370u); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiang, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiang, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiang, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiang, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiang, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiang, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkJuexiangTargetMod)), 3u); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getPattern, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getResidueNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getDistanceLimit, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getExtraTargetNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, requiresShowForUse, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getCorrection, IkJuexiangTargetMod); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getFixedValue, TargetModSkillV2);
 types.recordActivateSource<IkJuexiangTargetMod>(4371u); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJuexiangTargetMod, getBaseAmount, TargetModSkillV2);
}
