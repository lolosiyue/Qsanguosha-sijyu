#include "assassins.h"
#include "skill-instance-utils.h"
#include "standard.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include <QScopeGuard>

namespace {
static void addAllInstances(TriggerList &result, ServerPlayer *owner, const QString &name)
{
    if (!owner || !owner->isAlive() || !owner->hasSkill(name)) return;
    for (int id : owner->getValidSkillInstanceIds(name))
        result[owner] << SkillInstanceUtils::formatName(name, id);
}

static bool ownsCard(const Player *player, const Card *card)
{
    if (!player || !card || card->isEquipped() || card->hasFlag("using")) return false;
    const int id = card->getEffectiveId();
    return id >= 0 && player->handCards().contains(id);
}

static bool slashUse(const CardUseStruct &use)
{ return use.card && use.card->isKindOf("Slash"); }

static bool discardableMaterial(Room *room, ServerPlayer *chooser, ServerPlayer *owner, int id)
{
    return room && chooser && owner && chooser->isAlive() && owner->isAlive() && id >= 0
        && room->getCardOwner(id) == owner
        && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
        && Sanguosha->getCard(id) && !Sanguosha->getCard(id)->hasFlag("using")
        && chooser->canDiscard(owner, id);
}

// Verify the committed move, not ownership after callbacks (the recipient may have moved it again).
static bool paidMaterial(Room *room, qint64 skillEvent, qint64 after, const QString &skill,
                         ServerPlayer *payer, int id, ServerPlayer *receiver = nullptr)
{
    if (skillEvent <= 0) return false;
    QVariantMap filter{{"from", payer->objectName()}, {"after", after}};
    bool paid = false;
    while (true) {
        const QVariantMap page = room->queryHistoryMoves(filter);
        if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
        for (const QVariant &value : page.value("items").toList()) {
            const QVariantMap fact = value.toMap(), move = fact.value("data").toMap();
            if (move.value("card_id").toInt() != id || move.value("reason_skill").toString() != skill
                || room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() != skillEvent)
                continue;
            if (receiver) {
                paid |= move.value("from_place").toInt() == Player::PlaceHand
                    && move.value("to").toString() == receiver->objectName()
                    && move.value("to_place").toInt() == Player::PlaceHand
                    && move.value("reason").toInt() == CardMoveReason::S_REASON_GIVE;
            } else {
                paid |= (move.value("from_place").toInt() == Player::PlaceHand || move.value("from_place").toInt() == Player::PlaceEquip)
                    && move.value("to_place").toInt() == Player::DiscardPile
                    && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD;
            }
        }
        if (!page.value("has_more").toBool()) return paid;
        filter["watermark"] = page.value("watermark");
        filter["after"] = page.value("next_after");
    }
}
}

static QVariantList moukuiReceipts(Room *room, const Card *card)
{
    const qint64 use = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
    QVariantList result;
    if (use <= 0 || !card) return result;
    for (const QVariant &value : card->getTag("moukui_receipts").toList())
        if (value.toMap().value("use_event").toLongLong() == use) result << value;
    return result;
}

static void setMoukuiReceipts(Room *room, const Card *card, const QVariantList &receipts)
{
    const qint64 use = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
    if (use <= 0 || !card) return;
    QSet<qint64> ancestors;
    qint64 parent = room->historyParent(use, "use_card", false).value("id").toLongLong();
    while (parent > 0 && !ancestors.contains(parent)) {
        ancestors.insert(parent);
        parent = room->historyParent(parent, "use_card", false).value("id").toLongLong();
    }
    // Preserve an outer Slash's liability when the same physical card is reused.
    QVariantList saved;
    for (const QVariant &value : card->getTag("moukui_receipts").toList())
        if (ancestors.contains(value.toMap().value("use_event").toLongLong())) saved << value;
    for (const QVariant &value : receipts) {
        QVariantMap receipt = value.toMap();
        receipt.insert("use_event", use);
        saved << receipt;
    }
    if (saved.isEmpty()) card->removeTag("moukui_receipts");
    else card->setTag("moukui_receipts", saved);
}

