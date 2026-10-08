#include "touhou-hana.h"
#include "touhou-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"

#include <QScopedPointer>

using namespace TouhouUtils;

namespace {

// Zero-card, one-target proxy activation used once in the play phase.
class PlayOncePerPhaseSkill : public ViewAsSkillV2
{
public:
    explicit PlayOncePerPhaseSkill(const QString &name, int n = 0) : ViewAsSkillV2(name, n) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }
};

// ---------------------------------------------------------------- hana001

class ThHuaji : public TriggerSkillV2
{
public:
    ThHuaji() : TriggerSkillV2("thhuaji") { events << CardUsed; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || !(use.card->isKindOf("BasicCard") || use.card->isNDTrick())
            || use.nullified_list.contains("_ALL_TARGETS"))
            return TriggerList();
        ServerPlayer *current = room->getCurrent();
        if (!current || current == player || !current->isAlive() || !isOwnTurn(current)
            || !current->hasSkill(objectName()) || !current->canDiscard(current, "he"))
            return TriggerList();
        return TriggerList{{current, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, ".|black", "@thhuajiuse:" + ctx.invoker->objectName(), *ctx.original_data,
                              objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {ctx.invoker};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card)
            return false;
        QString className = use.card->getClassName();
        if (className.endsWith("Slash"))
            className = "Slash";
        QString name = use.card->objectName();
        if (name.endsWith("_slash"))
            name = "slash";
        if (room->askForCard(target, className, "@thhuaji:::" + name, *ctx.original_data))
            return false;
        use.nullified_list << "_ALL_TARGETS";
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

static int otherHanaCount(const Player *lord)
{
    int n = 0;
    foreach (const Player *p, lord->getAliveSiblings())
        if (p->getKingdom() == "hana")
            ++n;
    return n;
}

class ThFeizhan : public MaxCardsSkillV2
{
public:
    ThFeizhan() : MaxCardsSkillV2("thfeizhan$") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasLordSkill(objectName()))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(otherHanaCount(ctx.primary));
    }
};

class ThFeizhanRange : public AttackRangeSkillV2
{
public:
    ThFeizhanRange() : AttackRangeSkillV2("#thfeizhan") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasLordSkill("thfeizhan"))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(otherHanaCount(ctx.primary));
    }
};

// ---------------------------------------------------------------- hana002

class ThJiewu : public PlayOncePerPhaseSkill
{
public:
    ThJiewu() : PlayOncePerPhaseSkill("thjiewu") {}

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isNude()
            && request.initiator->inMyAttackRange(to);
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThJiewuCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || target->isNude())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = room->askForCardChosen(source, target, "he", objectName());
        if (id >= 0)
            room->obtainCard(source, id, false);
        if (!source->isAlive() || !target->isAlive())
            return ContinueEffects;
        auto *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_thjiewu");
        CardUseStruct use(slash, target, source);
        use.setOwnedCard(slash);
        if (target->canSlash(source, slash, false))
            room->useCardFromSkillEffect(use, ctx);
        return ContinueEffects;
    }
};

// The slash 借物 makes the target use ignores the source's armor.
class ThJiewuArmor : public TriggerSkillV2
{
public:
    ThJiewuArmor() : TriggerSkillV2("#thjiewu")
    {
        events << TargetSpecified;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash") || use.card->getSkillName() != "thjiewu")
            return false;
        room->sendCompulsoryTriggerLog(use.to.isEmpty() ? use.from : use.to.first(), "thjiewu", false);
        foreach (ServerPlayer *p, use.to)
            p->addQinggangTag(use.card);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThGenxing : public WakeSkill
{
public:
    ThGenxing() : WakeSkill("thgenxing")
    {
        events << EventPhaseStart;
        waked_skills = "thmopao";
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->getHp() == 1; }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->isWounded() && room->askForChoice(player, objectName(), "recover+draw") == "recover")
            room->recover(player, RecoverStruct(objectName(), player));
        else
            player->drawCards(2, objectName());
        if (room->changeMaxHpForAwakenSkill(player, -1, objectName()))
            room->acquireSkillFromEffect(player, "thmopao", ctx);
    }
};

class ThMopao : public TriggerSkillV2
{
public:
    ThMopao() : TriggerSkillV2("thmopao") { events << CardUsed << CardResponded; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || room->getOtherPlayers(player).isEmpty())
            return TriggerList();
        const Card *card = nullptr;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player)
                card = use.card;
        } else {
            card = data.value<CardResponseStruct>().m_card;
        }
        if (!card || !card->isKindOf("Jink"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@thmopao", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(1, objectName());
        if (target->isAlive())
            room->damage(DamageStruct(objectName(), ctx.owner->isAlive() ? ctx.owner : nullptr, target, 1,
                                      DamageStruct::Fire));
        return false;
    }
};

// ---------------------------------------------------------------- hana003

class ThYuwu : public TriggerSkillV2
{
public:
    ThYuwu() : TriggerSkillV2("thyuwu")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || damage.from != player || !damage.card
            || !damage.card->isKindOf("Slash") || !damage.to || !damage.to->isAlive() || damage.to->getHp() > 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The victim dies outright, so the damage itself is not dealt.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to || !damage.to->isAlive())
            return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        room->killPlayer(damage.to, &damage);
        return true;
    }
};

