#include "ikai-kin.h"
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

// TouhouTripleSha's 异界·金. Most of its skills are 三国 originals under new names; those are
// reused by their local names and only the differing ones are ported.

namespace {

// ---------------------------------------------------------------- wind017

class IkXinchao : public ViewAsSkillV2
{
public:
    IkXinchao() : ViewAsSkillV2("ikxinchao") { setPhaseName("Play"); }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkXinchaoCard"; }

    // Look at three: take any hearts, the rest go back on top in any order.
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        QList<int> left = room->getNCards(3, false);
        if (left.isEmpty())
            return FinishSkill;
        LogMessage log;
        log.type = "$ViewDrawPile";
        log.from = source;
        log.card_str = ListI2S(left).join("+");
        room->sendLog(log, source);
        QList<int> hearts, others;
        foreach (int id, left)
            (Sanguosha->getCard(id)->getSuit() == Card::Heart ? hearts : others) << id;
        QList<int> taken;
        while (!hearts.isEmpty()) {
            room->fillAG(left, source, others);
            const int id = room->askForAG(source, hearts, true, objectName());
            room->clearAG(source);
            if (id == -1)
                break;
            hearts.removeOne(id);
            left.removeOne(id);
            taken << id;
        }
        if (!taken.isEmpty()) {
            room->showCard(source, taken);
            DummyCard dummy(taken);
            room->obtainCard(source, &dummy);
        }
        if (!left.isEmpty())
            room->askForGuanxing(source, left, Room::GuanxingUpOnly);
        return FinishSkill;
    }
};

class IkShangshi : public TriggerSkillV2
{
public:
    IkShangshi() : TriggerSkillV2("ikshangshi") { events << Death; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        if (!player || death.who != player || !player->hasSkill(objectName()))
            return TriggerList();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        if (!killer || !killer->isAlive() || killer == player || !killer->canDiscard(killer, "he"))
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

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        if (killer && killer->isAlive())
            killer->throwAllHandCardsAndEquips();
        return false;
    }
};

// ---------------------------------------------------------------- wind018

class IkSishi : public ViewAsSkillV2
{
public:
    IkSishi() : ViewAsSkillV2("iksishi", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card)
            && card->getTypeId() != Card::TypeBasic
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkSishiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive())
            return ContinueEffects;
        Room *room = target->getRoom();
        QStringList choices{"draw"};
        if (target->isWounded())
            choices << "recover";
        if (!target->faceUp() || target->isChained())
            choices << "reset";
        const QString choice = room->askForChoice(target, objectName(), choices.join("+"));
        if (choice == "recover") {
            room->recover(target, RecoverStruct(objectName(), ctx.invoker));
        } else if (choice == "reset") {
            if (target->isChained())
                room->setPlayerChained(target, false, ctx.invoker);
            if (!target->faceUp())
                target->turnOver();
        } else {
            target->drawCards(2, objectName());
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- wind019

class IkWanhun : public TriggerSkillV2
{
public:
    IkWanhun() : TriggerSkillV2("ikwanhun") { events << DamageCaused; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.to
            || !damage.by_user || damage.chain || damage.transfer || !damage.card
            || !(damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel")))
            return TriggerList();
        const int distance = player->distanceTo(damage.to);
        if (distance < 0 || distance > 2)
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

    // A non-heart judgement turns the damage into a lost point of max HP.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        JudgeStruct judge;
        judge.pattern = ".|heart";
        judge.good = false;
        judge.who = player;
        judge.reason = objectName();
        room->judge(judge);
        if (!judge.isGood())
            return false;
        if (damage.to && damage.to->isAlive())
            room->loseMaxHp(damage.to, 1, objectName());
        return true;
    }
};

// ---------------------------------------------------------------- wind020

class IkFansheng : public TriggerSkillV2
{
public:
    IkFansheng() : TriggerSkillV2("ikfansheng")
    {
        events << AskForPeaches;
        frequency = Limited;
        limit_mark = "@fansheng";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DyingStruct>().who != player || !player->hasSkill(objectName()) || player->getHp() > 0
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
        const int amount = qMin(3, player->getMaxHp()) - player->getHp();
        if (amount > 0)
            room->recover(player, RecoverStruct(objectName(), player, amount));
        player->turnOver();
        return false;
    }
};

class IkMohun : public WakeSkill
{
public:
    IkMohun() : WakeSkill("ikmohun")
    {
        events << EventPhaseStart << PreCardUsed;
        waked_skills = "thheiguan";
    }

    // Two 杀 of one suit in the owner's turn.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != PreCardUsed || !player || !isOwnTurn(player))
            return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || !use.card->isKindOf("Slash"))
            return false;
        const QString mark = "ikmohun_" + QString::number(static_cast<int>(use.card->getSuit())) + "-Clear";
        room->addPlayerMark(player, mark);
        if (player->getMark(mark) >= 2)
            room->setPlayerMark(player, "ikmohun_ready-Clear", 1);
        return false;
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Finish;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->getMark("ikmohun_ready-Clear") > 0; }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->changeMaxHpForAwakenSkill(player, -1, objectName()) || !player->isAlive())
            return;
        player->throwAllHandCards();
        room->acquireSkillFromEffect(player, "thheiguan", ctx);
    }
};

// ---------------------------------------------------------------- wind021

class IkZuyao : public ViewAsSkillV2
{
public:
    IkZuyao() : ViewAsSkillV2("ikzuyao", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getPhase() != Player::Play)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(self);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE)
            && request.pattern.contains("slash", Qt::CaseInsensitive);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !card || card->getSuit() != Card::Diamond || card->hasFlag("using"))
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
        Slash *slash = new Slash(material->getSuit(), material->getNumber());
        slash->addSubcard(material);
        slash->setSkillName(objectName());
        return slash;
    }
};

// The colour of the owner's last 杀 this play phase, kept as colour + 1.
class IkZuyaoRecord : public TriggerSkillV2
{
public:
    IkZuyaoRecord() : TriggerSkillV2("#ikzuyao-record")
    {
        events << PreCardUsed;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (player && use.from == player && player->getPhase() == Player::Play && use.card && use.card->isKindOf("Slash")
            && player->hasSkill("ikzuyao"))
            room->setPlayerMark(player, "ikzuyao_colour-PlayClear", static_cast<int>(use.card->getColor()) + 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// A 杀 whose colour differs from the previous one is outside the limit.
class IkZuyaoTargetMod : public TargetModSkillV2
{
public:
    IkZuyaoTargetMod() : TargetModSkillV2("#ikzuyao-target", "Slash") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.primary || !ctx.card || !ctx.primary->hasSkill("ikzuyao"))
            return CorrectSkillResult::noEffect();
        const int last = ctx.primary->getMark("ikzuyao_colour-PlayClear");
        if (last == 0 || last == static_cast<int>(ctx.card->getColor()) + 1)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::unlimitedResidue();
    }
};

// ---------------------------------------------------------------- wind031

class IkMoguang : public TriggerSkillV2
{
public:
    IkMoguang() : TriggerSkillV2("ikmoguang") { events << EventPhaseStart << EventPhaseChanging << Death; }

    static void release(Room *room, ServerPlayer *owner)
    {
        const QStringList victims = owner->getTag("IkMoguangVictims").toStringList();
        owner->removeTag("IkMoguangVictims");
        foreach (const QString &entry, victims) {
            const QStringList parts = entry.split("|");
            ServerPlayer *p = room->findPlayerByObjectName(parts.value(0), true);
            if (!p)
                continue;
            room->removePlayerCardLimitationByReason(p, "ikmoguang_" + owner->objectName());
            room->setPlayerMark(p, "@moguang_" + parts.value(1), 0);
        }
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->getTag("IkMoguangVictims").toStringList().isEmpty())
            return false;
        if ((event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            || (event == Death && data.value<DeathStruct>().who == player))
            release(room, player);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play
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

    // Draw one, throw a hand card, and someone adjacent cannot use or play hand cards of its colour.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->drawCards(1, objectName());
        if (!player->isAlive() || !player->canDiscard(player, "h"))
            return false;
        const Card *card = room->askForCard(player, ".!", "@ikmoguang");
        if (!card || !player->canDiscard(player, card->getEffectiveId())) {
            QList<int> candidates;
            foreach (int id, player->handCards())
                if (player->canDiscard(player, id))
                    candidates << id;
            if (candidates.isEmpty())
                return false;
            card = Sanguosha->getCard(candidates.at(QRandomGenerator::global()->bounded(candidates.length())));
            room->throwCard(card, player);
        }
        const QString colour = card->isBlack() ? QStringLiteral("black") : (card->isRed() ? QStringLiteral("red") : QString());
        if (colour.isEmpty() || !player->isAlive())
            return false;
        QList<ServerPlayer *> adjacent;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->distanceTo(p) == 1)
                adjacent << p;
        if (adjacent.isEmpty())
            return false;
        ServerPlayer *victim = room->askForPlayerChosen(player, adjacent, objectName(), "@ikmoguang-target");
        if (!victim)
            return false;
        room->setPlayerCardLimitation(victim, "use,response", ".|" + colour + "|.|hand", false,
                                      "ikmoguang_" + player->objectName());
        room->addPlayerMark(victim, "@moguang_" + colour);
        QStringList victims = player->getTag("IkMoguangVictims").toStringList();
        victims << victim->objectName() + "|" + colour;
        player->setTag("IkMoguangVictims", victims);
        LogMessage log;
        log.type = "#IkMoguang";
        log.from = victim;
        log.arg = "no_suit_" + colour;
        room->sendLog(log);
        return false;
    }
};

