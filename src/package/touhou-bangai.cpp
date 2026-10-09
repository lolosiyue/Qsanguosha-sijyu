#include "touhou-bangai.h"
#include "touhou-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "util.h"

using namespace TouhouUtils;

namespace {

bool someonesTurn(Room *room)
{
    const ServerPlayer *current = room ? room->getCurrent() : nullptr;
    return current && current->getPhase() != Player::NotActive;
}

// ---------------------------------------------------------------- bangai001

class ThBianfang : public TriggerSkillV2
{
public:
    ThBianfang() : TriggerSkillV2("thbianfang")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    // Answered on the losing character's own dispatch so each move counts once.
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || isOwnTurn(player) || !move.from_places.contains(Player::PlaceHand))
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner != player && owner->hasSkill(objectName()) && owner->isAdjacentTo(player)
                && owner->getHandcardNum() <= player->getHandcardNum())
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

class ThZhishi : public TriggerSkillV2
{
public:
    ThZhishi() : TriggerSkillV2("thzhishi")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || isOwnTurn(player)
            || room->getTag("FirstRound").toBool())
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        const bool lost = move.from == player && move.from_places.contains(Player::PlaceHand);
        const bool gained = move.to == player && move.to_place == Player::PlaceHand;
        if (!lost && !gained)
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
        const QList<int> ids = room->getNCards(1, false);
        if (ids.isEmpty())
            return false;
        const Card *card = Sanguosha->getCard(ids.first());
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardTo(card, nullptr, Player::PlaceTable, turnover, true);
        if (room->getCardPlace(card->getEffectiveId()) != Player::PlaceTable)
            return false;
        if (room->askForChoice(player, objectName(), "draw+discard", QVariant::fromValue(card)) == "draw") {
            CardMoveReason put(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(), QString());
            room->moveCardTo(card, nullptr, Player::DrawPile, put, true);
        } else {
            CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
            room->throwCard(card, toPile, nullptr);
        }
        return false;
    }
};

// ---------------------------------------------------------------- bangai002

class ThShoujuan : public TriggerSkillV2
{
public:
    ThShoujuan() : TriggerSkillV2("thshoujuan") { events << EventPhaseStart << CardsMoveOneTime << EventPhaseChanging; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseChanging && player && player->hasFlag("ThShoujuanUsed"))
            room->setPlayerFlag(player, "-ThShoujuanUsed");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == EventPhaseStart) {
            if (player->getPhase() == Player::Play && player->isAlive() && player->hasSkill(objectName()))
                result[player] << objectName();
        } else if (event == CardsMoveOneTime) {
            // Every other character's loss, answered on its own dispatch.
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || !player->isAlive()
                || !(move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)))
                return result;
            foreach (ServerPlayer *owner, room->getOtherPlayers(player))
                if (owner->hasFlag("ThShoujuanUsed") && !owner->isNude())
                    result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardsMoveOneTime)
            return true;
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == EventPhaseStart) {
            owner->drawCards(2, objectName());
            room->setPlayerFlag(owner, "ThShoujuanUsed");
            return false;
        }
        if (!player->isAlive() || owner->isNude())
            return false;
        room->sendCompulsoryTriggerLog(owner, objectName());
        const Card *card = room->askForExchange(owner, objectName(), 1, 1, true, "@thshoujuan:" + player->objectName(), false);
        if (card && !card->getSubcards().isEmpty())
            room->giveCard(owner, player, card, objectName());
        return false;
    }
};

class ThMiqi : public TriggerSkillV2
{
public:
    ThMiqi() : TriggerSkillV2("thmiqi") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
            || player->isKongcheng())
            return TriggerList();
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getHandcardNum() < player->getHandcardNum())
                return TriggerList();
        if (victims(room, player, QList<int>()).isEmpty())
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

    // Upstream picks every card first and throws them together; so does this.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int n = player->getHandcardNum();
        QList<int> chosen;
        QMap<ServerPlayer *, QList<int>> byOwner;
        for (int i = 0; i < n; ++i) {
            const QList<ServerPlayer *> targets = victims(room, player, chosen);
            if (targets.isEmpty())
                break;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@thmiqi", i > 0, false);
            if (!target)
                break;
            QList<int> disabled = chosen;
            foreach (const Card *card, target->getCards("ej"))
                if (!player->canDiscard(target, card->getEffectiveId()))
                    disabled << card->getEffectiveId();
            const int id = room->askForCardChosen(player, target, "ej", objectName(), false, Card::MethodDiscard, disabled);
            if (id < 0 || disabled.contains(id))
                break;
            chosen << id;
            byOwner[target] << id;
        }
        QList<ServerPlayer *> owners = byOwner.keys();
        room->sortByActionOrder(owners);
        foreach (ServerPlayer *target, owners) {
            QList<int> ids;
            foreach (int id, byOwner.value(target))
                if (room->getCardOwner(id) == target)
                    ids << id;
            if (ids.isEmpty())
                continue;
            DummyCard dummy(ids);
            room->throwCard(&dummy, target, player);
        }
        return false;
    }

