#include "ikai-moku.h"
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

using namespace TouhouUtils;
using namespace IkaiUtils;

// TouhouTripleSha's 异界·木: mostly 山/林/火/风 heroes under new names. Those are reused by
// their local names; only the skills that changed are ported.

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

// ---------------------------------------------------------------- wind010

class IkFuhua : public ViewAsSkillV2
{
public:
    IkFuhua() : ViewAsSkillV2("ikfuhua", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !card || card->getSuit() != Card::Club || card->hasFlag("using"))
            return false;
        return ownsCard(request.initiator, card) || request.initiator->getHandPile().contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IronChain"; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        auto *card = new IronChain(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
};

class IkFuhuaDraw : public TriggerSkillV2
{
public:
    IkFuhuaDraw() : TriggerSkillV2("#ikfuhua")
    {
        events << CardUsed;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill("ikfuhua") || !use.card
            || !use.card->isKindOf("IronChain") || use.to.length() != 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, "ikfuhua");
        player->drawCards(1, "ikfuhua");
        return false;
    }
};

// Clear the judgement area, reset the general, draw three and heal to 3.
void rebirth(Room *room, ServerPlayer *player)
{
    foreach (const Card *trick, player->getJudgingArea())
        room->throwCard(trick, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName()), nullptr);
    if (!player->faceUp())
        player->turnOver();
    if (player->isChained())
        room->setPlayerChained(player, false, player);
    player->drawCards(3, "iksuinie");
    if (player->isAlive() && player->getHp() < 3)
        room->recover(player, RecoverStruct("iksuinie", player, 3 - player->getHp()));
}

class IkSuinieViewAs : public ViewAsSkillV2
{
public:
    IkSuinieViewAs() : ViewAsSkillV2("iksuinie") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("@suinie") > 0;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkSuinieCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark("@suinie") <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, "@suinie");
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive())
            rebirth(ctx.invoker->getRoom(), ctx.invoker);
        return FinishSkill;
    }
};

class IkSuinie : public TriggerSkillV2
{
public:
    IkSuinie() : TriggerSkillV2("iksuinie")
    {
        events << AskForPeaches;
        frequency = Limited;
        limit_mark = "@suinie";
        view_as_skill = new IkSuinieViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DyingStruct>().who != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getHp() > 0 || player->getMark(limit_mark) == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->removePlayerMark(player, limit_mark);
        room->doSuperLightbox(player, objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        rebirth(room, player);
        return false;
    }
};

// ---------------------------------------------------------------- wind012

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

// ---------------------------------------------------------------- wind013

class IkZailuan : public TriggerSkillV2
{
public:
    IkZailuan() : TriggerSkillV2("ikzailuan") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Draw || !player->hasSkill(objectName())
            || !player->isWounded())
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

    // Instead of drawing, reveal lost-HP cards; each heart heals or draws two, the rest are kept.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        const QList<int> ids = room->getNCards(player->getLostHp(), false);
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable,
                                              CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(),
                                                             objectName(), QString())),
                              true);
        QList<int> hearts, others;
        foreach (int id, ids) {
            if (room->getCardPlace(id) != Player::PlaceTable)
                continue;
            (Sanguosha->getCard(id)->getSuit() == Card::Heart ? hearts : others) << id;
        }
        foreach (int id, hearts) {
            Q_UNUSED(id);
            if (!player->isAlive())
                break;
            QStringList choices;
            if (player->isWounded())
                choices << "recover";
            choices << "draw";
            if (room->askForChoice(player, objectName(), choices.join("+")) == "recover")
                room->recover(player, RecoverStruct(objectName(), player));
            else
                player->drawCards(2, objectName());
        }
        QList<int> thrown, kept;
        foreach (int id, hearts)
            if (room->getCardPlace(id) == Player::PlaceTable)
                thrown << id;
        foreach (int id, others)
            if (room->getCardPlace(id) == Player::PlaceTable)
                kept << id;
        if (!thrown.isEmpty()) {
            DummyCard dummy(thrown);
            room->throwCard(&dummy, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(),
                                                   QString()),
                            nullptr);
        }
        if (!kept.isEmpty() && player->isAlive()) {
            DummyCard dummy(kept);
            room->obtainCard(player, &dummy);
        }
        return true;
    }
};

// ---------------------------------------------------------------- wind014