// ---------------------------------------------------------------- wind032

class IkLichiViewAs : public ViewAsSkillV2
{
public:
    IkLichiViewAs() : ViewAsSkillV2("iklichi", 2) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(request.initiator);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern.contains("slash", Qt::CaseInsensitive);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (request.selectedCardIds.size() >= 2 || !card || card->hasFlag("using"))
            return false;
        return ownsHandCard(request.initiator, card) || request.initiator->getHandPile().contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 2 && selectionValid(this, request);
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

// Damage from its 杀 in the play phase lends 赤莲 and 咆哮 for the turn.
class IkLichi : public TriggerSkillV2
{
public:
    IkLichi() : TriggerSkillV2("iklichi")
    {
        events << DamageComplete << EventPhaseChanging;
        view_as_skill = new IkLichiViewAs;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive
            && player->getTag("IkLichiSkills").isValid())
            revokeTracked(room, player, "IkLichiSkills");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != DamageComplete)
            return TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *from = damage.from;
        if (!from || !from->isAlive() || !from->hasSkill(objectName()) || from->getPhase() != Player::Play || !damage.card
            || !damage.card->isKindOf("Slash") || damage.card->getSkillName() != objectName()
            || (from->hasSkill("ikchilian", true) && from->hasSkill("paoxiao", true)))
            return TriggerList();
        return TriggerList{{from, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        grantTracked(room, ctx.owner, "IkLichiSkills", "ikchilian");
        grantTracked(room, ctx.owner, "IkLichiSkills", "paoxiao");
        return false;
    }
};

// ---------------------------------------------------------------- wind037 / snow021

class IkXuanrenViewAs : public ViewAsSkillV2
{
public:
    IkXuanrenViewAs() : ViewAsSkillV2("ikxuanren", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(request.initiator);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE)
            && request.pattern.contains("slash", Qt::CaseInsensitive);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.selectedCardIds.isEmpty() || !card || card->getTypeId() != Card::TypeEquip || card->hasFlag("using"))
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
        Slash *slash = new Slash(material->getSuit(), material->getNumber());
        slash->addSubcard(material);
        slash->setSkillName(objectName());
        return slash;
    }
};

class IkXuanren : public TriggerSkillV2
{
public:
    IkXuanren() : TriggerSkillV2("ikxuanren")
    {
        events << TargetSpecified;
        view_as_skill = new IkXuanrenViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !use.card || !use.card->isKindOf("Slash")
            || use.card->getSkillName() != objectName())
            return TriggerList();
        foreach (ServerPlayer *to, use.to)
            if (player->canDiscard(to, "he"))
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }

    // Each target may lose a card.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        foreach (ServerPlayer *to, use.to) {
            if (!player->isAlive() || !to->isAlive() || !player->canDiscard(to, "he")
                || !player->askForSkillInvoke(objectName(), QVariant::fromValue(to)))
                continue;
            room->broadcastSkillInvoke(objectName());
            const int id = room->askForCardChosen(player, to, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, to, player);
        }
        return false;
    }
};

class IkXuanrenTargetMod : public TargetModSkillV2
{
public:
    IkXuanrenTargetMod() : TargetModSkillV2("#ikxuanren-target", "Slash") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.card || ctx.card->getSkillName() != "ikxuanren")
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1000);
    }
};

class IkLanjian : public TriggerSkillV2
{
public:
    IkLanjian() : TriggerSkillV2("iklanjian") { events << CardOffset; }

    static QList<int> jinkIds(Room *room, const CardEffectStruct &effect)
    {
        QList<int> ids;
        if (!effect.offset_card)
            return ids;
        const QList<int> candidates = effect.offset_card->isVirtualCard() ? effect.offset_card->getSubcards()
                                                                          : QList<int>{effect.offset_card->getEffectiveId()};
        foreach (int id, candidates)
            if (id >= 0 && room->getCardPlace(id) == Player::DiscardPile)
                ids << id;
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        ServerPlayer *from = effect.from;
        if (!from || !from->isAlive() || !from->hasSkill(objectName()) || from->getPhase() != Player::Play || !effect.to
            || !effect.card || !effect.card->isKindOf("Slash") || !effect.offset_card
            || !effect.offset_card->isKindOf("Jink") || jinkIds(room, effect).isEmpty())
            return TriggerList();
        return TriggerList{{from, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        room->fillAG(jinkIds(room, effect), player);
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(effect.to), objectName(),
                                                        "iklanjian-invoke:" + effect.to->objectName(), true, true);
        room->clearAG(player);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    // The 闪 goes to someone else; unless that is the user, it slashes the same target again.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        const QList<int> ids = jinkIds(room, effect);
        if (!target || !target->isAlive() || ids.isEmpty())
            return false;
        DummyCard dummy(ids);
        room->obtainCard(target, &dummy);
        if (target != player && player->isAlive() && effect.to && effect.to->isAlive()
            && player->canSlash(effect.to, nullptr, false))
            room->askForUseSlashTo(player, effect.to, "iklanjian-slash:" + effect.to->objectName(), false, true);
        return false;
    }
};

// ---------------------------------------------------------------- wind038

class IkFengxin : public TriggerSkillV2
{
public:
    IkFengxin() : TriggerSkillV2("ikfengxin") { events << EventPhaseStart << EventPhaseEnd << Death; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != Death)
            return false;
        const DeathStruct death = data.value<DeathStruct>();
        if (death.damage && death.damage->from && death.damage->from->getPhase() == Player::Play)
            room->setPlayerMark(death.damage->from, "ikfengxin_kill", 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Play)
            return result;
        if (event == EventPhaseStart) {
            if (player->isAlive())
                foreach (ServerPlayer *owner, room->getOtherPlayers(player))
                    if (owner->hasSkill(objectName()))
                        result[owner] << objectName();
        } else if (event == EventPhaseEnd) {
            foreach (const QString &name, player->getTag("IkFengxinOwners").toStringList())
                if (ServerPlayer *owner = room->findPlayerByObjectName(name))
                    result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd)
            return true;
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == EventPhaseEnd) {
            QStringList owners = player->getTag("IkFengxinOwners").toStringList();
            owners.removeAll(owner->objectName());
            if (owners.isEmpty())
                player->removeTag("IkFengxinOwners");
            else
                player->setTag("IkFengxinOwners", owners);
            const bool killed = player->getMark("ikfengxin_kill") > 0;
            if (owners.isEmpty())
                room->setPlayerMark(player, "ikfengxin_kill", 0);
            if (killed || !owner->isAlive())
                return false;
            LogMessage log;
            log.type = "#IkFengxin";
            log.from = player;
            log.to << owner;
            log.arg = objectName();
            room->sendLog(log);
            if (owner->getHandcardNum() < 2
                || !room->askForDiscard(owner, objectName(), 2, 2, true, false, "ikfengxin-discard"))
                room->loseHp(owner, 1, true, owner, objectName());
            return false;
        }
        QStringList owners = player->getTag("IkFengxinOwners").toStringList();
        if (!owners.contains(owner->objectName()))
            owners << owner->objectName();
        player->setTag("IkFengxinOwners", owners);
        room->setPlayerMark(player, "ikfengxin_kill", 0);
        owner->drawCards(2, objectName());
        if (!owner->isAlive() || !player->isAlive() || owner->isNude())
            return false;
        const int n = qMin(2, owner->getCardCount());
        if (const Card *given = room->askForExchange(owner, objectName(), n, n, true,
                                                     QString("@ikfengxin-give::%1:%2").arg(player->objectName()).arg(n))) {
            room->giveCard(owner, player, given->getSubcards(), objectName(), false);
            delete given;
        }
        return false;
    }
};

// ---------------------------------------------------------------- wind039

bool allAdjacent(const Player *from)
{
    foreach (const Player *p, from->getAliveSiblings())
        if (from->distanceTo(p) != 1)
            return false;
    return true;
}

