#include "touhou-sp.h"
#include "touhou-utils.h"
#include "engine.h"
#include "exppattern.h"
#include "general.h"
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

// ---------------------------------------------------------------- sp001

class ThFanshi : public ViewAsSkillV2
{
public:
    ThFanshi() : ViewAsSkillV2("thfanshi", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getHandcardNum() < self->getHp())
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return isOwnTurn(self) && Slash::IsAvailable(self);
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            return false;
        return !nameFor(request).isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isRed();
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
        const QString name = nameFor(request);
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = Sanguosha->cloneCard(name, material->getSuit(), material->getNumber());
        if (!card)
            return nullptr;
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

private:
    // 杀 in the owner's turn; 闪 or 桃 outside it.
    static QString nameFor(const ActiveSkillRequest &request)
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return "slash";
        const QString pattern = request.pattern;
        if (isOwnTurn(request.initiator))
            return pattern.contains("slash") || pattern.contains("Slash") ? "slash" : QString();
        if (pattern.contains("jink") || pattern.contains("Jink"))
            return "jink";
        if (pattern.contains("peach") || pattern.contains("Peach"))
            return "peach";
        return QString();
    }
};

// ---------------------------------------------------------------- sp002

class ThKongsuo : public TriggerSkillV2
{
public:
    ThKongsuo() : TriggerSkillV2("thkongsuo") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@thkongsuo", true, true);
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
            room->setPlayerChained(target, !target->isChained(), ctx.owner);
        return false;
    }
};

// Distance between the owner and any chained character is 1 both ways.
class ThGuimen : public DistanceSkill
{
public:
    ThGuimen() : DistanceSkill("thguimen") { frequency = Compulsory; }

    int getFixed(const Player *from, const Player *to) const override
    {
        if (!from || !to || from == to)
            return 0;
        if ((from->hasSkill(objectName()) && to->isChained()) || (to->hasSkill(objectName()) && from->isChained()))
            return 1;
        return 0;
    }
};

class ThLiuren : public TriggerSkillV2
{
public:
    ThLiuren() : TriggerSkillV2("thliuren")
    {
        events << PreCardUsed << CardUsed;
        frequency = Compulsory;
    }

    // Counts each character's cards this turn.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != PreCardUsed || !player)
            return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill)
            room->addPlayerMark(player, "thliuren_count-Clear");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed || !player || !player->isChained() || player->getMark("thliuren_count-Clear") != 1)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill || !someonesTurn(room))
            return TriggerList();
        ServerPlayer *current = room->getCurrent();
        if (current == player || !current->isAlive() || !current->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{current, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        if (!use.nullified_list.contains("_ALL_TARGETS"))
            use.nullified_list << "_ALL_TARGETS";
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

// ---------------------------------------------------------------- sp003

class ThShenshi : public TriggerSkillV2
{
public:
    ThShenshi() : TriggerSkillV2("thshenshi") { events << EventPhaseSkipped; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->isWounded())
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->getMark("thshenshi-Clear") == 0 && owner->canDiscard(owner, "he"))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, "..", "@thshenshi:" + player->objectName(), QVariant::fromValue(player),
                              objectName()))
            return false;
        room->addPlayerMark(ctx.owner, "thshenshi-Clear");
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

class ThJiefan : public TriggerSkillV2
{
public:
    ThJiefan() : TriggerSkillV2("thjiefan")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start)
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
        for (;;) {
            const QList<int> ids = room->getNCards(1, false);
            if (ids.isEmpty())
                break;
            CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
            room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
            const Card *card = Sanguosha->getCard(ids.first());
            if (!card->isKindOf("BasicCard") || !player->isAlive()) {
                CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
                room->throwCard(card, toPile, nullptr);
                break;
            }
            player->setTag("ThJiefanId", card->getEffectiveId());
            ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                                                            "@thjiefan-give");
            player->removeTag("ThJiefanId");
            if (room->getCardPlace(card->getEffectiveId()) == Player::PlaceTable)
                room->obtainCard(target ? target : player, card);
            if (!player->isAlive() || !player->askForSkillInvoke(objectName()))
                break;
        }
        return false;
    }
};

// ---------------------------------------------------------------- sp004

class ThChuangshi : public TriggerSkillV2
{
public:
    ThChuangshi() : TriggerSkillV2("thchuangshi") { events << CardEffected; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!player || effect.to != player || !effect.card || !effect.from
            || !(effect.card->isKindOf("Dismantlement") || effect.card->isKindOf("Collateral") || effect.card->isKindOf("Duel")))
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner != effect.from
                && (owner == player || owner->inMyAttackRange(player)))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->setTag("ThChuangshiData", *ctx.original_data);
        const bool invoked = ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data);
        ctx.owner->removeTag("ThChuangshiData");
        if (!invoked)
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // The trick is offset; its user is taken to have slashed the owner instead.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        ServerPlayer *from = ctx.original_data->value<CardEffectStruct>().from;
        if (from && from->isAlive() && owner->isAlive()) {
            auto *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_thchuangshi");
            if (from->canSlash(owner, slash, false)) {
                CardUseStruct use(slash, from, owner);
                use.m_addHistory = false;
                use.setOwnedCard(slash);
                room->useCardFromSkillEffect(use, ctx);
            } else {
                delete slash;
            }
        }
        return true;
    }
};

class ThGaotian : public TriggerSkillV2
{
public:
    ThGaotian() : TriggerSkillV2("thgaotian")
    {
        events << CardAsked;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const QStringList asked = data.toStringList();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || asked.isEmpty() || asked.first() != "jink")
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
        QList<int> ids = room->getNCards(qMin(4, room->alivePlayerCount()), false);
        QList<int> reds;
        QList<int> blacks;
        foreach (int id, ids) {
            if (Sanguosha->getCard(id)->isRed())
                reds << id;
            else if (Sanguosha->getCard(id)->isBlack())
                blacks << id;
        }
        room->fillAG(ids, player);
        QString pattern = "..";
        if (reds.isEmpty())
            pattern = ".|black";
        else if (blacks.isEmpty())
            pattern = ".|red";
        if (!reds.isEmpty() || !blacks.isEmpty()) {
            const Card *card = player->canDiscard(player, "he")
                ? room->askForCard(player, pattern, "@gaotian-discard", QVariant(ListI2V(ids)), objectName())
                : nullptr;
            if (card) {
                const QList<int> &pool = card->isRed() ? reds : blacks;
                if (!pool.isEmpty()) {
                    room->clearAG(player);
                    room->fillAG(ids, player, card->isRed() ? blacks : reds);
                    int id = room->askForAG(player, pool, false, objectName());
                    if (!pool.contains(id))
                        id = pool.first();
                    room->clearAG(player);
                    room->fillAG(ids, player);
                    room->takeAG(player, id, false, QList<ServerPlayer *>{player});
                    ids.removeOne(id);
                    room->obtainCard(player, id);
                }
            }
        }
        QList<int> thrown;
        while (!ids.isEmpty() && player->isAlive()) {
            room->setPlayerFlag(player, "ThGaotianSecond");
            const int id = room->askForAG(player, ids, true, objectName());
            room->setPlayerFlag(player, "-ThGaotianSecond");
            if (!ids.contains(id))
                break;
            room->takeAG(nullptr, id, false, QList<ServerPlayer *>{player});
            ids.removeOne(id);
            thrown << id;
        }
        room->clearAG(player);
        if (!ids.isEmpty())
            room->askForGuanxing(player, ids, Room::GuanxingUpOnly);
        if (!thrown.isEmpty()) {
            DummyCard dummy(thrown);
            CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
            room->throwCard(&dummy, toPile, nullptr);
        }
        return false;
    }
};

// ---------------------------------------------------------------- sp005

class ThWanling : public TriggerSkillV2
{
public:
    ThWanling() : TriggerSkillV2("thwanling") { events << BeforeCardsMove; }

    LimitScope getLimitScope() const override { return Limit_Turn; }

    int getMaxUsageLimit(const SkillContext &) const override { return 3; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !move.from || move.from == player
            || !move.from_places.contains(Player::PlaceTable) || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE)
            return TriggerList();
        const Card *card = move.reason.m_useStruct.card;
        if (!card || !card->isRed() || !(card->isKindOf("Slash") || card->isNDTrick()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
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
        ServerPlayer *player = ctx.owner;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        ServerPlayer *user = room->findPlayerByObjectName(move.from->objectName());
        if (player->canDiscard(player, "he")
            && room->askForCard(player, "..", "@thwanling:" + move.from->objectName(), *ctx.original_data, objectName())) {
            const QList<int> ids = move.card_ids;
            move.removeCardIds(ids);
            *ctx.original_data = QVariant::fromValue(move);
            DummyCard dummy(ids);
            room->obtainCard(player, &dummy);
        } else if (user && user->isAlive()) {
            user->drawCards(1, objectName());
        }
        return false;
    }
};

class ThZuibu : public TriggerSkillV2
{
public:
    ThZuibu() : TriggerSkillV2("thzuibu") { events << DamageInflicted; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getMark("thzuibu-Clear") >= 3 || !someonesTurn(room) || !damage.from || !damage.from->isAlive())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The damage source decides.
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.original_data->value<DamageStruct>().from;
        if (!source || !source->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.owner)))
            return false;
        room->broadcastSkillInvoke(objectName());
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = source;
        log.to << ctx.owner;
        log.arg = objectName();
        room->sendLog(log);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->addPlayerMark(player, "thzuibu-Clear");
        player->drawCards(1, objectName());
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#ThZuibu";
        log.from = damage.from;
        log.to << player;
        log.arg = QString::number(damage.damage);
        --damage.damage;
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        *ctx.original_data = QVariant::fromValue(damage);
        return damage.damage < 1;
    }
};

// ---------------------------------------------------------------- sp006