class Moukui : public TriggerSkillV2
{
public:
    explicit Moukui(bool continuation = false)
        : TriggerSkillV2(continuation ? "#moukui-effect" : "moukui")
    {
        if (continuation) {
            events << CardOffset;
            frequency = Compulsory;
            global = true;
        } else {
            events << TargetSpecified << CardFinished;
        }
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
                                QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!room || !player || !player->isAlive()) return true;
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!slashUse(use) || use.from != player) return true;
            // Separate contexts preserve prompt/effect ordering for each Slash target.
            for (int id : player->getValidSkillInstanceIds(objectName())) {
                for (ServerPlayer *target : use.to) {
                    if (!target || !target->isAlive()) continue;
                    SkillContext ctx;
                    ctx.skill_name = objectName() + "->" + target->objectName();
                    ctx.owner = ctx.invoker = ctx.initiator = player;
                    ctx.instanceID = id;
                    ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                    ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                    if (!ctx.sourceRef.isValid()) continue;
                    ctx.preferredTarget = target;
                    ctx.preferredTargetSeat = target->getSeat();
                    ctx.targets << target;
                    bool ok = false;
                    ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &ok);
                    if (!ok) ctx.amount = getBaseAmount();
                    ctx.original_data = &data;
                    ctx.current_event = event;
                    contexts << ctx;
                }
            }
        } else if (event == CardOffset) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card || !effect.card->isKindOf("Slash") || effect.from != player
                || !effect.to || !effect.to->isAlive() || !effect.to->canDiscard(player, "he")) return true;
            const QVariantList receipts = moukuiReceipts(room, effect.card);
            for (const QVariant &value : receipts) {
                const QVariantMap receipt = value.toMap();
                if (receipt.value("owner").toString() != player->objectName()
                    || receipt.value("target").toString() != effect.to->objectName()) continue;
                SkillContext ctx;
                ctx.skill_name = objectName() + "->" + effect.to->objectName();
                ctx.owner = ctx.invoker = ctx.initiator = player;
                ctx.instanceID = receipt.value("instance").toInt();
                ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                    SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
                if (!ctx.sourceRef.isValid()) continue;
                ctx.extra_data = receipt;
                // This is an applied effect receipt, not a fresh activation of a possibly retired grant.
                ctx.preferredTarget = effect.to;
                ctx.preferredTargetSeat = effect.to->getSeat();
                ctx.targets << effect.to;
                ctx.amount = receipt.value("amount").toInt();
                ctx.original_data = &data;
                ctx.current_event = event;
                contexts << ctx;
            }
        }
        return true;
    }

    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        // Framework effect hooks replace current_event; continuation identity is static.
        if (!events.contains(CardOffset)) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            return TriggerSkillV2::isSourceAvailable(room, ctx) && slashUse(use)
                && use.from == ctx.owner && ctx.preferredTarget && ctx.preferredTarget->isAlive()
                && use.to.contains(ctx.preferredTarget);
        }
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        if (!effect.card || !effect.to || !effect.to->isAlive() || effect.from != ctx.owner
            || !effect.to->canDiscard(ctx.owner, "he")) return false;
        for (const QVariant &value : moukuiReceipts(room, effect.card)) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("owner").toString() == ctx.owner->objectName()
                && receipt.value("instance").toInt() == ctx.instanceID
                && receipt.value("target").toString() == effect.to->objectName()) return true;
        }
        return false;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != CardFinished || !room || !slashUse(data.value<CardUseStruct>())) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        setMoukuiReceipts(room, use.card, {});
        for (ServerPlayer *target : room->getAllPlayers(true))
            room->setPlayerMark(target, objectName() + use.card->toString(), 0);
        return true;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.preferredTarget || !ctx.original_data) return false;
        if (event == CardOffset) {
            // The liability removes the original user's cards; the dodging player chooses them.
            ctx.targets = {ctx.owner};
            return true;
        }
        if (!ctx.owner->askForSkillInvoke(this, QVariant::fromValue(ctx.preferredTarget))) return false;
        ctx.choice = "draw";
        if (ctx.owner->canDiscard(ctx.preferredTarget, "he"))
            ctx.choice = room->askForChoice(ctx.owner, objectName(), "draw+discard",
                                           QVariant::fromValue(ctx.preferredTarget), QString(), ":" + objectName());
        if (ctx.choice == "draw") ctx.targets.clear();
        else {
            ctx.targets = {ctx.preferredTarget};
            int id = -1;
            if (ctx.choice == "discard" && getEffectiveAmount(ctx) > 0
                && ctx.owner->canDiscard(ctx.preferredTarget, "he")) {
                const QVariant previous = room->getTag("MoukuiDiscard");
                const auto restoreTag = qScopeGuard([room, previous] {
                    if (previous.isValid()) room->setTag("MoukuiDiscard", previous);
                    else room->removeTag("MoukuiDiscard");
                });
                room->setTag("MoukuiDiscard", *ctx.original_data);
                id = room->askForCardChosen(ctx.owner, ctx.preferredTarget, "he", objectName(), false, Card::MethodDiscard);
            }
            ctx.extra_data = QVariantMap{{"card_id", id}};
        }
        return ctx.choice == "draw" || ctx.choice == "discard";
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Only the targetless draw settles here; liability uses framework target dispatch.
        if (event == TargetSpecified && ctx.choice == "draw") return resolve(event, room, ctx, ctx.owner);
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    { return resolve(event, room, ctx, target); }

    bool resolve(TriggerEvent event, Room *room, SkillContext &ctx, ServerPlayer *target) const
    {
        if (!room || !ctx.owner || !ctx.owner->isAlive() || !ctx.invoker || !ctx.invoker->isAlive()
            || !ctx.original_data || !target || !target->isAlive() || !isSourceAvailable(room, ctx)) return false;
        if (target != ((event == CardOffset || ctx.choice == "draw") ? ctx.owner : ctx.preferredTarget))
            return false;
        const int amount = getEffectiveAmount(ctx);
        if (event == TargetSpecified) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!slashUse(use)) return false;
            int voice = ctx.choice == "draw" ? 1 : 2;
            if (ctx.owner->getGeneralName().contains("fuwan")) voice += 3;
            room->broadcastSkillInvoke(objectName(), voice);
            if (ctx.choice == "draw") target->drawCards(amount, objectName());
            else {
                for (int i = 0; i < amount && ctx.owner->isAlive() && target->isAlive()
                     && ctx.owner->canDiscard(target, "he") && isSourceAvailable(room, ctx); ++i) {
                    const QVariant previous = room->getTag("MoukuiDiscard");
                    const auto restoreTag = qScopeGuard([room, previous] {
                        if (previous.isValid()) room->setTag("MoukuiDiscard", previous);
                        else room->removeTag("MoukuiDiscard");
                    });
                    room->setTag("MoukuiDiscard", *ctx.original_data);
                    // Later picks depend on the preceding discard and its movement callbacks.
                    const int id = i == 0 ? ctx.extra_data.toMap().value("card_id", -1).toInt()
                        : room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
                    if (!discardableMaterial(room, ctx.owner, target, id)) break;
                    room->throwCard(id, objectName(), target, ctx.owner);
                }
            }
            // Persist only primitive receipt values; losing the skill must not cancel its liability.
            QVariantList receipts = moukuiReceipts(room, use.card);
            QVariantMap receipt;
            receipt.insert("owner", ctx.owner->objectName());
            receipt.insert("instance", ctx.activationRef.key.instanceID);
            // Executor and granting root may belong to different players.
            receipt.insert("source_owner", ctx.sourceRef.ownerObjectName);
            receipt.insert("source_skill", ctx.sourceRef.key.skillName);
            receipt.insert("source_instance", ctx.sourceRef.key.instanceID);
            receipt.insert("activation_owner", ctx.activationRef.ownerObjectName);
            receipt.insert("activation_skill", ctx.activationRef.key.skillName);
            receipt.insert("target", ctx.preferredTarget->objectName());
            receipt.insert("amount", amount);
            receipts << receipt;
            setMoukuiReceipts(room, use.card, receipts);
            room->addPlayerMark(ctx.preferredTarget, objectName() + use.card->toString());
        } else if (event == CardOffset) {
            const CardEffectStruct offset = ctx.original_data->value<CardEffectStruct>();
            if (!offset.card || !offset.from || !offset.to) return false;
            QVariantList receipts = moukuiReceipts(room, offset.card);
            bool found = false;
            for (int i = 0; i < receipts.size(); ++i) {
                const QVariantMap receipt = receipts.at(i).toMap();
                if (receipt.value("owner").toString() == ctx.owner->objectName()
                    && receipt.value("instance").toInt() == ctx.instanceID
                    && receipt.value("target").toString() == offset.to->objectName()) {
                    receipts.removeAt(i);
                    found = true;
                    break;
                }
            }
            if (!found) return false;
            // Consume before discard callbacks can re-enter this continuation.
            // Keep even the first pick here: chooser != owner, and prompt callbacks must
            // observe the consumed receipt; later picks also depend on earlier discards.
            setMoukuiReceipts(room, offset.card, receipts);
            room->removePlayerMark(offset.to, "moukui" + offset.card->toString());
            room->broadcastSkillInvoke("moukui", 3);
            for (int i = 0; i < amount && target->isAlive() && offset.to->isAlive()
                 && offset.to->canDiscard(target, "he"); ++i) {
                const int id = room->askForCardChosen(offset.to, target, "he", "moukui", false, Card::MethodDiscard);
                if (!discardableMaterial(room, offset.to, target, id)) break;
                room->throwCard(id, "moukui", target, offset.to);
            }
        }
        return false;
    }
};