class ThGuihang : public TriggerSkillV2
{
public:
    ThGuihang() : TriggerSkillV2("thguihang") { events << Dying; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DyingStruct dying = data.value<DyingStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !dying.who || dying.who->isDead()
            || dying.who->getHp() > 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(who)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->loseMaxHp(ctx.owner, 1, objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        if (who && who->isAlive())
            room->recover(who, RecoverStruct(objectName(), ctx.owner->isAlive() ? ctx.owner : nullptr));
        return false;
    }
};

class ThBianViewAs : public ViewAsSkillV2
{
public:
    ThBianViewAs() : ViewAsSkillV2("thbian") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thbian"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &) const override
    {
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        return slash;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};

class ThBian : public TriggerSkillV2
{
public:
    ThBian() : TriggerSkillV2("thbian")
    {
        events << HpRecover;
        view_as_skill = new ThBianViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->hasFlag("Global_Dying")
            || player->getHp() < 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The nested @@ response is the whole effect.
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thbian", "@thbian");
        return false;
    }
};

class ThBianTargetMod : public TargetModSkillV2
{
public:
    ThBianTargetMod() : TargetModSkillV2("#thbian") {}

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.card || ctx.card->getSkillName() != "thbian")
            return CorrectSkillResult::noEffect();
        if (ctx.modType == TargetModSkill::ExtraTarget)
            return CorrectSkillResult::useAmount(qMax(0, ctx.primary->getMaxHp() - 1));
        if (ctx.modType == TargetModSkill::DistanceLimit)
            return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

// ---------------------------------------------------------------- hana004

class ThXuelan : public TriggerSkillV2
{
public:
    ThXuelan() : TriggerSkillV2("thxuelan") { events << CardEffected; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!player || effect.to != player || !effect.card || !effect.card->isKindOf("Peach") || effect.nullified)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->canDiscard(owner, "he"))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, ".|red", "@thxuelan:" + ctx.invoker->objectName(), *ctx.original_data,
                              objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        ServerPlayer *target = effect.to;
        effect.nullified = true;
        *ctx.original_data = QVariant::fromValue(effect);
        if (target && target->isAlive() && target->getMaxHp() <= target->getGeneralMaxHp())
            room->gainMaxHp(target, 1, objectName());
        return false;
    }
};

class ThXinwang : public TriggerSkillV2
{
public:
    ThXinwang() : TriggerSkillV2("thxinwang")
    {
        events << CardUsed << CardResponded << CardsMoveOneTime;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || isOwnTurn(player)
            || times(event, player, data) <= 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = times(event, ctx.owner, *ctx.original_data);
        return true;
    }

    // Each lost heart card counts; a refusal ends the rest.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int n = qMax(1, ctx.extra_data.toInt());
        for (int i = 0; i < n && player->isAlive(); ++i) {
            if (i > 0 && !player->askForSkillInvoke(objectName(), *ctx.original_data))
                break;
            QList<ServerPlayer *> wounded;
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (p->isWounded())
                    wounded << p;
            ServerPlayer *target = wounded.isEmpty()
                ? nullptr : room->askForPlayerChosen(player, wounded, objectName(), "@thxinwang", true);
            if (target)
                room->recover(target, RecoverStruct(objectName(), player));
            else
                player->drawCards(1, objectName());
        }
        return false;
    }

private:
    static int times(TriggerEvent event, ServerPlayer *player, const QVariant &data)
    {
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            return use.from == player && use.card && use.card->getSuit() == Card::Heart ? 1 : 0;
        }
        if (event == CardResponded) {
            const Card *card = data.value<CardResponseStruct>().m_card;
            return card && card->getSuit() == Card::Heart ? 1 : 0;
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
            return 0;
        int n = 0;
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const Player::Place place = move.from_places.value(i);
            if ((place == Player::PlaceHand || place == Player::PlaceEquip)
                && Sanguosha->getCard(move.card_ids.at(i))->getSuit() == Card::Heart)
                ++n;
        }
        return n;
    }
};

class ThJuedu : public TriggerSkillV2
{
public:
    ThJuedu() : TriggerSkillV2("thjuedu")
    {
        events << Death;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        if (!player || death.who != player || !player->hasSkill(objectName()) || !death.damage || !death.damage->from
            || death.damage->from == player || death.damage->from->isDead())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *killer = ctx.original_data->value<DeathStruct>().damage->from;
        if (!killer || killer->isDead())
            return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        // Upstream gave its own 崩坏; the local one is the same rule.
        room->acquireSkillFromEffect(killer, "benghuai", ctx);
        return false;
    }
};

// ---------------------------------------------------------------- hana005

class ThTingwu : public TriggerSkillV2
{
public:
    ThTingwu() : TriggerSkillV2("thtingwu")
    {
        events << PreDamageDone << DamageComplete;
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }

    // Whether the victim stood upright is decided before the damage resolves chains.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != PreDamageDone || !player)
            return false;
        // Every damage refreshes the flag, so a declined trigger never leaks into the next one.
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from)
            return false;
        const bool hit = damage.nature == DamageStruct::Thunder && damage.to == player && damage.from != player
            && !player->isChained() && damage.from->getPhase() == Player::Play && damage.from->hasSkill(objectName());
        if (hit)
            room->setPlayerFlag(player, flagFor(damage.from));
        else if (player->hasFlag(flagFor(damage.from)))
            room->setPlayerFlag(player, "-" + flagFor(damage.from));
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (event != DamageComplete || !player || !damage.from || !player->hasFlag(flagFor(damage.from)))
            return TriggerList();
        if (!player->isAlive() || !damage.from->isAlive() || !damage.from->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{damage.from, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player && player->hasFlag(flagFor(ctx.owner)))
            room->setPlayerFlag(player, "-" + flagFor(ctx.owner));
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
        ServerPlayer *owner = ctx.owner;
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
        JudgeStruct judge;
        judge.pattern = ".|heart";
        judge.good = false;
        judge.reason = objectName();
        judge.who = owner;
        room->judge(judge);
        if (!judge.isGood() || !owner->isAlive() || !victim || !victim->isAlive())
            return false;
        // The victim's upper and lower neighbours.
        QList<ServerPlayer *> targets;
        foreach (const Player *p, QList<const Player *>() << victim->getLastAlive() << victim->getNextAlive()) {
            ServerPlayer *neighbour = p ? room->findPlayerByObjectName(p->objectName()) : nullptr;
            if (neighbour && neighbour != victim && neighbour->isAlive() && !targets.contains(neighbour))
                targets << neighbour;
        }
        if (targets.isEmpty())
            return false;
        ServerPlayer *target = room->askForPlayerChosen(owner, targets, objectName(), "@thtingwu");
        if (target)
            room->damage(DamageStruct(objectName(), owner, target, 1, DamageStruct::Thunder));
        return false;
    }

private:
    static QString flagFor(const ServerPlayer *from) { return "thtingwu_" + from->objectName(); }
};

class ThYuchang : public FilterSkill
{
public:
    ThYuchang() : FilterSkill("thyuchang") {}