class ThModao : public TriggerSkillV2
{
public:
    ThModao() : TriggerSkillV2("thmodao") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw
            || candidates(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates(room, ctx.owner), objectName(), "@thmodao",
                                                        true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    // Replaces the draw.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return true;
        const QStringList choices = areas(player, target);
        if (choices.isEmpty())
            return true;
        player->setTag("ThModaoData", QVariant::fromValue(target));
        const QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target));
        player->removeTag("ThModaoData");
        QList<int> mine;
        QList<int> theirs;
        Player::Place place = Player::PlaceHand;
        if (choice == "h") {
            mine = player->handCards();
            theirs = target->handCards();
        } else if (choice == "e") {
            mine = player->getEquipsId();
            theirs = target->getEquipsId();
            place = Player::PlaceEquip;
        } else {
            mine = player->getJudgingAreaID();
            theirs = target->getJudgingAreaID();
            place = Player::PlaceDelayedTrick;
        }
        QList<CardsMoveStruct> moves;
        if (!mine.isEmpty())
            moves << CardsMoveStruct(mine, target, place,
                                     CardMoveReason(CardMoveReason::S_REASON_SWAP, player->objectName(), target->objectName(),
                                                    objectName(), QString()));
        if (!theirs.isEmpty())
            moves << CardsMoveStruct(theirs, player, place,
                                     CardMoveReason(CardMoveReason::S_REASON_SWAP, target->objectName(), player->objectName(),
                                                    objectName(), QString()));
        if (!moves.isEmpty())
            room->moveCardsAtomic(moves, false);
        QList<ServerPlayer *> drawers;
        const QList<ServerPlayer *> pair{player, target};
        foreach (ServerPlayer *p, pair)
            if (p->isAlive())
                drawers << p;
        room->sortByActionOrder(drawers);
        room->drawCards(drawers, 1, objectName());
        return true;
    }

private:
    static QStringList areas(const ServerPlayer *player, const ServerPlayer *target)
    {
        QStringList result;
        const QStringList flags{"h", "e", "j"};
        foreach (const QString &flag, flags)
            if (qAbs(target->getCards(flag).length() - player->getCards(flag).length()) <= player->getLostHp() + 1)
                result << flag;
        return result;
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->inMyAttackRange(p) && !areas(player, p).isEmpty())
                result << p;
        return result;
    }
};

// ---------------------------------------------------------------- sp007

class ThMengsheng : public TriggerSkillV2
{
public:
    ThMengsheng() : TriggerSkillV2("thmengsheng") { events << Damaged << EventPhaseChanging << Death; }

    // Lasts until the end of the owner's next turn.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->getTag("ThMengshengRecord").toBool())
            return false;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive || player->hasFlag("thmengsheng"))
                return false;
        } else if (event == Death) {
            if (data.value<DeathStruct>().who != player)
                return false;
        } else {
            return false;
        }
        player->removeTag("ThMengshengRecord");
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            const QStringList blocked = p->getTag("ThMengshengSkills_" + player->objectName()).toStringList();
            foreach (const QString &skill, blocked)
                room->removeSkillInvalidity(p, skill, player->objectName(), objectName());
            p->removeTag("ThMengshengSkills_" + player->objectName());
            bool still = false;
            foreach (ServerPlayer *q, room->getAllPlayers(true))
                if (!p->getTag("ThMengshengSkills_" + q->objectName()).toStringList().isEmpty())
                    still = true;
            if (!still)
                room->setPlayerMark(p, "@mengsheng", 0);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damaged || !player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.to != player || !damage.from || !damage.from->isAlive())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.original_data->value<DamageStruct>().from)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Every non-专属 skill of the source stops working.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        if (!from || !from->isAlive())
            return false;
        if (!from->isNude()) {
            const int id = room->askForCardChosen(player, from, "he", objectName());
            if (id >= 0)
                room->obtainCard(player, id, false);
        }
        const QString key = "ThMengshengSkills_" + player->objectName();
        QStringList blocked = from->getTag(key).toStringList();
        foreach (const Skill *skill, from->getVisibleSkillList()) {
            const QString name = skill->objectName();
            if (isOwnerOnlySkill(name) || blocked.contains(name))
                continue;
            room->addSkillInvalidity(from, name, player->objectName(), objectName());
            blocked << name;
        }
        from->setTag(key, blocked);
        player->setTag("ThMengshengRecord", true);
        room->setPlayerMark(from, "@mengsheng", 1);
        if (isOwnTurn(player))
            room->setPlayerFlag(player, "thmengsheng");
        return false;
    }
};

class ThQixiang : public TriggerSkillV2
{
public:
    ThQixiang() : TriggerSkillV2("thqixiang") { events << EventPhaseEnd; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Discard)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->canDiscard(owner, "he") && owner->inMyAttackRange(player))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, "..", "@thqixiang:" + player->objectName(), QVariant::fromValue(player),
                              objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->isAlive())
            return false;
        QStringList choices{"draw"};
        if (!player->isKongcheng())
            choices << "discard";
        if (room->askForChoice(ctx.owner, objectName(), choices.join("+"), QVariant::fromValue(player)) == "discard")
            room->askForDiscard(player, objectName(), 1, 1);
        else
            player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- sp008

class ThHuanlongViewAs : public ViewAsSkillV2
{
public:
    ThHuanlongViewAs() : ViewAsSkillV2("thhuanlong", 2) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || !self->hasFlag("thhuanlong"))
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(self);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && (request.pattern.contains("slash") || request.pattern.contains("Slash"));
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.size() < 2 && ownsHandCard(request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2)
            return false;
        foreach (int id, request.selectedCardIds)
            if (!ownsHandCard(request.initiator, Sanguosha->getCard(id)))
                return false;
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
};

class ThHuanlong : public TriggerSkillV2
{
public:
    ThHuanlong() : TriggerSkillV2("thhuanlong")
    {
        events << EventPhaseStart << EventPhaseChanging;
        view_as_skill = new ThHuanlongViewAs;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            if (player->hasFlag("thhuanlong"))
                room->setPlayerFlag(player, "-thhuanlong");
            room->setPlayerMark(player, "thhuanlong1", 0);
            room->setPlayerMark(player, "thhuanlong2", 0);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play)
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

    // Each pick lowers the hand limit by one.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        bool first = true;
        for (;;) {
            QStringList choices{"thhuanlong1", "thhuanlong2"};
            if (!player->hasFlag("thhuanlong"))
                choices << "thhuanlong3";
            if (!first)
                choices << "cancel";
            const QString choice = room->askForChoice(player, objectName(), choices.join("+"));
            if (choice == "cancel")
                break;
            LogMessage log;
            log.type = "#ThHuanlong";
            log.from = player;
            log.arg = objectName();
            log.arg2 = choice.right(1);
            room->sendLog(log);
            if (choice == "thhuanlong3")
                room->setPlayerFlag(player, "thhuanlong");
            else
                room->addPlayerMark(player, choice);
            if (player->getMaxCards() <= 0)
                break;
            first = false;
        }
        return false;
    }
};

class ThHuanlongTargetMod : public TargetModSkillV2
{
public:
    ThHuanlongTargetMod() : TargetModSkillV2("#thhuanlong-target", "Slash") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.primary || ctx.primary->getMark("thhuanlong2") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.primary->getMark("thhuanlong2"));
    }
};

class ThHuanlongRange : public AttackRangeSkillV2
{
public:
    ThHuanlongRange() : AttackRangeSkillV2("#thhuanlong-range") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("thhuanlong1") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.primary->getMark("thhuanlong1"));
    }
};

class ThHuanlongMaxCards : public MaxCardsSkillV2
{
public:
    ThHuanlongMaxCards() : MaxCardsSkillV2("#thhuanlong-max-cards") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary)
            return CorrectSkillResult::noEffect();
        int n = ctx.primary->getMark("thhuanlong1") + ctx.primary->getMark("thhuanlong2");
        if (ctx.primary->hasFlag("thhuanlong"))
            ++n;
        if (n == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(-n);
    }
};

// ---------------------------------------------------------------- sp009

// The current character's card answers a request for a basic card the owner must use.
class ThYudu : public TriggerSkillV2
{
public:
    ThYudu() : TriggerSkillV2("thyudu") { events << CardAsked; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const QStringList asked = data.toStringList();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || isOwnTurn(player) || asked.size() < 3
            || asked.at(2) != "use" || player->getMark("thyudu-Clear") > 0 || !someonesTurn(room))
            return TriggerList();
        ServerPlayer *current = room->getCurrent();
        if (current == player || current->isKongcheng() || basicNames(player, asked.first()).isEmpty())
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
        ServerPlayer *current = room->getCurrent();
        room->addPlayerMark(player, "thyudu-Clear");
        if (!current || current->isKongcheng())
            return false;
        const QString pattern = ctx.original_data->toStringList().first();
        const int id = room->askForCardChosen(player, current, "h", objectName());
        if (id < 0)
            return false;
        room->showCard(current, id);
        const Card *card = Sanguosha->getCard(id);
        if (Sanguosha->matchExpPattern(pattern, player, card) && player->askForSkillInvoke("thyudu_use", "use")) {
            room->provide(card);
            return true;
        }
        const QStringList names = basicNames(player, pattern);
        if (card->getSuit() == Card::Club && !names.isEmpty() && player->askForSkillInvoke("thyudu_use", "change")) {
            const QString name = names.size() == 1 ? names.first() : room->askForChoice(player, objectName(), names.join("+"));
            Card *converted = Sanguosha->cloneCard(name, card->getSuit(), card->getNumber());
            if (converted) {
                converted->addSubcard(card);
                converted->setSkillName(objectName());
                room->provide(converted);
                return true;
            }
        }
        return false;
    }

private:
    static QStringList basicNames(const Player *player, const QString &pattern)
    {
        QStringList names;
        const QStringList candidates{"slash", "fire_slash", "thunder_slash", "jink", "peach", "analeptic"};
        foreach (const QString &name, candidates) {
            QScopedPointer<Card> card(Sanguosha->cloneCard(name, Card::NoSuit, 0));
            if (card && ExpPattern(pattern).match(player, card.data()))
                names << name;
        }
        return names;
    }
};