class IkShensha : public TriggerSkillV2
{
public:
    IkShensha() : TriggerSkillV2("ikshensha")
    {
        events << CardFinished << TargetSpecified << EventPhaseStart;
        frequency = Compulsory;
    }

    // Each card the owner finishes in its own turn draws it one step closer.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished || !player || !player->hasSkill(objectName()) || !isOwnTurn(player))
            return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill)
            room->addPlayerMark(player, "ikshensha-Clear");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !isOwnTurn(player) || !allAdjacent(player))
            return TriggerList();
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill || use.to.isEmpty())
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        if (event == EventPhaseStart && player->getPhase() == Player::Discard)
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->isWounded())
                    return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == TargetSpecified) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            foreach (ServerPlayer *p, use.to)
                if (p != player)
                    p->addQinggangTag(use.card);
            return false;
        }
        int wounded = 0;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->isWounded())
                ++wounded;
        room->sendCompulsoryTriggerLog(player, objectName());
        player->drawCards(qMin(wounded, 4), objectName());
        return false;
    }
};

class IkShenshaDistance : public DistanceSkill
{
public:
    IkShenshaDistance() : DistanceSkill("#ikshensha-dist") {}

    int getCorrect(const Player *from, const Player *) const override
    {
        if (!from || !from->hasSkill("ikshensha") || from->getPhase() == Player::NotActive)
            return 0;
        return -from->getMark("ikshensha-Clear");
    }
};

class IkShenshaTargetMod : public TargetModSkillV2
{
public:
    IkShenshaTargetMod() : TargetModSkillV2("#ikshensha-target", "Slash") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::ExtraTarget || !ctx.primary || !ctx.primary->hasSkill("ikshensha")
            || ctx.primary->getPhase() == Player::NotActive || !allAdjacent(ctx.primary))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1);
    }
};

// ---------------------------------------------------------------- wind055

class IkZangyu : public ViewAsSkillV2
{
public:
    IkZangyu() : ViewAsSkillV2("ikzangyu", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
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
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isKongcheng();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkZangyuCard"; }

    // Both show a card: 杀 against a non-闪, or a non-杀 against a 闪, wins damage or a card.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || target->isKongcheng() || !ctx.use_card
            || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int mine = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(mine) != source || room->getCardPlace(mine) != Player::PlaceHand)
            return ContinueEffects;
        const Card *theirs = room->askForCardShow(target, source, objectName());
        if (!theirs)
            theirs = target->getRandomHandCard();
        if (!theirs)
            return ContinueEffects;
        room->showCard(source, mine);
        room->showCard(target, theirs->getEffectiveId());
        const Card *own = Sanguosha->getCard(mine);
        if (!(own->isKindOf("Slash") ^ theirs->isKindOf("Jink")))
            return ContinueEffects;
        if (source->canDiscard(source, mine))
            room->throwCard(mine, source);
        if (!source->isAlive() || !target->isAlive())
            return ContinueEffects;
        if (target->isNude() || room->askForChoice(source, objectName(), "damage+get", QVariant::fromValue(target)) == "damage") {
            room->damage(DamageStruct(objectName(), source, target));
        } else {
            const int id = room->askForCardChosen(source, target, "he", objectName());
            if (id >= 0)
                room->obtainCard(source, id, false);
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- wind062

class IkShitie : public ViewAsSkillV2
{
public:
    IkShitie() : ViewAsSkillV2("ikshitie") { setPhaseName("Play"); }

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
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isKongcheng();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkShitieCard"; }

    // Take a hand card; the target then takes the basics among two revealed, or a bigger next draw.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || target->isKongcheng())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = room->askForCardChosen(source, target, "h", objectName());
        if (id >= 0)
            room->obtainCard(source, id, false);
        if (!target->isAlive())
            return ContinueEffects;
        if (room->askForChoice(target, objectName(), "basic+draw") == "draw") {
            room->addPlayerMark(target, "@eat");
            return ContinueEffects;
        }
        const QList<int> ids = room->getNCards(2, false);
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        QList<int> basics, others;
        foreach (int cid, ids)
            (Sanguosha->getCard(cid)->getTypeId() == Card::TypeBasic ? basics : others) << cid;
        if (!basics.isEmpty()) {
            DummyCard dummy(basics);
            room->obtainCard(target, &dummy);
        }
        if (!others.isEmpty()) {
            DummyCard dummy(others);
            room->throwCard(&dummy, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, QString(), objectName(), QString()),
                            nullptr);
        }
        return ContinueEffects;
    }
};

class IkShitieDraw : public TriggerSkillV2
{
public:
    IkShitieDraw() : TriggerSkillV2("#ikshitie")
    {
        events << DrawNCards;
        frequency = Compulsory;
        global = true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DrawStruct draw = data.value<DrawStruct>();
        if (!player || draw.who != player || draw.reason != "draw_phase" || player->getMark("@eat") == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += player->getMark("@eat");
        room->setPlayerMark(player, "@eat", 0);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class IkLinbei : public TriggerSkillV2
{
public:
    IkLinbei() : TriggerSkillV2("iklinbei") { events << CardsMoveOneTime << EventPhaseChanging; }

    static QList<int> arrived(Room *room, ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        foreach (int id, move.card_ids)
            if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand
                && player->canDiscard(player, id))
                ids << id;
        return ids;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive || player->getMark("iklinbei_draw") == 0)
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to != player || move.to_place != Player::PlaceHand || !isOwnTurn(player)
            || arrived(room, player, move).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging)
            return true;
        const QList<int> ids = arrived(room, player, ctx.original_data->value<CardsMoveOneTimeStruct>());
        const Card *chosen = room->askForExchange(player, objectName(), ids.length(), 1, false, "@iklinbei", true,
                                                  ListI2S(ids).join(","));
        if (!chosen)
            return false;
        ctx.extra_data = ListI2V(chosen->getSubcards());
        delete chosen;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Throw some of the arrivals now; draw that many when the turn ends.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) {
            const int n = player->getMark("iklinbei_draw");
            room->setPlayerMark(player, "iklinbei_draw", 0);
            room->sendCompulsoryTriggerLog(player, objectName());
            player->drawCards(n, objectName());
            return false;
        }
        QList<int> ids;
        foreach (int id, ListV2I(ctx.extra_data.toList()))
            if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand)
                ids << id;
        if (ids.isEmpty())
            return false;
        LogMessage log;
        log.type = "$DiscardCardWithSkill";
        log.from = player;
        log.arg = objectName();
        log.card_str = ListI2S(ids).join("+");
        room->sendLog(log);
        DummyCard dummy(ids);
        room->moveCardTo(&dummy, player, nullptr, Player::DiscardPile,
                         CardMoveReason(CardMoveReason::S_REASON_THROW, player->objectName(), objectName(), QString()),
                         false);
        room->addPlayerMark(player, "iklinbei_draw", ids.length());
        return false;
    }
};

// ---------------------------------------------------------------- bloom016

class IkShihua : public TriggerSkillV2
{
public:
    IkShihua() : TriggerSkillV2("ikshihua")
    {
        events << BeforeCardsMove;
        frequency = Frequent;
    }

    // Another character's clubs discarded from hand or equipment, or leaving a judgement.
    static QList<int> clubs(Room *room, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        const bool judged = move.reason.m_reason == CardMoveReason::S_REASON_JUDGEDONE;
        const bool discarded = (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD;
        if (move.to_place != Player::DiscardPile || (!judged && !discarded))
            return ids;
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const int id = move.card_ids.at(i);
            const Player::Place from = move.from_places.value(i);
            if (Sanguosha->getCard(id)->getSuit() != Card::Club)
                continue;
            if (judged ? from == Player::PlaceJudge
                       : (room->getCardOwner(id) == move.from && (from == Player::PlaceHand || from == Player::PlaceEquip)))
                ids << id;
        }
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !move.from || move.from == player
            || clubs(room, move).isEmpty())
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

    // Unwanted cards are picked out one by one; the rest come to hand.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> ids = clubs(room, move);
        while (ids.length() > 1) {
            room->fillAG(ids, player);
            const int id = room->askForAG(player, ids, true, objectName());
            room->clearAG(player);
            if (id == -1)
                break;
            ids.removeOne(id);
        }
        if (ids.isEmpty())
            return false;
        move.removeCardIds(ids);
        *ctx.original_data = QVariant::fromValue(move);
        DummyCard dummy(ids);
        room->moveCardTo(&dummy, player, Player::PlaceHand, move.reason, true);
        return false;
    }
};

// ---------------------------------------------------------------- bloom017