private:
    static QList<ServerPlayer *> victims(Room *room, ServerPlayer *player, const QList<int> &chosen)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            foreach (const Card *card, p->getCards("ej"))
                if (!chosen.contains(card->getEffectiveId()) && player->canDiscard(p, card->getEffectiveId())) {
                    result << p;
                    break;
                }
        return result;
    }
};

// ---------------------------------------------------------------- bangai003

// Upstream's copy of 马术, kept separate from mashu.
class ThJibu : public DistanceSkillV2
{
public:
    ThJibu() : DistanceSkillV2("thjibu")
    {
        setBaseAmount(-1);
        setHolderSelector(CorrectSkill_Primary);
    }
};

class ThZhiyue : public TriggerSkillV2
{
public:
    ThZhiyue() : TriggerSkillV2("thzhiyue") { events << CardUsed << TargetSpecified; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || !use.card->isKindOf("Slash"))
            return TriggerList();
        if (event == TargetSpecified) {
            if (use.card->hasFlag("thzhiyuered") && player->isAlive())
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        if (!player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == TargetSpecified)
            return true;
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetSpecified) {
            room->setCardFlag(use.card, "-thzhiyuered");
            foreach (ServerPlayer *p, use.to) {
                if (!player->isAlive() || !p->isAlive() || !player->canDiscard(p, "he"))
                    continue;
                const int id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                if (id >= 0)
                    room->throwCard(id, p, player);
            }
            return false;
        }
        JudgeStruct judge;
        judge.good = true;
        judge.play_animation = false;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        if (!judge.card)
            return false;
        if (judge.card->isRed()) {
            room->setCardFlag(use.card, "thzhiyuered");
            return false;
        }
        if (!judge.card->isBlack() || !player->isAlive())
            return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (!use.to.contains(p) && player->canSlash(p, use.card))
                targets << p;
        if (targets.isEmpty())
            return false;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@thzhiyue");
        if (!target)
            return false;
        use.to << target;
        room->sortByActionOrder(use.to);
        LogMessage log;
        log.type = "#ThZhiyue";
        log.from = player;
        log.to << target;
        log.arg = objectName();
        log.arg2 = use.card->objectName();
        room->sendLog(log);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

// ---------------------------------------------------------------- bangai004

class ThZhongjie : public TriggerSkillV2
{
public:
    ThZhongjie() : TriggerSkillV2("thzhongjie") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || candidates(room).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates(room), objectName(), "@thzhongjie", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || target->isKongcheng())
            return false;
        room->showAllCards(target);
        QSet<QString> types;
        foreach (const Card *card, target->getHandcards())
            types << card->getType();
        const int n = 3 - types.size();
        if (n > 0)
            target->drawCards(n, objectName());
        return false;
    }

private:
    static QList<ServerPlayer *> candidates(Room *room)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (!p->isKongcheng())
                result << p;
        return result;
    }
};

