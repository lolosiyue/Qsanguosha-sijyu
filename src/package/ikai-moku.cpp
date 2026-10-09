#include "ikai-moku.h"
#include "touhou-utils.h"
#include "ikai-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "h-strategic-advantage.h"
#include "util.h"

using namespace TouhouUtils;
using namespace IkaiUtils;

// TouhouTripleSha's 异界·木: mostly 山/林/火/风 heroes under new names. Their skills are
// ported under the upstream names even where a local skill matches.

namespace {

// Lifts the skill invalidity recorded under `tag` (entries "skill|source").
void releaseInvalidity(Room *room, ServerPlayer *player, const QString &tag, const QString &reason)
{
    const QStringList blocked = player->getTag(tag).toStringList();
    if (blocked.isEmpty())
        return;
    player->removeTag(tag);
    foreach (const QString &entry, blocked) {
        const QStringList parts = entry.split("|");
        if (parts.size() == 2)
            room->removeSkillInvalidity(player, parts.first(), parts.last(), reason);
    }
}

// ---------------------------------------------------------------- wind008

class IkLiegong : public TriggerSkillV2
{
public:
    IkLiegong() : TriggerSkillV2("ikliegong") { events << TargetSpecified << EventPhaseChanging; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        foreach (ServerPlayer *p, room->getAllPlayers(true))
            releaseInvalidity(room, p, "IkLiegongBlocked", objectName());
        return false;
    }

    static bool fits(const ServerPlayer *player, const ServerPlayer *to)
    {
        const int n = to->getHandcardNum();
        return n >= player->getHp() || n <= player->getAttackRange();
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play || !use.card || !use.card->isKindOf("Slash"))
            return TriggerList();
        foreach (ServerPlayer *to, use.to)
            if (fits(player, to))
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }

    // Per target: no 闪, and its non-locked skills stop until the turn ends.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        bool changed = false;
        foreach (ServerPlayer *to, use.to) {
            if (!to->isAlive() || !fits(player, to) || use.no_respond_list.contains(to->objectName())
                || !player->askForSkillInvoke(objectName(), QVariant::fromValue(to)))
                continue;
            room->broadcastSkillInvoke(objectName());
            LogMessage log;
            log.type = "#NoJink";
            log.from = to;
            room->sendLog(log);
            use.no_respond_list << to->objectName();
            changed = true;
            QStringList blocked = to->getTag("IkLiegongBlocked").toStringList();
            foreach (const Skill *skill, to->getVisibleSkillList()) {
                const QString entry = skill->objectName() + "|" + player->objectName();
                if (skill->getFrequency() == Skill::Compulsory || blocked.contains(entry))
                    continue;
                room->addSkillInvalidity(to, skill->objectName(), player->objectName(), objectName());
                blocked << entry;
            }
            to->setTag("IkLiegongBlocked", blocked);
        }
        if (changed)
            *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class IkHuanghun : public ViewAsSkillV2
{
public:
    IkHuanghun() : ViewAsSkillV2("ikhuanghun", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card)
            && card->getTypeId() == Card::TypeTrick;
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkHuanghunCard"; }

    // Recast the trick.
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
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
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- wind012

// Upstream's copy of 挑衅, kept separate from tiaoxin.
class IkTiaoxin : public ViewAsSkillV2
{
public:
    IkTiaoxin() : ViewAsSkillV2("iktiaoxin") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to->inMyAttackRange(request.initiator);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkTiaoxinCard"; }

    // The target uses a 杀 on the owner, or the owner discards one of its cards.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        const bool used = target->canSlash(source, nullptr, false)
            && room->askForUseSlashTo(target, source, "@iktiaoxin-slash:" + source->objectName());
        if (!used && source->isAlive() && source->canDiscard(target, "he")) {
            const int id = room->askForCardChosen(source, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, target, source);
        }
        return ContinueEffects;
    }
};

class IkShengtian : public WakeSkill
{
public:
    IkShengtian() : WakeSkill("ikshengtian")
    {
        events << EventPhaseStart;
        waked_skills = "ikxuanwu,ikmohua";
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->isKongcheng(); }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->changeMaxHpForAwakenSkill(player, -1, objectName()) || !player->isAlive())
            return;
        if (player->isWounded() && room->askForChoice(player, objectName(), "recover+draw") == "recover")
            room->recover(player, RecoverStruct(objectName(), player));
        else
            player->drawCards(2, objectName());
        room->acquireSkillFromEffect(player, "ikxuanwu", ctx);
        room->acquireSkillFromEffect(player, "ikmohua", ctx);
    }
};