    bool viewFilter(const Card *to_select) const override
    {
        return to_select->objectName() == "slash" && to_select->getSuit() == Card::Club
            && Sanguosha->getCardPlace(to_select->getEffectiveId()) == Player::PlaceHand;
    }

    const Card *viewAs(const Card *originalCard) const override
    {
        ThunderSlash *slash = new ThunderSlash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());
        return slash;
    }
};

class ThYuchangTargetMod : public TargetModSkillV2
{
public:
    ThYuchangTargetMod() : TargetModSkillV2("#thyuchang", "ThunderSlash") {}

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || !ctx.card
            || !ctx.card->isKindOf("ThunderSlash") || !ctx.primary->hasSkill("thyuchang"))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1000);
    }
};

// ---------------------------------------------------------------- hana006

class ThXihua : public PlayOncePerPhaseSkill
{
public:
    ThXihua() : PlayOncePerPhaseSkill("thxihua", 1) {}

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card);
    }

    bool willThrowSelectedCards() const override { return false; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        if (!request.initiator || !selected.isEmpty() || !to || to == request.initiator)
            return false;
        return !choices(request.initiator, to).isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThXihuaCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        ServerPlayer *payer = ctx.initiator;
        if (!source || !payer || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->subcardsLength() != 1)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (!payer->handCards().contains(id))
            return ContinueEffects;
        payer->addToPile("joke", id, false);
        const QStringList options = choices(source, target);
        if (!options.isEmpty() && source->isAlive()) {
            const QString choice = room->askForChoice(source, objectName(), options.join("+"), QVariant::fromValue(target));
            Card *card = choice == "duel" ? static_cast<Card *>(new Duel(Card::NoSuit, 0))
                                          : static_cast<Card *>(new Slash(Card::NoSuit, 0));
            card->setSkillName("_thxihua");
            CardUseStruct use(card, source, target);
            use.setOwnedCard(card);
            room->useCardFromSkillEffect(use, ctx);
        }
        // The hidden card goes to the discard pile once everything is resolved.
        if (payer->getPile("joke").contains(id) || room->getCardPlace(id) == Player::PlaceTable) {
            CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString());
            room->throwCard(Sanguosha->getCard(id), reason, nullptr);
        }
        return ContinueEffects;
    }

private:
    static QStringList choices(const Player *source, const Player *target)
    {
        QStringList result;
        Slash slash(Card::NoSuit, 0);
        slash.setSkillName("_thxihua");
        if (!source->isCardLimited(&slash, Card::MethodUse) && source->canSlash(target, &slash, false))
            result << "slash";
        Duel duel(Card::NoSuit, 0);
        duel.setSkillName("_thxihua");
        if (!source->isCardLimited(&duel, Card::MethodUse) && !source->isProhibited(target, &duel))
            result << "duel";
        return result;
    }
};

// When the 戏画 card would deal damage, the hidden card decides: a Slash lets the
// owner discard a hand card of the other side, anything else prevents the damage.
class ThXihuaReveal : public TriggerSkillV2
{
public:
    ThXihuaReveal() : TriggerSkillV2("#thxihua")
    {
        events << Predamage;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !damage.card || damage.card->getSkillName() != "thxihua")
            return TriggerList();
        ServerPlayer *owner = jokeOwner(damage);
        if (!owner || !owner->hasSkill("thxihua"))
            return TriggerList();
        return TriggerList{{owner, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *owner = ctx.owner;
        if (owner->getPile("joke").isEmpty())
            return false;
        ServerPlayer *victim = damage.from == owner ? damage.to : damage.from;
        const int id = owner->getPile("joke").first();
        room->sendCompulsoryTriggerLog(owner, "thxihua");
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, owner->objectName(), "thxihua", QString());
        room->moveCardTo(Sanguosha->getCard(id), owner, Player::PlaceTable, reason, true);
        if (!Sanguosha->getCard(id)->isKindOf("Slash"))
            return true;
        if (victim && victim->isAlive() && owner->isAlive() && owner->canDiscard(victim, "h")) {
            const int card = room->askForCardChosen(owner, victim, "h", "thxihua", false, Card::MethodDiscard);
            if (card >= 0)
                room->throwCard(card, victim, owner);
        }
        return false;
    }

private:
    static ServerPlayer *jokeOwner(const DamageStruct &damage)
    {
        if (damage.from && !damage.from->getPile("joke").isEmpty())
            return damage.from;
        if (damage.to && !damage.to->getPile("joke").isEmpty())
            return damage.to;
        return nullptr;
    }
};

// ---------------------------------------------------------------- hana007

class ThMimeng : public ViewAsSkillV2
{
public:
    ThMimeng() : ViewAsSkillV2("thmimeng", 1)
    {
        response_or_use = true;
        // ServerPlayer::hasNullification still asks the legacy response probe.
        response_pattern = "nullification";
    }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::guhuo(objectName(), true, true, false, false, false);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || player->getHandcardNum() != 1)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return true;
        if (request.pattern.startsWith("@") || request.pattern.startsWith("."))
            return false;
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && !usableNames(request).isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card);
    }

protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        static const QStringList tricks = {"duel", "fire_attack", "savage_assault", "collateral", "iron_chain",
                                           "dismantlement", "nullification"};
        if (tricks.contains(name))
            return true;
        QScopedPointer<Card> card(Sanguosha->cloneCard(name));
        return card && card->isKindOf("BasicCard");
    }
};

class ThAnyun : public TriggerSkillV2
{
public:
    ThAnyun() : TriggerSkillV2("thanyun") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (player->getPhase() == Player::Draw && player->hasSkill(objectName()))
            return TriggerList{{player, {objectName()}}};
        if (player->getPhase() == Player::Finish && player->hasFlag(flagName()))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getPhase() == Player::Finish) {
            room->broadcastSkillInvoke(objectName(), 3);
            return true;
        }
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 1);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->getPhase() == Player::Draw) {
            room->setPlayerFlag(player, flagName());
            return true;
        }
        room->setPlayerFlag(player, "-" + flagName());
        player->drawCards(3, objectName());
        if (player->isAlive() && player->canDiscard(player, "he"))
            room->askForDiscard(player, objectName(), 1, 1, false, true);
        return false;
    }

private:
    static QString flagName() { return QStringLiteral("thanyun"); }
};