class ThXumeiViewAs : public ViewAsSkillV2
{
public:
    ThXumeiViewAs() : ViewAsSkillV2("thxumei", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using") || !card->isKindOf("BasicCard"))
            return false;
        const int id = card->getEffectiveId();
        return (self->handCards().contains(id) || self->getEquipsId().contains(id)) && self->canDiscard(self, id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThXumeiCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        QList<int> ids = room->getNCards(3, false);
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, source->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        QStringList types;
        foreach (int id, ids) {
            const QString type = Sanguosha->getCard(id)->getType();
            if (type != "skill" && !types.contains(type))
                types << type;
        }
        if (types.isEmpty())
            return FinishSkill;
        const QString type = room->askForChoice(source, objectName(), types.join("+"), QVariant(ListI2V(ids)));
        QList<int> gain;
        foreach (int id, ids)
            if (Sanguosha->getCard(id)->getType() == type && room->getCardPlace(id) == Player::PlaceTable)
                gain << id;
        ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), objectName(), "@thxumei:::" + type);
        if (target && !gain.isEmpty()) {
            DummyCard dummy(gain);
            room->obtainCard(target, &dummy);
        }
        QList<int> rest;
        foreach (int id, ids)
            if (room->getCardPlace(id) == Player::PlaceTable)
                rest << id;
        if (!rest.isEmpty()) {
            DummyCard dummy(rest);
            CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, QString(), objectName(), QString());
            room->throwCard(&dummy, toPile, nullptr);
        }
        if (target && target->isAlive()) {
            QString pattern = type;
            pattern[0] = pattern[0].toUpper();
            room->setPlayerCardLimitation(target, "use,response", pattern + "Card", false, objectName());
        }
        return FinishSkill;
    }
};

class ThXumei : public TriggerSkillV2
{
public:
    ThXumei() : TriggerSkillV2("thxumei")
    {
        events << EventPhaseChanging;
        view_as_skill = new ThXumeiViewAs;
    }

    // The limitation lasts until the end of the turn it was imposed in.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (data.value<PhaseChangeStruct>().to == Player::NotActive)
            foreach (ServerPlayer *p, room->getAllPlayers(true))
                room->removePlayerCardLimitationByReason(p, objectName());
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- bangai005

class ThXijing : public TriggerSkillV2
{
public:
    ThXijing() : TriggerSkillV2("thxijing") { events << AskForRetrial; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !someonesTurn(room)
            || player->getMark("thxijing-Clear") > 0)
            return TriggerList();
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!judge || !judge->who || judge->who->isKongcheng())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->addPlayerMark(ctx.owner, "thxijing-Clear");
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge || !judge->who || judge->who->isKongcheng())
            return false;
        const QList<int> hand = judge->who->handCards();
        room->showAllCards(judge->who);
        room->fillAG(hand, player);
        int id = room->askForAG(player, hand, false, objectName());
        room->clearAG(player);
        if (!hand.contains(id))
            id = hand.at(qsanRandomBounded(hand.length()));
        if (room->getCardOwner(id) == judge->who && room->getCardPlace(id) == Player::PlaceHand)
            room->retrial(Sanguosha->getCard(id), player, judge, objectName());
        return false;
    }
};

class ThMengwei : public TriggerSkillV2
{
public:
    ThMengwei() : TriggerSkillV2("thmengwei")
    {
        events << ShowCards;
        frequency = Frequent;
    }

    // A public show of at least one hand card, once per turn.
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (!someonesTurn(room))
            return result;
        bool handShown = false;
        foreach (const QString &text, data.toString().split("+")) {
            bool ok = false;
            const int id = text.toInt(&ok);
            if (!ok)
                continue;
            if (!Sanguosha->getCard(id)->hasFlag("visible"))
                return result;
            if (room->getCardPlace(id) == Player::PlaceHand)
                handShown = true;
        }
        if (!handShown)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->getMark("thmengwei-Clear") == 0)
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getMark("thmengwei-Clear") > 0 || !ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->addPlayerMark(ctx.owner, "thmengwei-Clear");
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- bangai006

class ThSilian : public FilterSkill
{
public:
    ThSilian() : FilterSkill("thsilian") { markOwnerOnly(this); }

    bool viewFilter(const Card *to_select) const override
    {
        Room *room = Sanguosha->currentRoom();
        return room && to_select->isKindOf("Weapon")
            && room->getCardPlace(to_select->getEffectiveId()) == Player::PlaceHand;
    }

    const Card *viewAs(const Card *original) const override
    {
        Slash *slash = new Slash(original->getSuit(), original->getNumber());
        slash->setSkillName(objectName());
        return slash;
    }
};