class IkPiaohu : public TriggerSkillV2
{
public:
    IkPiaohu() : TriggerSkillV2("ikpiaohu") { events << EventPhaseStart; }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getCardCount() > p->getHp() && player->canDiscard(p, "he"))
                targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Start || !player->hasSkill(objectName())
            || candidates(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, candidates(room, player), objectName(), "@ikpiaohu", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    // Trim the target down to its HP; then the owner pays for the non-equipment, or it redraws.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return false;
        const int n = target->getCardCount() - target->getHp();
        QList<int> ids;
        int nonEquip = 0;
        for (int i = 0; i < n && player->canDiscard(target, "he") && ids.length() < target->getCardCount(); ++i) {
            const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard, ids);
            if (id < 0)
                break;
            ids << id;
            if (Sanguosha->getCard(id)->getTypeId() != Card::TypeEquip)
                ++nonEquip;
        }
        if (ids.isEmpty())
            return false;
        room->throwCard(ids, objectName(), target, player);
        const QString prompt = QString("@ikpiaohu-discard::%1:%2:%3").arg(target->objectName()).arg(nonEquip).arg(ids.length());
        if (nonEquip == 0 || !player->isAlive() || player->getCardCount() < nonEquip
            || !room->askForDiscard(player, objectName(), nonEquip, nonEquip, true, true, prompt)) {
            if (target->isAlive())
                target->drawCards(ids.length(), objectName());
        }
        return false;
    }
};

// ---------------------------------------------------------------- bloom022

class IkLundao : public TriggerSkillV2
{
public:
    IkLundao() : TriggerSkillV2("iklundao") { events << AskForRetrial; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !judge || !judge->who)
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

    // The draw pile's top card replaces the judgement.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge)
            return false;
        const int id = room->drawCard();
        if (id >= 0)
            room->retrial(Sanguosha->getCard(id), player, judge, objectName());
        return false;
    }
};

class IkXuanwu : public TriggerSkillV2
{
public:
    IkXuanwu() : TriggerSkillV2("ikxuanwu")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

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

    // A black judgement hands 1 + lost HP cards from the top to someone.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        JudgeStruct judge;
        judge.pattern = ".|black";
        judge.good = true;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        if (!judge.isGood() || !player->isAlive())
            return false;
        const QList<int> ids = room->getNCards(player->getLostHp() + 1, false);
        room->fillAG(ids, player);
        ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@ikxuanwu");
        room->clearAG(player);
        if (!target)
            target = player;
        DummyCard dummy(ids);
        room->setPlayerFlag(player, "Global_GongxinOperator");
        room->obtainCard(target, &dummy, false);
        room->setPlayerFlag(player, "-Global_GongxinOperator");
        return false;
    }
};

// ---------------------------------------------------------------- bloom027

class IkBingyan : public ViewAsSkillV2
{
public:
    IkBingyan() : ViewAsSkillV2("ikbingyan") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsHandCard(request.initiator, card) && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && selectionValid(this, request);
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkBingyanCard"; }

    // The target discards a hand card of a type the user did not throw, or turns over and draws.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.use_card)
            return ContinueEffects;
        Room *room = target->getRoom();
        QStringList types{"BasicCard", "TrickCard", "EquipCard"};
        foreach (int id, ctx.use_card->getSubcards()) {
            switch (Sanguosha->getCard(id)->getTypeId()) {
            case Card::TypeBasic: types.removeAll("BasicCard"); break;
            case Card::TypeTrick: types.removeAll("TrickCard"); break;
            case Card::TypeEquip: types.removeAll("EquipCard"); break;
            default: break;
            }
        }
        if (target->canDiscard(target, "h") && !types.isEmpty()
            && room->askForCard(target, types.join(",") + "|.|.|hand", "@ikbingyan-discard", QVariant(), objectName()))
            return ContinueEffects;
        target->turnOver();
        target->drawCards(ctx.use_card->subcardsLength(), objectName());
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- bloom031

class IkFangsheng : public TriggerSkillV2
{
public:
    IkFangsheng() : TriggerSkillV2("ikfangsheng") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || !player->hasSkill(objectName())
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

    // Draw X, then hand out up to X cards.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        const int x = player->getLostHp();
        player->drawCards(x, objectName());
        int given = 0;
        while (given < x && player->isAlive() && !player->isKongcheng()) {
            QList<int> hand = player->handCards();
            const CardsMoveStruct move = room->askForYijiStruct(player, hand, objectName(), false, false, true, x - given);
            if (move.card_ids.isEmpty())
                break;
            given += move.card_ids.length();
        }
        return false;
    }
};

class IkJueli : public TriggerSkillV2
{
public:
    IkJueli() : TriggerSkillV2("ikjueli") { events << TargetConfirmed; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.to.contains(player) || !use.from
            || use.from == player || !use.card || !(use.card->isKindOf("Slash") || use.card->isNDTrick())
            || use.nullified_list.contains(player->objectName()) || use.nullified_list.contains("_ALL_TARGETS"))
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

    // A point of HP voids the card for the owner, who then strips one of the user's cards.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->loseHp(player, 1, true, player, objectName());
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains(player->objectName()))
            use.nullified_list << player->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        if (player->isAlive() && use.from && use.from->isAlive() && player->canDiscard(use.from, "he")) {
            const int id = room->askForCardChosen(player, use.from, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, use.from, player);
        }
        return false;
    }
};

// ---------------------------------------------------------------- bloom037

class IkMuhe : public TriggerSkillV2
{
public:
    IkMuhe() : TriggerSkillV2("ikmuhe") { events << CardUsed << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !use.card || !(use.card->isKindOf("Jink") || use.card->isKindOf("Peach")))
                return result;
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->hasSkill(objectName()) && (owner == player || isOwnTurn(owner)))
                    result[owner] << objectName();
        } else if (player->isAlive() && player->getPhase() == Player::Play) {
            foreach (ServerPlayer *owner, room->getOtherPlayers(player))
                if (owner->hasSkill(objectName()) && !owner->getPile("symbol").isEmpty())
                    result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), event == EventPhaseStart ? QVariant::fromValue(player) : QVariant()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Jinks and peaches stack "识"; spending one cuts another's 杀 allowance for the phase.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == CardUsed) {
            const QList<int> ids = room->getNCards(1, false);
            if (ids.isEmpty())
                return false;
            room->moveCardsAtomic(CardsMoveStruct(ids, owner, Player::PlaceTable,
                                                  CardMoveReason(CardMoveReason::S_REASON_TURNOVER, owner->objectName(),
                                                                 objectName(), QString())),
                                  true);
            owner->addToPile("symbol", ids);
            return false;
        }
        const QList<int> pile = owner->getPile("symbol");
        if (pile.isEmpty())
            return false;
        room->fillAG(pile, owner);
        int id = room->askForAG(owner, pile, false, objectName());
        room->clearAG(owner);
        if (!pile.contains(id))
            id = pile.first();
        room->throwCard(id, nullptr);
        room->addPlayerMark(player, "ikmuhe_less-PlayClear");
        return false;
    }
};

class IkMuheTargetMod : public TargetModSkillV2
{
public:
    IkMuheTargetMod() : TargetModSkillV2("#ikmuhe-target", "Slash")
    {
        frequency = Compulsory;
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.primary || ctx.primary->getMark("ikmuhe_less-PlayClear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(-ctx.primary->getMark("ikmuhe_less-PlayClear"));
    }
};

// ---------------------------------------------------------------- bloom038

class IkDingpin : public ViewAsSkillV2
{
public:
    IkDingpin() : ViewAsSkillV2("ikdingpin", 1) {}

    static bool available(const Player *p) { return p->isWounded() && p->getMark("ikdingpin_done-Clear") == 0; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || self->getPhase() != Player::Play
            || !self->canDiscard(self, "h"))
            return false;
        if (available(self))
            return true;
        foreach (const Player *p, self->getAliveSiblings())
            if (available(p))
                return true;
        return false;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && selectionValid(this, request);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *to) const override
    {
        return selected.isEmpty() && to && available(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkDingpinCard"; }

    // Black: the target draws its lost HP; red: the user turns over.
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->setPlayerMark(target, "ikdingpin_done-Clear", 1);
        JudgeStruct judge;
        judge.who = target;
        judge.good = true;
        judge.pattern = ".|black";
        judge.reason = objectName();
        room->judge(judge);
        if (judge.isGood()) {
            if (target->isAlive() && target->getLostHp() > 0)
                target->drawCards(target->getLostHp(), objectName());
        } else if (source->isAlive()) {
            source->turnOver();
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- bloom057

class IkLingxun : public TriggerSkillV2
{
public:
    IkLingxun() : TriggerSkillV2("iklingxun") { events << TargetSpecified << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish || player->getPile("&pokemon").isEmpty())
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || player->getPhase() != Player::Play || player->getMark("iklingxun-PlayClear") > 0
            || !use.card || use.card->getTypeId() == Card::TypeSkill || use.to.length() != 1 || use.to.first() == player
            || use.to.first()->isKongcheng())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart)
            return true;
        ServerPlayer *target = ctx.original_data->value<CardUseStruct>().to.first();
        if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(target)))
            return false;
        room->setPlayerMark(player, "iklingxun-PlayClear", 1);
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // A hand card of the target joins the owner's usable "精"; the end phase charges for any left.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            room->sendCompulsoryTriggerLog(player, objectName());
            player->clearOnePrivatePile("&pokemon");
            room->loseHp(player, 1, true, player, objectName());
            return false;
        }
        ServerPlayer *target = ctx.original_data->value<CardUseStruct>().to.first();
        if (!target || target->isKongcheng())
            return false;
        const int id = room->askForCardChosen(player, target, "h", objectName());
        if (id >= 0)
            player->addToPile("&pokemon", id);
        return false;
    }
};