class IkRuoyu : public WakeSkill
{
public:
    IkRuoyu() : WakeSkill("ikruoyu$")
    {
        events << EventPhaseStart;
        waked_skills = "ikxinqi";
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start && player->hasLordSkill(objectName());
    }

    bool canAwaken(Room *room, ServerPlayer *player) const override
    {
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getHp() < player->getHp())
                return false;
        return true;
    }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->changeMaxHpForAwakenSkill(player, 1, objectName()) || !player->isAlive())
            return;
        room->recover(player, RecoverStruct(objectName(), player));
        if (player->isLord())
            room->acquireSkillFromEffect(player, "ikxinqi", ctx);
    }
};

// ---------------------------------------------------------------- wind015

class IkLieren : public TriggerSkillV2
{
public:
    IkLieren() : TriggerSkillV2("iklieren") { events << TargetSpecified; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
            || !use.card->isKindOf("Slash") || player->isKongcheng())
            return TriggerList();
        foreach (ServerPlayer *to, use.to)
            if (player->canPindian(to))
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }

    // A won pindian against a target takes one of its cards.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        foreach (ServerPlayer *to, use.to) {
            if (!player->isAlive() || !to->isAlive() || !player->canPindian(to)
                || !player->askForSkillInvoke(objectName(), QVariant::fromValue(to)))
                continue;
            room->broadcastSkillInvoke(objectName());
            if (!player->pindian(to, objectName()) || to->isNude() || !player->isAlive())
                continue;
            const int id = room->askForCardChosen(player, to, "he", objectName());
            if (id >= 0)
                room->obtainCard(player, id, false);
        }
        return false;
    }
};

// ---------------------------------------------------------------- wind029

// 大雾 in its original form, spending 七星's "stars".
class IkMiaowu : public TriggerSkillV2
{
public:
    IkMiaowu() : TriggerSkillV2("ikmiaowu")
    {
        events << EventPhaseStart << DamageForseen << Death;
        global = true;
    }

    static void clearFog(Room *room, ServerPlayer *owner)
    {
        const QStringList names = owner->getTag("IkMiaowuFog").toStringList();
        owner->removeTag("IkMiaowuFog");
        foreach (const QString &name, names)
            if (ServerPlayer *p = room->findPlayerByObjectName(name, true))
                room->removePlayerMark(p, "@fog");
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->getTag("IkMiaowuFog").toStringList().isEmpty())
            return false;
        if ((event == EventPhaseStart && player->getPhase() == Player::RoundStart)
            || (event == Death && data.value<DeathStruct>().who == player))
            clearFog(room, player);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return TriggerList();
        if (event == DamageForseen) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.to != player || player->getMark("@fog") == 0 || damage.nature == DamageStruct::Thunder)
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        if (event != EventPhaseStart || !player->isAlive() || player->getPhase() != Player::Finish
            || !player->hasSkill(objectName()) || player->getPile("stars").isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == DamageForseen)
            return true;
        const int stars = player->getPile("stars").length();
        const QList<ServerPlayer *> targets = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0,
                                                                        stars, "@ikmiaowu-card", true);
        if (targets.isEmpty())
            return false;
        QStringList names;
        foreach (ServerPlayer *p, targets)
            names << p->objectName();
        ctx.extra_data = names;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == DamageForseen) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            LogMessage log;
            log.type = "#IkMiaowuProtect";
            log.from = player;
            log.arg = QString::number(damage.damage);
            log.arg2 = damage.nature == DamageStruct::Fire ? "fire_nature" : "normal_nature";
            room->sendLog(log);
            return true;
        }
        const QStringList names = ctx.extra_data.toStringList();
        QList<int> stars = player->getPile("stars");
        QList<int> thrown;
        while (thrown.length() < names.length() && !stars.isEmpty()) {
            int id = stars.first();
            if (stars.length() > names.length() - thrown.length()) {
                room->fillAG(stars, player);
                id = room->askForAG(player, stars, false, objectName());
                room->clearAG(player);
                if (!stars.contains(id))
                    id = stars.first();
            }
            stars.removeOne(id);
            thrown << id;
        }
        if (!thrown.isEmpty()) {
            DummyCard dummy(thrown);
            room->throwCard(&dummy, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString()),
                            nullptr);
        }
        clearFog(room, player);
        QStringList fogged;
        for (int i = 0; i < thrown.length() && i < names.length(); ++i)
            if (ServerPlayer *p = room->findPlayerByObjectName(names.at(i))) {
                room->addPlayerMark(p, "@fog");
                fogged << p->objectName();
            }
        player->setTag("IkMiaowuFog", fogged);
        return false;
    }
};