class ThZhaoguo : public ViewAsSkillV2
{
public:
    ThZhaoguo() : ViewAsSkillV2("thzhaoguo") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsCard(request.initiator, card) && card->getSuit() == Card::Club
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty())
            return false;
        foreach (int id, request.selectedCardIds)
            if (!canSelectCard(request, Sanguosha->getCard(id)))
                return false;
        return true;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThZhaoguoCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !ctx.use_card)
            return FinishSkill;
        Room *room = source->getRoom();
        const QList<int> ids = room->getNCards(ctx.use_card->subcardsLength(), false);
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, source->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        QList<int> spades;
        QList<int> others;
        foreach (int id, ids)
            (Sanguosha->getCard(id)->getSuit() == Card::Spade ? spades : others) << id;
        if (!others.isEmpty()) {
            DummyCard dummy(others);
            room->obtainCard(source, &dummy);
        }
        if (spades.isEmpty())
            return FinishSkill;
        DummyCard shown(spades);
        source->setTag("ThZhaoguoData", QVariant::fromValue<const Card *>(&shown));
        ServerPlayer *target = room->askForPlayerChosen(source, room->getOtherPlayers(source), objectName(), "@thzhaoguo");
        source->removeTag("ThZhaoguoData");
        QList<int> rest;
        foreach (int id, spades)
            if (room->getCardPlace(id) == Player::PlaceTable)
                rest << id;
        if (!target || rest.isEmpty()) {
            if (!rest.isEmpty()) {
                DummyCard dummy(rest);
                CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), objectName(), QString());
                room->throwCard(&dummy, toPile, nullptr);
            }
            return FinishSkill;
        }
        DummyCard dummy(rest);
        room->obtainCard(target, &dummy);
        if (target->isAlive() && target->getHandcardNum() > source->getHandcardNum())
            target->turnOver();
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- sp010

class ThLunmin : public ViewAsSkillV2
{
public:
    ThLunmin() : ViewAsSkillV2("thlunmin", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    int getMaxUsageLimit(const SkillContext &) const override { return 3; }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "ThLunminCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive())
            ctx.invoker->drawCards(1, objectName());
        return FinishSkill;
    }
};

class ThYupan : public TriggerSkillV2
{
public:
    ThYupan() : TriggerSkillV2("thyupan") { events << PreCardUsed << CardResponded << BeforeCardsMove << EventPhaseStart; }

    // The suits the owner used or discarded this turn, as bits.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !isOwnTurn(player) || !player->hasSkill(objectName(), true))
            return false;
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill)
                note(room, player, use.card);
        } else if (event == CardResponded) {
            const CardResponseStruct resp = data.value<CardResponseStruct>();
            if (resp.m_isUse && resp.m_card)
                note(room, player, resp.m_card);
        } else if (event == BeforeCardsMove) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
                foreach (int id, move.card_ids)
                    note(room, player, Sanguosha->getCard(id));
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Finish || player->getMark("thyupan-Clear") != 0xF || wounded(room).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, wounded(room), objectName(), "@thyupan", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (target && target->isAlive() && target->isWounded())
            room->recover(target, RecoverStruct(objectName(), ctx.owner));
        return false;
    }

private:
    static void note(Room *room, ServerPlayer *player, const Card *card)
    {
        const int suit = int(card->getSuit());
        if (suit < 0 || suit > 3)
            return;
        const int mark = player->getMark("thyupan-Clear");
        if ((mark & (1 << suit)) == 0)
            room->setPlayerMark(player, "thyupan-Clear", mark | (1 << suit));
    }

    static QList<ServerPlayer *> wounded(Room *room)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->isWounded())
                result << p;
        return result;
    }
};

// ---------------------------------------------------------------- sp011

class ThJiuzhang : public FilterSkill
{
public:
    ThJiuzhang() : FilterSkill("thjiuzhang") {}

    bool viewFilter(const Card *to_select) const override { return to_select->getNumber() > 9; }

    const Card *viewAs(const Card *original) const override
    {
        Card *card = Sanguosha->cloneCard(original->objectName(), original->getSuit(), 9);
        card->setSkillName(objectName());
        return card;
    }
};

class ThShushuViewAs : public ViewAsSkillV2
{
public:
    ThShushuViewAs() : ViewAsSkillV2("thshushu") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thshushu";
    }

    // The numbers must add up to the "thshushu" mark ThShushu sets while asking.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ownsCard(request.initiator, card) || request.initiator->isJilei(card) || card->getNumber() <= 0)
            return false;
        int sum = 0;
        foreach (int id, request.selectedCardIds)
            sum += Sanguosha->getCard(id)->getNumber();
        return sum + card->getNumber() <= request.initiator->getMark("thshushu");
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        int sum = 0;
        foreach (int id, request.selectedCardIds) {
            const Card *card = Sanguosha->getCard(id);
            if (!ownsCard(request.initiator, card))
                return false;
            sum += card->getNumber();
        }
        return !request.selectedCardIds.isEmpty() && sum == request.initiator->getMark("thshushu");
    }

    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "DummyCard"; }
};

class ThShushu : public TriggerSkillV2
{
public:
    ThShushu() : TriggerSkillV2("thshushu")
    {
        events << TargetSpecifying << TargetConfirming;
        view_as_skill = new ThShushuViewAs;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
            || (event == TargetSpecifying ? use.from != player : !use.to.contains(player))
            || !(use.card->isKindOf("BasicCard") || (use.card->isNDTrick() && use.card->isBlack())))
            return TriggerList();
        const QList<int> ids = use.card->isVirtualCard() ? use.card->getSubcards() : QList<int>{use.card->getId()};
        if (ids.isEmpty() || room->getCardIdsOnTable(ids).size() != ids.size() || !canReach(player, use.card->getNumber()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        room->setPlayerMark(ctx.owner, "thshushu", use.card->getNumber());
        ctx.owner->setTag("ThShushuData", *ctx.original_data);
        const Card *card = room->askForCard(ctx.owner, "@@thshushu",
                                            QString("@thshushu:::%1:%2").arg(use.card->objectName()).arg(use.card->getNumber()),
                                            *ctx.original_data, Card::MethodNone);
        ctx.owner->removeTag("ThShushuData");
        room->setPlayerMark(ctx.owner, "thshushu", 0);
        if (!card)
            return false;
        QVariantList ids;
        foreach (int id, card->getSubcards())
            ids << id;
        ctx.extra_data = ids;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // The chosen cards take the card's place on the table and the owner gets the card;
    // the use goes on as the same card (name, suit, number) made of the chosen cards.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<int> ids;
        foreach (const QVariant &id, ctx.extra_data.toList())
            ids << id.toInt();

        LogMessage log;
        log.type = "$ThShushu";
        log.from = player;
        log.arg = objectName();
        log.card_str = ListI2S(ids).join("+");
        room->sendLog(log);

        CardMoveReason reason(CardMoveReason::S_REASON_OVERRIDE, player->objectName(), objectName(), QString());
        QList<CardsMoveStruct> moves;
        moves << CardsMoveStruct(ids, nullptr, Player::PlaceTable, reason)
              << CardsMoveStruct(room->getCardIdsOnTable(use.card), player, Player::PlaceHand, reason);
        room->moveCardsAtomic(moves, true);

        Card *card = Sanguosha->cloneCard(use.card->objectName(), use.card->getSuit(), use.card->getNumber());
        card->addSubcards(ids);
        card->setSkillName(objectName());
        use.changeCard(card);
        use.setOwnedCard(card);
        // A replacement, not a conversion: the old card's skills no longer apply to it.
        card->clearChangeCards();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }

private:
    // Whether some of the player's cards have numbers adding up to `number`.
    static bool canReach(const ServerPlayer *player, int number)
    {
        if (number <= 0 || number >= 64)
            return false;
        quint64 sums = 1;
        foreach (const Card *card, player->getCards("he"))
            if (!card->hasFlag("using") && !player->isJilei(card) && card->getNumber() > 0 && card->getNumber() <= number)
                sums |= sums << card->getNumber();
        return sums >> number & 1;
    }
};

class ThFenglingViewAs : public ViewAsSkillV2
{
public:
    ThFenglingViewAs() : ViewAsSkillV2("thfengling") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thfengling"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ownsCard(request.initiator, card) || !request.initiator->canDiscard(request.initiator, card->getEffectiveId()))
            return false;
        int sum = 0;
        foreach (int id, request.selectedCardIds)
            sum += Sanguosha->getCard(id)->getNumber();
        return sum < 9 && sum + card->getNumber() <= 9;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        int sum = 0;
        foreach (int id, request.selectedCardIds) {
            const Card *card = Sanguosha->getCard(id);
            if (!ownsCard(request.initiator, card))
                return false;
            sum += card->getNumber();
        }
        return !request.selectedCardIds.isEmpty() && sum == 9;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThFenglingCard"; }
};

class ThFengling : public TriggerSkillV2
{
public:
    ThFengling() : TriggerSkillV2("thfengling")
    {
        events << Dying;
        frequency = Limited;
        limit_mark = "@fengling";
        view_as_skill = new ThFenglingViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DyingStruct>().who != player || !player->isAlive() || player->getHp() > 0
            || !player->hasSkill(objectName()) || player->getMark(limit_mark) == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->removePlayerMark(ctx.owner, limit_mark);
        room->doSuperLightbox(ctx.owner, objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        player->drawCards(5, objectName());
        while (player->isAlive() && player->isWounded() && player->canDiscard(player, "he")
               && room->askForUseCard(player, "@@thfengling", "@thfengling", -1, Card::MethodDiscard))
            room->recover(player, RecoverStruct(objectName(), player));
        return false;
    }
};

// ---------------------------------------------------------------- sp012

class ThYingshi : public ViewAsSkillV2
{
public:
    ThYingshi() : ViewAsSkillV2("thyingshi") { setPhaseName("Play"); }

    // Three shots at distances 1, 2, 3; a declined shot ends the phase's run.
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 3; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || self->getMark("thyingshi_failed-PlayClear") > 0)
            return false;
        const int n = self->getMark("thyingshi_times-PlayClear");
        if (n >= 3)
            return false;
        foreach (const Player *p, self->getAliveSiblings())
            if (self->distanceTo(p) == n + 1 && self->canSlash(p, false))
                return true;
        return false;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThYingshiCard"; }