class ThSilianWeapon : public TriggerSkillV2
{
public:
    ThSilianWeapon() : TriggerSkillV2("#thsilian")
    {
        events << BeforeCardsMove;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.to != player || move.to_place != Player::PlaceEquip || !player->isAlive()
            || !player->hasSkill("thsilian"))
            return TriggerList();
        foreach (int id, move.card_ids)
            if (Sanguosha->getEngineCard(id)->isKindOf("Weapon"))
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    // The weapon goes to the hand instead, where 死镰 reads it as a 杀.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> weapons;
        foreach (int id, move.card_ids)
            if (Sanguosha->getEngineCard(id)->isKindOf("Weapon"))
                weapons << id;
        if (weapons.isEmpty())
            return false;
        room->sendCompulsoryTriggerLog(player, "thsilian");
        room->broadcastSkillInvoke("thsilian");
        move.removeCardIds(weapons);
        *ctx.original_data = QVariant::fromValue(move);
        DummyCard dummy(weapons);
        room->obtainCard(player, &dummy);
        return false;
    }
};

// Without a weapon of its own, the owner counts as holding 青龙偃月刀 (upstream 离魂之镰).
class ThSilianBlade : public ViewAsEquipSkill
{
public:
    ThSilianBlade() : ViewAsEquipSkill("#thsilian-blade") {}

    QString viewAsEquip(const Player *target) const override
    {
        if (target && target->hasSkill("thsilian") && !target->getWeapon())
            return "blade";
        return QString();
    }
};

class ThLingzhanViewAs : public ViewAsSkillV2
{
public:
    ThLingzhanViewAs() : ViewAsSkillV2("thlingzhan", 1)
    {
        expand_pile = "nightmare";
        setResponseOrUse(true);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getPile("nightmare").isEmpty())
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(self);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && (request.pattern.contains("slash") || request.pattern.contains("Slash"));
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using")
            && request.initiator->getPile("nightmare").contains(card->getEffectiveId());
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
        Slash *slash = new Slash(material->getSuit(), material->getNumber());
        slash->addSubcard(material);
        slash->setSkillName(objectName());
        return slash;
    }
};

class ThLingzhan : public TriggerSkillV2
{
public:
    ThLingzhan() : TriggerSkillV2("thlingzhan")
    {
        events << Damage << FinishJudge;
        view_as_skill = new ThLingzhanViewAs;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (event == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge && judge->who == player && judge->reason == objectName() && judge->isGood() && judge->card
                && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from != player || !player->hasSkill(objectName()) || !damage.card || !damage.card->isKindOf("Slash")
            || damage.chain || damage.transfer)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == FinishJudge)
            return true;
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == FinishJudge) {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (judge && judge->card)
                player->addToPile("nightmare", judge->card);
            return false;
        }
        JudgeStruct judge;
        judge.pattern = ".|heart";
        judge.good = false;
        judge.who = player;
        judge.reason = objectName();
        room->judge(judge);
        return false;
    }
};

// As upstream, HolderIgnoresInvaliditySkills lifts every InvaliditySkill from the holder's
// general skills; recorded invalidity and loss caused by another player are refused at
// EventSkillInvalidated / EventLosingSkill. 衍梦 itself is never invalid.
class ThYanmeng : public TriggerSkillV2
{
public:
    ThYanmeng() : TriggerSkillV2("thyanmeng")
    {
        events << EventSkillInvalidated << EventLosingSkill;
        frequency = Compulsory;
        setProperty("IgnoreInvalidity", true);
        setProperty("HolderIgnoresInvaliditySkills", true);
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->hasSkill(objectName()))
            return TriggerList();
        QString skillName;
        if (event == EventLosingSkill) {
            // Grants keep their own lifecycle; only the general's own skills are shielded.
            const SkillChangeStruct change = data.value<SkillChangeStruct>();
            if (change.source != SourceInnate)
                return TriggerList();
            skillName = change.skillName;
        } else {
            SkillInstanceUtils::parseName(data.toString(), skillName);
        }
        const Skill *skill = Sanguosha->getSkill(skillName); // null for "all"
        if ((skill && skill->isEquipSkill()) || !causedByOther(room, player))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        return true;
    }

private:
    // The innermost active skill event names who acts. As in Room::historyCause,
    // a skill that inserted the current turn is not its cause.
    static bool causedByOther(Room *room, const ServerPlayer *player)
    {
        const QVariantMap event = room->historyParent(room->currentHistoryEventId(), "skill", true);
        if (event.value("turn_id") != room->historyScopes().value("turn_id"))
            return false;
        const QVariantMap identity = event.value("data").toMap();
        QString actor = identity.value("activation_owner").toString();
        if (actor.isEmpty())
            actor = identity.value("skill_owner").toString();
        return !actor.isEmpty() && actor != player->objectName();
    }
};