// ---------------------------------------------------------------- hana008

// 劝善: the target hands at least one hand card to a third character; one type means a draw.
static void quanshan(Room *room, ServerPlayer *source, ServerPlayer *target)
{
    if (!source || !target || !target->isAlive() || target->isKongcheng())
        return;
    QList<ServerPlayer *> receivers = room->getOtherPlayers(target);
    receivers.removeOne(source);
    if (receivers.isEmpty())
        return;
    const Card *cards = room->askForExchange(target, "thquanshan", target->getHandcardNum(), 1, false,
                                             "@thquanshan:" + source->objectName(), false);
    if (!cards || cards->getSubcards().isEmpty())
        return;
    const QList<int> ids = cards->getSubcards();
    ServerPlayer *receiver = room->askForPlayerChosen(target, receivers, "thquanshan",
                                                      "@thquanshan-to:" + source->objectName());
    if (!receiver)
        receiver = receivers.first();
    bool oneType = true;
    const Card::CardType type = Sanguosha->getCard(ids.first())->getTypeId();
    foreach (int id, ids)
        if (Sanguosha->getCard(id)->getTypeId() != type)
            oneType = false;
    room->giveCard(target, receiver, ids, "thquanshan");
    if (oneType && source->isAlive())
        source->drawCards(1, "thquanshan");
}

class ThQuanshan : public PlayOncePerPhaseSkill
{
public:
    ThQuanshan() : PlayOncePerPhaseSkill("thquanshan") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return PlayOncePerPhaseSkill::canActivate(request) && request.initiator->aliveCount() > 2;
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isKongcheng();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThQuanshanCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.invoker)
            quanshan(ctx.invoker->getRoom(), ctx.invoker, target);
        return ContinueEffects;
    }
};

class ThXiangang : public TriggerSkillV2
{
public:
    ThXiangang() : TriggerSkillV2("thxiangang")
    {
        events << DamageInflicted;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
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
        JudgeStruct judge;
        judge.pattern = ".|club";
        judge.good = true;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        if (judge.isGood()) {
            LogMessage log;
            log.type = "#thxiangang";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            return true;
        }
        ServerPlayer *current = room->getCurrent();
        if (player->isAlive() && current && current != player && current->isAlive() && isOwnTurn(current)
            && !current->isKongcheng() && room->alivePlayerCount() > 2
            && player->askForSkillInvoke("thquanshan", QVariant::fromValue(current)))
            quanshan(room, player, current);
        return false;
    }
};

// ---------------------------------------------------------------- hana009

class ThDuanzui : public PlayOncePerPhaseSkill
{
public:
    ThDuanzui() : PlayOncePerPhaseSkill("thduanzui") {}

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && !to->isKongcheng();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThDuanzuiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || target->isKongcheng())
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = room->askForCardChosen(source, target, "h", objectName());
        if (id < 0)
            return ContinueEffects;
        room->showCard(target, id);
        const Card *shown = Sanguosha->getCard(id);
        if (shown->isKindOf("Jink") || shown->isKindOf("Peach")) {
            auto *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_thduanzui");
            CardUseStruct use(slash, source, target);
            use.setOwnedCard(slash);
            if (source->canSlash(target, slash, false))
                room->useCardFromSkillEffect(use, ctx);
        } else {
            auto *duel = new Duel(Card::NoSuit, 0);
            duel->setSkillName("_thduanzui");
            duel->setCancelable(false);
            CardUseStruct use(duel, source, target);
            use.setOwnedCard(duel);
            if (!source->isProhibited(target, duel))
                room->useCardFromSkillEffect(use, ctx);
        }
        return ContinueEffects;
    }
};

// ---------------------------------------------------------------- hana010

class ThZheyin : public TriggerSkillV2
{
public:
    ThZheyin() : TriggerSkillV2("thzheyin") { events << TargetConfirming; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || usedThisPhase(room, player, objectName()))
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.to.contains(player) || !use.from || use.from == player || !use.from->isAlive()
            || (use.from->isNude() && player->isNude()))
            return TriggerList();
        if (!use.card->isKindOf("BasicCard") && !(use.card->isKindOf("TrickCard") && player->getHp() == 1))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        markUsedThisPhase(room, ctx.owner, objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *from = use.from;
        bool given = false;
        if (!player->isNude()) {
            const Card *card = room->askForCard(player, "..", "@thzheyin-give:" + from->objectName(),
                                                *ctx.original_data, Card::MethodNone);
            if (card) {
                room->giveCard(player, from, card, objectName());
                given = true;
            }
        }
        if (!given && from->isAlive() && !from->isNude()) {
            const Card *card = room->askForCard(from, "..", "@thzheyin-give:" + player->objectName(),
                                                *ctx.original_data, Card::MethodNone);
            if (card) {
                room->giveCard(from, player, card, objectName());
                given = true;
            }
        }
        if (!given || !use.to.contains(player))
            return false;
        LogMessage log;
        log.type = "#ThZheyinRemove";
        log.from = player;
        log.arg = objectName();
        log.card_str = use.card->toString();
        room->sendLog(log);
        room->cancelTarget(use, player);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

// ---------------------------------------------------------------- hana011

class ThYachuiViewAs : public ViewAsSkillV2
{
public:
    ThYachuiViewAs() : ViewAsSkillV2("thyachui") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thyachui"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsHandCard(request.initiator, card) && card->isRed();
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

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator
            && to->getLostHp() >= qMax(1, request.selectedCardIds.length());
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThYachuiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *payer = ctx.initiator;
        if (!payer || !target || !target->isAlive() || !ctx.use_card)
            return ContinueEffects;
        QList<int> ids;
        foreach (int id, ctx.use_card->getSubcards())
            if (payer->handCards().contains(id))
                ids << id;
        if (ids.isEmpty())
            return ContinueEffects;
        payer->getRoom()->giveCard(payer, target, ids, objectName(), true);
        if (payer->isAlive())
            payer->drawCards(ids.length(), objectName());
        return ContinueEffects;
    }
};