    // A 杀 against a character exactly n away, outside the use limit.
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !player->isAlive())
            return FinishSkill;
        Room *room = player->getRoom();
        const int n = player->getMark("thyingshi_times-PlayClear") + 1;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->distanceTo(p) == n)
                targets << p;
        if (targets.isEmpty() || !room->askForUseSlashTo(player, targets, "@thyingshi:::" + QString::number(n), false, true)) {
            room->setPlayerMark(player, "thyingshi_failed-PlayClear", 1);
            return FinishSkill;
        }
        room->setPlayerMark(player, "@yingshi" + QString::number(n - 1), 0);
        room->setPlayerMark(player, "@yingshi" + QString::number(n), 1);
        room->addPlayerMark(player, "thyingshi_times-PlayClear");
        return FinishSkill;
    }
};

class ThYingshiClear : public TriggerSkillV2
{
public:
    ThYingshiClear() : TriggerSkillV2("#thyingshi") { events << EventPhaseChanging; }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (int i = 1; i <= 3; ++i)
                if (player->getMark("@yingshi" + QString::number(i)) > 0)
                    room->setPlayerMark(player, "@yingshi" + QString::number(i), 0);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThZanghun : public TriggerSkillV2
{
public:
    ThZanghun() : TriggerSkillV2("thzanghun") { events << Damage; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.card
            || !damage.card->isKindOf("Slash"))
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
        ctx.owner->drawCards(1, objectName());
        room->addPlayerMark(ctx.owner, "@gallop-Clear");
        return false;
    }
};

class ThZanghunDistance : public DistanceSkill
{
public:
    ThZanghunDistance() : DistanceSkill("#thzanghun-distance") { frequency = Compulsory; }

    int getCorrect(const Player *from, const Player *) const override { return from ? from->getMark("@gallop-Clear") : 0; }
};

// ---------------------------------------------------------------- sp013

class ThYimeng : public TriggerSkillV2
{
public:
    ThYimeng() : TriggerSkillV2("thyimeng") { events << Damage; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || player->isKongcheng() || !damage.to)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && (damage.to == owner || owner->isAdjacentTo(damage.to)))
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
        if (player->isKongcheng())
            return false;
        const int id = room->askForCardChosen(ctx.owner, player, "h", objectName());
        if (id >= 0)
            room->obtainCard(ctx.owner, id, false);
        if (player->isAlive())
            player->drawCards(1, objectName());
        return false;
    }
};

// The 杀 request runs under the generic cardIgnoreLegality flag, so its first target may
// be any character. #thxuyou clears the flag at PreCardUsed, after the server's target
// check. ThXuyouUse marks the request for #thxuyou.
class ThXuyou : public ViewAsSkillV2
{
public:
    ThXuyou() : ViewAsSkillV2("thxuyou") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    // The 杀 still counts toward the use limit; only its targets ignore legality.
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return false;
        Slash slash(Card::NoSuit, 0);
        return self->canSlashWithoutCrossbow(&slash);
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThXuyouCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !player->isAlive())
            return FinishSkill;
        Room *room = player->getRoom();
        room->setPlayerFlag(player, "ThXuyouUse");
        room->setPlayerFlag(player, "cardIgnoreLegality");
        room->askForUseCard(player, "slash", "@thxuyou");
        if (player->hasFlag("ThXuyouUse")) {
            room->setPlayerFlag(player, "-ThXuyouUse");
            room->setPlayerFlag(player, "-cardIgnoreLegality");
        }
        return FinishSkill;
    }
};

// Each damage of the 虚遊 杀 draws one card; a 杀 that dealt none discards one card
// from each character adjacent to the user.
class ThXuyouEffect : public TriggerSkillV2
{
public:
    ThXuyouEffect() : TriggerSkillV2("#thxuyou")
    {
        events << PreCardUsed << PreDamageDone << Damage << CardFinished;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && use.from->hasFlag("ThXuyouUse") && use.card && use.card->isKindOf("Slash")) {
                room->setPlayerFlag(use.from, "-ThXuyouUse");
                room->setPlayerFlag(use.from, "-cardIgnoreLegality");
                room->setCardFlag(use.card, "thxuyou_slash");
            }
        } else if (event == PreDamageDone) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->hasFlag("thxuyou_slash"))
                room->setCardFlag(damage.card, "thxuyou_damage");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (event == Damage) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from == player && damage.card && damage.card->hasFlag("thxuyou_slash"))
                return TriggerList{{player, {objectName()}}};
        } else if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !use.card || !use.card->hasFlag("thxuyou_slash")
                || use.card->hasFlag("thxuyou_damage"))
                return TriggerList();
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (player->isAdjacentTo(p) && player->canDiscard(p, "he"))
                    return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, "thxuyou");
        room->broadcastSkillInvoke("thxuyou");
        if (event == Damage) {
            player->drawCards(1, "thxuyou");
            return false;
        }
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!player->isAlive())
                break;
            if (player->isAdjacentTo(p) && player->canDiscard(p, "he")) {
                const int id = room->askForCardChosen(player, p, "he", "thxuyou", false, Card::MethodDiscard);
                room->throwCard(id, p, player);
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------- sp014

class ThHuanghu : public TriggerSkillV2
{
public:
    ThHuanghu() : TriggerSkillV2("thhuanghu") { events << CardUsed << CardResponded; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->canDiscard(player, "h"))
            return TriggerList();
        ServerPlayer *source = sourceOf(event, player, data);
        if (!source || source == player || !source->isAlive())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = sourceOf(event, ctx.owner, *ctx.original_data);
        if (!target)
            return false;
        ctx.owner->setTag("ThHuanghuTarget", QVariant::fromValue(target));
        const Card *card = room->askForExchange(ctx.owner, objectName(), 998, 1, false, "@thhuanghu:" + target->objectName(),
                                                true);
        ctx.owner->removeTag("ThHuanghuTarget");
        if (!card || card->getSubcards().isEmpty())
            return false;
        LogMessage log;
        log.type = "#ChoosePlayerWithSkill";
        log.from = ctx.owner;
        log.to << target;
        log.arg = objectName();
        room->sendLog(log);
        const int n = card->subcardsLength();
        room->throwCard(card, ctx.owner);
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = QVariantList{target->objectName(), n};
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariantList data = ctx.extra_data.toList();
        if (data.size() != 2)
            return false;
        ServerPlayer *target = room->findPlayerByObjectName(data.first().toString());
        const int n = data.last().toInt();
        if (target && target->isAlive() && n > 0 && target->canDiscard(target, "he"))
            room->askForDiscard(target, objectName(), n, n, false, true);
        return false;
    }

private:
    // The character whose card the owner's 闪 answered.
    static ServerPlayer *sourceOf(TriggerEvent event, ServerPlayer *player, const QVariant &data)
    {
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            return use.from == player && use.card && use.card->isKindOf("Jink") ? use.who : nullptr;
        }
        const CardResponseStruct resp = data.value<CardResponseStruct>();
        return resp.m_card && resp.m_card->isKindOf("Jink") ? resp.m_who : nullptr;
    }
};

class ThLinyao : public TriggerSkillV2
{
public:
    ThLinyao() : TriggerSkillV2("thlinyao") { events << CardAsked; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const QStringList asked = data.toStringList();
        if (!player || !player->isAlive() || asked.isEmpty() || asked.first() != "jink")
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && !owner->faceUp())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (ctx.owner->faceUp() || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        ctx.owner->turnOver();
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        Jink *jink = new Jink(Card::NoSuit, 0);
        jink->setSkillName("_thlinyao");
        room->provide(jink);
        return true;
    }
};

class ThFeijing : public TriggerSkillV2
{
public:
    ThFeijing() : TriggerSkillV2("thfeijing") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !move.is_last_handcard || !player->isAlive() || !player->hasSkill(objectName()))
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
        if (player->getHandcardNum() < player->getMaxHp())
            player->drawCards(player->getMaxHp() - player->getHandcardNum(), objectName());
        player->turnOver();
        return false;
    }
};

// ---------------------------------------------------------------- sp015

class ThOuji : public TriggerSkillV2
{
public:
    ThOuji() : TriggerSkillV2("thouji")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    // Answered on the losing character's dispatch, once per move.
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !player->isAlive() || !move.from_places.contains(Player::PlaceEquip))
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && isOwnTurn(owner))
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
        ServerPlayer *player = ctx.owner;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canDiscard(p, "h"))
                targets << p;
        ServerPlayer *target = targets.isEmpty() ? nullptr : room->askForPlayerChosen(player, targets, objectName(), "@thouji", true);
        if (target) {
            const int id = room->askForCardChosen(player, target, "h", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, target, player);
        } else {
            player->drawCards(1, objectName());
        }
        return false;
    }
};