class IkMohua : public FilterSkill
{
public:
    IkMohua() : FilterSkill("ikmohua") { frequency = Compulsory; }

    bool viewFilter(const Card *to_select) const override { return to_select->getSuit() == Card::Diamond; }

    const Card *viewAs(const Card *original) const override
    {
        Card *card = Sanguosha->cloneCard(original->objectName(), Card::Club, original->getNumber());
        card->setSkillName(objectName());
        return card;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const override { return -2; }
};

// ---------------------------------------------------------------- bloom008

// 神速 paying with any non-trick card.
class IkXunyu : public TriggerSkillV2
{
public:
    IkXunyu() : TriggerSkillV2("ikxunyu") { events << EventPhaseChanging; }

    static QList<ServerPlayer *> victims(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        Slash probe(Card::NoSuit, 0);
        probe.setSkillName("_ikxunyu");
        if (player->isCardLimited(&probe, Card::MethodUse))
            return targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canSlash(p, &probe, false))
                targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || victims(room, player).isEmpty())
            return TriggerList();
        const Player::Phase to = data.value<PhaseChangeStruct>().to;
        if (to == Player::Judge && !player->isSkipped(Player::Judge) && !player->isSkipped(Player::Draw))
            return TriggerList{{player, {objectName()}}};
        if (to == Player::Play && !player->isSkipped(Player::Play) && player->canDiscard(player, "he"))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const bool early = ctx.original_data->value<PhaseChangeStruct>().to == Player::Judge;
        ServerPlayer *target = room->askForPlayerChosen(player, victims(room, player), objectName(),
                                                        early ? "@ikxunyu1" : "@ikxunyu2", true, true);
        if (!target)
            return false;
        if (!early && !room->askForCard(player, "^TrickCard", "@ikxunyu-discard", QVariant(), objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (ctx.original_data->value<PhaseChangeStruct>().to == Player::Judge) {
            player->skip(Player::Judge, true);
            player->skip(Player::Draw, true);
        } else {
            player->skip(Player::Play, true);
        }
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        useSkillSlash(room, ctx, player, target, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- 颂威 (no general; thzhizun grants it)

// 颂威 for hana characters.
class IkSongwei : public TriggerSkillV2
{
public:
    IkSongwei() : TriggerSkillV2("iksongwei$")
    {
        events << FinishJudge;
        global = true;
    }

    static QList<ServerPlayer *> lords(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (p->hasLordSkill("iksongwei"))
                result << p;
        return result;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!player || !player->isAlive() || player->getKingdom() != "hana" || !judge || judge->who != player
            || !judge->card || !judge->card->isBlack() || lords(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QList<ServerPlayer *> candidates = lords(room, player);
        ServerPlayer *lord = candidates.length() == 1
            ? candidates.first()
            : room->askForPlayerChosen(player, candidates, objectName(), "@iksongwei", true);
        if (!lord || !player->askForSkillInvoke(objectName(), QVariant::fromValue(lord)))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(lord, objectName());
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = player;
        log.to << lord;
        log.arg = objectName();
        room->sendLog(log);
        ctx.extra_data = lord->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lord = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (lord && lord->isAlive())
            lord->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- bloom029

class IkYihuoViewAs : public ViewAsSkillV2
{
public:
    IkYihuoViewAs() : ViewAsSkillV2("ikyihuov", 1) { attached_lord_skill = true; }

    static bool openTo(const Player *owner)
    {
        return owner->hasSkill("ikyihuo") && (owner->hasEquip() || !owner->faceUp())
            && owner->getMark("ikyihuo_used-PlayClear") == 0;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || self->getPhase() != Player::Play
            || self->isKongcheng())
            return false;
        foreach (const Player *p, self->getAliveSiblings())
            if (openTo(p))
                return true;
        return false;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card);
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
        return request.initiator && selected.isEmpty() && to && to != request.initiator && openTo(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkYihuoCard"; }

    // The owner sees the offered card; trading an equip for it also draws the owner one.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *owner) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !owner || !owner->isAlive() || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        room->setPlayerMark(owner, "ikyihuo_used-PlayClear", 1);
        room->broadcastSkillInvoke("ikyihuo");
        room->notifySkillInvoked(owner, "ikyihuo");
        if (room->getCardOwner(id) != source || room->getCardPlace(id) != Player::PlaceHand)
            return ContinueEffects;
        room->showCard(source, id, owner);
        const Card *equip = room->askForCard(owner, "EquipCard", "@ikyihuo-equip:" + source->objectName(), QVariant(),
                                             Card::MethodNone);
        if (!equip)
            return ContinueEffects;
        room->obtainCard(source, equip,
                         CardMoveReason(CardMoveReason::S_REASON_GIVE, owner->objectName(), source->objectName(), "ikyihuo",
                                        QString()));
        if (owner->isAlive() && room->getCardOwner(id) == source)
            room->obtainCard(owner, id);
        if (owner->isAlive())
            owner->drawCards(1, "ikyihuo");
        return ContinueEffects;
    }
};

class IkYihuo : public TriggerSkillV2
{
public:
    IkYihuo() : TriggerSkillV2("ikyihuo")
    {
        events << GameStart << EventAcquireSkill << EventLoseSkill << Death << GeneralShown << GeneralHidden;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        syncAttachedChildren(room, objectName(), "ikyihuov", false);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class IkGuixin : public TriggerSkillV2
{
public:
    IkGuixin() : TriggerSkillV2("ikguixin") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DamageStruct>().to != player || !player->isAlive() || !player->hasSkill(objectName())
            || !(player->aliveCount() < 4 || player->faceUp()))
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
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!player->isAlive())
                break;
            if (!p->isAlive() || p->isAllNude())
                continue;
            const int id = room->askForCardChosen(player, p, "hej", objectName());
            if (id >= 0)
                room->obtainCard(player, Sanguosha->getCard(id),
                                 CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, player->objectName()), false);
        }
        if (player->isAlive())
            player->turnOver();
        return false;
    }
};

// ---------------------------------------------------------------- snow009

// 英魂 without the need to be wounded (X is at least 1).
class IkLiangban : public TriggerSkillV2
{
public:
    IkLiangban() : TriggerSkillV2("ikliangban") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *to = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "ikliangban-invoke",
                                                    true, true);
        if (!to)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = to->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *to = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!to || !to->isAlive())
            return false;
        const int x = qMax(player->getLostHp(), 1);
        const bool drawMany = x > 1 && room->askForChoice(player, objectName(), "d1tx+dxt1", QVariant::fromValue(to)) == "dxt1";
        to->drawCards(drawMany ? x : 1, objectName());
        if (to->isAlive())
            room->askForDiscard(to, objectName(), drawMany ? 1 : x, drawMany ? 1 : x, false, true);
        return false;
    }
};