class Tianming : public TriggerSkillV2
{
public:
    Tianming(const QString &name = "tianming") : TriggerSkillV2(name) { events << TargetConfirming; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == TargetConfirming && player && player->isAlive() && slashUse(data.value<CardUseStruct>()))
            addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room && ctx.owner && ctx.owner->isAlive()
            && ctx.owner->askForSkillInvoke(objectName() + "$1");
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner) return false;
        const int amount = 2 * getEffectiveAmount(ctx);
        room->askForDiscard(ctx.owner, objectName(), amount, amount, false, true);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner) return false;
        if (ctx.owner->isAlive()) ctx.owner->drawCards(2 * getEffectiveAmount(ctx), objectName());
        int maxHp = -1;
        QList<ServerPlayer *> highest;
        for (ServerPlayer *p : room->getAllPlayers()) {
            if (p->getHp() > maxHp) { maxHp = p->getHp(); highest.clear(); }
            if (p->getHp() == maxHp) highest << p;
        }
        if (ctx.owner->getHp() == maxHp || highest.size() != 1) return false;
        ServerPlayer *target = highest.first();
        if (!target->askForSkillInvoke(objectName(), *ctx.original_data, false)) return false;
        ctx.targets = {target};
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ctx.owner->objectName(), target->objectName());
        room->broadcastSkillInvoke(objectName(), target->isFemale() ? 3 : 2);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!room || !ctx.owner || !target) return false;
        const int amount = 2 * getEffectiveAmount(ctx);
        room->askForDiscard(target, objectName(), amount, amount, false, true);
        if (target->isAlive()) target->drawCards(amount, objectName());
        return false;
    }
};

class XiaoxinTianming : public Tianming
{
public:
    XiaoxinTianming() : Tianming("xiaoxin_tianming") {}
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->isAlive() && ctx.owner->askForSkillInvoke(this); }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        if (ctx.owner->canDiscard(ctx.owner, "he")) {
            const int amount = getEffectiveAmount(ctx);
            room->askForDiscard(ctx.owner, objectName(), amount, amount, false, true);
        }
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.owner && ctx.owner->isAlive()) ctx.owner->drawCards(2 * getEffectiveAmount(ctx), objectName()); return false; }
};