class ThJingyuansp : public ViewAsSkillV2
{
public:
    ThJingyuansp() : ViewAsSkillV2("thjingyuansp", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->isKindOf("BasicCard")
            && card->isRed() && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
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
        return selected.length() < 2 && to && to->hasEquip();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty() && selected.length() <= 2;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThJingyuanspCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        const bool forced = source == target || target->isKongcheng();
        const Card *card = target->hasEquip()
            ? room->askForCard(target, ".|.|.|equipped",
                               QString(forced ? "@thjingyuansp-give:%1" : "@thjingyuansp:%1").arg(source->objectName()),
                               QVariant(), Card::MethodNone)
            : nullptr;
        if (!card && forced && target->hasEquip()) {
            const QList<const Card *> equips = target->getEquips();
            card = equips.at(qsanRandomBounded(equips.length()));
        }
        if (card) {
            ServerPlayer *to = room->askForPlayerChosen(target, room->getOtherPlayers(target), objectName(),
                                                        "@thjingyuansp-to");
            if (to)
                room->giveCard(target, to, card, objectName(), true);
        } else if (!target->isKongcheng() && source->isAlive()) {
            const int id = room->askForCardChosen(source, target, "h", objectName());
            if (id >= 0)
                room->obtainCard(source, id, false);
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- sp016

class ThFeihu : public ViewAsSkillV2
{
public:
    ThFeihu() : ViewAsSkillV2("thfeihu") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        const Player *self = request.initiator;
        return self && to && selected.isEmpty() && to->getHp() <= self->getHp() && (self->getLostHp() > 2 || to != self);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThFeihuCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        QStringList choices;
        if (source->canDiscard(target, "he"))
            choices << "recover";
        choices << "damage";
        if (room->askForChoice(source, objectName(), choices.join("+"), QVariant::fromValue(target)) == "recover") {
            if (source == target) {
                room->askForDiscard(target, objectName(), 1, 1, false, true);
            } else {
                const int id = room->askForCardChosen(source, target, "he", objectName(), false, Card::MethodDiscard);
                if (id >= 0)
                    room->throwCard(id, target, source);
            }
            if (target->isAlive())
                room->recover(target, RecoverStruct(objectName(), source));
        } else {
            room->damage(DamageStruct(objectName(), source, target));
            if (target->isAlive()) {
                room->recover(target, RecoverStruct(objectName(), source));
                target->drawCards(1, objectName());
            }
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- sp017

class ThHuanling : public TriggerSkillV2
{
public:
    ThHuanling() : TriggerSkillV2("thhuanling")
    {
        events << Death;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || data.value<DeathStruct>().who != player)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner != player && owner->hasSkill(objectName()))
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
        ServerPlayer *player = ctx.owner;
        player->drawCards(2, objectName());
        QStringList skills;
        const QStringList candidates{"ikchilian", "ikmeiying", "ikjingmu"};
        foreach (const QString &skill, candidates)
            if (!player->hasSkill(skill, true))
                skills << skill;
        if (skills.isEmpty() || !player->isAlive())
            return false;
        room->acquireSkillFromEffect(player, room->askForChoice(player, objectName(), skills.join("+")), ctx);
        return false;
    }
};

// Upstream counts 幻属性 characters, which this port keeps as the female flag.
class ThYoukong : public DistanceSkill
{
public:
    ThYoukong() : DistanceSkill("thyoukong") { frequency = Compulsory; }

    int getCorrect(const Player *from, const Player *) const override
    {
        if (!from || !from->hasSkill(objectName()))
            return 0;
        int n = from->isFemale() ? -1 : 0;
        foreach (const Player *p, from->getAliveSiblings())
            if (p->isFemale())
                --n;
        return n;
    }
};

class IkChilian : public ViewAsSkillV2
{
public:
    IkChilian() : ViewAsSkillV2("ikchilian", 1)
    {
        setResponseOrUse(true);
    }

    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false, false, true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(request.initiator);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && (request.pattern.contains("slash") || request.pattern.contains("Slash"));
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->isRed();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        return name == "slash" || name == "fire_slash";
    }
};

class IkMeiying : public TriggerSkillV2
{
public:
    IkMeiying() : TriggerSkillV2("ikmeiying")
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
        QList<ServerPlayer *> others;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (!p->isKongcheng())
                others << p;
        QStringList choices{"play"};
        if (!others.isEmpty())
            choices << "view";
        if (room->askForChoice(player, objectName(), choices.join("+")) == "view") {
            ServerPlayer *target = room->askForPlayerChosen(player, others, objectName(), "@ikmeiying");
            if (target)
                room->showAllCards(target, player);
        } else {
            player->insertPhase(Player::Play);
        }
        return false;
    }
};

class IkJingmu : public TriggerSkillV2
{
public:
    IkJingmu() : TriggerSkillV2("ikjingmu") { events << BeforeCardsMove; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || move.to_place != Player::DiscardPile
            || move.reason.m_playerId.isEmpty() || move.reason.m_playerId == player->objectName()
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_RESPONSE)
            return TriggerList();
        foreach (int id, move.card_ids)
            if (Sanguosha->getCard(id)->isKindOf("Slash"))
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
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
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> ids;
        foreach (int id, move.card_ids)
            if (Sanguosha->getCard(id)->isKindOf("Slash"))
                ids << id;
        move.removeCardIds(ids);
        *ctx.original_data = QVariant::fromValue(move);
        DummyCard dummy(ids);
        room->obtainCard(ctx.owner, &dummy);
        return false;
    }
};

// ---------------------------------------------------------------- sp018

ServerPlayer *roomLord(Room *room)
{
    foreach (ServerPlayer *p, room->getAlivePlayers())
        if (p->isLord())
            return p;
    return nullptr;
}

class ThGuanzhiViewAs : public ViewAsSkillV2
{
public:
    ThGuanzhiViewAs() : ViewAsSkillV2("thguanzhi") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thguanzhi"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &, const Player *to) const override
    {
        const Player *self = request.initiator;
        if (!self || !to || to == self)
            return false;
        const Player *lord = self->isLord() ? self : nullptr;
        if (!lord)
            foreach (const Player *p, self->getAliveSiblings())
                if (p->isLord())
                    lord = p;
        return lord && to->inMyAttackRange(lord) && self->canDiscard(to, "he");
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty();
    }

    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThGuanzhiCard"; }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source)
            return FinishSkill;
        Room *room = source->getRoom();
        foreach (ServerPlayer *p, targets) {
            if (!source->isAlive() || !p->isAlive() || !source->canDiscard(p, "he"))
                continue;
            const int id = room->askForCardChosen(source, p, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, p, source);
        }
        QList<ServerPlayer *> alive;
        foreach (ServerPlayer *p, targets)
            if (p->isAlive())
                alive << p;
        room->drawCards(alive, 1, objectName());
        ServerPlayer *lord = roomLord(room);
        if (!lord || !source->isAlive())
            return FinishSkill;
        foreach (ServerPlayer *p, alive)
            if (p->getHandcardNum() > lord->getHandcardNum()) {
                source->drawCards(1, objectName());
                break;
            }
        return FinishSkill;
    }
};

class ThGuanzhi : public TriggerSkillV2
{
public:
    ThGuanzhi() : TriggerSkillV2("thguanzhi")
    {
        events << EventPhaseStart;
        view_as_skill = new ThGuanzhiViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish)
            return TriggerList();
        ServerPlayer *lord = roomLord(room);
        if (!lord)
            return TriggerList();
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (p->inMyAttackRange(lord) && player->canDiscard(p, "he"))
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thguanzhi", "@thguanzhi");
        return false;
    }
};

class ThFuhuaViewAs : public ViewAsSkillV2
{
public:
    ThFuhuaViewAs() : ViewAsSkillV2("thfuhua") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thfuhua"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsCard(request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty())
            return false;
        foreach (int id, request.selectedCardIds)
            if (!ownsCard(request.initiator, Sanguosha->getCard(id)))
                return false;
        return true;
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThFuhuaCard"; }

    // Only records the shown cards; the trigger resolves them.
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.use_card)
            ctx.invoker->setTag("ThFuhuaIds", ListI2V(ctx.use_card->getSubcards()));
        return FinishSkill;
    }
};

class ThFuhua : public TriggerSkillV2
{
public:
    ThFuhua() : TriggerSkillV2("thfuhua")
    {
        events << DamageInflicted;
        view_as_skill = new ThFuhuaViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.from
            || damage.from == player || !damage.from->isAlive() || player->isNude()
            || damage.from->getMark("thfuhua_" + player->objectName()) > 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->removeTag("ThFuhuaIds");
        if (!room->askForUseCard(ctx.owner, "@@thfuhua", "@thfuhua", -1, Card::MethodNone))
            return false;
        return !ctx.owner->getTag("ThFuhuaIds").toList().isEmpty();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *from = damage.from;
        QList<int> ids = ListV2I(player->getTag("ThFuhuaIds").toList());
        player->removeTag("ThFuhuaIds");
        if (!from || !from->isAlive() || ids.isEmpty())
            return false;
        room->addPlayerMark(from, "@comfort");
        room->setPlayerMark(from, "thfuhua_" + player->objectName(), 1);
        room->fillAG(ids, from);
        LogMessage show;
        show.type = "$ShowCard";
        show.from = player;
        show.card_str = ListI2S(ids).join("+");
        room->sendLog(show, from);
        LogMessage log;
        log.type = "#ThFuhua";
        log.from = player;
        log.to << from;
        log.arg = QString::number(ids.length());
        room->sendLog(log, room->getOtherPlayers(from));
        const int id = room->askForAG(from, ids, from->getCardCount() >= ids.length(), objectName());
        room->clearAG(from);
        if (ids.contains(id)) {
            if (room->getCardOwner(id) == player)
                room->obtainCard(from, id);
            LogMessage prevented;
            prevented.type = "#ThFuhua2";
            prevented.from = player;
            prevented.to << from;
            prevented.arg = QString::number(damage.damage);
            room->sendLog(prevented);
            return true;
        }
        room->askForDiscard(from, objectName(), ids.length(), ids.length(), false, true);
        return false;
    }
};

// ---------------------------------------------------------------- sp019

// X counts every damage the owner has dealt this game; a short target list resets it.
class ThYongjie : public TriggerSkillV2
{
public:
    ThYongjie() : TriggerSkillV2("thyongjie") { events << PreDamageDone << PreCardUsed << TargetSpecified; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != PreDamageDone)
            return false;
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (from && from->isAlive()) {
            room->addPlayerMark(from, "doom");
            if (from->hasSkill(objectName(), true))
                room->setPlayerMark(from, "@doom", from->getMark("doom"));
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == PreDamageDone || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getMark("@doom") == 0)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card)
            return TriggerList();
        if (event == PreCardUsed) {
            // 无中生有 only targets its user; let the owner add others.
            if (use.card->isKindOf("ExNihilo") && use.card->isBlack() && !extraTargets(room, player, use).isEmpty())
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        if ((use.card->isKindOf("Slash") || (use.card->isNDTrick() && use.card->isBlack()))
            && use.to.length() < player->getMark("@doom"))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != PreCardUsed)
            return true;
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        bool added = false;
        for (int i = 0; i < player->getMark("@doom"); ++i) {
            const QList<ServerPlayer *> targets = extraTargets(room, player, use);
            if (targets.isEmpty())
                break;
            ServerPlayer *extra = room->askForPlayerChosen(player, targets, objectName(),
                                                           "@thyongye-add:::" + use.card->objectName(), true);
            if (!extra)
                break;
            use.to << extra;
            room->sortByActionOrder(use.to);
            LogMessage log;
            log.type = "#ThYongyeAdd";
            log.from = player;
            log.to << extra;
            log.arg = objectName();
            log.card_str = use.card->toString();
            room->sendLog(log);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), extra->objectName());
            added = true;
        }
        if (added)
            *ctx.original_data = QVariant::fromValue(use);
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->setPlayerMark(ctx.owner, "doom", 0);
        room->setPlayerMark(ctx.owner, "@doom", 0);
        return false;
    }