// ---------------------------------------------------------------- wind030

// 龙魂 with spades as fire 杀 and diamonds as 无懈可击.
class IkZhihun : public ViewAsSkillV2
{
public:
    IkZhihun() : ViewAsSkillV2("ikzhihun") { setResponseOrUse(true); }

    static int needed(const Player *self) { return qMax(1, self->getHp()); }

    static Card::Suit suitFor(const ActiveSkillRequest &request)
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Card::NoSuit;
        const QString pattern = request.pattern.toLower();
        if (pattern.contains("jink"))
            return Card::Club;
        if (pattern.contains("nullification"))
            return Card::Diamond;
        if (pattern.contains("peach"))
            return Card::Heart;
        if (pattern.contains("slash"))
            return Card::Spade;
        return Card::NoSuitBlack;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return self->isWounded() || Slash::IsAvailable(self);
        const Card::Suit suit = suitFor(request);
        return suit == Card::Club || suit == Card::Diamond || suit == Card::Spade
            || (suit == Card::Heart && self->getMark("Global_PreventPeach") == 0);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!card || card->hasFlag("using") || request.selectedCardIds.size() >= needed(self)
            || !(ownsCard(self, card) || self->getHandPile().contains(card->getEffectiveId())))
            return false;
        if (!request.selectedCardIds.isEmpty())
            return Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == card->getSuit();
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return (card->getSuit() == Card::Heart && self->isWounded())
                || (card->getSuit() == Card::Spade && Slash::IsAvailable(self));
        return card->getSuit() == suitFor(request);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == needed(request.initiator) && selectionValid(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Card *card = nullptr;
        switch (Sanguosha->getCard(request.selectedCardIds.first())->getSuit()) {
        case Card::Diamond: card = new Nullification(Card::SuitToBeDecided, 0); break;
        case Card::Heart: card = new Peach(Card::SuitToBeDecided, 0); break;
        case Card::Club: card = new Jink(Card::SuitToBeDecided, 0); break;
        case Card::Spade: card = new FireSlash(Card::SuitToBeDecided, 0); break;
        default: return nullptr;
        }
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
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

// ---------------------------------------------------------------- bloom010

class IkKujie : public ViewAsSkillV2
{
public:
    IkKujie() : ViewAsSkillV2("ikkujie", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !card || !card->isBlack() || card->getTypeId() == Card::TypeTrick
            || card->hasFlag("using"))
            return false;
        return ownsCard(request.initiator, card) || request.initiator->getHandPile().contains(card->getEffectiveId());
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
        auto *card = new SupplyShortage(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
};

class IkJieying : public TriggerSkillV2
{
public:
    IkJieying() : TriggerSkillV2("ikjieying")
    {
        events << EventPhaseSkipped;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Draw)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        ctx.owner->drawCards(1, objectName());
        return false;
    }
};

// 竭盈's 兵粮寸断 reaches anyone holding at least as many cards.
class IkJieyingTargetMod : public TargetModSkillV2
{
public:
    IkJieyingTargetMod() : TargetModSkillV2("#ikjieying", "SupplyShortage") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || !ctx.secondary
            || !ctx.primary->hasSkill("ikjieying") || ctx.secondary->getHandcardNum() < ctx.primary->getHandcardNum())
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1000);
    }
};

// ---------------------------------------------------------------- bloom012

class IkQiangxi : public ViewAsSkillV2
{
public:
    IkQiangxi() : ViewAsSkillV2("ikqiangxi") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->isKindOf("Weapon")
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() <= 1 && selectionValid(this, request);
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkQiangxiCard"; }

    // Pay a point of HP or a weapon, then deal a point of damage, at any distance.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target)
            return ContinueEffects;
        Room *room = source->getRoom();
        if (!ctx.use_card || ctx.use_card->subcardsLength() == 0)
            room->loseHp(source, 1, true, source, objectName());
        if (source->isAlive() && target->isAlive())
            room->damage(DamageStruct(objectName(), source, target));
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- bloom014

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

// ---------------------------------------------------------------- bloom030

class IkTiangai : public WakeSkill
{
public:
    IkTiangai() : WakeSkill("iktiangai")
    {
        events << EventPhaseStart;
        waked_skills = "ikjilve";
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->getMark("&bear") >= 4; }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (room->changeMaxHpForAwakenSkill(player, -1, objectName()) && player->isAlive())
            room->acquireSkillFromEffect(player, "ikjilve", ctx);
    }
};