class IkDiewu : public TriggerSkillV2
{
public:
    IkDiewu() : TriggerSkillV2("ikdiewu") { events << CardsMoveOneTime; }

    static QList<int> handIds(const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceHand)
                ids << move.card_ids.at(i);
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getHp() <= 0 || !move.from
            || move.from == player || move.to_place != Player::DiscardPile || handIds(move).isEmpty()
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *from = qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().from);
        if (!from || !player->askForSkillInvoke(objectName(), QVariant::fromValue(from)))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->loseHp(player, 1, true, player, objectName());
        return true;
    }

    // The discarder swaps that many hand cards for the discarded ones.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        ServerPlayer *target = qobject_cast<ServerPlayer *>(move.from);
        if (!target || !target->isAlive() || target->isKongcheng())
            return false;
        const QList<int> lost = handIds(move);
        QList<int> back;
        foreach (int id, lost)
            if (room->getCardPlace(id) == Player::DiscardPile)
                back << id;
        QList<int> put;
        const int n = qMin(lost.length(), target->getHandcardNum());
        if (const Card *chosen = room->askForExchange(target, objectName(), n, n, false, "@ikdiewu", false)) {
            put = chosen->getSubcards();
            delete chosen;
        }
        if (put.isEmpty())
            put = target->handCards().mid(0, n);
        QList<CardsMoveStruct> moves;
        moves << CardsMoveStruct(put, nullptr, Player::DiscardPile,
                                 CardMoveReason(CardMoveReason::S_REASON_PUT, target->objectName(), objectName(), QString()));
        if (!back.isEmpty())
            moves << CardsMoveStruct(back, target, Player::PlaceHand,
                                     CardMoveReason(CardMoveReason::S_REASON_RECYCLE, target->objectName(), objectName(),
                                                    QString()));
        LogMessage log;
        log.type = "$MoveToDiscardPile";
        log.from = target;
        log.card_str = ListI2S(put).join("+");
        room->sendLog(log);
        room->moveCardsAtomic(moves, true);
        return false;
    }
};