private:
    static QList<ServerPlayer *> extraTargets(Room *room, ServerPlayer *player, const CardUseStruct &use)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (!use.to.contains(p) && !room->isProhibited(player, p, use.card))
                result << p;
        return result;
    }
};

// Collateral needs a second victim per added holder; it is left out.
class ThYongjieTargetMod : public TargetModSkillV2
{
public:
    ThYongjieTargetMod() : TargetModSkillV2("#thyongjie-tar", "Slash,TrickCard") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::ExtraTarget || !ctx.primary || !ctx.card || !ctx.primary->hasSkill("thyongjie"))
            return CorrectSkillResult::noEffect();
        const Card *card = ctx.card;
        const bool fits = card->isKindOf("Slash")
            || (card->isNDTrick() && card->isBlack() && !card->isKindOf("Collateral") && !card->isKindOf("ExNihilo"));
        if (!fits || ctx.primary->getMark("@doom") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.primary->getMark("@doom"));
    }
};

// ---------------------------------------------------------------- sp020

// thhuanyao_step: 0 basic names only; 1 adds tricks; 2 the owner declares.
class ThHuanyaoViewAs : public ViewAsSkillV2
{
public:
    ThHuanyaoViewAs() : ViewAsSkillV2("thhuanyao", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        const QStringList record = self->property("thhuanyao").toString().split("->");
        if (record.size() < 2) {
            return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
                && self->getMark("thhuanyao_used-PlayClear") == 0 && !self->isKongcheng();
        }
        QScopedPointer<Card> card(Sanguosha->cloneCard(record.last()));
        if (!card)
            return false;
        card->setSkillName(objectName());
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return card->isAvailable(self);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && Sanguosha->matchExpPattern(request.pattern, self, card.data());
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !ownsHandCard(request.initiator, card))
            return false;
        const QStringList record = request.initiator->property("thhuanyao").toString().split("->");
        return record.size() < 2 || record.first().toInt() == card->getEffectiveId();
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
        const QStringList record = request.initiator->property("thhuanyao").toString().split("->");
        if (record.size() < 2)
            return ViewAsSkillV2::createCard(request);
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = Sanguosha->cloneCard(record.last(), material->getSuit(), material->getNumber());
        if (!card)
            return nullptr;
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    // The declaring character is the nearest other one, unless the owner declares (step 2).
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        const Player *self = request.initiator;
        if (!self || !to || to == self || !selected.isEmpty() || self->getMark("thhuanyao_step") >= 2)
            return false;
        int nearest = 998;
        foreach (const Player *p, self->getAliveSiblings()) {
            const int d = self->distanceTo(p);
            if (d >= 0 && d < nearest)
                nearest = d;
        }
        return self->distanceTo(to) == nearest;
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (!request.initiator)
            return false;
        return request.initiator->getMark("thhuanyao_step") >= 2 ? selected.isEmpty() : selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThHuanyaoCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator)
            return false;
        room->addPlayerMark(ctx.initiator, "thhuanyao_used-PlayClear");
        return true;
    }

    // From step 2 the owner declares; before that, the chosen nearest character does.
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || ctx.invoker->getMark("thhuanyao_step") < 2)
            return ContinueEffects;
        declare(ctx, ctx.invoker);
        return FinishSkill;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        declare(ctx, target);
        return ContinueEffects;
    }

private:
    void declare(SkillContext &ctx, ServerPlayer *declarer) const
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !declarer || !declarer->isAlive() || !ctx.use_card
            || ctx.use_card->subcardsLength() == 0)
            return;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != source || room->getCardPlace(id) != Player::PlaceHand)
            return;
        room->showCard(source, id);
        QStringList basics;
        QStringList singles;
        QStringList others;
        foreach (const QString &name, Sanguosha->getCardNames()) {
            QScopedPointer<Card> card(Sanguosha->cloneCard(name));
            if (!card)
                continue;
            const QString key = card->isKindOf("Slash") ? QStringLiteral("slash") : name;
            if (card->getTypeId() == Card::TypeBasic) {
                if (!basics.contains(key))
                    basics << key;
            } else if (card->isKindOf("SingleTargetTrick")) {
                if (!singles.contains(key))
                    singles << key;
            } else if (card->isNDTrick() && !others.contains(key)) {
                others << key;
            }
        }
        QStringList groups;
        groups << basics.join("+");
        if (source->getMark("thhuanyao_step") > 0) {
            if (!singles.isEmpty())
                groups << singles.join("+");
            if (!others.isEmpty())
                groups << others.join("+");
        }
        const QString name = room->askForChoice(declarer, objectName(), groups.join("|"));
        LogMessage log;
        log.type = "#RhHuanjie";
        log.from = declarer;
        log.arg = name;
        room->sendLog(log);
        room->setPlayerProperty(source, "thhuanyao", QString("%1->%2").arg(id).arg(name));
    }
};

class ThHuanyao : public TriggerSkillV2
{
public:
    ThHuanyao() : TriggerSkillV2("thhuanyao")
    {
        events << EventPhaseChanging;
        view_as_skill = new ThHuanyaoViewAs;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && data.value<PhaseChangeStruct>().to == Player::NotActive
            && !player->property("thhuanyao").toString().isEmpty())
            room->setPlayerProperty(player, "thhuanyao", QString());
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThHuanyaoProhibit : public ProhibitSkill
{
public:
    ThHuanyaoProhibit() : ProhibitSkill("#thhuanyao") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return from && from == to && card && card->getSkillName() == "thhuanyao";
    }
};

class ThZhouzhu : public TriggerSkillV2
{
public:
    ThZhouzhu() : TriggerSkillV2("thzhouzhu")
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Moves 幻曜 one step along; once both changes are made only the draw is left.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int step = player->getMark("thhuanyao_step");
        QStringList choices;
        if (step == 0)
            choices << "change1";
        else if (step == 1)
            choices << "change2";
        choices << "draw";
        const QString choice = choices.size() > 1 ? room->askForChoice(player, objectName(), choices.join("+")) : "draw";
        if (choice == "change1" || choice == "change2")
            room->setPlayerMark(player, "thhuanyao_step", step + 1);
        else
            player->drawCards(1, objectName());
        return false;
    }
};

}

ThYuduCard::ThYuduCard() { setSkillName("thyudu"); mute = true; }
ThZhaoguoCard::ThZhaoguoCard() { setSkillName("thzhaoguo"); mute = true; }
ThLunminCard::ThLunminCard() { setSkillName("thlunmin"); mute = true; }
ThFenglingCard::ThFenglingCard() { setSkillName("thfengling"); mute = true; }
ThYingshiCard::ThYingshiCard() { setSkillName("thyingshi"); mute = true; }
ThJingyuanspCard::ThJingyuanspCard() { setSkillName("thjingyuansp"); mute = true; }
ThFeihuCard::ThFeihuCard() { setSkillName("thfeihu"); mute = true; }
ThGuanzhiCard::ThGuanzhiCard() { setSkillName("thguanzhi"); mute = true; }
ThFuhuaCard::ThFuhuaCard() { setSkillName("thfuhua"); mute = true; }
ThHuanyaoCard::ThHuanyaoCard() { setSkillName("thhuanyao"); mute = true; }