// ---------------------------------------------------------------- bangai007

class ThQinshao : public TriggerSkillV2
{
public:
    ThQinshao() : TriggerSkillV2("thqinshao")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Discard)
            return TriggerList();
        const int x = player->getHandcardNum() - qMax(player->getHp(), 0);
        if (x == 0 || x == -1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->getHandcardNum() > qMax(player->getHp(), 0)) {
            ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
                                                            "@thqinshao", true, true);
            if (!target)
                return false;
            ctx.extra_data = target->objectName();
        } else if (!player->askForSkillInvoke(objectName())) {
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int x = player->getHandcardNum() - qMax(player->getHp(), 0);
        if (x > 0) {
            ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
            if (target && target->isAlive())
                target->drawCards(x, objectName());
        } else if (x < -1) {
            player->drawCards(-x - 1, objectName());
        }
        return false;
    }
};

class ThXingxieViewAs : public ViewAsSkillV2
{
public:
    ThXingxieViewAs() : ViewAsSkillV2("thxingxie", 1) { setPhaseName("Play"); }

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
        return selected.isEmpty() && to && to->hasEquip();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThXingxieCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->hasEquip())
            return ContinueEffects;
        DummyCard dummy(target->getEquipsId());
        source->addToPile("spark", &dummy);
        source->setTag("ThXingxieTarget", target->objectName());
        return ContinueEffects;
    }
};

class ThXingxie : public TriggerSkillV2
{
public:
    ThXingxie() : TriggerSkillV2("thxingxie")
    {
        events << EventPhaseChanging;
        view_as_skill = new ThXingxieViewAs;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive
            || player->getTag("ThXingxieTarget").toString().isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // At the end of this turn the target takes every 璨 back and uses the equipment.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *target = room->findPlayerByObjectName(player->getTag("ThXingxieTarget").toString());
        player->removeTag("ThXingxieTarget");
        const QList<int> ids = player->getPile("spark");
        if (!target || !target->isAlive() || ids.isEmpty())
            return false;
        DummyCard dummy(ids);
        room->obtainCard(target, &dummy);
        QList<int> equips;
        foreach (int id, ids) {
            const Card *card = Sanguosha->getCard(id);
            if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand
                && card->getTypeId() == Card::TypeEquip && !target->isProhibited(target, card))
                equips << id;
        }
        while (!equips.isEmpty() && target->isAlive()) {
            const Card *used = room->askForUseCard(target, ListI2S(equips).join(","), "@thxingxie");
            int id = used ? used->getEffectiveId() : -1;
            if (!equips.contains(id)) {
                id = equips.at(qsanRandomBounded(equips.length()));
                if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand)
                    room->useCard(CardUseStruct(Sanguosha->getCard(id), target, target));
            }
            equips.removeOne(id);
            foreach (int rest, equips)
                if (room->getCardOwner(rest) != target || room->getCardPlace(rest) != Player::PlaceHand)
                    equips.removeOne(rest);
        }
        return false;
    }
};

// ---------------------------------------------------------------- bangai008

class ThYubo : public ViewAsSkillV2
{
public:
    ThYubo() : ViewAsSkillV2("thyubo", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isBlack()
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
        return selected.length() < 2 && to && !to->isChained();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty() && selected.length() <= 2;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThYuboCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && !target->isChained())
            target->getRoom()->setPlayerChained(target, true, ctx.invoker);
        return ContinueEffects;
    }
};