// 极略 spending "忍": 虚视 (观星), 慧泉 (old 集智), 死噬 (完杀) or 隙境.
class IkJilveViewAs : public ViewAsSkillV2
{
public:
    IkJilveViewAs() : ViewAsSkillV2("ikjilve") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("&bear") > 0 && !request.initiator->hasSkill("wansha", true);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkJilveCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || player->getMark("&bear") == 0)
            return FinishSkill;
        player->loseMark("&bear");
        grantTracked(player->getRoom(), player, "IkJilveSkills", "wansha");
        return FinishSkill;
    }
};

class IkJilve : public TriggerSkillV2
{
public:
    IkJilve() : TriggerSkillV2("ikjilve")
    {
        events << CardUsed << EventPhaseStart << AskForRetrial << EventPhaseChanging;
        view_as_skill = new IkJilveViewAs;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive
            && player->getTag("IkJilveSkills").isValid())
            revokeTracked(room, player, "IkJilveSkills");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getMark("&bear") == 0)
            return TriggerList();
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !use.card || use.card->getTypeId() != Card::TypeTrick)
                return TriggerList();
        } else if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Start)
                return TriggerList();
        } else if (event == AskForRetrial) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (!judge || !judge->who || judge->who->isKongcheng())
                return TriggerList();
        } else {
            return TriggerList();
        }
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        player->loseMark("&bear");
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == CardUsed) {
            player->drawCards(1, objectName());
        } else if (event == EventPhaseStart) {
            const QList<int> ids = room->getNCards(qMin(5, room->alivePlayerCount()), false);
            room->askForGuanxing(player, ids, Room::GuanxingBothSides);
        } else {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (!judge || !judge->who || judge->who->isKongcheng())
                return false;
            const QList<int> hand = judge->who->handCards();
            room->showAllCards(judge->who);
            room->fillAG(hand, player);
            int id = room->askForAG(player, hand, false, objectName());
            room->clearAG(player);
            if (!hand.contains(id))
                id = hand.first();
            if (room->getCardOwner(id) == judge->who && room->getCardPlace(id) == Player::PlaceHand)
                room->retrial(Sanguosha->getCard(id), player, judge, objectName());
        }
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

// ---------------------------------------------------------------- snow010

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

// ---------------------------------------------------------------- snow013

class IkHuapan : public TriggerSkillV2
{
public:
    IkHuapan() : TriggerSkillV2("ikhuapan") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getHp() <= 0 || !move.from
            || !move.from->isAlive() || !move.from_places.contains(Player::PlaceHand))
            return TriggerList();
        const int reason = move.reason.m_reason;
        const bool dismantled = reason == CardMoveReason::S_REASON_DISMANTLE
            && move.reason.m_playerId != move.reason.m_targetId;
        const bool taken = move.to && move.to != move.from && move.to_place == Player::PlaceHand
            && reason != CardMoveReason::S_REASON_GIVE && reason != CardMoveReason::S_REASON_SWAP;
        if (!dismantled && !taken)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->loseHp(player, 1, true, player, objectName());
        ServerPlayer *from = qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().from);
        if (from && from->isAlive())
            from->drawCards(2, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- snow014

class IkChizhu : public WakeSkill
{
public:
    IkChizhu() : WakeSkill("ikchizhu")
    {
        events << EventPhaseStart;
        waked_skills = "yingzi,ikliangban";
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->getHp() == 1; }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->changeMaxHpForAwakenSkill(player, -1, objectName()) || !player->isAlive())
            return;
        if (player->isWounded() && room->askForChoice(player, objectName(), "recover+draw") == "recover")
            room->recover(player, RecoverStruct(objectName(), player));
        else
            player->drawCards(2, objectName());
        room->acquireSkillFromEffect(player, "yingzi", ctx);
        room->acquireSkillFromEffect(player, "ikliangban", ctx);
    }
};

// 制霸 for yuki characters; an awakened lord may refuse.
class IkBianshengPindian : public ViewAsSkillV2
{
public:
    IkBianshengPindian() : ViewAsSkillV2("ikbiansheng_pindian") { attached_lord_skill = true; }