// ---------------------------------------------------------------- snow016

class IkNilan : public TriggerSkillV2
{
public:
    IkNilan() : TriggerSkillV2("iknilan") { events << CardsMoveOneTime << EventPhaseEnd << EventPhaseChanging; }

    // Hand cards the owner discards in its discard phase.
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->hasSkill(objectName()))
            return false;
        if (event == EventPhaseChanging) {
            player->removeTag("IkNilanList");
            return false;
        }
        if (event != CardsMoveOneTime || player->getPhase() != Player::Discard)
            return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
            return false;
        QVariantList list = player->getTag("IkNilanList").toList();
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceHand && !list.contains(move.card_ids.at(i)))
                list << move.card_ids.at(i);
        player->setTag("IkNilanList", list);
        return false;
    }

    static bool sameColourPair(const Player *player)
    {
        const QVariantList list = player->getTag("IkNilanList").toList();
        if (list.length() < 2)
            return false;
        const Card::Color colour = Sanguosha->getCard(list.first().toInt())->getColor();
        foreach (const QVariant &v, list)
            if (Sanguosha->getCard(v.toInt())->getColor() != colour)
                return false;
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || !move.from_places.contains(Player::PlaceEquip))
                return TriggerList();
        } else if (event != EventPhaseEnd || player->getPhase() != Player::Discard || !sameColourPair(player)) {
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

    // A free thunder 杀, or a point of thunder damage to someone adjacent.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ThunderSlash probe(Card::NoSuit, 0);
        probe.setSkillName("_" + objectName());
        QList<ServerPlayer *> slashTargets, adjacent;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->canSlash(p, &probe, false))
                slashTargets << p;
            if (player->distanceTo(p) == 1)
                adjacent << p;
        }
        QStringList choices;
        if (!slashTargets.isEmpty() && !player->isCardLimited(&probe, Card::MethodUse))
            choices << "slash";
        if (!adjacent.isEmpty())
            choices << "damage";
        if (choices.isEmpty())
            return false;
        if (room->askForChoice(player, objectName(), choices.join("+")) == "slash") {
            ServerPlayer *target = room->askForPlayerChosen(player, slashTargets, "iknilan_slash", "@dummy-slash");
            if (!target)
                return false;
            auto *slash = new ThunderSlash(Card::NoSuit, 0);
            slash->setSkillName("_" + objectName());
            CardUseStruct use(slash, player, target);
            use.m_addHistory = false;
            use.setOwnedCard(slash);
            room->useCardFromSkillEffect(use, ctx);
        } else {
            ServerPlayer *target = room->askForPlayerChosen(player, adjacent, "iknilan_damage", "@iknilan-damage");
            if (target)
                room->damage(DamageStruct(objectName(), player, target, 1, DamageStruct::Thunder));
        }
        return false;
    }
};

// ---------------------------------------------------------------- snow018

class IkZhongqu : public TriggerSkillV2
{
public:
    IkZhongqu() : TriggerSkillV2("ikzhongqu") { events << Damage << CardOffset; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Damage) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.card
                || !damage.card->isKindOf("Slash") || !damage.to || !damage.to->isAlive())
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.from || !effect.from->isAlive() || !effect.from->hasSkill(objectName()) || !effect.card
            || !effect.card->isKindOf("Slash") || !effect.offset_card || !effect.offset_card->isKindOf("Jink"))
            return TriggerList();
        return TriggerList{{effect.from, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariant about = event == Damage ? QVariant::fromValue(ctx.original_data->value<DamageStruct>().to) : QVariant();
        if (!ctx.owner->askForSkillInvoke(objectName(), about))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // A hit turns the victim over and lets it draw its HP or lost HP; a dodge draws the owner one.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardOffset) {
            ctx.owner->drawCards(1, objectName());
            return false;
        }
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!target || !target->isAlive())
            return false;
        target->turnOver();
        if (!target->isAlive())
            return false;
        QStringList choices;
        if (target->getHp() > 0)
            choices << "hp";
        if (target->getLostHp() > 0)
            choices << "losehp";
        if (choices.isEmpty())
            return false;
        const QString choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), QVariant::fromValue(target));
        target->drawCards(choice == "losehp" ? target->getLostHp() : target->getHp(), objectName());
        return false;
    }
};

// ---------------------------------------------------------------- snow019

class IkXiaozui : public TriggerSkillV2
{
public:
    IkXiaozui() : TriggerSkillV2("ikxiaozui") { events << EventPhaseStart << AskForPeaches; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Discard || !player->getPile("crime").isEmpty())
                return TriggerList();
            foreach (const Card *card, player->getHandcards())
                if (card->isKindOf("Slash"))
                    return TriggerList{{player, {objectName()}}};
            return TriggerList();
        }
        const DyingStruct dying = data.value<DyingStruct>();
        if (!dying.who || !dying.who->isAlive() || dying.who->getHp() > 0 || player->getPile("crime").isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            int slashes = 0;
            foreach (const Card *card, player->getHandcards())
                if (card->isKindOf("Slash"))
                    ++slashes;
            const Card *chosen = room->askForExchange(player, objectName(), slashes, 1, false, "@ikxiaozui", true, "Slash");
            if (!chosen)
                return false;
            ctx.extra_data = ListI2V(chosen->getSubcards());
            delete chosen;
            room->broadcastSkillInvoke(objectName());
            return true;
        }
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        const QList<int> pile = player->getPile("crime");
        if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(who)))
            return false;
        int id = pile.first();
        if (pile.length() > 1) {
            room->fillAG(pile, player);
            id = room->askForAG(player, pile, false, objectName());
            room->clearAG(player);
            if (!pile.contains(id))
                id = pile.first();
        }
        ctx.extra_data = id;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Slashes become "罪"; one 罪 lets a dying character use a 桃.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            QList<int> ids;
            foreach (int id, ListV2I(ctx.extra_data.toList()))
                if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand)
                    ids << id;
            if (!ids.isEmpty())
                player->addToPile("crime", ids);
            return false;
        }
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        const int id = ctx.extra_data.toInt();
        if (!player->getPile("crime").contains(id))
            return false;
        room->throwCard(Sanguosha->getCard(id),
                        CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString()), nullptr);
        if (!who || !who->isAlive() || who->getHp() > 0)
            return false;
        auto *peach = new Peach(Card::NoSuit, 0);
        peach->setSkillName("_" + objectName());
        if (who->isProhibited(who, peach) || who->isCardLimited(peach, Card::MethodUse)) {
            delete peach;
            return false;
        }
        CardUseStruct use(peach, who, who);
        use.m_addHistory = false;
        use.setOwnedCard(peach);
        room->useCardFromSkillEffect(use, ctx);
        return false;
    }
};

// ---------------------------------------------------------------- snow021