class Mizhao : public ViewAsSkillV2
{
public:
    Mizhao(const QString &name = "mizhao") : ViewAsSkillV2(name) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng(); }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &) const override
    {
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "MizhaoCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target) return ContinueEffects;
        Room *room = source->getRoom();
        if (ctx.choice == "pindian") {
            ServerPlayer *first = room->findPlayerByObjectName(ctx.extra_data.toString());
            if (!first || !first->isAlive() || !first->canPindian(target)) return ContinueEffects;
            const bool previous = target->hasFlag("MizhaoPindianTarget");
            const auto restore = qScopeGuard([target, previous] {
                if (!previous) target->setFlags("-MizhaoPindianTarget");
            });
            target->setFlags("MizhaoPindianTarget");
            const int result = first->pindianInt(target, objectName());
            if (result == 0 && objectName() == "xiaoxin_mizhao") {
                for (ServerPlayer *other : QList<ServerPlayer *>{first, target}) {
                    if (!source->isAlive() || !other->isAlive() || !source->canGet(other, "he")) continue;
                    const int id = room->askForCardChosen(source, other, "he", objectName(), false, Card::MethodGet);
                    if (id >= 0 && room->getCardOwner(id) == other && source->canGet(other, id)
                        && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                        && !Sanguosha->getCard(id)->hasFlag("using"))
                        room->obtainCard(source, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, source->objectName(), other->objectName(), objectName(), QString()), false);
                }
                return ContinueEffects;
            }
            if (result == 0 || result == -2) return ContinueEffects;
            ServerPlayer *winner = result == 1 ? first : target;
            ServerPlayer *loser = result == 1 ? target : first;
            if (!winner->isAlive() || !loser->isAlive()) return ContinueEffects;
            auto *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_" + objectName());
            CardUseStruct use(slash, winner, loser);
            use.setOwnedCard(slash);
            if (winner->canSlash(loser, slash, false)) room->useCardFromSkillEffect(use, ctx);
            return ContinueEffects;
        }
        // Actor redirection does not substitute another player's hand as material.
        ServerPlayer *payer = ctx.initiator;
        if (!payer || payer->isKongcheng()) return ContinueEffects;
        DummyCard handcards;
        for (int id : payer->handCards()) {
            if (Sanguosha->getCard(id)->hasFlag("using") || !payer->canMove(id, target)) return ContinueEffects;
            handcards.addSubcard(id);
        }
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, payer->objectName(), objectName(), QString());
        room->obtainCard(target, &handcards, reason, false);
        if (!source->isAlive() || !target->isAlive() || target->isKongcheng()) return ContinueEffects;
        const int index = (target->getGeneralName().contains("liubei")
            || target->getGeneral2Name().contains("liubei")) ? 2 : 1;
        room->broadcastSkillInvoke(objectName(), index);
        QList<ServerPlayer *> targets;
        for (ServerPlayer *p : room->getOtherPlayers(target))
            if (target->canPindian(p) && (objectName() != "xiaoxin_mizhao" || p != source)) targets << p;
        if (targets.isEmpty()) return ContinueEffects;
        ServerPlayer *target2 = room->askForPlayerChosen(source, targets, objectName(),
                                                         "@mizhao-pindian:" + target->objectName());
        if (!target2 || !target2->isAlive() || !target->canPindian(target2)) return ContinueEffects;
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), target2->objectName());
        SkillContext duel = ctx;
        duel.choice = "pindian";
        duel.extra_data = target->objectName();
        duel.targets = {target2};
        skillEffect(duel, target2);
        return ContinueEffects;
    }
};

MizhaoCard::MizhaoCard() { setSkillName("mizhao"); mute = true; }
bool MizhaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select,
                              const Player *Self) const
{ return targets.isEmpty() && to_select != Self; }
void MizhaoCard::onEffect(CardEffectStruct &effect) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "mizhao", QString());
    Room *room = effect.from->getRoom();
    DummyCard *handcards = effect.from->wholeHandCards();
    room->obtainCard(effect.to, handcards, reason, false);
    if (effect.to->isKongcheng()) return;
    const int index = (effect.to->getGeneralName().contains("liubei")
        || effect.to->getGeneral2Name().contains("liubei")) ? 2 : 1;
    room->broadcastSkillInvoke("mizhao", index);
    QList<ServerPlayer *> targets;
    for (ServerPlayer *p : room->getOtherPlayers(effect.to))
        if (effect.to->canPindian(p)) targets << p;
    if (targets.isEmpty()) return;
    ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "mizhao",
                                                    "@mizhao-pindian:" + effect.to->objectName());
    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, effect.to->objectName(), target->objectName());
    int result;
    {
        const bool alreadyFlagged = target->hasFlag("MizhaoPindianTarget");
        const auto restoreFlag = qScopeGuard([target, alreadyFlagged] {
            if (!alreadyFlagged) target->setFlags("-MizhaoPindianTarget");
        });
        target->setFlags("MizhaoPindianTarget");
        result = effect.to->pindianInt(target, "mizhao");
    }
    if (result == 0 || result == -2) return;
    ServerPlayer *winner = result == 1 ? effect.to : target;
    ServerPlayer *loser = result == 1 ? target : effect.to;
    if (winner->isAlive()) {
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_mizhao");
        CardUseStruct use(slash, winner, loser);
        use.setOwnedCard(slash);
        if (winner->canSlash(loser, slash, false)) room->useCard(use);
    }
}

class Jieyuan : public TriggerSkillV2
{
public:
    Jieyuan() : TriggerSkillV2("jieyuan") { events << DamageCaused << DamageInflicted; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        const DamageStruct damage = data.value<DamageStruct>();
        const QString hand = player->getMark("jieyuan_renegade-Keep") > 0 ? "he" : "h";
        if (event == DamageCaused && damage.to && damage.to->isAlive() && damage.to != player
            && (damage.to->getHp() >= player->getHp() || player->getMark("jieyuan_rebel-Keep") > 0)
            && player->canDiscard(player, hand)) addAllInstances(result, player, objectName());
        else if (event == DamageInflicted && damage.from && damage.from->isAlive() && damage.from != player
            && (damage.from->getHp() >= player->getHp() || player->getMark("jieyuan_loyalist-Keep") > 0)
            && player->canDiscard(player, hand)) addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const bool renegade = ctx.owner->getMark("jieyuan_renegade-Keep") > 0;
        const QString pattern = renegade ? ".|.|.|he"
            : (event == DamageCaused ? ".|black|.|hand" : ".|red|.|hand");
        const QString prompt = renegade ? (event == DamageCaused ? "@jieyuan-increase2:" : "@jieyuan-decrease2:")
            + (event == DamageCaused ? damage.to->objectName() : damage.from->objectName())
            : (event == DamageCaused ? "@jieyuan-increase:" + damage.to->objectName() : "@jieyuan-decrease:" + damage.from->objectName());
        const Card *card = room->askForCard(ctx.owner, pattern, prompt, *ctx.original_data,
                                            Card::MethodNone, nullptr, false, objectName());
        const int id = card ? card->getEffectiveId() : -1;
        const bool inHandOrEquip = id >= 0 && (ctx.owner->handCards().contains(id) || ctx.owner->getEquipsId().contains(id));
        if (!card || card->isVirtualCard() || !inHandOrEquip || card->hasFlag("using") || !ctx.owner->canDiscard(id)) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner) return false;
        const int id = ctx.extra_data.toInt();
        if (!discardableMaterial(room, ctx.owner, ctx.owner, id)) return false;
        const Card *card = Sanguosha->getCard(id);
        if (!card) return false;
        if (ctx.owner->getMark("jieyuan_renegade-Keep") <= 0
            && (!ctx.owner->handCards().contains(id)
                || (event == DamageCaused ? !card->isBlack() : !card->isRed()))) return false;
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        if (!before.value("complete").toBool()) return false;
        const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
        room->throwCard(id, objectName(), ctx.owner, ctx.owner);
        return paidMaterial(room, skillEvent, before.value("watermark").toLongLong(), objectName(), ctx.owner, id);
    }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ctx.owner->peiyin(this, event == DamageCaused ? 1 : 2);
        const int amount = getEffectiveAmount(ctx);
        return ctx.owner->damageRevises(*ctx.original_data, event == DamageCaused ? amount : -amount);
    }
};