class ThYachui : public TriggerSkillV2
{
public:
    ThYachui() : TriggerSkillV2("thyachui")
    {
        events << EventPhaseStart;
        view_as_skill = new ThYachuiViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw
            || player->isKongcheng())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForUseCard(ctx.owner, "@@thyachui", "@thyachui", -1, Card::MethodNone) != nullptr;
    }

    // The draw phase was given up for the gift.
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
};

class ThChunhen : public TriggerSkillV2
{
public:
    ThChunhen() : TriggerSkillV2("thchunhen")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.from || move.from == player
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
            return TriggerList();
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const Player::Place place = move.from_places.value(i);
            if ((place == Player::PlaceHand || place == Player::PlaceEquip)
                && Sanguosha->getCard(move.card_ids.at(i))->getSuit() == Card::Diamond)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
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

// ---------------------------------------------------------------- hana012

class ThXiagong : public TargetModSkillV2
{
public:
    ThXiagong() : TargetModSkillV2("thxiagong") {}

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || ctx.primary->getWeapon()
            || !ctx.primary->hasSkill(objectName()))
            return CorrectSkillResult::noEffect();
        const int range = ctx.primary->getAttackRange();
        return range < 2 ? CorrectSkillResult::useAmount(2 - range) : CorrectSkillResult::noEffect();
    }
};

class ThGuaitanViewAs : public ViewAsSkillV2
{
public:
    ThGuaitanViewAs() : ViewAsSkillV2("thguaitan") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thguaitan"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.length() < 2 && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty() && selected.length() <= 2;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThGuaitanCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        const QString choice = room->askForChoice(source, objectName(), "BasicCard+EquipCard+TrickCard",
                                                  QVariant::fromValue(target));
        LogMessage log;
        log.type = "#ThGuaitan";
        log.from = source;
        log.to << target;
        log.arg = choice;
        room->sendLog(log);
        room->addPlayerMark(target, "@guaitan_" + choice.left(5).toLower());
        room->setPlayerCardLimitation(target, "use,response", choice, false, objectName());
        if (isOwnTurn(target))
            room->setPlayerFlag(target, "guaitan_current");
        return ContinueEffects;
    }
};

class ThGuaitan : public TriggerSkillV2
{
public:
    ThGuaitan() : TriggerSkillV2("thguaitan")
    {
        events << PreCardUsed << CardFinished;
        view_as_skill = new ThGuaitanViewAs;
    }

    // #thguaitan marks every card that deals damage at PreDamageDone.
    // A fresh use starts without the mark, so a declined trigger never leaks.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && use.card->hasFlag(flagName()))
                room->setCardFlag(use.card, "-" + flagName());
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (event != CardFinished || !use.card || !use.card->hasFlag(flagName()))
            return TriggerList();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || use.card->getTypeId() == Card::TypeSkill)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The nested @@ response chooses the victims and their locked types.
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card && use.card->hasFlag(flagName()))
            room->setCardFlag(use.card, "-" + flagName());
        room->askForUseCard(ctx.owner, "@@thguaitan", "@thguaitan", -1, Card::MethodNone);
        return false;
    }

private:
    static QString flagName() { return QStringLiteral("thguaitan_hit"); }
};

// Lifts 怪谈 when the locked character is damaged again or its next turn ends.
class ThGuaitanClear : public TriggerSkillV2
{
public:
    ThGuaitanClear() : TriggerSkillV2("#thguaitan")
    {
        events << Damaged << EventPhaseChanging << PreDamageDone;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == PreDamageDone) {
            // Feed 怪谈's "this card dealt damage" record even though its own events lack this one.
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->getTypeId() != Card::TypeSkill)
                room->setCardFlag(damage.card, "thguaitan_hit");
            return false;
        }
        if (!player)
            return false;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive)
                return false;
            if (player->hasFlag("guaitan_current")) {
                room->setPlayerFlag(player, "-guaitan_current");
                return false;
            }
        }
        if (player->getMark("@guaitan_basic") + player->getMark("@guaitan_equip") + player->getMark("@guaitan_trick") <= 0)
            return false;
        room->setPlayerMark(player, "@guaitan_basic", 0);
        room->setPlayerMark(player, "@guaitan_equip", 0);
        room->setPlayerMark(player, "@guaitan_trick", 0);
        room->removePlayerCardLimitationByReason(player, "thguaitan");
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// ---------------------------------------------------------------- hana013

class ThHouzhi : public TriggerSkillV2
{
public:
    ThHouzhi() : TriggerSkillV2("thhouzhi")
    {
        events << DamageInflicted << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == EventPhaseStart && (player->getPhase() != Player::Finish || player->getMark("@stiff") <= 0))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        if (event == DamageInflicted) {
            player->gainMark("@stiff", ctx.original_data->value<DamageStruct>().damage);
            return true;
        }
        const int n = player->getMark("@stiff");
        player->loseAllMarks("@stiff");
        if (n > 0)
            room->loseHp(player, n, true, player, objectName());
        return false;
    }
};

class ThShayu : public TriggerSkillV2
{
public:
    ThShayu() : TriggerSkillV2("thshayu")
    {
        events << Damage;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play
            || player->getMark("@stiff") <= 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        ctx.owner->loseMark("@stiff");
        return false;
    }
};

class ThDujia : public ViewAsSkillV2
{
public:
    ThDujia() : ViewAsSkillV2("thdujia", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isKindOf("BasicCard")
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThDujiaCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        source->gainMark("@stiff");
        if (source->isAlive())
            source->drawCards(3, objectName());
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- hana014

class ThWendao : public WakeSkill
{
public:
    ThWendao() : WakeSkill("thwendao")
    {
        events << EventPhaseStart;
        waked_skills = "wuyan";
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->isKongcheng(); }

    // Upstream grants its own 迷途; the local 无言 is the same rule.
    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->isWounded() && room->askForChoice(player, objectName(), "recover+draw") == "recover")
            room->recover(player, RecoverStruct(objectName(), player));
        else
            player->drawCards(2, objectName());
        if (room->changeMaxHpForAwakenSkill(player, -1, objectName()))
            room->acquireSkillFromEffect(player, "wuyan", ctx);
    }
};

// ---------------------------------------------------------------- hana015