class IkJieyou : public TriggerSkillV2
{
public:
    IkJieyou() : TriggerSkillV2("ikjieyou") { events << Dying << DamageCaused; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == DamageCaused) {
            const DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer *saved = room->findPlayerByObjectName(player->getTag("IkJieyouTarget").toString());
            if (damage.from != player || !player->hasFlag("ikjieyou_using") || !damage.card || !damage.card->isKindOf("Slash")
                || !saved || !saved->isAlive() || saved->getHp() > 0)
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        const DyingStruct dying = data.value<DyingStruct>();
        ServerPlayer *current = room->getCurrent();
        if (!dying.who || dying.who->isDead() || dying.who->getHp() > 0 || !current || !isOwnTurn(current)
            || current == player || !player->canSlash(current, false))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The 杀 is the cost; its damage (see the DamageCaused branch) feeds the dying character a 桃.
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == DamageCaused)
            return true;
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        player->setTag("IkJieyouTarget", who->objectName());
        room->setPlayerFlag(player, "ikjieyou_using");
        room->askForUseSlashTo(player, room->getCurrent(), "ikjieyou-slash:" + who->objectName(), false, true);
        room->setPlayerFlag(player, "-ikjieyou_using");
        player->removeTag("IkJieyouTarget");
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *saved = room->findPlayerByObjectName(player->getTag("IkJieyouTarget").toString());
        if (!saved || !saved->isAlive() || saved->getHp() > 0)
            return false;
        room->setPlayerFlag(player, "-ikjieyou_using");
        auto *peach = new Peach(Card::NoSuit, 0);
        peach->setSkillName("_" + objectName());
        if (player->isCardLimited(peach, Card::MethodUse) || player->isProhibited(saved, peach)) {
            delete peach;
            return false;
        }
        room->sendCompulsoryTriggerLog(player, objectName());
        CardUseStruct use(peach, player, saved);
        use.m_addHistory = false;
        use.setOwnedCard(peach);
        room->useCardFromSkillEffect(use, ctx);
        return false;
    }
};

// ---------------------------------------------------------------- snow023

class IkQianbian : public TriggerSkillV2
{
public:
    IkQianbian() : TriggerSkillV2("ikqianbian") { events << CardsMoveOneTime; }

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
        if (!player || move.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || !move.from_places.contains(Player::PlaceEquip) || victims(room, player).isEmpty())
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

    // Two discards, from one or two other characters.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        for (int i = 0; i < 2 && player->isAlive(); ++i) {
            const QList<ServerPlayer *> targets = victims(room, player);
            if (targets.isEmpty())
                break;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@ikqianbian", i > 0);
            if (!target)
                break;
            const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, target, player);
        }
        return false;
    }
};

// Up to three pieces of equipment change hands, each into an empty slot.
class IkHuanzhou : public ViewAsSkillV2
{
public:
    IkHuanzhou() : ViewAsSkillV2("ikhuanzhou")
    {
        frequency = Limited;
        limit_mark = "@huanzhou";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    static bool movable(const Card *card, const Player *to)
    {
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        return equip && to && !to->getEquip(equip->location()) && to->hasEquipArea(equip->location());
    }

    static QList<ServerPlayer *> sources(Room *room, const QList<int> &moved)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *from, room->getAlivePlayers())
            foreach (const Card *card, from->getEquips()) {
                if (moved.contains(card->getEffectiveId()))
                    continue;
                bool ok = false;
                foreach (ServerPlayer *to, room->getOtherPlayers(from))
                    if (movable(card, to)) {
                        ok = true;
                        break;
                    }
                if (ok) {
                    result << from;
                    break;
                }
            }
        return result;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkHuanzhouCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source)
            return FinishSkill;
        Room *room = source->getRoom();
        QList<int> moved;
        for (int i = 0; i < 3 && source->isAlive(); ++i) {
            const QList<ServerPlayer *> froms = sources(room, moved);
            if (froms.isEmpty())
                break;
            ServerPlayer *from = room->askForPlayerChosen(source, froms, objectName(), "@ikhuanzhou-from", i > 0);
            if (!from)
                break;
            QList<int> disabled;
            foreach (const Card *card, from->getEquips()) {
                bool ok = !moved.contains(card->getEffectiveId());
                if (ok) {
                    ok = false;
                    foreach (ServerPlayer *to, room->getOtherPlayers(from))
                        if (movable(card, to))
                            ok = true;
                }
                if (!ok)
                    disabled << card->getEffectiveId();
            }
            const int id = room->askForCardChosen(source, from, "e", objectName(), false, Card::MethodNone, disabled);
            if (id < 0 || disabled.contains(id))
                break;
            const Card *card = Sanguosha->getCard(id);
            QList<ServerPlayer *> tos;
            foreach (ServerPlayer *to, room->getOtherPlayers(from))
                if (movable(card, to))
                    tos << to;
            ServerPlayer *to = room->askForPlayerChosen(source, tos, objectName(), "@ikhuanzhou-to:::" + card->objectName());
            if (!to)
                break;
            room->moveCardTo(card, from, to, Player::PlaceEquip,
                             CardMoveReason(CardMoveReason::S_REASON_TRANSFER, source->objectName(), objectName(),
                                            QString()));
            moved << id;
        }
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- snow026

class IkDuoren : public TriggerSkillV2
{
public:
    IkDuoren() : TriggerSkillV2("ikduoren") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DamageStruct>().to != player || !player->isAlive() || !player->hasSkill(objectName())
            || !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->askForCard(player, "..", "@ikduoren-get", *ctx.original_data, objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Take the source's weapon, or draw one and knock it off.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        const bool hasWeapon = from && from->isAlive() && from->getWeapon();
        QStringList choices{"throw"};
        if (hasWeapon)
            choices << "obtain";
        if (room->askForChoice(player, objectName(), choices.join("+")) == "obtain") {
            room->obtainCard(player, from->getWeapon());
            return false;
        }
        player->drawCards(1, objectName());
        if (hasWeapon && from->getWeapon() && player->canDiscard(from, from->getWeapon()->getEffectiveId()))
            room->throwCard(from->getWeapon(), from, player);
        return false;
    }
};

class IkAnju : public TriggerSkillV2
{
public:
    IkAnju() : TriggerSkillV2("ikanju") { events << DamageCaused; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.to
            || damage.chain || damage.transfer || !damage.by_user || !damage.card
            || !(damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel")) || damage.to->inMyAttackRange(player))
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

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (room->askForChoice(player, objectName(), "draw+buff") != "buff") {
            player->drawCards(1, objectName());
            return false;
        }
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#IkAnjuBuff";
        log.from = damage.from;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(damage.damage + 1);
        room->sendLog(log);
        ++damage.damage;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

// ---------------------------------------------------------------- snow028

class IkYinzhai : public TriggerSkillV2
{
public:
    IkYinzhai() : TriggerSkillV2("ikyinzhai") { events << Damage; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DamageStruct>().from != player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->hasFlag("Global_Dying"))
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

    // Draw one, then every resolution stops and the current turn ends.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->drawCards(1, objectName());
        if (ServerPlayer *current = room->getCurrent()) {
            LogMessage log;
            log.type = "#SkipAllPhase";
            log.from = current;
            room->sendLog(log);
        }
        throw TurnBroken;
    }
};

// ---------------------------------------------------------------- snow037

class IkLinghuang : public TriggerSkillV2
{
public:
    IkLinghuang() : TriggerSkillV2("iklinghuang") { events << DamageInflicted; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.from
            || damage.from == player || !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!room->askForCard(player, "EquipCard", "@iklinghuang", *ctx.original_data, objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#IkLinghuangDecrease";
        log.from = player;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(damage.damage - 1);
        room->sendLog(log);
        if (--damage.damage < 1)
            return true;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

// ---------------------------------------------------------------- snow062

class IkChijian : public TriggerSkillV2
{
public:
    IkChijian() : TriggerSkillV2("ikchijian")
    {
        events << CardsMoveOneTime << EventPhaseEnd << EventPhaseChanging;
        frequency = Frequent;
    }

    // Cards the owner discards in its discard phase.
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->hasSkill(objectName()))
            return false;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::Discard)
                player->removeTag("IkChijian");
            return false;
        }
        if (event != CardsMoveOneTime || player->getPhase() != Player::Discard)
            return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
            return false;
        QVariantList list = player->getTag("IkChijian").toList();
        foreach (int id, move.card_ids)
            if (!list.contains(id))
                list << id;
        player->setTag("IkChijian", list);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Discard
            || !player->hasSkill(objectName()))
            return TriggerList();
        const QVariantList list = player->getTag("IkChijian").toList();
        if (list.length() != 2
            || Sanguosha->getCard(list.first().toInt())->sameColorWith(Sanguosha->getCard(list.last().toInt())))
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
        QStringList choices;
        if (player->isWounded())
            choices << "recover";
        choices << "draw";
        if (room->askForChoice(player, objectName(), choices.join("+")) == "recover")
            room->recover(player, RecoverStruct(objectName(), player));
        else
            player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- luna010

// 陷阵 with the loser keeping the winner's pindian card.
class IkLvdong : public ViewAsSkillV2
{
public:
    IkLvdong() : ViewAsSkillV2("iklvdong") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canPindian();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkLvdongCard"; }

    static QString reason(const Player *owner) { return "iklvdong_" + owner->objectName(); }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !source->canPindian(target))
            return ContinueEffects;
        Room *room = source->getRoom();
        PindianStruct *pindian = source->PinDian(target, objectName());
        if (!pindian)
            return ContinueEffects;
        if (pindian->success) {
            source->setTag("IkLvdongTarget", target->objectName());
            room->setPlayerEquipsNullified(target, "Armor|.|.|.|target:" + source->objectName(), reason(source), false);
            return ContinueEffects;
        }
        room->setPlayerCardLimitation(source, "use", "Slash", true, reason(source));
        const Card *theirs = pindian->to_card;
        if (theirs && source->isAlive() && room->getCardPlace(theirs->getEffectiveId()) == Player::DiscardPile)
            room->obtainCard(source, theirs);
        return ContinueEffects;
    }
};