class Fenxin : public TriggerSkillV2
{
public:
    Fenxin() : TriggerSkillV2("fenxin") { events << BeforeGameOverJudge; frequency = Limited; limit_mark = "@burnheart"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != BeforeGameOverJudge || !room || !player || !isNormalGameMode(room->getMode())) return result;
        const DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        if (!killer || killer->isLord() || player->isLord() || player->getHp() > 0 || !killer->isAlive()
            || killer->getMark(limit_mark) <= 0 || !killer->hasSkill(objectName())) return result;
        addAllInstances(result, killer, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !isUsable(ctx)) return false;
        ServerPlayer *target = ctx.invoker;
        const bool previous = target->hasFlag("FenxinTarget");
        const auto restore = qScopeGuard([target, previous] {
            if (!previous) target->setFlags("-FenxinTarget");
        });
        target->setFlags("FenxinTarget");
        ctx.preferredTarget = target;
        return ctx.owner->askForSkillInvoke(objectName() + "$-1", QVariant::fromValue(target));
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !isUsable(ctx) || ctx.owner->getMark(limit_mark) <= 0) return false;
        // Commit before mark callbacks can re-enter this exact activation.
        addUsage(ctx);
        room->removePlayerMark(ctx.owner, limit_mark);
        return true;
    }
    bool allowsDeadTarget(const SkillContext &ctx, const ServerPlayer *target) const override
    { return target == ctx.preferredTarget; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.preferredTarget) return false;
        if (!ctx.owner->isAlive()) return false;
        ctx.manual_effect = true;
        ctx.targets = {ctx.preferredTarget};
        skillEffect(event, room, ctx.owner, ctx, ctx.preferredTarget);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || target != ctx.preferredTarget) return false;
        room->doSuperLightbox(ctx.owner, objectName());
        const QString role = ctx.owner->getRole();
        ctx.owner->setRole(target->getRole());
        room->notifyProperty(ctx.owner, ctx.owner, "role", target->getRole());
        room->setPlayerProperty(target, "role", role);
        return false;
    }
};

MixinCard::MixinCard() { setSkillName("mixin"); will_throw = false; mute = true; handling_method = Card::MethodNone; }
bool MixinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{ return targets.isEmpty() && to_select != Self; }
void MixinCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from, *target = effect.to;
    Room *room = source->getRoom();
    room->broadcastSkillInvoke("mixin", 1);
    room->obtainCard(target, this, CardMoveReason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "mixin", QString()), false);
    QList<ServerPlayer *> others;
    for (ServerPlayer *p : room->getOtherPlayers(target)) if (target->canSlash(p, false)) others << p;
    if (others.isEmpty()) return;
    ServerPlayer *target2 = room->askForPlayerChosen(source, others, "mixin");
    LogMessage log; log.type = "#CollateralSlash"; log.from = source; log.to << target2; room->sendLog(log);
    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), target2->objectName());
    if (room->askForUseSlashTo(target, target2, "#mixin", false)) room->broadcastSkillInvoke("mixin", 2);
    else {
        room->broadcastSkillInvoke("mixin", 3);
        QList<int> cards;
        for (int handId : target->handCards()) if (target2->canGet(target, handId)) cards << handId;
        if (cards.isEmpty()) return;
        room->fillAG(cards, target2);
        const int id = room->askForAG(target2, cards, false, objectName()); room->clearAG(target2);
        if (cards.contains(id))
            room->obtainCard(target2, Sanguosha->getCard(id),
                CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, target2->objectName(), target->objectName(), "mixin", QString()), false);
    }
}