// ---------------------------------------------------------------- 缘结 (no general; IkSheluo grants it)

// 缔盟 for two characters whose hands differ by at most three.
class IkYuanjie : public ViewAsSkillV2
{
public:
    IkYuanjie() : ViewAsSkillV2("ikyuanjie") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.size() < 3 && ownsCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() <= 3 && selectionValid(this, request);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        if (!request.initiator || !to || to == request.initiator || selected.length() >= 2)
            return false;
        if (selected.isEmpty())
            return true;
        return qAbs(to->getHandcardNum() - selected.first()->getHandcardNum()) == request.selectedCardIds.size();
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.length() == 2
            && qAbs(selected.first()->getHandcardNum() - selected.last()->getHandcardNum()) == request.selectedCardIds.size();
    }

    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkYuanjieCard"; }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (targets.length() != 2 || !targets.first()->isAlive() || !targets.last()->isAlive())
            return FinishSkill;
        ServerPlayer *a = targets.first();
        ServerPlayer *b = targets.last();
        Room *room = a->getRoom();
        const int n1 = a->getHandcardNum();
        const int n2 = b->getHandcardNum();
        QList<CardsMoveStruct> moves;
        moves << CardsMoveStruct(a->handCards(), b, Player::PlaceHand,
                                 CardMoveReason(CardMoveReason::S_REASON_SWAP, a->objectName(), b->objectName(), objectName(),
                                                QString()));
        moves << CardsMoveStruct(b->handCards(), a, Player::PlaceHand,
                                 CardMoveReason(CardMoveReason::S_REASON_SWAP, b->objectName(), a->objectName(), objectName(),
                                                QString()));
        room->moveCardsAtomic(moves, false);
        LogMessage log;
        log.type = "#IkYuanjie";
        log.from = a;
        log.to << b;
        log.arg = QString::number(n1);
        log.arg2 = QString::number(n2);
        room->sendLog(log);
        Q_UNUSED(ctx);
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- 舞华 (no general; thzhizun grants it)

// 暴虐 for tsuki characters.
class IkWuhua : public TriggerSkillV2
{
public:
    IkWuhua() : TriggerSkillV2("ikwuhua$")
    {
        events << Damage;
        global = true;
    }

    static QList<ServerPlayer *> lords(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (p->hasLordSkill("ikwuhua"))
                result << p;
        return result;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DamageStruct>().from != player || !player->isAlive() || player->getKingdom() != "tsuki"
            || lords(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QList<ServerPlayer *> candidates = lords(room, player);
        ServerPlayer *lord = candidates.length() == 1
            ? candidates.first()
            : room->askForPlayerChosen(player, candidates, objectName(), "@ikwuhua", true);
        if (!lord || !player->askForSkillInvoke(objectName(), QVariant::fromValue(lord)))
            return false;
        room->notifySkillInvoked(lord, objectName());
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = player;
        log.to << lord;
        log.arg = objectName();
        room->sendLog(log);
        ctx.extra_data = lord->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        JudgeStruct judge;
        judge.pattern = ".|spade";
        judge.good = true;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        ServerPlayer *lord = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (judge.isGood() && lord && lord->isAlive() && lord->isWounded()) {
            room->broadcastSkillInvoke(objectName());
            room->recover(lord, RecoverStruct(objectName(), player));
        }
        return false;
    }
};

// ---------------------------------------------------------------- luna004

// Upstream's copy of 乱击 (two hand cards of one suit), kept separate from luanji.
class IkXinghuang : public ViewAsSkillV2
{
public:
    IkXinghuang() : ViewAsSkillV2("ikxinghuang", 2) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (request.selectedCardIds.size() >= 2 || !ownsHandCard(request.initiator, card))
            return false;
        if (request.selectedCardIds.isEmpty())
            return true;
        return Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == card->getSuit();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 2 && selectionValid(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        auto *card = new ArcheryAttack(Card::SuitToBeDecided, 0);
        foreach (int id, request.selectedCardIds)
            card->addSubcard(id);
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ArcheryAttack"; }
};

class IkXuzhao : public ViewAsSkillV2
{
public:
    IkXuzhao() : ViewAsSkillV2("ikxuzhao", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card);
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkXuzhaoCard"; }