    static bool openTo(const Player *self, const Player *lord)
    {
        return lord->hasLordSkill("ikbiansheng") && lord != self && !lord->isKongcheng()
            && lord->getMark("ikbiansheng_used-PlayClear") == 0;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || self->getKingdom() != "yuki"
            || self->isKongcheng())
            return false;
        foreach (const Player *p, self->getAliveSiblings())
            if (openTo(self, p))
                return true;
        return false;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkBianshengCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *lord) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !lord || !lord->isAlive() || !source->canPindian(lord))
            return ContinueEffects;
        Room *room = source->getRoom();
        room->setPlayerMark(lord, "ikbiansheng_used-PlayClear", 1);
        room->notifySkillInvoked(lord, "ikbiansheng");
        if (lord->getMark("ikchizhu") > 0 && room->askForChoice(lord, objectName(), "accept+reject") == "reject") {
            LogMessage log;
            log.type = "#IkBianshengReject";
            log.from = lord;
            log.to << source;
            log.arg = objectName();
            room->sendLog(log);
            return ContinueEffects;
        }
        PindianStruct *pindian = source->PinDian(lord, objectName());
        if (!pindian || pindian->success || !lord->isAlive() || !lord->askForSkillInvoke("ikbiansheng", "pindian"))
            return ContinueEffects;
        QList<int> ids;
        const QList<const Card *> shown{pindian->from_card, pindian->to_card};
        foreach (const Card *card, shown)
            if (card && room->getCardPlace(card->getEffectiveId()) == Player::DiscardPile)
                ids << card->getEffectiveId();
        if (!ids.isEmpty()) {
            DummyCard dummy(ids);
            room->obtainCard(lord, &dummy);
        }
        return ContinueEffects;
    }
};