class Mixin : public ViewAsSkillV2
{
public:
    Mixin() : ViewAsSkillV2("mixin", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && ownsCard(request.initiator, card) && request.selectedCardIds.isEmpty(); }
    bool willThrowSelectedCards() const override { return false; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return nullptr;
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        card->addSubcard(request.selectedCardIds.first());
        return card;
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MixinCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !ctx.use_card) return ContinueEffects;
        Room *room = source->getRoom();
        if (ctx.choice == "take") {
            ServerPlayer *giver = room->findPlayerByObjectName(ctx.extra_data.toString());
            if (!giver || !giver->isAlive() || !target->canGet(giver, "h")) return ContinueEffects;
            QList<int> cards;
            for (int handId : giver->handCards()) if (target->canGet(giver, handId)) cards << handId;
            room->fillAG(cards, target);
            const auto clear = qScopeGuard([room, target] { room->clearAG(target); });
            const int id = room->askForAG(target, cards, false, objectName());
            if (target->isAlive() && cards.contains(id) && room->getCardOwner(id) == giver
                && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using"))
                room->obtainCard(target, Sanguosha->getCard(id),
                    CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), giver->objectName(), objectName(), QString()), false);
            return ContinueEffects;
        }
        ServerPlayer *payer = ctx.initiator;
        if (!payer || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        if (!payer->handCards().contains(id) || Sanguosha->getCard(id)->hasFlag("using")
            || !payer->canMove(id, target)) return ContinueEffects;
        room->broadcastSkillInvoke(objectName(), 1);
        room->giveCard(payer, target, ctx.use_card, objectName());
        if (!source->isAlive() || !target->isAlive()) return ContinueEffects;
        QList<ServerPlayer *> others;
        for (ServerPlayer *p : room->getOtherPlayers(target))
            if (target->canSlash(p, false)) others << p;
        if (others.isEmpty()) return ContinueEffects;
        ServerPlayer *target2 = room->askForPlayerChosen(source, others, objectName());
        if (!target2 || !target2->isAlive() || !target->isAlive()) return ContinueEffects;
        LogMessage log;
        log.type = "#CollateralSlash";
        log.from = source;
        log.to << target2;
        room->sendLog(log);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), target2->objectName());
        if (room->askForUseSlashTo(target, target2, "#mixin", false))
            room->broadcastSkillInvoke(objectName(), 2);
        else {
            room->broadcastSkillInvoke(objectName(), 3);
            if (!target2->isAlive() || !target2->canGet(target, "h")) return ContinueEffects;
            SkillContext take = ctx;
            take.choice = "take";
            take.extra_data = target->objectName();
            take.targets = {target2};
            skillEffect(take, target2);
        }
        return ContinueEffects;
    }
};

class Cangni : public TriggerSkillV2
{
public:
    Cangni() : TriggerSkillV2("cangni") { events << EventPhaseStart << CardsMoveOneTime; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return result;
        if (event == EventPhaseStart && player->getPhase() == Player::Discard) addAllInstances(result, player, objectName());
        else if (event == CardsMoveOneTime && !player->faceUp() && room->hasCurrent()
                 && room->getCurrent() != player) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            const bool relevant = (move.from == player && move.to != player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)))
                || (move.to == player && move.from != player && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip));
            if (relevant) addAllInstances(result, player, objectName());
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.original_data) return false;
        if (event == EventPhaseStart) {
            if (!ctx.owner->askForSkillInvoke(objectName() + "$1")) return false;
            QStringList choices{"draw"}; if (ctx.owner->isWounded()) choices << "recover";
            ctx.choice = choices.size() == 1 ? choices.first() : room->askForChoice(ctx.owner, objectName(), choices.join("+"),
                QVariant(), QString(), ":" + objectName()); return true;
        }
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        ServerPlayer *current = room->getCurrent(); if (!current || current->isDead() || current == ctx.owner) return false;
        if (move.from == ctx.owner && move.to != ctx.owner) {
            bool relevant = false; for (Player::Place place : move.from_places) relevant |= place == Player::PlaceHand || place == Player::PlaceEquip;
            if (!relevant || current->isNude() || !ctx.owner->askForSkillInvoke(objectName() + "$3", "cangnilose")) return false;
            ctx.choice = "lose";
        } else if (move.to == ctx.owner && move.from != ctx.owner && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip)
                   && isUsable(ctx) && ctx.owner->askForSkillInvoke(objectName() + "$2", "cangniget")) ctx.choice = "get";
        else return false;
        ctx.targets = {current}; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice != "get") return true;
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    void payBypassed(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.choice == "get") addUsage(ctx); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner) return false;
        if (event == EventPhaseStart) {
            const int amount = getEffectiveAmount(ctx);
            if (ctx.choice == "recover") room->recover(ctx.owner, RecoverStruct(ctx.owner, nullptr, amount, objectName()));
            else ctx.owner->drawCards(amount * 2, objectName());
            if (ctx.owner->isAlive()) ctx.owner->turnOver();
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *current) const override
    {
        if (!room || !ctx.owner || !current) return false;
        const int amount = getEffectiveAmount(ctx);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ctx.owner->objectName(), current->objectName());
        if (ctx.choice == "lose")
            room->askForDiscard(current, objectName(), amount, amount, false, true);
        else {
            room->addPlayerMark(current, "cangni_used-Clear");
            current->drawCards(amount, objectName());
        }
        return false;
    }
};

DuyiCard::DuyiCard() { setSkillName("duyi"); target_fixed = true; mute = true; }
void DuyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    const QList<int> ids = room->getNCards(1); if (ids.isEmpty()) return; const int id = ids.first(); room->fillAG(ids, nullptr); room->getThread()->delay();
    ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), "duyi"); const Card *card = Sanguosha->getCard(id); room->obtainCard(target, card, QString("duyi")); room->clearAG();
    if (card->isBlack()) { room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", false); room->addPlayerMark(target, "duyi_target"); LogMessage log; log.type = "#duyi_eff"; log.from = source; log.to << target; log.arg = "duyi"; room->sendLog(log); room->broadcastSkillInvoke("duyi", 1); }
    else room->broadcastSkillInvoke("duyi", 2);
}

