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
            ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                                                            "@thjiefan-give");
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
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
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
        const QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target));
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
        ServerPlayer *target = room->askForPlayerChosen(source, room->getOtherPlayers(source), objectName(), "@thzhaoguo");
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
        const Card *card = room->askForExchange(ctx.owner, objectName(), 998, 1, false, "@thhuanghu:" + target->objectName(),
                                                true);
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
    sp011->addSkill(new PendingSkill("thshushu"));
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
    sp013->addSkill(new PendingSkill("thxuyou"));

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