static QList<const Player *> nearestTo(const Player *from)
{
    QList<const Player *> result;
    int nearest = 0;
    foreach (const Player *p, from->getAliveSiblings()) {
        const int distance = from->distanceTo(p);
        if (distance < 0)
            continue;
        if (result.isEmpty() || distance < nearest) {
            result.clear();
            nearest = distance;
        }
        if (distance == nearest)
            result << p;
    }
    return result;
}

class ThLeishiViewAs : public ViewAsSkillV2
{
public:
    ThLeishiViewAs() : ViewAsSkillV2("thleishi", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thleishi"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
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
        if (!request.initiator || !selected.isEmpty() || !to)
            return false;
        const QString victim = request.initiator->property("thleishi").toString();
        const Player *from = nullptr;
        QList<const Player *> everyone = request.initiator->getAliveSiblings();
        everyone << request.initiator;
        foreach (const Player *p, everyone)
            if (p->objectName() == victim)
                from = p;
        return from && nearestTo(from).contains(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThLeishiCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive())
            return ContinueEffects;
        ServerPlayer *from = ctx.invoker && ctx.invoker->isAlive() ? ctx.invoker : nullptr;
        target->getRoom()->damage(DamageStruct(objectName(), from, target, 1, DamageStruct::Thunder));
        return ContinueEffects;
    }
};

class ThLeishi : public TriggerSkillV2
{
public:
    ThLeishi() : TriggerSkillV2("thleishi")
    {
        events << DamageComplete;
        view_as_skill = new ThLeishiViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || player->getHp() > 1 || !damage.from
            || !damage.from->isAlive() || !damage.from->hasSkill(objectName()) || damage.from->isNude())
            return TriggerList();
        return TriggerList{{damage.from, {objectName()}}};
    }

    // The nested @@ response discards and strikes; it reads the victim from a property.
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->setPlayerProperty(ctx.owner, "thleishi", ctx.invoker->objectName());
        room->askForUseCard(ctx.owner, "@@thleishi", "@thleishi", -1, Card::MethodDiscard);
        room->setPlayerProperty(ctx.owner, "thleishi", QString());
        return false;
    }
};

class ThShanling : public TriggerSkillV2
{
public:
    ThShanling() : TriggerSkillV2("thshanling") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start)
            return TriggerList();
        if (inRange(room, player).isEmpty())
            return TriggerList();
        foreach (ServerPlayer *p, inRange(room, player))
            if (qMax(0, p->getHp()) < player->getHp())
                return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, inRange(room, ctx.owner), objectName(),
                                                        "@thshanling", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->damage(DamageStruct(objectName(), ctx.owner, target, 1, DamageStruct::Thunder));
        return false;
    }

private:
    static QList<ServerPlayer *> inRange(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->inMyAttackRange(p))
                result << p;
        return result;
    }
};

// ---------------------------------------------------------------- hana016

class ThShijieViewAs : public ViewAsSkillV2
{
public:
    ThShijieViewAs() : ViewAsSkillV2("thshijie", 1)
    {
        response_or_use = true;
        expand_pile = "utensil";
        // ServerPlayer::hasNullification still asks the legacy response probe.
        response_pattern = "nullification";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "nullification" && !request.initiator->getPile("utensil").isEmpty()
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && request.initiator->getPile("utensil").contains(card->getEffectiveId());
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
        auto *card = new Nullification(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Nullification"; }
};

class ThShijie : public TriggerSkillV2
{
public:
    ThShijie() : TriggerSkillV2("thshijie")
    {
        events << HpRecover;
        view_as_skill = new ThShijieViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
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

    // One utensil per point recovered; a refusal ends the rest.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int n = qMax(1, ctx.original_data->value<RecoverStruct>().recover);
        for (int i = 0; i < n && ctx.owner->isAlive(); ++i) {
            if (i > 0 && !ctx.owner->askForSkillInvoke(objectName()))
                break;
            ctx.owner->addToPile("utensil", room->drawCard(), true);
        }
        return false;
    }
};

class ThShengzhi : public TriggerSkillV2
{
public:
    ThShengzhi() : TriggerSkillV2("thshengzhi") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::RoundStart)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()))
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
        ServerPlayer *owner = ctx.owner;
        // Offer only suits the target can actually discard two of.
        QMap<Card::Suit, int> counts;
        foreach (const Card *card, target->getCards("he"))
            if (target->canDiscard(target, card->getEffectiveId()))
                ++counts[card->getSuit()];
        QStringList suits;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
            if (it.value() >= 2 && it.key() <= Card::Diamond)
                suits << Card::Suit2String(it.key());
        if (!suits.isEmpty()) {
            const QString suit = room->askForChoice(target, objectName(), suits.join("+") + "+cancel",
                                                    QVariant::fromValue(owner));
            if (suit != "cancel"
                && room->askForDiscard(target, objectName(), 2, 2, true, true, "@thshengzhi:" + owner->objectName(),
                                       ".|" + suit))
                return false;
        }
        QStringList phases;
        const QList<Player::Phase> order = {Player::Start, Player::Judge, Player::Draw, Player::Play, Player::Discard,
                                            Player::Finish};
        foreach (Player::Phase phase, order)
            if (target->getPhases().contains(phase) && !target->isSkipped(phase))
                phases << phaseName(phase);
        if (!phases.isEmpty() && owner->isAlive()) {
            const QString chosen = room->askForChoice(owner, objectName(), phases.join("+"), QVariant::fromValue(target));
            foreach (Player::Phase phase, order)
                if (phaseName(phase) == chosen)
                    target->skip(phase);
        }
        if (owner->isAlive())
            room->damage(DamageStruct(objectName(), target->isAlive() ? target : nullptr, owner));
        return false;
    }

private:
    static QString phaseName(Player::Phase phase)
    {
        switch (phase) {
        case Player::Start: return "start";
        case Player::Judge: return "judge";
        case Player::Draw: return "draw";
        case Player::Play: return "play";
        case Player::Discard: return "discard";
        case Player::Finish: return "finish";
        default: return QString();
        }
    }
};

// ---------------------------------------------------------------- hana017