class DuyiViewAs : public ViewAsSkillV2
{
public:
    DuyiViewAs() : ViewAsSkillV2("duyi") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &) const override
    {
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "DuyiCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source) return FinishSkill;
        Room *room = source->getRoom();
        const int amount = getEffectiveAmount(ctx);
        ctx.manual_effect = true;
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return FinishSkill;
        const qint64 serial = room->getTag("DuyiNextSerial").toLongLong() + 1;
        room->setTag("DuyiNextSerial", serial);
        const QVariantMap pending{{"serial", serial}, {"turn", turn}};
        QVariantList pendingEffects = room->getTag("DuyiPendingEffects").toList();
        pendingEffects << pending;
        room->setTag("DuyiPendingEffects", pendingEffects);
        const auto finishEffect = qScopeGuard([room, pending] {
            QVariantList remaining = room->getTag("DuyiPendingEffects").toList();
            remaining.removeAll(pending);
            room->setTag("DuyiPendingEffects", remaining);
        });
        for (int i = 0; i < amount && source->isAlive()
             && room->getTag("DuyiPendingEffects").toList().contains(pending); ++i) {
            const QList<int> ids = room->getNCards(1);
            if (ids.isEmpty()) break;
            const int id = ids.first();
            const auto finishReveal = qScopeGuard([room, id] {
                room->clearAG();
                // A cancelled target effect must not strand the card removed by getNCards().
                if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id))
                    room->returnToTopDrawPile({id});
            });
            room->fillAG(ids, nullptr);
            room->getThread()->delay();
            ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), objectName());
            if (!target) break;
            ctx.targets = {target};
            ctx.extra_data = QVariantMap{{"card", id}, {"pending", pending}};
            if (skillEffect(ctx, target) == FinishSkill) break;
        }
        return FinishSkill;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target) return ContinueEffects;
        Room *room = source->getRoom();
        const QVariantMap selection = ctx.extra_data.toMap();
        const QVariantMap pending = selection.value("pending").toMap();
        const int id = selection.value("card", -1).toInt();
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        // The revealed card is temporarily outside the draw-pile list. Never reclaim it
        // from another move made during selection or the target hook.
        if (!card || card->hasFlag("using") || room->getCardPlace(id) != Player::DrawPile
            || room->getDrawPile().contains(id)
            || !room->getTag("DuyiPendingEffects").toList().contains(pending)) return ContinueEffects;
        const bool black = card->isBlack();
        const qint64 serial = room->getTag("DuyiNextSerial").toLongLong() + 1;
        room->setTag("DuyiNextSerial", serial);
        const QString reason = objectName() + ":" + QString::number(serial);
        const QVariantMap receipt{{"serial", serial}, {"turn", pending.value("turn")}, {"reason", reason},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
            {"actor", source->objectName()}, {"applied", false}};
        if (black) {
            QVariantList receipts = target->getTag("DuyiReceipts").toList();
            receipts << receipt;
            target->setTag("DuyiReceipts", receipts);
        }
        bool committed = false;
        const auto finishPending = qScopeGuard([target, receipt, &committed] {
            if (committed) return;
            QVariantList receipts = target->getTag("DuyiReceipts").toList();
            receipts.removeAll(receipt);
            target->setTag("DuyiReceipts", receipts);
        });
        room->obtainCard(target, card, objectName());
        if (black) {
            QVariantList receipts = target->getTag("DuyiReceipts").toList();
            const int index = receipts.indexOf(receipt);
            // Expiry may have consumed the pending receipt inside the obtain callback.
            if (!target->isAlive() || index < 0
                || !room->getTag("DuyiPendingEffects").toList().contains(pending)) return ContinueEffects;
            QVariantMap applied = receipt;
            applied["applied"] = true;
            receipts[index] = applied;
            target->setTag("DuyiReceipts", receipts);
            committed = true;
            room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", false, reason);
            room->addPlayerMark(target, "duyi_target");
            LogMessage log;
            log.type = "#duyi_eff";
            log.from = source;
            log.to << target;
            log.arg = objectName();
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName(), 1);
        } else {
            room->broadcastSkillInvoke(objectName(), 2);
        }
        return ContinueEffects;
    }
};

class Duyi : public TriggerSkillV2
{
public:
    Duyi() : TriggerSkillV2("duyi") { view_as_skill = new DuyiViewAs; events << EventPhaseChanging << TurnBroken << Death; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!room || !player) return false;
        if (event == Death && data.value<DeathStruct>().who != player) return false;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        if (event != Death && event != EventPhaseChanging && event != TurnBroken) return false;
        const QVariant turn = room->historyScopes().value("turn_id");
        if (event != Death) {
            QVariantList remaining;
            for (const QVariant &value : room->getTag("DuyiPendingEffects").toList())
                if (value.toMap().value("turn") != turn) remaining << value;
            room->setTag("DuyiPendingEffects", remaining);
        }
        // Receipts survive source retirement; unrelated deaths and nested turns do not expire them.
        for (ServerPlayer *p : room->getAllPlayers(true)) {
            QVariantList kept, expired;
            int active = 0;
            for (const QVariant &value : p->getTag("DuyiReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                const bool expires = event == Death ? p == player : receipt.value("turn") == turn;
                if (expires) expired << value;
                else { kept << value; active += receipt.value("applied").toBool() ? 1 : 0; }
            }
            if (expired.isEmpty()) continue;
            p->setTag("DuyiReceipts", kept);
            bool removed = false;
            for (const QVariant &value : expired) {
                const QVariantMap receipt = value.toMap();
                if (!receipt.value("applied").toBool()) continue;
                room->removePlayerCardLimitationByReason(p, receipt.value("reason").toString());
                removed = true;
            }
            if (!removed) continue;
            room->setPlayerMark(p, "duyi_target", active);
            LogMessage log;
            log.type = "#duyi_clear";
            log.from = p;
            log.arg = objectName();
            room->sendLog(log);
        }
        return true;
    }
};

