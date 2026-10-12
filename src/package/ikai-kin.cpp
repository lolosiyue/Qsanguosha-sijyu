#include "ikai-kin.h"
#include "touhou-utils.h"
#include "ikai-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"

using namespace TouhouUtils;
using namespace IkaiUtils;

// TouhouTripleSha's 异界·金. Most of its skills are 三国 originals under new names; those are
// reused by their local names and only the differing ones are ported.

namespace {

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

// ---------------------------------------------------------------- wind037

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

// ---------------------------------------------------------------- 玄舞

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

IkSizhuoCard::IkSizhuoCard() { setSkillName("iksizhuo"); mute = true; }
IkJunanCard::IkJunanCard() { setSkillName("ikjunan"); mute = true; }

IkaiKinPackage::IkaiKinPackage()
    : Package("ikai-kin")
{

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

    General *wind037 = new General(this, "wind037", "kaze", 4, true, true);
    wind037->addSkill(new IkXuanren);
    wind037->addSkill(new IkXuanrenTargetMod);
    related_skills.insert("ikxuanren", "#ikxuanren-target");
    wind037->addSkill(new IkLanjian);

    General *snow016 = new General(this, "snow016", "yuki");
    snow016->addSkill(new IkNilan);

    General *luna016 = new General(this, "luna016", "tsuki", 6);
    luna016->addSkill("ikxinshang");
    luna016->addSkill(new IkQilei);

    General *luna062 = new General(this, "luna062", "tsuki");
    luna062->addSkill(new IkSizhuo);
    luna062->addSkill(new IkJunan);

    // No general here holds 玄舞, but 异界·木 wind012's 升天 grants it.
    skills << new IkXuanwu;

    addMetaObject<IkSizhuoCard>();
    addMetaObject<IkJunanCard>();
}

ADD_PACKAGE(IkaiKin)