class ThQiongfa : public TriggerSkillV2
{
public:
    ThQiongfa() : TriggerSkillV2("thqiongfa") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->isChained() || player->getPhase() != Player::Finish)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->isChained() || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canDiscard(p, "he"))
                victims << p;
        bool discarded = false;
        if (owner == player) {
            ServerPlayer *victim = victims.isEmpty()
                ? nullptr
                : room->askForPlayerChosen(owner, victims, objectName(), "@thqiongfa", true);
            if (victim) {
                const int id = room->askForCardChosen(player, victim, "he", objectName(), false, Card::MethodDiscard);
                if (id >= 0) {
                    room->throwCard(id, victim, player);
                    discarded = true;
                }
            }
        } else if (!victims.isEmpty()) {
            ServerPlayer *victim = room->askForPlayerChosen(owner, victims, objectName(), "@thqiongfa-victim:" + player->objectName());
            if (victim) {
                LogMessage log;
                log.type = "#ThQiongfa";
                log.from = owner;
                log.to << victim;
                room->sendLog(log);
                if (room->askForChoice(player, objectName(), "discard+cancel", QVariant::fromValue(victim)) == "discard") {
                    const int id = room->askForCardChosen(player, victim, "he", objectName(), false, Card::MethodDiscard);
                    if (id >= 0) {
                        room->throwCard(id, victim, player);
                        discarded = true;
                    }
                }
            }
        }
        if (!discarded && owner->isAlive())
            owner->drawCards(1, objectName());
        if (player->isAlive() && player->isChained())
            room->setPlayerChained(player, false, owner);
        return false;
    }
};

// ---------------------------------------------------------------- bangai009

class ThWeide : public TriggerSkillV2
{
public:
    ThWeide() : TriggerSkillV2("thweide") { events << EventPhaseStart; }

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
        const QList<int> ids = room->getNCards(2, false);
        room->fillAG(ids, player);
        ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@thweide-give");
        room->clearAG(player);
        if (!target)
            target = player;
        CardMoveReason reason(CardMoveReason::S_REASON_PREVIEWGIVE, player->objectName(), target->objectName(),
                              objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, target, Player::PlaceHand, reason), false);
        if (target != player && player->isAlive() && player->isWounded()) {
            QList<ServerPlayer *> victims;
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (!p->isKongcheng())
                    victims << p;
            ServerPlayer *victim = victims.isEmpty()
                ? nullptr
                : room->askForPlayerChosen(player, victims, objectName(), "@thweide", true);
            if (victim) {
                const int id = room->askForCardChosen(player, victim, "h", objectName());
                if (id >= 0)
                    room->obtainCard(player, id, false);
            }
        }
        return true;
    }
};

// ---------------------------------------------------------------- bangai010

class ThGuijuan : public ViewAsSkillV2
{
public:
    ThGuijuan() : ViewAsSkillV2("thguijuan") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("thguijuan_forbid-Clear") == 0;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThGuijuanCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        const QList<int> before = source->handCards();
        source->drawCards(1, objectName());
        int id = -1;
        foreach (int hand, source->handCards())
            if (!before.contains(hand))
                id = hand;
        if (id < 0 || !source->isAlive())
            return FinishSkill;
        room->showCard(source, id);
        const Card *card = Sanguosha->getCard(id);
        const Card *used = nullptr;
        if (card->isAvailable(source))
            used = room->askForUseCard(source, QString::number(id), "@thguijuan");
        if (!used)
            room->loseHp(source, 1, true, source, objectName());
        else if (used->isKindOf("Slash") || used->isKindOf("EquipCard"))
            room->addPlayerMark(source, "thguijuan_forbid-Clear");
        return FinishSkill;
    }
};

class ThZhayou : public TriggerSkillV2
{
public:
    ThZhayou() : TriggerSkillV2("thzhayou") { events << CardOffset; }

    // Another character's 杀 offset by the owner's 闪.
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.card || !effect.card->isKindOf("Slash") || !effect.to || !effect.from || effect.from == effect.to
            || !effect.to->isAlive() || !effect.to->hasSkill(objectName()))
            return TriggerList();
        if (effect.offset_card && !effect.offset_card->isKindOf("Jink"))
            return TriggerList();
        return TriggerList{{effect.to, {objectName()}}};
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
        ServerPlayer *from = ctx.original_data->value<CardEffectStruct>().from;
        owner->drawCards(1, objectName());
        if (!from || !from->isAlive() || !owner->isAlive())
            return false;
        if (!room->askForUseSlashTo(from, owner, "@thzhayou:" + owner->objectName(), false))
            room->damage(DamageStruct(objectName(), owner, from));
        return false;
    }
};

// ---------------------------------------------------------------- bangai011

class ThHuilun : public FilterSkill
{
public:
    ThHuilun() : FilterSkill("thhuilun") {}

    bool viewFilter(const Card *to_select) const override
    {
        return (to_select->isKindOf("Slash") && to_select->isBlack()) || (to_select->isKindOf("Peach") && to_select->isRed());
    }