class Duanzhi : public TriggerSkillV2
{
public:
    Duanzhi() : TriggerSkillV2("duanzhi") { events << TargetConfirmed; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result; const CardUseStruct use = data.value<CardUseStruct>();
        if (event == TargetConfirmed && player && player->isAlive() && use.card && use.card->getTypeId() != Card::TypeSkill && use.from && use.from != player && use.to.contains(player)) addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data))
            return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || !use.from->isAlive()) return false;
        // The card user loses cards; the skill owner pays HP after that effect.
        ctx.targets = {use.from};
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override
    {
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!room || !ctx.original_data || !target || !target->isAlive()
            || !ctx.invoker || !ctx.invoker->isAlive()
            || !ctx.owner || !ctx.owner->isAlive()
            || !isSourceAvailable(room, ctx))
            return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (target != use.from) return false;
        DummyCard dummy;
        const int amount = getEffectiveAmount(ctx);
        for (int i = 0; i < 2 * amount && target->getCardCount() > i && ctx.owner->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false,
                                                  Card::MethodDiscard, dummy.getSubcards(), i > 0);
            if (!discardableMaterial(room, ctx.owner, target, id) || dummy.getSubcards().contains(id)) break;
            dummy.addSubcard(id);
        }
        // Later selection callbacks can move an earlier selection out of the legal zone.
        DummyCard payable;
        for (int id : dummy.getSubcards())
            if (discardableMaterial(room, ctx.owner, target, id)) payable.addSubcard(id);
        if (payable.subcardsLength() > 0)
            room->throwCard(&payable, objectName(), target, ctx.owner);
        return false;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner) return false;
        ctx.manual_effect = true;
        for (ServerPlayer *target : ctx.targets) {
            SkillContext discard = ctx;
            skillEffect(event, room, ctx.owner, discard, target);
        }
        if (ctx.owner->isAlive())
            room->loseHp(HpLostStruct(ctx.owner, getEffectiveAmount(ctx), objectName(), ctx.owner));
        return false;
    }
};

class Fengyin : public TriggerSkillV2
{
public:
    Fengyin() : TriggerSkillV2("fengyin") { events << EventPhaseChanging; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result; if (event != EventPhaseChanging || !room || !player || data.value<PhaseChangeStruct>().to != Player::Start) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player)) if (owner->isAlive() && owner->getHp() <= player->getHp()) addAllInstances(result, owner, objectName()); return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.invoker) return false;
        const Card *card = room->askForCard(ctx.owner, "Slash|.|.|hand", "@fengyin", *ctx.original_data,
                                            Card::MethodNone, nullptr, false, objectName());
        if (!card || card->isVirtualCard() || !ownsCard(ctx.owner, card)) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets = {ctx.invoker};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || ctx.targets.size() != 1) return false;
        const int id = ctx.extra_data.toInt();
        ServerPlayer *receiver = ctx.targets.first();
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (!receiver || !receiver->isAlive() || !card || !card->isKindOf("Slash")
            || !ownsCard(ctx.owner, card) || !ctx.owner->canMove(id, receiver)) return false;
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        if (!before.value("complete").toBool()) return false;
        const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
        room->giveCard(ctx.owner, receiver, card, objectName(), true);
        return paidMaterial(room, skillEvent, before.value("watermark").toLongLong(), objectName(), ctx.owner, id, receiver);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.invoker) return false;
        ctx.owner->skillInvoked(this);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ctx.owner->objectName(), ctx.invoker->objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!room || !ctx.owner || !target) return false;
        // Transfer is the activation cost. A waived cost must not be paid again here.
        target->skip(Player::Play);
        target->skip(Player::Discard);
        return false;
    }
};

class ChizhongMaxCards : public MaxCardsSkillV2
{
public:
    ChizhongMaxCards() : MaxCardsSkillV2("#chizhong") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &) const override { return CorrectSkillResult::noEffect(); }
    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override { return ctx.holder && ctx.holder->hasSkill("chizhong") ? CorrectSkillResult::useAmount(ctx.holder->getMaxHp()) : CorrectSkillResult::noEffect(); }
};

class Chizhong : public TriggerSkillV2
{
public:
    Chizhong() : TriggerSkillV2("chizhong") { events << Death; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Death || !room || !player) return result;
        const DeathStruct death = data.value<DeathStruct>();
        // Death is already broadcast seat by seat; preserve that trigger ordering.
        if (death.who != player) addAllInstances(result, player, objectName());
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.owner->isAlive()) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        room->gainMaxHp(ctx.owner, getEffectiveAmount(ctx), objectName());
        return false;
    }
};

AssassinsPackage::AssassinsPackage() : Package("assassins")
{
    General *fuhuanghou = new General(this, "as_fuhuanghou", "qun", 3, false); fuhuanghou->addSkill(new Mixin); fuhuanghou->addSkill(new Cangni);
    General *jiben = new General(this, "as_jiben", "qun", 3); jiben->addSkill(new Duyi); jiben->addSkill(new Duanzhi);
    General *fuwan = new General(this, "as_fuwan", "qun", 3); fuwan->addSkill(new Fengyin); fuwan->addSkill(new Chizhong); fuwan->addSkill(new ChizhongMaxCards); related_skills.insert("chizhong", "#chizhong");
    General *mushun = new General(this, "as_mushun", "qun"); mushun->addSkill(new Moukui);
    skills << new Moukui(true);
    related_skills.insert("moukui", "#moukui-effect");
    General *hanxiandi = new General(this, "as_liuxie", "qun", 3); hanxiandi->addSkill(new Tianming); hanxiandi->addSkill(new Mizhao);
    General *xiaoxin_liuxie = new General(this, "xiaoxin_liuxie", "qun", 3);
    xiaoxin_liuxie->setImage("as_liuxie");
    xiaoxin_liuxie->addSkill(new XiaoxinTianming);
    xiaoxin_liuxie->addSkill("xiaoxin_mizhao");
    General *lingju = new General(this, "as_lingju", "qun", 3, false); lingju->addSkill(new Jieyuan); lingju->addSkill(new Fenxin);
    skills << new Mizhao("xiaoxin_mizhao");
    addMetaObject<MizhaoCard>(); addMetaObject<MixinCard>(); addMetaObject<DuyiCard>();
}
ADD_PACKAGE(Assassins)
