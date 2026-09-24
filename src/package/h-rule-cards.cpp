// Source: QSanguosha-For-Hegemony-xxyheaven standard-package.cpp and power.cpp.
// Rule rewards use V2 payment; converted basic cards retain the shared card pipeline.
#include "h-rule-cards.h"
#include "engine.h"
#include "json.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill.h"
#include "standard.h"
#include <QScopeGuard>

namespace {

bool hasConcealedOpponent(const Player *player)
{
    for (const Player *other : player->getAliveSiblings()) {
        if (!other->hasShownAllGenerals()) return true;
    }
    return false;
}

void firstShowEffect(Room *room, ServerPlayer *player, ServerPlayer *target, bool fillHand = true)
{
    const int count = 4 - player->getHandcardNum();
    if (fillHand && count > 0) player->drawCards(count, "heg_firstshow");
    if (!player->isAlive() || !target || !target->isAlive()) return;

    QStringList choices;
    if (!target->hasShownGeneral1()) choices << "head_general";
    if (!target->getActualGeneral2Name().isEmpty() && !target->hasShownGeneral2()) choices << "deputy_general";
    if (choices.isEmpty()) return;
    target->setFlags("XianquTarget");
    const auto clearFlag = qScopeGuard([target] { target->setFlags("-XianquTarget"); });
    const QString choice = room->askForChoice(player, "heg_firstshow_see", choices.join("+"),
                                              QVariant::fromValue(target));
    if (!choices.contains(choice)) return;

    LogMessage log;
    log.type = "#KnownBothView";
    log.from = player;
    log.to << target;
    log.arg = choice;
    room->sendLog(log, room->getOtherPlayers(player, true));

    // Actual identities are server authority; only the viewer receives their names.
    const QString name = choice == "head_general" ? target->getActualGeneral1Name()
                                                   : target->getActualGeneral2Name();
    if (name.isEmpty()) return;
    // The rules own knowledge grants; AI may read only the viewer's snapshot.
    const QString knowledgeKey = "KnownBoth_" + target->objectName();
    QStringList known = player->getTag(knowledgeKey).toString().split('+');
    while (known.size() < 2) known << QString();
    known[choice == "head_general" ? 0 : 1] = name;
    player->setTag(knowledgeKey, known.join('+'));
    log.type = "$KnownBothViewGeneral";
    log.arg = name;
    log.arg2 = choice;
    room->sendLog(log, player);
    JsonArray args;
    args << "heg_firstshow" << JsonUtils::toJsonArray(QStringList{name});
    room->doNotify(player, QSanProtocol::S_COMMAND_VIEW_GENERALS, args);
}

class HRuleReward : public ViewAsSkillV2
{
public:
    HRuleReward(const QString &name, const QString &mark, const QString &history)
        : ViewAsSkillV2(name), history(history)
    {
        frequency = Limited;
        limit_mark = mark;
        // Rule rewards are not general skills and do not enter their description.
        attached_lord_skill = true;
        // The reward is presented as a mark card in the hand area.
        hide_skill = true;
        setProperty("MarkCard", history.mid(1));
        // XXY's mark cards exist only while their reward token is held.
        setProperty("VisibilityMark", mark);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.initiator->isAlive()
            && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.isEmpty(); }
    QString historyKey(const ActiveSkillRequest &) const override { return history; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return canActivate(request) && cardSelectionFeasible(request)
            ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        // Choice/reveal cancellation must not spend the token. Only the initiator pays.
        if (!ctx.initiator || !ctx.initiator->isAlive() || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        return true;
    }

private:
    QString history;
};

class HFirstShow : public HRuleReward
{
public:
    HFirstShow() : HRuleReward("heg_firstshow", "@firstshow", "HFirstShowCard") {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return HRuleReward::canActivate(request)
            && (request.initiator->getHandcardNum() < 4 || hasConcealedOpponent(request.initiator));
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && selected.isEmpty()
            && candidate != request.initiator && !candidate->hasShownAllGenerals();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        if (!request.initiator) return false;
        return hasConcealedOpponent(request.initiator)
            ? targets.size() == 1 && canSelectTarget(request, {}, targets.first()) : targets.isEmpty();
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive())
            firstShowEffect(ctx.invoker->getRoom(), ctx.invoker, nullptr);
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        // Filling the hand is independent of whether the optional viewing target survives.
        if (ctx.invoker && ctx.invoker->isAlive())
            firstShowEffect(ctx.invoker->getRoom(), ctx.invoker, target, false);
        return ContinueEffects;
    }
};

class HHalfMaxHp : public HRuleReward
{
public:
    HHalfMaxHp() : HRuleReward("heg_halfmaxhp", "@halfmaxhp", "HHalfMaxHpCard") {}
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive()) ctx.invoker->drawCards(1, objectName());
        return ContinueEffects;
    }
};