    const Card *viewAs(const Card *original) const override
    {
        Card *card = original->isBlack()
            ? static_cast<Card *>(new Peach(original->getSuit(), original->getNumber()))
            : static_cast<Card *>(new Slash(original->getSuit(), original->getNumber()));
        card->setSkillName(objectName());
        return card;
    }
};

class ThWangdao : public ViewAsSkillV2
{
public:
    ThWangdao() : ViewAsSkillV2("thwangdao", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isKindOf("Peach");
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

    QString historyKey(const ActiveSkillRequest &) const override { return "ThWangdaoCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || !ctx.use_card
            || ctx.use_card->subcardsLength() == 0)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != source || room->getCardPlace(id) != Player::PlaceHand)
            return ContinueEffects;
        const Card *peach = Sanguosha->getCard(id);
        room->showCard(source, id);
        const Card *slash = nullptr;
        if (target->canSlash(source, nullptr, false)) {
            const QString pattern = "Slash|^" + peach->getSuitString();
            room->setPlayerCardLimitation(target, "use", pattern, false, objectName());
            slash = room->askForUseSlashTo(target, source, "@thwangdao:" + source->objectName() + "::" + peach->getSuitString(),
                                           false);
            room->removePlayerCardLimitationByReason(target, objectName());
        }
        if (slash || !source->isAlive())
            return ContinueEffects;
        if (room->getCardOwner(id) == source && room->getCardPlace(id) == Player::PlaceHand && source->canDiscard(source, id))
            room->throwCard(id, source);
        if (!target->isAlive())
            return ContinueEffects;
        if (source->canDiscard(target, "he")
            && room->askForChoice(source, objectName(), "discard+lose", QVariant::fromValue(target)) == "discard") {
            const int n = qMin(2, target->getCards("he").length());
            const QList<int> ids = room->askForCardsChosen(source, target, "he", objectName(), n, n, false,
                                                           Card::MethodDiscard, QList<int>(), false);
            if (!ids.isEmpty()) {
                DummyCard dummy(ids);
                room->throwCard(&dummy, target, source);
            }
        } else {
            room->loseHp(target, 1, true, source, objectName());
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- bangai012

class ThKongxiang : public ViewAsSkillV2
{
public:
    ThKongxiang() : ViewAsSkillV2("thkongxiang", 2) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || card->hasFlag("using") || request.selectedCardIds.size() >= 2)
            return false;
        const int id = card->getEffectiveId();
        if (!(self->handCards().contains(id) || self->getEquipsId().contains(id)) || !self->canDiscard(self, id))
            return false;
        return request.selectedCardIds.isEmpty()
            || Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == card->getSuit();
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

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        const Player *self = request.initiator;
        if (!self || !to || request.selectedCardIds.isEmpty())
            return false;
        switch (Sanguosha->getCard(request.selectedCardIds.first())->getSuit()) {
        case Card::Spade:
            return selected.isEmpty() && to->isWounded();
        case Card::Diamond:
            return selected.length() < 2 && to != self && self->canDiscard(to, "hej");
        default:
            return selected.isEmpty() && to != self;
        }
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (!cardSelectionFeasible(request) || selected.isEmpty())
            return false;
        const Card::Suit suit = Sanguosha->getCard(request.selectedCardIds.first())->getSuit();
        return suit == Card::Diamond ? selected.length() <= 2 : selected.length() == 1;
    }

    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThKongxiangCard"; }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !ctx.use_card || ctx.use_card->subcardsLength() == 0 || targets.isEmpty())
            return FinishSkill;
        Room *room = source->getRoom();
        const Card::Suit suit = Sanguosha->getCard(ctx.use_card->getSubcards().first())->getSuit();
        ServerPlayer *target = targets.first();
        if (suit == Card::Spade) {
            if (target->isAlive() && target->isWounded())
                room->recover(target, RecoverStruct(objectName(), source));
        } else if (suit == Card::Diamond) {
            QList<CardsMoveStruct> moves;
            foreach (ServerPlayer *p, targets) {
                if (!p->isAlive() || !source->canDiscard(p, "hej"))
                    continue;
                const int id = room->askForCardChosen(source, p, "hej", objectName(), false, Card::MethodDiscard);
                if (id < 0)
                    continue;
                CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, source->objectName(),
                                      room->getCardPlace(id) == Player::PlaceDelayedTrick ? QString() : p->objectName(),
                                      objectName(), QString());
                moves << CardsMoveStruct(id, nullptr, Player::DiscardPile, reason);
                LogMessage log;
                log.type = "$DiscardCardByOther";
                log.from = source;
                log.to << p;
                log.card_str = QString::number(id);
                room->sendLog(log);
            }
            if (!moves.isEmpty())
                room->moveCardsAtomic(moves, true);
        } else if (suit == Card::Club) {
            if (target->isAlive())
                target->drawCards(2, objectName());
        } else if (suit == Card::Heart && target->isAlive()) {
            target->drawCards(1, objectName());
            if (target->isAlive() && target->canDiscard(target, "h"))
                room->askForDiscard(target, objectName(), 1, 1);
            if (target->isAlive())
                target->turnOver();
        }
        return FinishSkill;
    }
};

}