class IkLvdongEffect : public TriggerSkillV2
{
public:
    IkLvdongEffect() : TriggerSkillV2("#iklvdong")
    {
        events << EventPhaseChanging << Death;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        foreach (ServerPlayer *owner, room->getAllPlayers(true)) {
            const QString name = owner->getTag("IkLvdongTarget").toString();
            if (name.isEmpty())
                continue;
            const bool turnEnd = event == EventPhaseChanging && owner == player
                && data.value<PhaseChangeStruct>().to == Player::NotActive;
            const bool death = event == Death
                && (data.value<DeathStruct>().who == owner || data.value<DeathStruct>().who->objectName() == name);
            if (!turnEnd && !death)
                continue;
            owner->removeTag("IkLvdongTarget");
            if (ServerPlayer *target = room->findPlayerByObjectName(name, true))
                room->removePlayerEquipsNullified(target, "Armor|.|.|.|target:" + owner->objectName(),
                                                  IkLvdong::reason(owner));
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class IkLvdongTargetMod : public TargetModSkillV2
{
public:
    IkLvdongTargetMod() : TargetModSkillV2("#iklvdong-target", "^SkillCard") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.secondary || !ctx.card
            || ctx.primary->getTag("IkLvdongTarget").toString() != ctx.secondary->objectName())
            return CorrectSkillResult::noEffect();
        if (ctx.modType == TargetModSkill::DistanceLimit)
            return CorrectSkillResult::useAmount(1000);
        if (ctx.modType == TargetModSkill::Residue && ctx.card->isKindOf("Slash"))
            return CorrectSkillResult::unlimitedResidue();
        return CorrectSkillResult::noEffect();
    }
};

// ---------------------------------------------------------------- luna016

class IkQilei : public TriggerSkillV2
{
public:
    IkQilei() : TriggerSkillV2("ikqilei") { events << MaxHpChanged << DamageComplete; }

    static QList<ServerPlayer *> inRange(Room *room, ServerPlayer *owner)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(owner))
            if (owner->inMyAttackRange(p))
                targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *owner = nullptr;
        QStringList times;
        if (event == MaxHpChanged) {
            const MaxHpStruct change = data.value<MaxHpStruct>();
            owner = player;
            for (int i = 0; i > change.change; --i)
                times << objectName();
        } else {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->isKindOf("ThunderSlash") && damage.nature == DamageStruct::Thunder
                && damage.to == player) {
                owner = damage.from;
                times << objectName();
            }
        }
        if (!owner || !owner->isAlive() || !owner->hasSkill(objectName()) || times.isEmpty()
            || inRange(room, owner).isEmpty())
            return TriggerList();
        return TriggerList{{owner, times}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, inRange(room, ctx.owner), objectName(), "@ikqilei", true,
                                                        true);
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
            room->damage(DamageStruct(objectName(), ctx.owner, target, 1, DamageStruct::Thunder));
        return false;
    }
};

// ---------------------------------------------------------------- luna027

class IkJuece : public TriggerSkillV2
{
public:
    IkJuece() : TriggerSkillV2("ikjuece") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !isOwnTurn(player) || !move.from
            || !move.from->isAlive() || !move.from_places.contains(Player::PlaceHand) || move.from->getHandcardNum() > 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *from = qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().from);
        if (!from || !player->askForSkillInvoke(objectName(), QVariant::fromValue(from)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *from = qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().from);
        if (from && from->isAlive())
            room->damage(DamageStruct(objectName(), player, from));
        return false;
    }
};

// A black single-target ordinary trick may take one more target.
class IkShangye : public TargetModSkillV2
{
public:
    IkShangye() : TargetModSkillV2("ikshangye", "TrickCard|black") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::ExtraTarget || !ctx.primary || !ctx.card || !ctx.card->isBlack()
            || !ctx.card->isNDTrick() || ctx.card->isKindOf("Collateral") || ctx.card->isKindOf("ExNihilo")
            || ctx.card->isKindOf("AOE") || ctx.card->isKindOf("GlobalEffect") || ctx.card->isKindOf("Nullification")
            || ctx.card->isKindOf("IronChain") || !ctx.primary->hasSkill(objectName()))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1);
    }
};

// ---------------------------------------------------------------- luna028

class IkRongxin : public TriggerSkillV2
{
public:
    IkRongxin() : TriggerSkillV2("ikrongxin") { events << EventPhaseStart << EventPhaseChanging; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        if (player->getMark("ikrongxin_prohibit") > 0)
            room->setPlayerMark(player, "ikrongxin_prohibit", 0);
        const QStringList owners = player->getTag("IkRongxinNear").toStringList();
        player->removeTag("IkRongxinNear");
        foreach (const QString &name, owners)
            if (ServerPlayer *owner = room->findPlayerByObjectName(name, true))
                room->removeFixedDistance(player, owner, 1);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::RoundStart
            || player->isKongcheng())
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->canPindian(player))
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

    // Win: the turn player can aim only at itself; lose: it reaches the owner at distance 1.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->canPindian(player))
            return false;
        if (ctx.owner->pindian(player, objectName())) {
            room->setPlayerMark(player, "ikrongxin_prohibit", 1);
        } else if (player->isAlive() && ctx.owner->isAlive()) {
            room->setFixedDistance(player, ctx.owner, 1);
            QStringList owners = player->getTag("IkRongxinNear").toStringList();
            owners << ctx.owner->objectName();
            player->setTag("IkRongxinNear", owners);
        }
        return false;
    }
};

class IkRongxinProhibit : public ProhibitSkill
{
public:
    IkRongxinProhibit() : ProhibitSkill("#ikrongxin") {}

    bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const override
    {
        return from && to && from != to && from->getMark("ikrongxin_prohibit") > 0;
    }
};

// ---------------------------------------------------------------- luna038

class IkGuijing : public TriggerSkillV2
{
public:
    IkGuijing() : TriggerSkillV2("ikguijing")
    {
        events << PreDamageDone << Damaged;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == PreDamageDone && player && data.value<DamageStruct>().to == player && player->hasSkill(objectName())
            && room->getCurrent())
            room->addPlayerMark(player, "ikguijing_count-Clear");
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (event != Damaged || !player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName())
            || damage.nature != DamageStruct::Normal || player->getMark("ikguijing_count-Clear") == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The turn's first plain damage heals; later ones cost a point of HP.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        if (player->getMark("ikguijing_count-Clear") == 1) {
            if (player->isWounded())
                room->recover(player, RecoverStruct(objectName(), player));
        } else {
            room->loseHp(player, 1, true, player, objectName());
        }
        return false;
    }
};

// ---------------------------------------------------------------- luna041

class IkHuowu : public TriggerSkillV2
{
public:
    IkHuowu() : TriggerSkillV2("ikhuowu") { events << EventPhaseStart; }

    static QList<ServerPlayer *> emptyHanded(Room *room)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->isKongcheng())
                targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || !player->hasSkill(objectName())
            || emptyHanded(room).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, emptyHanded(room), objectName(), "@ikhuowu", true, true);
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
            room->damage(DamageStruct(objectName(), player, target, 1, DamageStruct::Fire));
        return false;
    }
};

// ---------------------------------------------------------------- luna062

class IkSizhuo : public ViewAsSkillV2
{
public:
    IkSizhuo() : ViewAsSkillV2("iksizhuo", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isBlack();
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
        Collateral *card = new Collateral(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkSizhuoCard"; }
};

class IkJunan : public ViewAsSkillV2
{
public:
    IkJunan() : ViewAsSkillV2("ikjunan", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isNude();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card) && card->isKindOf("Weapon");
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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkJunanCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
            return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        if (source->getRoom()->getCardOwner(id) == source)
            source->getRoom()->giveCard(source, target, QList<int>() << id, objectName(), true);
        return ContinueEffects;
    }
};

}