class ThZhaoyu : public TriggerSkillV2
{
public:
    ThZhaoyu() : TriggerSkillV2("thzhaoyu") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && !owner->isNude())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *card = room->askForCard(ctx.owner, "..", "@thzhaoyu:" + ctx.invoker->objectName(),
                                            *ctx.original_data, Card::MethodNone);
        if (!card)
            return false;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = ctx.owner;
        log.arg = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = card->getEffectiveId();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        ServerPlayer *player = ctx.invoker;
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != owner)
            return false;
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, owner->objectName(), objectName(), QString());
        room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPile, reason, false);
        if (owner->isAlive() && player && (!player->getJudgingArea().isEmpty() || player->getWeapon())
            && owner->askForSkillInvoke("thzhaoyu-draw", "draw"))
            room->drawCards(owner, 1, objectName(), false);
        return false;
    }
};

class ThWuwu : public TriggerSkillV2
{
public:
    ThWuwu() : TriggerSkillV2("thwuwu")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Discard
            || player->isKongcheng())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        room->askForDiscard(ctx.owner, objectName(), 1, 1, false, false);
        return false;
    }
};

class ThRudao : public WakeSkill
{
public:
    ThRudao() : WakeSkill("thrudao") { events << EventPhaseStart; }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->isKongcheng(); }

    void awaken(Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->drawCards(2, objectName());
        if (room->changeMaxHpForAwakenSkill(player, -3, objectName()))
            room->detachSkillFromPlayer(player, "thwuwu");
    }
};

// ---------------------------------------------------------------- hana018

class ThLiuzhenViewAs : public ViewAsSkillV2
{
public:
    ThLiuzhenViewAs() : ViewAsSkillV2("thliuzhen") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thliuzhen"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &,
                         const Player *to) const override
    {
        if (!request.initiator || !to || to == request.initiator || to->hasFlag("liuzhenold"))
            return false;
        Slash slash(Card::NoSuit, 0);
        return request.initiator->canSlash(to, &slash, false);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThLiuzhenCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive())
            target->getRoom()->setPlayerFlag(target, "liuzhennew");
        return ContinueEffects;
    }
};

class ThLiuzhen : public TriggerSkillV2
{
public:
    ThLiuzhen() : TriggerSkillV2("thliuzhen")
    {
        events << TargetSpecified << BeforeCardsMove << CardOffset;
        view_as_skill = new ThLiuzhenViewAs;
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return TriggerList();
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !player->isAlive() || !player->hasSkill(objectName())
                || player->getPhase() != Player::Play || !use.card || !use.card->isKindOf("Slash")
                || use.card->hasFlag("liuzhenslash"))
                return TriggerList();
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (!use.to.contains(p) && player->canSlash(p, use.card, false))
                    return TriggerList{{player, {objectName()}}};
        } else if (event == CardOffset) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.from == player && player->hasSkill(objectName()) && effect.card
                && effect.card->hasFlag("liuzhenslash") && effect.to && effect.to->isAlive())
                return TriggerList{{player, {objectName()}}};
        } else if (event == BeforeCardsMove) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            const Card *card = move.reason.m_useStruct.card;
            if (card && move.reason.m_useStruct.from == player && card->hasFlag("thliuzhen")
                && move.from_places.contains(Player::PlaceTable) && move.to_place == Player::DiscardPile
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == TargetSpecified) {
            if (!isUsable(ctx))
                return false;
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            foreach (ServerPlayer *p, use.to)
                room->setPlayerFlag(p, "liuzhenold");
            const bool used = room->askForUseCard(player, "@@thliuzhen", "@thliuzhen", -1, Card::MethodNone) != nullptr;
            foreach (ServerPlayer *p, use.to)
                room->setPlayerFlag(p, "-liuzhenold");
            if (used) {
                addUsage(ctx);
                room->setCardFlag(use.card, "thliuzhen");
            }
            return false;
        }
        if (event == CardOffset) {
            ServerPlayer *target = ctx.original_data->value<CardEffectStruct>().to;
            if (!target->askForSkillInvoke("thliuzhen_missed", QVariant::fromValue(player)))
                return false;
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = target;
            log.to << player;
            log.arg = objectName();
            room->sendLog(log);
            return true;
        }
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == CardOffset) {
            ServerPlayer *target = ctx.original_data->value<CardEffectStruct>().to;
            if (!player->canDiscard(player, "he")
                || room->askForChoice(target, objectName(), "discard+draw", QVariant::fromValue(player)) == "draw")
                player->drawCards(1, objectName());
            else
                room->askForDiscard(player, objectName(), 1, 1, false, true);
            return false;
        }
        // The resolved slash is used again, without distance limits, on the marked characters.
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const Card *card = move.reason.m_useStruct.card;
        if (!card || !room->CardInTable(card))
            return false;
        room->setCardFlag(card, "-thliuzhen");
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!p->hasFlag("liuzhennew"))
                continue;
            room->setPlayerFlag(p, "-liuzhennew");
            if (p->isAlive() && player->isAlive() && player->canSlash(p, card, false))
                targets << p;
        }
        if (targets.isEmpty())
            return false;
        const QList<int> materials = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        QList<int> intercepted;
        foreach (int id, materials)
            if (move.card_ids.contains(id))
                intercepted << id;
        move.removeCardIds(intercepted);
        *ctx.original_data = QVariant::fromValue(move);
        room->sortByActionOrder(targets);
        room->setCardFlag(card, "liuzhenslash");
        CardUseStruct use(card, player, targets);
        room->useCard(use, false);
        return false;
    }
};

class ThTianchanViewAs : public ViewAsSkillV2
{
public:
    ThTianchanViewAs() : ViewAsSkillV2("thtianchanv", 1)
    {
        attached_lord_skill = true;
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || self->getKingdom() != "hana" || request.pattern != "peach"
            || (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE))
            return false;
        const QString dying = self->property("currentdying").toString();
        const SkillInstance *instance = self->findSkillInstance(request.activationRef.key.skillName,
                                                                request.activationRef.key.instanceID);
        return instance && instance->parentRef.key.skillName == "thtianchan"
            && instance->parentRef.ownerObjectName == dying;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using") || card->getSuit() != Card::Spade)
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
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
        auto *peach = new Peach(material->getSuit(), material->getNumber());
        peach->addSubcard(material);
        peach->setSkillName(objectName());
        return peach;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Peach"; }
};