TouhouSPPackage::TouhouSPPackage()
    : Package("touhou-sp")
{
    General *sp001 = new General(this, "sp001", "kaze", 4, true, true);
    sp001->addSkill(new ThFanshi);
    sp001->addSkill("thjifeng");

    General *sp002 = new General(this, "sp002", "hana", 4, false);
    sp002->addSkill(new ThKongsuo);
    sp002->addSkill(new ThGuimen);
    sp002->addSkill(new ThLiuren);

    General *sp003 = new General(this, "sp003", "yuki", 3);
    sp003->addSkill(new ThShenshi);
    sp003->addSkill(new ThJiefan);

    General *sp004 = new General(this, "sp004", "tsuki", 3);
    sp004->addSkill(new ThChuangshi);
    sp004->addSkill(new ThGaotian);

    General *sp005 = new General(this, "sp005", "kaze");
    sp005->addSkill(new ThWanling);
    sp005->addSkill(new ThZuibu);

    General *sp006 = new General(this, "sp006", "hana");
    sp006->addSkill(new ThModao);

    General *sp007 = new General(this, "sp007", "yuki", 3);
    sp007->addSkill(new ThMengsheng);
    sp007->addSkill(new ThQixiang);

    General *sp008 = new General(this, "sp008", "tsuki");
    sp008->addSkill(new ThHuanlong);
    sp008->addSkill(new ThHuanlongTargetMod);
    sp008->addSkill(new ThHuanlongMaxCards);
    sp008->addSkill(new ThHuanlongRange);
    related_skills.insert("thhuanlong", "#thhuanlong-range");
    related_skills.insert("thhuanlong", "#thhuanlong-target");
    related_skills.insert("thhuanlong", "#thhuanlong-max-cards");

    General *sp009 = new General(this, "sp009", "kaze", 3);
    sp009->addSkill(new ThYudu);
    sp009->addSkill(new ThZhaoguo);

    General *sp010 = new General(this, "sp010", "hana", 3);
    sp010->addSkill(new ThLunmin);
    sp010->addSkill(new ThYupan);

    General *sp011 = new General(this, "sp011", "yuki");
    sp011->addSkill(new ThJiuzhang);
    sp011->addSkill(new ThShushu);
    sp011->addSkill(new ThFengling);

    General *sp012 = new General(this, "sp012", "tsuki");
    sp012->addSkill(new ThYingshi);
    sp012->addSkill(new ThYingshiClear);
    related_skills.insert("thyingshi", "#thyingshi");
    sp012->addSkill(new ThZanghun);
    sp012->addSkill(new ThZanghunDistance);
    related_skills.insert("thzanghun", "#thzanghun-distance");

    General *sp013 = new General(this, "sp013", "kaze", 3, false);
    sp013->addSkill(new ThYimeng);
    sp013->addSkill(new ThXuyou);
    sp013->addSkill(new ThXuyouEffect);
    related_skills.insert("thxuyou", "#thxuyou");

    General *sp014 = new General(this, "sp014", "hana", 3);
    sp014->addSkill(new ThHuanghu);
    sp014->addSkill(new ThLinyao);
    sp014->addSkill(new ThFeijing);

    General *sp015 = new General(this, "sp015", "yuki", 3);
    sp015->addSkill(new ThOuji);
    sp015->addSkill(new ThJingyuansp);

    General *sp016 = new General(this, "sp016", "tsuki");
    sp016->addSkill(new ThFeihu);

    General *sp017 = new General(this, "sp017", "kaze");
    sp017->addSkill(new ThHuanling);
    sp017->addSkill(new ThYoukong);
    sp017->addRelateSkill("ikchilian");
    sp017->addRelateSkill("ikmeiying");
    sp017->addRelateSkill("ikjingmu");

    General *sp018 = new General(this, "sp018", "hana", 3);
    sp018->addSkill(new ThGuanzhi);
    sp018->addSkill(new ThFuhua);

    General *sp019 = new General(this, "sp019", "yuki");
    sp019->addSkill(new ThYongjie);
    sp019->addSkill(new ThYongjieTargetMod);
    related_skills.insert("thyongjie", "#thyongjie-tar");

    General *sp020 = new General(this, "sp020", "tsuki", 3, false);
    sp020->addSkill(new ThHuanyao);
    sp020->addSkill(new ThHuanyaoProhibit);
    related_skills.insert("thhuanyao", "#thhuanyao");
    sp020->addSkill(new ThZhouzhu);

    skills << new IkChilian << new IkMeiying << new IkJingmu;

    addMetaObject<ThYuduCard>();
    addMetaObject<ThZhaoguoCard>();
    addMetaObject<ThLunminCard>();
    addMetaObject<ThFenglingCard>();
    addMetaObject<ThYingshiCard>();
    addMetaObject<ThJingyuanspCard>();
    addMetaObject<ThFeihuCard>();
    addMetaObject<ThGuanzhiCard>();
    addMetaObject<ThFuhuaCard>();
    addMetaObject<ThHuanyaoCard>();
}

ADD_PACKAGE(TouhouSP)

// Xufa notification metadata: exact compiled dynamic types and declaring slots.
// No correction-family bit or event/activation execution authority is granted.
#include "ai-strategy/native-registry.h"