    // The 调虎离山 costs a point of HP.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.invoker)
            return false;
        room->loseHp(ctx.invoker, 1, true, ctx.invoker, objectName());
        return ctx.invoker->isAlive();
    }
};

// ---------------------------------------------------------------- luna005

class IkJingfa : public TriggerSkillV2
{
public:
    IkJingfa() : TriggerSkillV2("ikjingfa")
    {
        events << PreDamageDone << EventPhaseEnd;
        frequency = Frequent;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != PreDamageDone)
            return false;
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (from && from->getPhase() == Player::Play && from->getMark("ikjingfa_dealt-PlayClear") == 0)
            room->setPlayerMark(from, "ikjingfa_dealt-PlayClear", 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Play
            || !player->hasSkill(objectName()) || player->getMark("ikjingfa_dealt-PlayClear") > 0)
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

    // Draw one, or knock a card out of play.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (player->canDiscard(p, "ej"))
                targets << p;
        ServerPlayer *target = targets.isEmpty() ? nullptr
                                                 : room->askForPlayerChosen(player, targets, objectName(), "@ikjingfa", true);
        if (!target) {
            player->drawCards(1, objectName());
            return false;
        }
        const int id = room->askForCardChosen(player, target, "ej", objectName(), false, Card::MethodDiscard);
        if (id >= 0)
            room->throwCard(id, room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : target, player);
        return false;
    }
};

// Upstream's copy of 双雄, kept separate from shuangxiong. The mark holds the colour that
// may be used as 决斗 this turn: 1 black, 2 red.
class IkQiyuViewAs : public ViewAsSkillV2
{
public:
    IkQiyuViewAs() : ViewAsSkillV2("ikqiyu", 1) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("ikqiyu-Clear") > 0;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !ownsHandCard(request.initiator, card))
            return false;
        const int colour = request.initiator->getMark("ikqiyu-Clear");
        return colour == 1 ? card->isBlack() : colour == 2 && card->isRed();
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
        auto *card = new Duel(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Duel"; }
};

class IkQiyu : public TriggerSkillV2
{
public:
    IkQiyu() : TriggerSkillV2("ikqiyu")
    {
        events << EventPhaseStart << FinishJudge;
        view_as_skill = new IkQiyuViewAs;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (event == FinishJudge) {
            const JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge && judge->who == player && judge->reason == objectName() && judge->card
                && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
                return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        if (player->getPhase() != Player::Draw || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (event == FinishJudge)
            return true;
        if (!player->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Judge instead of drawing and keep the judge card.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == FinishJudge) {
            const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (judge && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
                player->obtainCard(judge->card);
            return false;
        }
        JudgeStruct judge;
        judge.pattern = ".";
        judge.good = true;
        judge.play_animation = false;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        if (judge.card && player->isAlive())
            room->setPlayerMark(player, "ikqiyu-Clear", judge.card->isRed() ? 1 : 2);
        return true;
    }
};

// ---------------------------------------------------------------- luna008

class IkKongsa : public TriggerSkillV2
{
public:
    IkKongsa() : TriggerSkillV2("ikkongsa") { events << CardOffset << Damage; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardOffset) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            ServerPlayer *from = effect.from;
            if (!from || !from->isAlive() || !from->hasSkill(objectName()) || !effect.card || !effect.card->isKindOf("Slash")
                || !effect.offset_card || !effect.offset_card->isKindOf("Jink") || !effect.to || !effect.to->isAlive()
                || !from->canDiscard(effect.to, "he"))
                return TriggerList();
            return TriggerList{{from, {objectName()}}};
        }
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName()) || damage.transfer
            || damage.chain || !damage.card || !damage.card->isKindOf("Slash") || !damage.card->isRed())
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

    // A dodged 杀 strips a card from its target; a red 杀 that hurts draws one.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == Damage) {
            owner->drawCards(1, objectName());
            return false;
        }
        ServerPlayer *target = ctx.original_data->value<CardEffectStruct>().to;
        if (!target || !target->isAlive() || !owner->canDiscard(target, "he"))
            return false;
        const int id = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodDiscard);
        if (id >= 0)
            room->throwCard(id, target, owner);
        return false;
    }
};