ThMiqiCard::ThMiqiCard() { setSkillName("thmiqi"); mute = true; }
ThXumeiCard::ThXumeiCard() { setSkillName("thxumei"); mute = true; }
ThXingxieCard::ThXingxieCard() { setSkillName("thxingxie"); mute = true; }
ThYuboCard::ThYuboCard() { setSkillName("thyubo"); mute = true; }
ThGuijuanCard::ThGuijuanCard() { setSkillName("thguijuan"); mute = true; }
ThWangdaoCard::ThWangdaoCard() { setSkillName("thwangdao"); mute = true; }
ThKongxiangCard::ThKongxiangCard() { setSkillName("thkongxiang"); mute = true; }

TouhouBangaiPackage::TouhouBangaiPackage()
    : Package("touhou-bangai")
{
    General *bangai001 = new General(this, "bangai001", "kaze", 3);
    bangai001->addSkill(new ThBianfang);
    bangai001->addSkill(new ThZhishi);

    General *bangai002 = new General(this, "bangai002", "hana");
    bangai002->addSkill(new ThShoujuan);
    bangai002->addSkill(new ThMiqi);

    // 疾步 is 马术.
    General *bangai003 = new General(this, "bangai003", "yuki");
    bangai003->addSkill("mashu");
    bangai003->addSkill(new ThZhiyue);

    General *bangai004 = new General(this, "bangai004", "tsuki", 3, false);
    bangai004->addSkill(new ThZhongjie);
    bangai004->addSkill(new ThXumei);

    General *bangai005 = new General(this, "bangai005", "kaze", 3);
    bangai005->addSkill(new ThXijing);
    bangai005->addSkill(new ThMengwei);

    General *bangai006 = new General(this, "bangai006", "hana");
    bangai006->addSkill(new ThSilian);
    bangai006->addSkill(new ThSilianWeapon);
    bangai006->addSkill(new ThSilianBlade);
    related_skills.insert("thsilian", "#thsilian");
    related_skills.insert("thsilian", "#thsilian-blade");
    bangai006->addSkill(new ThLingzhan);
    bangai006->addSkill(new ThYanmeng);

    General *bangai007 = new General(this, "bangai007", "yuki");
    bangai007->addSkill(new ThQinshao);
    bangai007->addSkill(new ThXingxie);

    General *bangai008 = new General(this, "bangai008", "tsuki", 3);
    bangai008->addSkill(new ThYubo);
    bangai008->addSkill(new ThQiongfa);

    General *bangai009 = new General(this, "bangai009", "kaze");
    bangai009->addSkill(new ThWeide);

    General *bangai010 = new General(this, "bangai010", "hana", 3, false);
    bangai010->addSkill(new ThGuijuan);
    bangai010->addSkill(new ThZhayou);

    General *bangai011 = new General(this, "bangai011", "yuki", 3);
    bangai011->addSkill(new ThHuilun);
    bangai011->addSkill(new ThWangdao);

    General *bangai012 = new General(this, "bangai012", "tsuki");
    bangai012->addSkill(new ThKongxiang);

    // 疾步 has no owner here; ikai-moku's luna008 uses it.
    skills << new ThJibu;

    addMetaObject<ThMiqiCard>();
    addMetaObject<ThXumeiCard>();
    addMetaObject<ThXingxieCard>();
    addMetaObject<ThYuboCard>();
    addMetaObject<ThGuijuanCard>();
    addMetaObject<ThWangdaoCard>();
    addMetaObject<ThKongxiangCard>();
}

ADD_PACKAGE(TouhouBangai)