IkXinchaoCard::IkXinchaoCard() { setSkillName("ikxinchao"); mute = true; }
IkSishiCard::IkSishiCard() { setSkillName("iksishi"); mute = true; }
IkZangyuCard::IkZangyuCard() { setSkillName("ikzangyu"); mute = true; }
IkShitieCard::IkShitieCard() { setSkillName("ikshitie"); mute = true; }
IkBingyanCard::IkBingyanCard() { setSkillName("ikbingyan"); mute = true; }
IkDingpinCard::IkDingpinCard() { setSkillName("ikdingpin"); mute = true; }
IkHuanzhouCard::IkHuanzhouCard() { setSkillName("ikhuanzhou"); mute = true; }
IkLvdongCard::IkLvdongCard() { setSkillName("iklvdong"); mute = true; }
IkSizhuoCard::IkSizhuoCard() { setSkillName("iksizhuo"); mute = true; }
IkJunanCard::IkJunanCard() { setSkillName("ikjunan"); mute = true; }

IkaiKinPackage::IkaiKinPackage()
    : Package("ikai-kin")
{

    General *wind017 = new General(this, "wind017", "kaze", 3);
    wind017->addSkill(new IkXinchao);
    wind017->addSkill(new IkShangshi);

    // 迷途 is 无言.
    General *wind018 = new General(this, "wind018", "kaze", 3);
    wind018->addSkill("wuyan");
    wind018->addSkill(new IkSishi);

    General *wind019 = new General(this, "wind019", "kaze");
    wind019->addSkill("thxiagong");
    wind019->addSkill(new IkWanhun);

    General *wind020 = new General(this, "wind020", "kaze");
    wind020->addSkill("ikmeiying");
    wind020->addSkill(new IkFansheng);
    wind020->addSkill(new IkMohun);
    wind020->addRelateSkill("thheiguan");

    General *wind021 = new General(this, "wind021", "kaze");
    wind021->addSkill("thxiagong");
    wind021->addSkill(new IkZuyao);
    wind021->addSkill(new IkZuyaoRecord);
    wind021->addSkill(new IkZuyaoTargetMod);
    related_skills.insert("ikzuyao", "#ikzuyao-record");
    related_skills.insert("ikzuyao", "#ikzuyao-target");

    // 疾步 is 马术.
    General *wind031 = new General(this, "wind031", "kaze");
    wind031->addSkill("mashu");
    wind031->addSkill(new IkMoguang);

    General *wind032 = new General(this, "wind032", "kaze");
    wind032->addSkill(new IkLichi);
    wind032->addRelateSkill("ikchilian");
    wind032->addRelateSkill("paoxiao");

    General *wind037 = new General(this, "wind037", "kaze", 4, true, true);
    wind037->addSkill(new IkXuanren);
    wind037->addSkill(new IkXuanrenTargetMod);
    related_skills.insert("ikxuanren", "#ikxuanren-target");
    wind037->addSkill(new IkLanjian);

    // 强识 is 强识.
    General *wind038 = new General(this, "wind038", "kaze", 3);
    wind038->addSkill("qiangzhi");
    wind038->addSkill(new IkFengxin);

    General *wind039 = new General(this, "wind039", "kaze");
    wind039->addSkill(new IkShensha);
    wind039->addSkill(new IkShenshaDistance);
    wind039->addSkill(new IkShenshaTargetMod);
    related_skills.insert("ikshensha", "#ikshensha-dist");
    related_skills.insert("ikshensha", "#ikshensha-target");

    // 歃狂 is 失智.
    General *wind055 = new General(this, "wind055", "kaze");
    wind055->addSkill(new IkZangyu);
    wind055->addSkill("shizhi");

    General *wind062 = new General(this, "wind062", "kaze", 3, false);
    wind062->addSkill(new IkShitie);
    wind062->addSkill(new IkShitieDraw);
    related_skills.insert("ikshitie", "#ikshitie");
    wind062->addSkill(new IkLinbei);

    // 酒诗 is 酒诗.
    General *bloom016 = new General(this, "bloom016", "hana", 3);
    bloom016->addSkill(new IkShihua);
    bloom016->addSkill("jiushi");

    General *bloom017 = new General(this, "bloom017", "hana");
    bloom017->addSkill(new IkPiaohu);

    General *bloom022 = new General(this, "bloom022", "hana", 3, false);
    bloom022->addSkill(new IkLundao);
    bloom022->addSkill(new IkXuanwu);

    // 雪涟 is 御策.
    General *bloom027 = new General(this, "bloom027", "hana", 3);
    bloom027->addSkill(new IkBingyan);
    bloom027->addSkill("yuce");

    General *bloom031 = new General(this, "bloom031", "hana", 3, false);
    bloom031->addSkill(new IkFangsheng);
    bloom031->addSkill(new IkJueli);

    General *bloom037 = new General(this, "bloom037", "hana");
    bloom037->addSkill(new IkMuhe);
    bloom037->addSkill(new IkMuheTargetMod);
    related_skills.insert("ikmuhe", "#ikmuhe-target");

    // 魔弈 is 法恩.
    General *bloom038 = new General(this, "bloom038", "hana", 3);
    bloom038->addSkill(new IkDingpin);
    bloom038->addSkill("faen");

    General *bloom057 = new General(this, "bloom057", "hana");
    bloom057->addSkill(new IkLingxun);

    General *snow016 = new General(this, "snow016", "yuki");
    snow016->addSkill(new IkNilan);

    General *snow018 = new General(this, "snow018", "yuki");
    snow018->addSkill(new IkZhongqu);

    // 灵炮 is 疠火.
    General *snow019 = new General(this, "snow019", "yuki");
    snow019->addSkill("lihuo");
    snow019->addSkill(new IkXiaozui);

    General *snow021 = new General(this, "snow021", "yuki");
    snow021->addSkill("ikxuanren");
    snow021->addSkill("#ikxuanren-target");
    snow021->addSkill(new IkJieyou);

    General *snow023 = new General(this, "snow023", "yuki");
    snow023->addSkill(new IkQianbian);
    snow023->addSkill(new IkHuanzhou);

    General *snow026 = new General(this, "snow026", "yuki");
    snow026->addSkill(new IkDuoren);
    snow026->addSkill(new IkAnju);

    General *snow028 = new General(this, "snow028", "yuki");
    snow028->addSkill(new IkYinzhai);

    // 矫誓 is 谮毁.
    General *snow037 = new General(this, "snow037", "yuki", 4, false);
    snow037->addSkill("zenhui");
    snow037->addSkill(new IkLinghuang);

    // 憎鬼 is 急寓 (the upstream show of the usable card is dropped).
    General *snow062 = new General(this, "snow062", "yuki", 3, false);
    snow062->addSkill(new IkChijian);
    snow062->addSkill("jiyu");

    // 过载 is 禁酒.
    General *luna010 = new General(this, "luna010", "tsuki");
    luna010->addSkill(new IkLvdong);
    luna010->addSkill(new IkLvdongEffect);
    luna010->addSkill(new IkLvdongTargetMod);
    related_skills.insert("iklvdong", "#iklvdong");
    related_skills.insert("iklvdong", "#iklvdong-target");
    luna010->addSkill("jinjiu");

    General *luna016 = new General(this, "luna016", "tsuki", 6);
    luna016->addSkill("ikxinshang");
    luna016->addSkill(new IkQilei);

    // 焚世 is the old 焚城.
    General *luna027 = new General(this, "luna027", "tsuki", 3);
    luna027->addSkill(new IkJuece);
    luna027->addSkill(new IkShangye);
    luna027->addSkill("nosfencheng");

    // 剋魂 is 求援.
    General *luna028 = new General(this, "luna028", "tsuki", 3, false);
    luna028->addSkill(new IkRongxin);
    luna028->addSkill(new IkRongxinProhibit);
    related_skills.insert("ikrongxin", "#ikrongxin");
    luna028->addSkill("qiuyuan");

    // 连庄 is 渐营.
    General *luna038 = new General(this, "luna038", "tsuki", 3);
    luna038->addSkill("jianying");
    luna038->addSkill(new IkGuijing);

    // 狱锁 is 灭计; 崩焰 is 焚城.
    General *luna041 = new General(this, "luna041", "tsuki", 3);
    luna041->addSkill(new IkHuowu);
    luna041->addSkill("mieji");
    luna041->addSkill("fencheng");

    General *luna062 = new General(this, "luna062", "tsuki");
    luna062->addSkill(new IkSizhuo);
    luna062->addSkill(new IkJunan);

    addMetaObject<IkXinchaoCard>();
    addMetaObject<IkSishiCard>();
    addMetaObject<IkZangyuCard>();
    addMetaObject<IkShitieCard>();
    addMetaObject<IkBingyanCard>();
    addMetaObject<IkDingpinCard>();
    addMetaObject<IkHuanzhouCard>();
    addMetaObject<IkLvdongCard>();
    addMetaObject<IkSizhuoCard>();
    addMetaObject<IkJunanCard>();
}

ADD_PACKAGE(IkaiKin)