class IkBiansheng : public TriggerSkillV2
{
public:
    IkBiansheng() : TriggerSkillV2("ikbiansheng$")
    {
        events << GameStart << EventAcquireSkill << EventLoseSkill << Death << GeneralShown << GeneralHidden;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        syncAttachedChildren(room, "ikbiansheng", "ikbiansheng_pindian", true);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- luna001

// A 杀 between the owner and anyone else takes two 闪 to offset.
class IkHuanbei : public TriggerSkillV2
{
public:
    IkHuanbei() : TriggerSkillV2("ikhuanbei")
    {
        events << CardEffected;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.card || !effect.card->isKindOf("Slash") || !effect.from || !effect.to || effect.from == effect.to
            || effect.offset_num != 1)
            return TriggerList();
        if (effect.from->isAlive() && effect.from->hasSkill(objectName()))
            return TriggerList{{effect.from, {objectName()}}};
        if (effect.to->isAlive() && effect.to->hasSkill(objectName()))
            return TriggerList{{effect.to, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        effect.offset_num = 2;
        *ctx.original_data = QVariant::fromValue(effect);
        return false;
    }
};

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

// ---------------------------------------------------------------- luna014

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
IkSuinieCard::IkSuinieCard() { setSkillName("iksuinie"); mute = true; }
IkQiangxiCard::IkQiangxiCard() { setSkillName("ikqiangxi"); mute = true; }
IkYihuoCard::IkYihuoCard() { setSkillName("ikyihuov"); mute = true; }
IkJilveCard::IkJilveCard() { setSkillName("ikjilve"); mute = true; }
IkYuanjieCard::IkYuanjieCard() { setSkillName("ikyuanjie"); mute = true; }
IkBianshengCard::IkBianshengCard() { setSkillName("ikbiansheng_pindian"); mute = true; }
IkXuzhaoCard::IkXuzhaoCard() { setSkillName("ikxuzhao"); mute = true; }
IkYujiCard::IkYujiCard() { setSkillName("ikyujiv"); mute = true; }

IkaiMokuPackage::IkaiMokuPackage()
    : Package("ikai-moku")
{
    General *wind008 = new General(this, "wind008", "kaze");
    wind008->addSkill(new IkLiegong);
    wind008->addSkill(new IkHuanghun);

    // 狂骨 is 十周年 狂骨; 疾步 is 马术.
    General *wind009 = new General(this, "wind009", "kaze");
    wind009->addSkill("tenyearkuanggu");
    wind009->addSkill("mashu");

    General *wind010 = new General(this, "wind010", "kaze", 3);
    wind010->addSkill(new IkFuhua);
    wind010->addSkill(new IkFuhuaDraw);
    related_skills.insert("ikfuhua", "#ikfuhua");
    wind010->addSkill(new IkSuinie);

    // 净涅 is 八阵; 歼焰 is 火计; 绚影 is 看破.
    General *wind011 = new General(this, "wind011", "kaze", 3);
    wind011->addSkill("bazhen");
    wind011->addSkill("huoji");
    wind011->addSkill("kanpo");

    // 挑衅 is 挑衅.
    General *wind012 = new General(this, "wind012", "kaze");
    wind012->addSkill("tiaoxin");
    wind012->addSkill(new IkShengtian);
    wind012->addRelateSkill("ikxuanwu");
    wind012->addRelateSkill("ikmohua");

    // 祸首 is 祸首.
    General *wind013 = new General(this, "wind013", "kaze");
    wind013->addSkill("huoshou");
    wind013->addSkill(new IkZailuan);

    // 福乐 is 享乐; 攸祭 is 放权.
    General *wind014 = new General(this, "wind014$", "kaze", 3);
    wind014->addSkill("xiangle");
    wind014->addSkill("fangquan");
    wind014->addSkill(new IkRuoyu);

    // 巨鬼 is 巨象.
    General *wind015 = new General(this, "wind015", "kaze", 4, false);
    wind015->addSkill("juxiang");
    wind015->addSkill(new IkLieren);

    // 七曜 is 七星; 烈风 is 狂风.
    General *wind029 = new General(this, "wind029", "kaze", 3);
    wind029->addSkill("qixing");
    wind029->addSkill("kuangfeng");
    wind029->addSkill(new IkMiaowu);

    // 绝境 is 绝境.
    General *wind030 = new General(this, "wind030", "kaze", 2);
    wind030->addSkill("juejing");
    wind030->addSkill(new IkZhihun);

    General *bloom008 = new General(this, "bloom008", "hana");
    bloom008->addSkill(new IkXunyu);

    // 曼才 is 巧变.
    General *bloom009 = new General(this, "bloom009", "hana");
    bloom009->addSkill("qiaobian");

    General *bloom010 = new General(this, "bloom010", "hana");
    bloom010->addSkill(new IkKujie);
    bloom010->addSkill(new IkJieying);
    bloom010->addSkill(new IkJieyingTargetMod);
    related_skills.insert("ikjieying", "#ikjieying");

    // 宅魂 is 据守; 佛脚 is 解围.
    General *bloom011 = new General(this, "bloom011", "hana");
    bloom011->addSkill("jushou");
    bloom011->addSkill("jiewei");

    General *bloom012 = new General(this, "bloom012", "hana");
    bloom012->addSkill(new IkQiangxi);

    // 御神 is 驱虎; 节命 is 节命.
    General *bloom013 = new General(this, "bloom013", "hana", 3);
    bloom013->addSkill("quhu");
    bloom013->addSkill("jieming");

    // 叹惋 is 行殇; 闭锁 is 放逐.
    General *bloom014 = new General(this, "bloom014$", "hana", 3);
    bloom014->addSkill("xingshang");
    bloom014->addSkill("fangzhu");
    bloom014->addSkill(new IkSongwei);

    // 隐蝶 is 屯田; 鬼月 is 凿险 (幻舞 is 急袭).
    General *bloom015 = new General(this, "bloom015", "hana");
    bloom015->addSkill("tuntian");
    bloom015->addSkill("zaoxian");
    bloom015->addRelateSkill("jixi");

    General *bloom029 = new General(this, "bloom029", "hana", 3);
    bloom029->addSkill(new IkYihuo);
    bloom029->addSkill(new IkGuixin);

    // 忍枷 is 忍戒 (its "忍" stand for "桎").
    General *bloom030 = new General(this, "bloom030", "hana");
    bloom030->addSkill("renjie");
    bloom030->addSkill(new IkTiangai);
    bloom030->addRelateSkill("ikjilve");

    General *snow009 = new General(this, "snow009", "yuki");
    snow009->addSkill(new IkLiangban);
    snow009->addSkill(new IkDiewu);

    // 卓始 is 好施.
    General *snow010 = new General(this, "snow010", "yuki", 3);
    snow010->addSkill("haoshi");
    snow010->addSkill(new IkYuanjie);

    // 知惠 is 天香; 赤秋 is 红颜.
    General *snow011 = new General(this, "snow011", "yuki", 3, false);
    snow011->addSkill("tianxiang");
    snow011->addSkill("hongyan");

    // 歼略 is 天义.
    General *snow012 = new General(this, "snow012", "yuki");
    snow012->addSkill("tianyi");

    // 苏生 is 不屈.
    General *snow013 = new General(this, "snow013", "yuki");
    snow013->addSkill("buqu");
    snow013->addSkill(new IkHuapan);

    // 赫訳 is 激昂.
    General *snow014 = new General(this, "snow014$", "yuki");
    snow014->addSkill("jiang");
    snow014->addSkill(new IkChizhu);
    snow014->addSkill(new IkBiansheng);
    snow014->addRelateSkill("yingzi");
    snow014->addRelateSkill("ikliangban");

    // 羁绊 is 直谏; 箕箒 is 固政.
    General *snow015 = new General(this, "snow015", "yuki", 3);
    snow015->addSkill("zhijian");
    snow015->addSkill("guzheng");

    // 略决 is 涉猎; 灵视 is 攻心.
    General *snow029 = new General(this, "snow029", "yuki", 3);
    snow029->addSkill("shelie");
    snow029->addSkill("gongxin");

    // 龙息 is 琴音; 业焰 is 业炎.
    General *snow030 = new General(this, "snow030", "yuki");
    snow030->addSkill("qinyin");
    snow030->addSkill("yeyan");

    // 腐生 is 腐生; 崩坏 is 崩坏.
    General *luna001 = new General(this, "luna001$", "tsuki", 8);
    luna001->addSkill("ikfusheng");
    luna001->addSkill(new IkHuanbei);
    luna001->addSkill("benghuai");
    luna001->addSkill(new IkWuhua);

    // 星煌 is 乱击.
    General *luna004 = new General(this, "luna004", "tsuki");
    luna004->addSkill("luanji");
    luna004->addSkill(new IkXuzhao);

    // 契羽 is 双雄.
    General *luna005 = new General(this, "luna005", "tsuki");
    luna005->addSkill(new IkJingfa);
    luna005->addSkill("shuangxiong");

    // 死噬 is 完杀; 文乐 is 乱武; 墨羽 is 帷幕.
    General *luna007 = new General(this, "luna007", "tsuki", 3);
    luna007->addSkill("wansha");
    luna007->addSkill("luanwu");
    luna007->addSkill("weimu");

    // 疾步 is 马术.
    General *luna008 = new General(this, "luna008", "tsuki");
    luna008->addSkill("mashu");
    luna008->addSkill(new IkKongsa);

    // 幻身 is 化身; 灵契 is 新生.
    General *luna009 = new General(this, "luna009", "tsuki", 3);
    luna009->addSkill("huashen");
    luna009->addSkill("xinsheng");

    // 诡惑 is the old 蛊惑.
    General *luna011 = new General(this, "luna011", "tsuki");
    luna011->addSkill("nosguhuo");

    // 辉耀 is 悲歌; 淒煌 is 断肠.
    General *luna012 = new General(this, "luna012", "tsuki", 3, false);
    luna012->addSkill("beige");
    luna012->addSkill("duanchang");

    // 雷击 is 十周年 雷击; 天势 is 鬼道.
    General *luna014 = new General(this, "luna014$", "tsuki", 3);
    luna014->addSkill("tenyearleiji");
    luna014->addSkill("guidao");
    luna014->addSkill(new IkYuji);

    // 拙火 is 狂暴; 无谋 is 无谋; 碎空 is 无前; 天舞 is 神愤.
    General *luna029 = new General(this, "luna029", "tsuki", 5);
    luna029->addSkill("kuangbao");
    luna029->addSkill("wumou");
    luna029->addSkill("wuqian");
    luna029->addSkill("shenfen");

    skills << new IkMohua << new IkYihuoViewAs << new IkJilve << new IkBianshengPindian << new IkYujiGive;

    addMetaObject<IkHuanghunCard>();
    addMetaObject<IkSuinieCard>();
    addMetaObject<IkQiangxiCard>();
    addMetaObject<IkYihuoCard>();
    addMetaObject<IkJilveCard>();
    addMetaObject<IkYuanjieCard>();
    addMetaObject<IkBianshengCard>();
    addMetaObject<IkXuzhaoCard>();
    addMetaObject<IkYujiCard>();
}

ADD_PACKAGE(IkaiMoku)