class HCompanion : public HRuleReward
{
public:
    explicit HCompanion(bool careerist = false)
        : HRuleReward(careerist ? "heg_careerman" : "heg_companion",
                      careerist ? "@careerist" : "@companion",
                      careerist ? "HCareermanCard" : "HCompanionCard"), careerist(careerist)
    {
        response_or_use = true;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || !player->isAlive() || player->getMark(limit_mark) <= 0) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
        Peach peach(Card::NoSuit, 0);
        peach.setSkillName(objectName());
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE)
            && !player->hasFlag("Global_PreventPeach")
            // Rescue requests use registered aliases (peach / peach+analeptic),
            // not raw expressions: '+' in ExpPattern means conjunction.
            && Sanguosha->matchPattern(request.pattern, player, &peach)
            && !player->isCardLimited(&peach, Card::MethodUse);
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return ViewAsSkillV2::createCard(request);
        // Dying responses are real Peach cards, so save legality and triggers stay shared.
        auto *peach = new Peach(Card::NoSuit, 0);
        peach->setSkillName(objectName());
        peach->setShowSkill(objectName());
        return peach;
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return true;
        ServerPlayer *player = ctx.initiator;
        if (!player) return false;
        Peach peach(Card::NoSuit, 0);
        peach.setSkillName(objectName());
        QStringList choices = careerist ? QStringList{"draw1card", "draw2cards"} : QStringList{"draw"};
        if (peach.isAvailable(player)) {
            if (careerist) choices << "peach";
            else choices.prepend("peach");
        }
        if (careerist && (player->getHandcardNum() < 4 || !player->hasShownAllGenerals()
            || hasConcealedOpponent(player))) choices << "firstshow";
        const QString choice = choices.size() == 1 ? choices.first()
            : room->askForChoice(player, objectName(), choices.join("+"));
        if (!choices.contains(choice)) return false;
        QVariantMap state{{"choice", choice}};
        if (choice == "firstshow") {
            QList<ServerPlayer *> targets;
            for (ServerPlayer *other : room->getAlivePlayers()) {
                if (!other->hasShownAllGenerals()) targets << other;
            }
            if (!targets.isEmpty()) {
                ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@heg_careerman-target");
                if (!target || !targets.contains(target)) return false;
                state.insert("target", QVariant::fromValue(target));
            }
        }
        ctx.extra_data = state;
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && ctx.extra_data.toMap().value("choice").toString() == "peach") {
            Peach peach(Card::NoSuit, 0);
            peach.setSkillName(objectName());
            if (!ctx.invoker || !peach.isAvailable(ctx.invoker)) return false;
        }
        return HRuleReward::pay(room, ctx, request);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !player->isAlive()) return FinishSkill;
        Room *room = player->getRoom();
        const QVariantMap state = ctx.extra_data.toMap();
        const QString choice = state.value("choice").toString();
        if (choice == "peach") {
            auto *peach = new Peach(Card::NoSuit, 0);
            peach->setSkillName("_" + objectName());
            room->useCard(CardUseStruct(peach, player, player));
        } else if (choice == "firstshow") {
            firstShowEffect(room, player, state.value("target").value<ServerPlayer *>());
        } else if (!choice.isEmpty()) {
            player->drawCards(choice == "draw1card" ? 1 : 2, objectName());
        }
        return ContinueEffects;
    }

private:
    bool careerist;
};

class HCommandSelect : public ViewAsSkillV2
{
public:
    HCommandSelect() : ViewAsSkillV2("heg_commandefect", 2)
    {
        attached_lord_skill = true;
        // Command 6 selects cards through its explicit response request only.
        setProperty("VisibilityMark", QString());
        setProperty("VisibilityResponsePattern", "@@heg_commandefect");
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && (request.pattern == "@@heg_commandefect!" || request.pattern == "@@heg_commandefect");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !candidate || request.selectedCardIds.size() >= 2) return false;
        const int id = candidate->getEffectiveId();
        if (id < 0 || request.selectedCardIds.contains(id)) return false;
        const bool hand = request.initiator->handCards().contains(id);
        const bool equip = request.initiator->hasEquip(candidate);
        if (!hand && !equip) return false;
        return request.selectedCardIds.isEmpty()
            || request.initiator->handCards().contains(request.selectedCardIds.first()) != hand;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        const int count = (request.initiator->isKongcheng() ? 0 : 1)
            + (request.initiator->hasEquip() ? 1 : 0);
        if (request.selectedCardIds.size() != count || count == 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    bool willThrowSelectedCards() const override { return false; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        // These are the cards kept by command 6, never payment or cards to discard.
        auto *card = new DummyCard(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
};

} // namespace

QList<const Skill *> createHegemonyRuleSkills(QObject *owner)
{
    const QList<Skill *> definitions{new HFirstShow, new HHalfMaxHp, new HCompanion,
                                     new HCompanion(true), new HCommandSelect};
    QList<const Skill *> skills;
    for (Skill *skill : definitions) {
        // Separate instances follow their engine/room QObject lifetime and thread.
        skill->setParent(owner);
        skills << skill;
    }
    return skills;
}