namespace {
template<class Owner> Owner *xufaExtraOwner(int (Owner::*)(const Player *) const);
template<class Owner> Owner *xufaExtraModeOwner(int (Owner::*)(const Player *, MaxCardsType::MaxCardsCount) const);
template<class Owner> Owner *xufaLimitOneOwner(QString (Owner::*)(const Player *) const);
template<class Owner> Owner *xufaLimitTwoOwner(QString (Owner::*)(const Player *, const Card *) const);
}
void registerAINativeTypes_src_package_touhou_sp_cpp(AINativeTypes &types) {
 types.emplace(std::type_index(typeid(ThFanshi)), 2u);
 types.recordActivateSource<ThFanshi>(4800u); AI_NATIVE_REQUIRE_DECLARATION(ThFanshi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFanshi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFanshi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFanshi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFanshi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFanshi, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThKongsuo)), 2u);
 types.recordActivateSource<ThKongsuo>(4801u); AI_NATIVE_REQUIRE_DECLARATION(ThKongsuo, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThKongsuo, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThKongsuo, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThKongsuo, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThKongsuo, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThKongsuo, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThGuimen)), 3u); AI_NATIVE_REQUIRE_DECLARATION(ThGuimen, getCorrect, DistanceSkill); AI_NATIVE_REQUIRE_DECLARATION(ThGuimen, getFixed, ThGuimen);
 types.recordActivateSource<ThGuimen>(4802u); AI_NATIVE_REQUIRE_DECLARATION(ThGuimen, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuimen, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuimen, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuimen, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuimen, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(ThLiuren)), 2u);
 types.recordActivateSource<ThLiuren>(4803u); AI_NATIVE_REQUIRE_DECLARATION(ThLiuren, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLiuren, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLiuren, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLiuren, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLiuren, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLiuren, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThShenshi)), 2u);
 types.recordActivateSource<ThShenshi>(4804u); AI_NATIVE_REQUIRE_DECLARATION(ThShenshi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShenshi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShenshi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShenshi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShenshi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShenshi, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThJiefan)), 2u);
 types.recordActivateSource<ThJiefan>(4805u); AI_NATIVE_REQUIRE_DECLARATION(ThJiefan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJiefan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJiefan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJiefan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJiefan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJiefan, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThChuangshi)), 2u);
 types.recordActivateSource<ThChuangshi>(4806u); AI_NATIVE_REQUIRE_DECLARATION(ThChuangshi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThChuangshi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThChuangshi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThChuangshi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThChuangshi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThChuangshi, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThGaotian)), 2u);
 types.recordActivateSource<ThGaotian>(4807u); AI_NATIVE_REQUIRE_DECLARATION(ThGaotian, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGaotian, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGaotian, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGaotian, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGaotian, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGaotian, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThWanling)), 2u);
 types.recordActivateSource<ThWanling>(4808u); AI_NATIVE_REQUIRE_DECLARATION(ThWanling, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThWanling, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThWanling, getLimitScope, ThWanling); AI_NATIVE_REQUIRE_DECLARATION(ThWanling, getMaxUsageLimit, ThWanling); AI_NATIVE_REQUIRE_DECLARATION(ThWanling, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThWanling, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThZuibu)), 2u);
 types.recordActivateSource<ThZuibu>(4809u); AI_NATIVE_REQUIRE_DECLARATION(ThZuibu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZuibu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZuibu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZuibu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZuibu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZuibu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThModao)), 2u);
 types.recordActivateSource<ThModao>(4810u); AI_NATIVE_REQUIRE_DECLARATION(ThModao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThModao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThModao, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThModao, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThModao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThModao, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThMengsheng)), 2u);
 types.recordActivateSource<ThMengsheng>(4811u); AI_NATIVE_REQUIRE_DECLARATION(ThMengsheng, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThMengsheng, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThMengsheng, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThMengsheng, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThMengsheng, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThMengsheng, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThQixiang)), 2u);
 types.recordActivateSource<ThQixiang>(4812u); AI_NATIVE_REQUIRE_DECLARATION(ThQixiang, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThQixiang, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThQixiang, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThQixiang, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThQixiang, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThQixiang, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThHuanlongViewAs)), 2u);
 types.recordActivateSource<ThHuanlongViewAs>(4813u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongViewAs, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongViewAs, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongViewAs, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongViewAs, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongViewAs, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongViewAs, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThHuanlong)), 2u);
 types.recordActivateSource<ThHuanlong>(4814u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlong, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlong, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlong, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlong, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlong, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlong, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThHuanlongTargetMod)), 3u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getPattern, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getResidueNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getDistanceLimit, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getExtraTargetNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, requiresShowForUse, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getCorrection, ThHuanlongTargetMod); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getFixedValue, TargetModSkillV2);
 types.recordActivateSource<ThHuanlongTargetMod>(4815u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongTargetMod, getBaseAmount, TargetModSkillV2);
 types.emplace(std::type_index(typeid(ThHuanlongRange)), 3u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, getExtra, AttackRangeSkill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, getFixed, AttackRangeSkill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, getCorrection, ThHuanlongRange); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, getFixedValue, AttackRangeSkillV2);
 types.recordActivateSource<ThHuanlongRange>(4816u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongRange, getBaseAmount, AttackRangeSkillV2);
 types.emplace(std::type_index(typeid(ThHuanlongMaxCards)), 3u); static_assert(std::is_same<decltype(xufaExtraOwner(&ThHuanlongMaxCards::getExtra)), MaxCardsSkill*>::value, "Xufa getExtra owner changed"); static_assert(std::is_same<decltype(xufaExtraModeOwner(&ThHuanlongMaxCards::getExtra)), MaxCardsSkill*>::value, "Xufa getExtra mode owner changed"); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongMaxCards, getFixed, MaxCardsSkill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongMaxCards, getCorrection, ThHuanlongMaxCards); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongMaxCards, getFixedValue, MaxCardsSkillV2);
 types.recordActivateSource<ThHuanlongMaxCards>(4817u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongMaxCards, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongMaxCards, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongMaxCards, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongMaxCards, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongMaxCards, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanlongMaxCards, getBaseAmount, MaxCardsSkillV2);
 types.emplace(std::type_index(typeid(ThYudu)), 2u);
 types.recordActivateSource<ThYudu>(4818u); AI_NATIVE_REQUIRE_DECLARATION(ThYudu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYudu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYudu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYudu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYudu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYudu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThZhaoguo)), 2u);
 types.recordActivateSource<ThZhaoguo>(4819u); AI_NATIVE_REQUIRE_DECLARATION(ThZhaoguo, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZhaoguo, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZhaoguo, getLimitScope, ThZhaoguo); AI_NATIVE_REQUIRE_DECLARATION(ThZhaoguo, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZhaoguo, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZhaoguo, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThLunmin)), 2u);
 types.recordActivateSource<ThLunmin>(4820u); AI_NATIVE_REQUIRE_DECLARATION(ThLunmin, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLunmin, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLunmin, getLimitScope, ThLunmin); AI_NATIVE_REQUIRE_DECLARATION(ThLunmin, getMaxUsageLimit, ThLunmin); AI_NATIVE_REQUIRE_DECLARATION(ThLunmin, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLunmin, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThYupan)), 2u);
 types.recordActivateSource<ThYupan>(4821u); AI_NATIVE_REQUIRE_DECLARATION(ThYupan, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYupan, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYupan, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYupan, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYupan, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYupan, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThJiuzhang)), 2u);
 types.recordActivateSource<ThJiuzhang>(4822u); AI_NATIVE_REQUIRE_DECLARATION(ThJiuzhang, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJiuzhang, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJiuzhang, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJiuzhang, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJiuzhang, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(ThShushuViewAs)), 2u);
 types.recordActivateSource<ThShushuViewAs>(4823u); AI_NATIVE_REQUIRE_DECLARATION(ThShushuViewAs, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushuViewAs, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushuViewAs, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushuViewAs, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushuViewAs, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushuViewAs, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThShushu)), 2u);
 types.recordActivateSource<ThShushu>(4824u); AI_NATIVE_REQUIRE_DECLARATION(ThShushu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThShushu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThFenglingViewAs)), 2u);
 types.recordActivateSource<ThFenglingViewAs>(4825u); AI_NATIVE_REQUIRE_DECLARATION(ThFenglingViewAs, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFenglingViewAs, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFenglingViewAs, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFenglingViewAs, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFenglingViewAs, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFenglingViewAs, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThFengling)), 2u);
 types.recordActivateSource<ThFengling>(4826u); AI_NATIVE_REQUIRE_DECLARATION(ThFengling, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFengling, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFengling, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFengling, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFengling, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFengling, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThYingshi)), 2u);
 types.recordActivateSource<ThYingshi>(4827u); AI_NATIVE_REQUIRE_DECLARATION(ThYingshi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYingshi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYingshi, getLimitScope, ThYingshi); AI_NATIVE_REQUIRE_DECLARATION(ThYingshi, getMaxUsageLimit, ThYingshi); AI_NATIVE_REQUIRE_DECLARATION(ThYingshi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYingshi, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThYingshiClear)), 2u);
 types.recordActivateSource<ThYingshiClear>(4828u); AI_NATIVE_REQUIRE_DECLARATION(ThYingshiClear, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYingshiClear, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYingshiClear, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYingshiClear, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYingshiClear, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYingshiClear, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThZanghun)), 2u);
 types.recordActivateSource<ThZanghun>(4829u); AI_NATIVE_REQUIRE_DECLARATION(ThZanghun, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZanghun, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZanghun, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZanghun, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZanghun, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZanghun, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThZanghunDistance)), 3u); AI_NATIVE_REQUIRE_DECLARATION(ThZanghunDistance, getCorrect, ThZanghunDistance); AI_NATIVE_REQUIRE_DECLARATION(ThZanghunDistance, getFixed, DistanceSkill);
 types.recordActivateSource<ThZanghunDistance>(4830u); AI_NATIVE_REQUIRE_DECLARATION(ThZanghunDistance, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZanghunDistance, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZanghunDistance, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZanghunDistance, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZanghunDistance, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(ThYimeng)), 2u);
 types.recordActivateSource<ThYimeng>(4831u); AI_NATIVE_REQUIRE_DECLARATION(ThYimeng, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYimeng, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYimeng, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYimeng, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYimeng, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYimeng, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThXuyou)), 2u);
 types.recordActivateSource<ThXuyou>(4832u); AI_NATIVE_REQUIRE_DECLARATION(ThXuyou, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThXuyou, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThXuyou, getLimitScope, ThXuyou); AI_NATIVE_REQUIRE_DECLARATION(ThXuyou, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThXuyou, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThXuyou, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThXuyouEffect)), 2u);
 types.recordActivateSource<ThXuyouEffect>(4833u); AI_NATIVE_REQUIRE_DECLARATION(ThXuyouEffect, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThXuyouEffect, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThXuyouEffect, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThXuyouEffect, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThXuyouEffect, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThXuyouEffect, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThHuanghu)), 2u);
 types.recordActivateSource<ThHuanghu>(4834u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanghu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanghu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanghu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanghu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanghu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanghu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThLinyao)), 2u);
 types.recordActivateSource<ThLinyao>(4835u); AI_NATIVE_REQUIRE_DECLARATION(ThLinyao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLinyao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLinyao, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLinyao, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLinyao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThLinyao, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThFeijing)), 2u);
 types.recordActivateSource<ThFeijing>(4836u); AI_NATIVE_REQUIRE_DECLARATION(ThFeijing, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFeijing, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFeijing, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFeijing, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFeijing, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFeijing, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThOuji)), 2u);
 types.recordActivateSource<ThOuji>(4837u); AI_NATIVE_REQUIRE_DECLARATION(ThOuji, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThOuji, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThOuji, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThOuji, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThOuji, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThOuji, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThJingyuansp)), 2u);
 types.recordActivateSource<ThJingyuansp>(4838u); AI_NATIVE_REQUIRE_DECLARATION(ThJingyuansp, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJingyuansp, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJingyuansp, getLimitScope, ThJingyuansp); AI_NATIVE_REQUIRE_DECLARATION(ThJingyuansp, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJingyuansp, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThJingyuansp, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThFeihu)), 2u);
 types.recordActivateSource<ThFeihu>(4839u); AI_NATIVE_REQUIRE_DECLARATION(ThFeihu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFeihu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFeihu, getLimitScope, ThFeihu); AI_NATIVE_REQUIRE_DECLARATION(ThFeihu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFeihu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFeihu, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThHuanling)), 2u);
 types.recordActivateSource<ThHuanling>(4840u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanling, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanling, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanling, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanling, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanling, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanling, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThYoukong)), 3u); AI_NATIVE_REQUIRE_DECLARATION(ThYoukong, getCorrect, ThYoukong); AI_NATIVE_REQUIRE_DECLARATION(ThYoukong, getFixed, DistanceSkill);
 types.recordActivateSource<ThYoukong>(4841u); AI_NATIVE_REQUIRE_DECLARATION(ThYoukong, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYoukong, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYoukong, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYoukong, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYoukong, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(IkChilian)), 2u);
 types.recordActivateSource<IkChilian>(4842u); AI_NATIVE_REQUIRE_DECLARATION(IkChilian, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkChilian, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkChilian, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkChilian, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkChilian, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkChilian, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(IkMeiying)), 2u);
 types.recordActivateSource<IkMeiying>(4843u); AI_NATIVE_REQUIRE_DECLARATION(IkMeiying, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeiying, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeiying, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeiying, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeiying, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkMeiying, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(IkJingmu)), 2u);
 types.recordActivateSource<IkJingmu>(4844u); AI_NATIVE_REQUIRE_DECLARATION(IkJingmu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJingmu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJingmu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJingmu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJingmu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(IkJingmu, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThGuanzhiViewAs)), 2u);
 types.recordActivateSource<ThGuanzhiViewAs>(4845u); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhiViewAs, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhiViewAs, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhiViewAs, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhiViewAs, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhiViewAs, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhiViewAs, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThGuanzhi)), 2u);
 types.recordActivateSource<ThGuanzhi>(4846u); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhi, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhi, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhi, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhi, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhi, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThGuanzhi, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThFuhuaViewAs)), 2u);
 types.recordActivateSource<ThFuhuaViewAs>(4847u); AI_NATIVE_REQUIRE_DECLARATION(ThFuhuaViewAs, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhuaViewAs, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhuaViewAs, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhuaViewAs, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhuaViewAs, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhuaViewAs, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThFuhua)), 2u);
 types.recordActivateSource<ThFuhua>(4848u); AI_NATIVE_REQUIRE_DECLARATION(ThFuhua, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhua, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhua, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhua, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhua, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThFuhua, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThYongjie)), 2u);
 types.recordActivateSource<ThYongjie>(4849u); AI_NATIVE_REQUIRE_DECLARATION(ThYongjie, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjie, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjie, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjie, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjie, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjie, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThYongjieTargetMod)), 3u); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getPattern, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getResidueNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getDistanceLimit, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getExtraTargetNum, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, requiresShowForUse, TargetModSkill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getCorrection, ThYongjieTargetMod); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getFixedValue, TargetModSkillV2);
 types.recordActivateSource<ThYongjieTargetMod>(4850u); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThYongjieTargetMod, getBaseAmount, TargetModSkillV2);
 types.emplace(std::type_index(typeid(ThHuanyaoViewAs)), 2u);
 types.recordActivateSource<ThHuanyaoViewAs>(4851u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoViewAs, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoViewAs, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoViewAs, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoViewAs, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoViewAs, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoViewAs, getBaseAmount, ViewAsSkillV2);
 types.emplace(std::type_index(typeid(ThHuanyao)), 2u);
 types.recordActivateSource<ThHuanyao>(4852u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyao, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyao, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyao, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyao, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyao, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyao, getBaseAmount, TriggerSkillV2);
 types.emplace(std::type_index(typeid(ThHuanyaoProhibit)), 3u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoProhibit, isProhibited, ThHuanyaoProhibit);
 types.recordActivateSource<ThHuanyaoProhibit>(4853u); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoProhibit, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoProhibit, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoProhibit, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoProhibit, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThHuanyaoProhibit, getUsageRef, Skill);
 types.emplace(std::type_index(typeid(ThZhouzhu)), 2u);
 types.recordActivateSource<ThZhouzhu>(4854u); AI_NATIVE_REQUIRE_DECLARATION(ThZhouzhu, getFrequency, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZhouzhu, isEquipSkill, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZhouzhu, getLimitScope, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZhouzhu, getMaxUsageLimit, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZhouzhu, getUsageRef, Skill); AI_NATIVE_REQUIRE_DECLARATION(ThZhouzhu, getBaseAmount, TriggerSkillV2);
}