class ThTianchan : public TriggerSkillV2
{
public:
    ThTianchan() : TriggerSkillV2("thtianchan$")
    {
        events << GameStart << EventPhaseStart << EventAcquireSkill << EventLoseSkill << Death << GeneralShown
               << GeneralHidden;
        global = true;
    }

    // Each lord instance grants one exact "thtianchanv" child to every other living character.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        QList<SkillInstanceRef> parents;
        foreach (ServerPlayer *lord, room->getAlivePlayers())
            if (lord->hasLordSkill(objectName(), true))
                foreach (int id, lord->getSkillInstanceIds(objectName()))
                    parents << SkillInstanceRef(lord->objectName(), SkillInstanceKey(objectName(), id));
        foreach (ServerPlayer *donor, room->getAllPlayers(true)) {
            foreach (const SkillInstance &instance, donor->getSkillInstances()) {
                if (instance.skillName != "thtianchanv" || instance.source != SourceAttached
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
                    room->attachSkillToPlayer(donor, "thtianchanv", parent);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

}

ThJiewuCard::ThJiewuCard() { setSkillName("thjiewu"); mute = true; }
ThXihuaCard::ThXihuaCard() { setSkillName("thxihua"); mute = true; }
ThQuanshanCard::ThQuanshanCard() { setSkillName("thquanshan"); mute = true; }
ThDuanzuiCard::ThDuanzuiCard() { setSkillName("thduanzui"); mute = true; }
ThYachuiCard::ThYachuiCard() { setSkillName("thyachui"); mute = true; }
ThGuaitanCard::ThGuaitanCard() { setSkillName("thguaitan"); mute = true; }
ThDujiaCard::ThDujiaCard() { setSkillName("thdujia"); mute = true; }
ThLeishiCard::ThLeishiCard() { setSkillName("thleishi"); mute = true; }
ThLiuzhenCard::ThLiuzhenCard() { setSkillName("thliuzhen"); mute = true; }

TouhouHanaPackage::TouhouHanaPackage()
    : Package("touhou-hana")
{
    General *hana001 = new General(this, "hana001$", "hana");
    hana001->addSkill(new ThHuaji);
    hana001->addSkill(new ThFeizhan);
    hana001->addSkill(new ThFeizhanRange);
    related_skills.insert("thfeizhan", "#thfeizhan");

    General *hana002 = new General(this, "hana002", "hana");
    hana002->addSkill(new ThJiewu);
    hana002->addSkill(new ThJiewuArmor);
    related_skills.insert("thjiewu", "#thjiewu");
    hana002->addSkill(new ThGenxing);
    hana002->addRelateSkill("thmopao");

    General *hana003 = new General(this, "hana003", "hana", 3, false);
    hana003->addSkill(new ThYuwu);
    hana003->addSkill(new ThGuihang);
    hana003->addSkill(new ThBian);
    hana003->addSkill(new ThBianTargetMod);
    related_skills.insert("thbian", "#thbian");

    General *hana004 = new General(this, "hana004", "hana", 3);
    hana004->addSkill(new ThXuelan);
    hana004->addSkill(new ThXinwang);
    hana004->addSkill(new ThJuedu);

    General *hana005 = new General(this, "hana005", "hana");
    hana005->addSkill(new ThTingwu);
    hana005->addSkill(new ThYuchang);
    hana005->addSkill(new ThYuchangTargetMod);
    related_skills.insert("thyuchang", "#thyuchang");

    General *hana006 = new General(this, "hana006", "hana");
    hana006->addSkill(new ThXihua);
    hana006->addSkill(new ThXihuaReveal);
    related_skills.insert("thxihua", "#thxihua");

    General *hana007 = new General(this, "hana007", "hana", 3, false);
    hana007->addSkill(new ThMimeng);
    hana007->addSkill(new ThAnyun);

    General *hana008 = new General(this, "hana008", "hana", 3);
    hana008->addSkill(new ThQuanshan);
    hana008->addSkill(new ThXiangang);

    General *hana009 = new General(this, "hana009", "hana");
    hana009->addSkill(new ThDuanzui);

    General *hana010 = new General(this, "hana010", "hana", 3);
    hana010->addSkill(new ThZheyin);
    hana010->addSkill(new PendingSkill("thyingdeng"));

    General *hana011 = new General(this, "hana011", "hana", 3);
    hana011->addSkill(new ThYachui);
    hana011->addSkill(new ThChunhen);

    General *hana012 = new General(this, "hana012", "hana");
    hana012->addSkill(new ThXiagong);
    hana012->addSkill(new ThGuaitan);
    hana012->addSkill(new ThGuaitanClear);
    related_skills.insert("thguaitan", "#thguaitan");

    General *hana013 = new General(this, "hana013", "hana", 3);
    hana013->addSkill(new ThHouzhi);
    hana013->addSkill(new ThShayu);
    hana013->addSkill(new ThDujia);

    General *hana014 = new General(this, "hana014", "hana", 4, false);
    hana014->addSkill(new PendingSkill("thxianfa"));
    hana014->addSkill(new ThWendao);

    General *hana015 = new General(this, "hana015", "hana", 3);
    hana015->addSkill(new ThLeishi);
    hana015->addSkill(new ThShanling);

    General *hana016 = new General(this, "hana016", "hana", 3);
    hana016->addSkill(new ThShijie);
    hana016->addSkill(new ThShengzhi);

    General *hana017 = new General(this, "hana017", "hana", 7);
    hana017->addSkill(new ThZhaoyu);
    hana017->addSkill(new ThWuwu);
    hana017->addSkill(new ThRudao);

    General *hana018 = new General(this, "hana018$", "hana");
    hana018->addSkill(new ThLiuzhen);
    hana018->addSkill(new ThTianchan);

    skills << new ThMopao << new ThTianchanViewAs;

    addMetaObject<ThJiewuCard>();
    addMetaObject<ThXihuaCard>();
    addMetaObject<ThQuanshanCard>();
    addMetaObject<ThDuanzuiCard>();
    addMetaObject<ThYachuiCard>();
    addMetaObject<ThGuaitanCard>();
    addMetaObject<ThDujiaCard>();
    addMetaObject<ThLeishiCard>();
    addMetaObject<ThLiuzhenCard>();
}

ADD_PACKAGE(TouhouHana)