// ---------------------------------------------------------------- 御姬 (no general; thzhizun grants it)

// 黄天 for tsuki characters, with 闪 or 八卦阵.
class IkYujiGive : public ViewAsSkillV2
{
public:
    IkYujiGive() : ViewAsSkillV2("ikyujiv", 1) { attached_lord_skill = true; }

    static bool openTo(const Player *self, const Player *lord)
    {
        return lord != self && lord->hasLordSkill("ikyuji") && lord->getMark("ikyuji_used-PlayClear") == 0;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || self->getKingdom() != "tsuki")
            return false;
        foreach (const Player *p, self->getAliveSiblings())
            if (openTo(self, p))
                return true;
        return false;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card)
            && (card->isKindOf("Jink") || card->isKindOf("EightDiagram"));
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
        return request.initiator && selected.isEmpty() && to && openTo(request.initiator, to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkYujiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *lord) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !lord || !lord->isAlive() || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->setPlayerMark(lord, "ikyuji_used-PlayClear", 1);
        room->broadcastSkillInvoke("ikyuji");
        room->notifySkillInvoked(lord, "ikyuji");
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) == source)
            room->giveCard(source, lord, QList<int>() << id, "ikyuji", true);
        return ContinueEffects;
    }
};

class IkYuji : public TriggerSkillV2
{
public:
    IkYuji() : TriggerSkillV2("ikyuji$")
    {
        events << GameStart << EventAcquireSkill << EventLoseSkill << Death << GeneralShown << GeneralHidden;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        syncAttachedChildren(room, "ikyuji", "ikyujiv", true);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

}

IkHuanghunCard::IkHuanghunCard() { setSkillName("ikhuanghun"); mute = true; }
IkTiaoxinCard::IkTiaoxinCard() { setSkillName("iktiaoxin"); mute = true; }
IkYihuoCard::IkYihuoCard() { setSkillName("ikyihuov"); mute = true; }
IkYuanjieCard::IkYuanjieCard() { setSkillName("ikyuanjie"); mute = true; }
IkXuzhaoCard::IkXuzhaoCard() { setSkillName("ikxuzhao"); mute = true; }
IkYujiCard::IkYujiCard() { setSkillName("ikyujiv"); mute = true; }

IkaiMokuPackage::IkaiMokuPackage()
    : Package("ikai-moku")
{
    General *wind008 = new General(this, "wind008", "kaze");
    wind008->addSkill(new IkLiegong);
    wind008->addSkill(new IkHuanghun);

    General *wind012 = new General(this, "wind012", "kaze");
    wind012->addSkill(new IkTiaoxin);
    wind012->addSkill(new IkShengtian);
    wind012->addRelateSkill("ikxuanwu");
    wind012->addRelateSkill("ikmohua");

    General *bloom008 = new General(this, "bloom008", "hana");
    bloom008->addSkill(new IkXunyu);

    General *bloom029 = new General(this, "bloom029", "hana", 3);
    bloom029->addSkill(new IkYihuo);
    bloom029->addSkill(new IkGuixin);

    General *snow009 = new General(this, "snow009", "yuki");
    snow009->addSkill(new IkLiangban);
    snow009->addSkill(new IkDiewu);

    General *luna004 = new General(this, "luna004", "tsuki");
    luna004->addSkill(new IkXinghuang);
    luna004->addSkill(new IkXuzhao);

    General *luna005 = new General(this, "luna005", "tsuki");
    luna005->addSkill(new IkJingfa);
    luna005->addSkill(new IkQiyu);

    // 疾步 lives in touhou-bangai.
    General *luna008 = new General(this, "luna008", "tsuki");
    luna008->addSkill("thjibu");
    luna008->addSkill(new IkKongsa);

    skills << new IkMohua << new IkYihuoViewAs << new IkYujiGive;
    // No general owns these: thzhizun grants 颂威, 舞华 and 御姬; IkSheluo grants 缘结.
    skills << new IkSongwei << new IkWuhua << new IkYuji << new IkYuanjie;

    addMetaObject<IkHuanghunCard>();
    addMetaObject<IkTiaoxinCard>();
    addMetaObject<IkYihuoCard>();
    addMetaObject<IkYuanjieCard>();
    addMetaObject<IkXuzhaoCard>();
    addMetaObject<IkYujiCard>();
}

ADD_PACKAGE(IkaiMoku)
