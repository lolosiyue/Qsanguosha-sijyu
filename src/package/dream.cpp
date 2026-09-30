#include "dream.h"
#include "qt-collection-utils.h"
#include "skill-instance-utils.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include <QScopeGuard>

// Applied-effect identity is independent of optional history and Trigger execution IDs.
static qint64 nextDreamReceipt(Room *room)
{
    const qint64 serial = room->getTag("DreamV2ReceiptSerial").toLongLong() + 1;
    room->setTag("DreamV2ReceiptSerial", serial);
    return serial;
}
// Primitive provenance survives the accepted instance retiring during nested effects.
static QVariantMap dreamReceipt(Room *room, const SkillContext &ctx)
{
    return {{"holder", ctx.owner ? ctx.owner->objectName() : QString()},
        {"actor", ctx.initiator ? ctx.initiator->objectName() : ctx.owner ? ctx.owner->objectName() : QString()},
        {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
        {"source_instance", ctx.sourceRef.key.instanceID},
        {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
        {"activation_instance", ctx.activationRef.key.instanceID}, {"serial", nextDreamReceipt(room)},
        {"amount", ctx.hasModifiedAmount() ? ctx.modified_amount : ctx.amount}};
}
static SkillInstanceRef dreamSource(const QVariantMap &receipt)
{
    return SkillInstanceRef(receipt.value("source_owner").toString(),
        SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
}
static SkillInstanceRef dreamActivation(const QVariantMap &receipt)
{
    return SkillInstanceRef(receipt.value("activation_owner").toString(),
        SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_instance").toInt()));
}
static SkillContext dreamContinuation(Room *room, const QString &name, TriggerEvent event,
    ServerPlayer *actor, QVariant &data, const QVariantMap &receipt)
{
    SkillContext ctx;
    ctx.skill_name = name;
    ctx.owner = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
    ctx.invoker = actor;
    ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
    ctx.sourceRef = dreamSource(receipt);
    ctx.instanceID = dreamActivation(receipt).key.instanceID;
    ctx.trigger_count = receipt.value("serial").toInt();
    ctx.amount = receipt.value("amount", 1).toInt();
    ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true;
    return ctx;
}
static bool dreamMoved(Room *room, qint64 after, qint64 cause, int card, const QString &from,
    const QString &to, Player::Place place)
{
    if (cause <= 0) return false;
    QVariantMap query{{"after", after}};
    bool found = false;
    for (;;) {
        const QVariantMap page = room->queryHistoryMoves(query);
        if (!page.value("complete").toBool() || page.contains("error")) return false;
        for (const QVariant &entry : page.value("items").toList()) {
            const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
            if (move.value("card_id", -1).toInt() == card && move.value("from").toString() == from
                && move.value("to").toString() == to && move.value("to_place").toInt() == place
                && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause) found = true;
        }
        if (!page.value("has_more").toBool()) break;
        query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
    }
    return found;
}
static void addInstances(TriggerList &result, ServerPlayer *owner, const QString &skill)
{
    if (!owner || !owner->isAlive()) return;
    for (int id : owner->getValidSkillInstanceIds(skill))
        result[owner] << SkillInstanceUtils::formatName(skill, id);
}
static bool ownsHand(const Player *player, const Card *card)
{
    if (!player || !card || card->isEquipped() || card->hasFlag("using")) return false;
    const int id = card->getEffectiveId();
    return id >= 0 && player->handCards().contains(id);
}

static bool selectionAccepted(const ViewAsSkillV2 *skill, const ActiveSkillRequest &request)
{
    if (!skill || request.selectedCardIds.size() != skill->getN()) return false;
    ActiveSkillRequest prefix = request;
    prefix.selectedCardIds.clear();
    for (int id : request.selectedCardIds) {
        const Card *card = id >= 0 && id < Sanguosha->getCardCount() ? Sanguosha->getCard(id) : nullptr;
        if (!card || !skill->canSelectCard(prefix, card)) return false;
        prefix.selectedCardIds << id;
    }
    return true;
}

class IfAnxu : public TriggerSkillV2
{
public:
    IfAnxu() : TriggerSkillV2("ifanxu") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(objectName()))
            for (int id : player->getValidSkillInstanceIds(objectName()))
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(objectName())) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "recover") {
            room->recover(target, RecoverStruct(objectName(), ctx.owner, amount));
            return false;
        }
        if (ctx.choice == "lose-hp") {
            room->loseHp(HpLostStruct(target, amount, objectName(), ctx.owner));
            return false;
        }
        target->drawCards(2 * amount, objectName());
        if (!target->isAlive()) return false;
        const Card *discarded = room->askForDiscard(target, objectName(), 2 * amount, 2 * amount, false, true);
        if (!discarded) return false;
        SkillContext followup = ctx;
        // The follow-up recipient gets its own hook; no owner-wide receipt is needed.
        if (discarded->getSuit() == Card::NoSuit) {
            followup.choice = "recover";
            followup.targets = {target};
            skillEffect(event, room, ctx.owner, followup, target);
        } else if (discarded->isBlack() && ctx.owner->isAlive()) {
            ServerPlayer *victim = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "ifanxu0");
            if (victim) {
                room->doAnimate(1, ctx.owner->objectName(), victim->objectName());
                followup.choice = "lose-hp";
                followup.targets = {victim};
                skillEffect(event, room, ctx.owner, followup, victim);
            }
        }
        return false;
    }
};
class IfMishouViewAs : public ViewAsSkillV2
{
public:
    IfMishouViewAs() : ViewAsSkillV2("ifmishou", 1) {}

    bool willThrowSelectedCards() const override { return false; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@ifmishou"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsHand(request.initiator, card) && request.selectedCardIds.isEmpty();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return selectionAccepted(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return selected.isEmpty() && target && target->isAlive() && target != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !target || !ctx.use_card) return ContinueEffects;
        Room *room = target->getRoom();
        const QList<int> material = ctx.use_card->getSubcards();
        if (material.size() != 1 || room->getCardOwner(material.first()) != ctx.initiator
            || room->getCardPlace(material.first()) != Player::PlaceHand || Sanguosha->getCard(material.first())->hasFlag("using")) return ContinueEffects;
        const SkillInstanceRef source = ctx.sourceRef;
        QVariantList receipts = target->getTag("IfMishouReceipts").toList();
        receipts << QVariantMap{{"owner", source.ownerObjectName}, {"skill", source.key.skillName},
            {"instance", source.key.instanceID}, {"holder", ctx.owner->objectName()},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
            {"activation_instance", ctx.activationRef.key.instanceID}, {"event", target->getRoom()->currentHistoryEventId()},
                {"serial", nextDreamReceipt(target->getRoom())}, {"captured", false}};
        target->setTag("IfMishouReceipts", receipts);
        room->addPlayerMark(target, "ifmishouBf-SelfClear");
        if (room->getCardOwner(material.first()) == ctx.initiator && room->getCardPlace(material.first()) == Player::PlaceHand
            && !Sanguosha->getCard(material.first())->hasFlag("using")) room->giveCard(ctx.initiator, target, ctx.use_card, objectName());
        return ContinueEffects;
    }
};

class IfMishou : public TriggerSkillV2
{
public:
    IfMishou() : TriggerSkillV2("ifmishou")
    {
        events << TargetSpecified << DamageCaused << EventPhaseStart << EventPhaseChanging << TurnBroken;
        global = true;
        view_as_skill = new IfMishouViewAs;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // The gift's duration belongs to its recipient and survives source removal.
        if (player && (event == TurnBroken || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)))
            player->removeTag("IfMishouReceipts");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event == EventPhaseStart) return false;
        if (!player || !player->isAlive() || (event == EventPhaseChanging || event == TurnBroken)) return true;
        const Card *card = event == TargetSpecified ? data.value<CardUseStruct>().card : data.value<DamageStruct>().card;
        if (!card || !card->isKindOf("Slash")) return true;
        for (const QVariant &value : player->getTag("IfMishouReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (event == TargetSpecified && receipt.value("captured").toBool()) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("activation_instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.extra_data = value;
            ctx.trigger_count = receipt.value("serial").toInt();
            ctx.preferredTarget = player;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.is_forced = true; // Continue the already applied receipt without a second optional activation.
            ctx.targets = event == DamageCaused ? QList<ServerPlayer *>{data.value<DamageStruct>().to}
                : QList<ServerPlayer *>{player};
            out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.invoker) return false;
        const QVariantList receipts = ctx.invoker->getTag("IfMishouReceipts").toList();
        // Admission keeps the pre-cost context. Consuming its first-Slash opportunity
        // must not retire the accepted receipt (nor its ongoing damage conversion).
        QVariantMap consumed = ctx.extra_data.toMap();
        consumed.insert("captured", true);
        return receipts.contains(ctx.extra_data) || receipts.contains(consumed);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->getPhase() == Player::Finish
            && player->getHandcardNum() > 0) addInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) ctx.targets = {ctx.owner};
        if (event == TargetSpecified) {
            QVariantList receipts = ctx.invoker->getTag("IfMishouReceipts").toList();
            const int index = receipts.indexOf(ctx.extra_data);
            if (index < 0 || receipts.at(index).toMap().value("captured").toBool()) return false;
            // This is the next Slash, even if WillInvoke/Effect or every target is
            // canceled. It is an opportunity receipt, not a bypassable payment.
            QVariantMap receipt = receipts.at(index).toMap(); receipt.insert("captured", true);
            receipts[index] = receipt; ctx.invoker->setTag("IfMishouReceipts", receipts);
            ctx.extra_data = receipt;
            ctx.targets = ctx.original_data->value<CardUseStruct>().to;
        }
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (event == EventPhaseStart) {
            if (target->isKongcheng()) return false;
            Room::AcceptedViewAsEffectScope continuation(room, target, objectName(), ctx);
            if (continuation.isValid()) room->askForUseCard(target, "@@ifmishou", "ifmishou0");
            return false;
        }
        if (event == TargetSpecified) {
            room->addPlayerMark(ctx.invoker, target->objectName() + "ifmishouBfto-SelfClear");
            return false;
        }
        if (event != DamageCaused) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target || !damage.card || !damage.card->isKindOf("Slash")) return false;
        room->loseHp(target, damage.damage, true, damage.from, damage.card->objectName());
        return true;
    }
};
class IfMishouBf : public TargetModSkillV2
{
public:
    IfMishouBf() : TargetModSkillV2("#IfMishouBf", ".")
    {
        setHolderSelector(CorrectSkill_System);
        setBaseAmount(999);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.card || ctx.currentAmount <= 0) return CorrectSkillResult::noEffect();
        if (ctx.modType == TargetModSkill::DistanceLimit)
            return ctx.card->getSkillName() == "ifshenfeng"
                ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
        if (ctx.modType != TargetModSkill::Residue) return CorrectSkillResult::noEffect();
        if (ctx.card->getSkillName() == "ifshenfeng" && ctx.card->getTag("IfShenfengAllTypes").toBool())
            return CorrectSkillResult::unlimitedResidue();
        const int material = ctx.card->getEffectiveId();
        if (material >= 0 && ctx.primary->handCards().contains(material)
            && Sanguosha->getCard(material)->hasTip("ifpiyong")) return CorrectSkillResult::unlimitedResidue();
        if (ctx.primary->getMark("&ifdianbian-Clear") > 0)
            return CorrectSkillResult::unlimitedResidue();
        if (!ctx.card->isKindOf("Slash")) return CorrectSkillResult::noEffect();
        if (ctx.secondary && ctx.primary->getMark(ctx.secondary->objectName() + "ifmishouBfto-SelfClear") > 0)
            return CorrectSkillResult::unlimitedResidue();
        const int extra = ctx.primary->getMark("ifxiechangUp");
        return extra > 0 ? CorrectSkillResult::useAmount(extra) : CorrectSkillResult::noEffect();
    }
};

class IfDianbianViewAs : public ViewAsSkillV2
{
public:
    IfDianbianViewAs() : ViewAsSkillV2("ifdianbian") {}
    LimitScope getLimitScope() const override { return Limit_Game; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    static bool latestDeathLostHp(Room *room)
    {
        QVariantMap query{{"kind", "death"}}, latest;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool() || page.contains("error")) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap();
                if (fact.value("id").toLongLong() > latest.value("id").toLongLong()) latest = fact;
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        // A newer legacy row with no cause is unknown; never skip it for an older eligible death.
        return latest.value("data").toMap().value("cause_kind").toString() == "hp_lost";
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        if (const auto *server = qobject_cast<const ServerPlayer *>(request.initiator)) return latestDeathLostHp(server->getRoom());
        return true; // The server admits from immutable death history, not an owner-wide observer flag.
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (!card || !request.initiator) return card;
        QVariantList losses;
        const SkillInstance *activation = request.initiator->findSkillInstance(request.activationRef.key.skillName, request.activationRef.key.instanceID);
        if (activation && activation->bindHead != 0) for (int id : request.initiator->getSkillInstanceIds("ifanxu")) {
            const SkillInstance *candidate = request.initiator->findSkillInstance("ifanxu", id);
            if (candidate && candidate->bindHead == activation->bindHead) losses << id;
        }
        // Admission freezes relations even if a later effect hook retires the original grant.
        card->setTag("IfDianbianLosses", losses);
        return card;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!latestDeathLostHp(room)) return false;
        room->removePlayerMark(ctx.initiator, "@ifdianbian"); return true;
    }
    EffectFlow effect(SkillContext &ctx) const override { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || !ctx.use_card) return ContinueEffects;
        Room *room = target->getRoom();
        room->doSuperLightbox(ctx.initiator, objectName());
        QVariantMap receipt = dreamReceipt(room, ctx);
        receipt.insert("recipient", target->objectName());
        receipt.insert("turn", room->historyScopes().value("turn_id"));
        receipt.insert("losses", ctx.use_card->getTag("IfDianbianLosses"));
        QVariantList receipts = room->getTag("IfDianbianApplied").toList();
        receipts << receipt; room->setTag("IfDianbianApplied", receipts);
        room->setPlayerMark(target, "&ifdianbian-Clear", 1);
        QMap<int, QVariantMap> latest;
        QVariantMap query;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool() || page.contains("error")) return ContinueEffects;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap value = entry.toMap().value("data").toMap();
                latest.insert(value.value("card_id", -1).toInt(), value);
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        DummyCard obtained;
        for (int id : room->getDiscardPile()) {
            const QVariantMap move = latest.value(id);
            const Card *card = Sanguosha->getCard(id);
            if (move.value("from").toString() == target->objectName() && move.value("to_place").toInt() == Player::DiscardPile
                && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
                && (card->isKindOf("Slash") || card->getTypeId() == Card::TypeEquip) && !card->hasFlag("using")) obtained.addSubcard(id);
        }
        if (obtained.subcardsLength()) room->obtainCard(target, &obtained, objectName());
        return ContinueEffects;
    }
};
class IfDianbian : public TriggerSkillV2
{
public:
    IfDianbian(const QString &name = "ifdianbian") : TriggerSkillV2(name)
    {
        if (name == "ifdianbian") {
            limit_mark = "@ifdianbian"; waked_skills = "ifpiyong"; frequency = Limited;
            view_as_skill = new IfDianbianViewAs;
        } else { events << Death << EventPhaseChanging << TurnBroken << EventSkillEffectFinished; global = true; frequency = Compulsory; }
    }
    static void consume(Room *room, const QVariant &receipt)
    { QVariantList receipts = room->getTag("IfDianbianApplied").toList(); receipts.removeAll(receipt); room->setTag("IfDianbianApplied", receipts); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && ctx.choice == "end") consume(room, ctx.extra_data);
        } else if (event == TurnBroken || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            for (const QVariant &entry : room->getTag("IfDianbianApplied").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("turn").toLongLong() != turn) continue;
                ServerPlayer *target = room->findPlayerByObjectName(receipt.value("recipient").toString(), true);
                // A dead recipient cannot receive the upgrade; expiry still consumes this turn's receipt.
                if (event == TurnBroken || !target || !target->isAlive()) consume(room, entry);
            }
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != Death && (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)) return true;
        if (event == Death && actor != data.value<DeathStruct>().who) return true;
        for (const QVariant &entry : room->getTag("IfDianbianApplied").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("turn").toLongLong() != room->historyScopes().value("turn_id").toLongLong()) continue;
            SkillContext ctx = dreamContinuation(room, objectName(), event, actor, data, receipt);
            ServerPlayer *recipient = room->findPlayerByObjectName(receipt.value("recipient").toString(), true);
            if (!recipient || !recipient->isAlive() || !ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.choice = event == Death ? "grow" : "end";
            ctx.targets = {recipient}; out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.sourceRef.isValid() && room->getTag("IfDianbianApplied").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.choice == "end") consume(room, ctx.extra_data); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "grow") { room->gainMaxHp(target, getEffectiveAmount(ctx), "ifdianbian"); return false; }
        int highest = 0;
        for (ServerPlayer *alive : room->getAlivePlayers()) highest = qMax(highest, alive->getMaxHp());
        if (target->getMaxHp() < highest) return false;
        for (const QVariant &id : ctx.extra_data.toMap().value("losses").toList())
            room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName("ifanxu", id.toInt()), false, false);
        SkillContext accepted = ctx; accepted.activationRef = dreamActivation(ctx.extra_data.toMap());
        room->acquireSkillFromEffect(target, "ifpiyong", accepted);
        return false;
    }
};
class IfPiyong : public ViewAsSkillV2
{
public:
    IfPiyong() : ViewAsSkillV2("ifpiyong$", 1) {}
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->hasLordSkill(objectName()); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && card->isKindOf("Weapon") && request.selectedCardIds.isEmpty()
            && card->getEffectiveId() >= 0 && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return selectionAccepted(this, request); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        const Weapon *weapon = qobject_cast<const Weapon *>(material->getRealCard());
        if (!weapon) return nullptr;
        const Card *created = ViewAsSkillV2::createCard(request);
        SkillCard *card = const_cast<SkillCard *>(qobject_cast<const SkillCard *>(created));
        if (card) card->setUserString(QString::number(weapon->getRange()));
        return created;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "IfPiyongCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !ctx.use_card || !cardSelectionFeasible(request)) return false;
        LogMessage log; log.type = "$RecastCard"; log.from = ctx.initiator; log.card_str = ListI2S(ctx.use_card->getSubcards()).join("+");
        room->sendLog(log);
        const CardMoveReason reason(CardMoveReason::S_REASON_RECAST, ctx.initiator->objectName(), objectName(), "");
        room->moveCardTo(ctx.use_card, nullptr, Player::DiscardPile, reason, true);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const SkillCard *card = qobject_cast<const SkillCard *>(ctx.use_card);
        const int amount = getEffectiveAmount(ctx);
        if (!card || amount <= 0) return ContinueEffects;
        const int range = card->getUserString().toInt();
        target->drawCards(amount, "recast");
        Room *room = target->getRoom();
        const qint64 cause = room->currentHistoryEventId();
        const qint64 frontier = room->queryHistoryMoves({{"limit", 1}}).value("watermark").toLongLong();
        const QList<int> submitted = target->drawCardsList(range * amount, objectName());
        QVariantMap query{{"after", frontier}};
        QMap<int, QVariantMap> lastMoves;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool() || page.contains("error") || cause <= 0) return ContinueEffects;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap();
                lastMoves.insert(fact.value("data").toMap().value("card_id", -1).toInt(), fact);
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        for (int id : submitted) {
            const QVariantMap fact = lastMoves.value(id), move = fact.value("data").toMap();
            const QVariantMap moved = room->historyEvent(fact.value("event_id").toLongLong());
            const QVariantMap draw = room->historyEvent(moved.value("parent_id").toLongLong());
            if (draw.value("kind").toString() != "draw" || draw.value("parent_id").toLongLong() != cause
                || draw.value("data").toMap().value("reason").toString() != objectName()
                || move.value("from_place").toInt() != Player::DrawPile || move.value("to_place").toInt() != Player::PlaceHand
                || move.value("to").toString() != target->objectName() || !target->handCards().contains(id)) continue;
            // A later loss/regain is a different last move and cannot regain this draw's modifier.
            room->setCardFlag(id, "cardTip:" + objectName(), target);
        }
        return ContinueEffects;
    }
};
class IfXiance : public TriggerSkillV2
{
public:
    IfXiance() : TriggerSkillV2("ifxiance")
    { events << EventPhaseChanging << TargetSpecified << CardUsed << CardResponded << DamageInflicted << CardFinished; frequency = Compulsory; global = true; }
    static bool sameActivation(const QVariantMap &value, const SkillContext &ctx)
    { return value.value("activation_owner").toString() == ctx.activationRef.ownerObjectName
        && value.value("activation_skill").toString() == ctx.activationRef.key.skillName
        && value.value("activation_instance").toInt() == ctx.activationRef.key.instanceID; }
    static void project(Room *room, ServerPlayer *target)
    {
        int total = 0;
        for (const QVariant &entry : room->getTag("IfXianceTokens").toList()) {
            const QVariantMap value = entry.toMap();
            if (value.value("holder").toString() == target->objectName()) total += value.value("points").toInt();
        }
        room->setPlayerMark(target, "&ifxian_ce", total);
    }
    static bool firstDamage(Room *room, ServerPlayer *target)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return false;
        const QVariantMap page = room->queryActualDamage({{"turn_id", turn}, {"to", target->objectName()}, {"limit", 1}});
        return page.value("complete").toBool() && page.value("items").toList().isEmpty();
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) if (const Card *card = data.value<CardUseStruct>().card) card->removeTag("IfXianceResponses");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event == TargetSpecified || event == DamageInflicted) return false;
        if (event == CardFinished) return true;
        QVariantList receipts;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
            receipts = room->getTag("IfXianceTokens").toList();
        } else {
            const Card *card = event == CardUsed ? data.value<CardUseStruct>().whocard : data.value<CardResponseStruct>().m_toCard;
            if (card) receipts = card->tag.value("IfXianceResponses").toList();
        }
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            if (event == EventPhaseChanging && receipt.value("points").toInt() <= 0) continue;
            ServerPlayer *source = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ServerPlayer *recipient = event == EventPhaseChanging ? room->findPlayerByObjectName(receipt.value("holder").toString(), true) : player;
            if (!source || !recipient || (event != EventPhaseChanging && !recipient->isAlive())) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("holder").toString(), true); ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(source->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = receipt.value("activation_instance").toInt(); ctx.amount = event == EventPhaseChanging ? 1 : receipt.value("amount").toInt();
            ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true;
            ctx.trigger_count = receipt.value("serial").toInt(); ctx.preferredTarget = recipient; ctx.targets = {recipient}; out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.sourceRef.isValid() || !ctx.original_data) return false;
        if (ctx.original_data->canConvert<PhaseChangeStruct>()) return room->getTag("IfXianceTokens").toList().contains(ctx.extra_data);
        const Card *card = ctx.original_data->canConvert<CardResponseStruct>() ? ctx.original_data->value<CardResponseStruct>().m_toCard
            : ctx.original_data->value<CardUseStruct>().whocard;
        return card && card->getTag("IfXianceResponses").toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill && !use.to.contains(player)) addInstances(result, player, objectName());
        } else if (event == DamageInflicted && data.value<DamageStruct>().to == player && firstDamage(room, player)) addInstances(result, player, objectName());
        return result;
    }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (event == TargetSpecified || event == DamageInflicted) ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == CardUsed || event == CardResponded) {
            room->loseHp(target, amount, true, ctx.owner, objectName()); return false;
        }
        QVariantList tokens = room->getTag("IfXianceTokens").toList();
        if (event == EventPhaseChanging) {
            const qint64 serial = ctx.extra_data.toMap().value("serial").toLongLong();
            for (int i = 0; i < tokens.size(); ++i) {
                QVariantMap value = tokens.at(i).toMap();
                if (value.value("serial").toLongLong() != serial) continue;
                value.insert("points", qMax(0, value.value("points").toInt() - amount)); tokens[i] = value; break;
            }
            room->setTag("IfXianceTokens", tokens); project(room, target); return false;
        }
        if (event == DamageInflicted) {
            if (!firstDamage(room, target)) return false;
            int points = 0;
            for (const QVariant &entry : tokens) if (sameActivation(entry.toMap(), ctx)) points += entry.toMap().value("points").toInt();
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            return target->damageRevises(*ctx.original_data, damage.damage * (points * amount - 1));
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.to.contains(target)) return false;
        QVariantMap receipt{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"holder", target->objectName()},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
            {"amount", amount}, {"serial", nextDreamReceipt(room)}};
        QVariantList responses = use.card->tag.value("IfXianceResponses").toList(); responses << receipt;
        use.card->tag.insert("IfXianceResponses", responses);
        bool found = false;
        for (int i = 0; i < tokens.size(); ++i) {
            QVariantMap value = tokens.at(i).toMap();
            if (!sameActivation(value, ctx)) continue;
            value.insert("points", value.value("points").toInt() + use.to.size() * amount); tokens[i] = value; found = true; break;
        }
        if (!found) { receipt.insert("points", use.to.size() * amount); tokens << receipt; }
        room->setTag("IfXianceTokens", tokens); project(room, target);
        return false;
    }
};
class IfZhenshi : public TriggerSkillV2
{
public:
    IfZhenshi() : TriggerSkillV2("ifzhenshi")
    {
        events << EventPhaseStart << TargetConfirmed << AskForRetrial;
        global = true;
        waked_skills = "ifjiusuo";
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::RoundStart) return false;
        QVariantList keep, expired;
        for (const QVariant &value : room->getTag("IfZhenshiReceipts").toList()) {
            if (value.toMap().value("holder").toString() == player->objectName()) expired << value;
            else keep << value;
        }
        // Remove receipts before callbacks; detach only the exact acquired grants.
        room->setTag("IfZhenshiReceipts", keep);
        for (const QVariant &value : expired) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            const int id = receipt.value("grant").toInt();
            if (target && id > 0)
                room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName("ifjiusuo", id), false, true);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != TargetConfirmed) return false;
        if (!player || !player->isAlive()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isDamageCard() || !use.to.contains(player)) return true;
        for (const QVariant &value : room->getTag("IfZhenshiReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("target").toString() != player->objectName() || receipt.value("grant").toInt() != 0) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("activation_instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill", objectName()).toString(), receipt.value("instance").toInt()));
            ctx.extra_data = value;
            ctx.trigger_count = receipt.value("serial").toInt();
            ctx.preferredTarget = player;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.is_forced = true; // Continue the already applied receipt without a second optional activation.
            ctx.targets = {player};
            out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : room->getTag("IfZhenshiReceipts").toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->getPhase() == Player::Finish)
            addInstances(result, player, objectName());
        else if (event == AskForRetrial) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge && judge->reason == "ifjiusuo")
                for (ServerPlayer *holder : room->getAlivePlayers())
                    if (!holder->isKongcheng()) addInstances(result, holder, objectName());
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == TargetConfirmed) return true;
        if (event == AskForRetrial) {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (!judge) return false;
            // Select first; retrial owns the response movement after the target hook.
            const Card *card = room->askForCard(ctx.owner, ".|.|.|hand", "ifzhenshi1", *ctx.original_data,
                Card::MethodNone, judge->who, true, objectName());
            if (!card) return false;
            ctx.extra_data = card->getEffectiveId();
            ctx.targets = {judge->who};
            return true;
        }
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (turn <= 0) return false;
        QSet<int> entered;
        QVariantMap query{{"turn_id", turn}};
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap();
                if (!move.contains("to_place") || !move.contains("card_id")) return false;
                if (move.value("to_place").toInt() == Player::DiscardPile) entered.insert(move.value("card_id").toInt());
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark"));
            query.insert("after", page.value("next_after"));
        }
        QSet<int> suits;
        for (int id : room->getDiscardPile())
            if (entered.contains(id)) suits.insert(Sanguosha->getCard(id)->getSuit());
        const int maximum = suits.size() * getEffectiveAmount(ctx);
        if (maximum <= 0) return false;
        ctx.targets = room->askForPlayersChosen(ctx.owner, room->getAlivePlayers(), objectName(), 0, maximum,
            QString("ifzhenshi0:%1").arg(maximum));
        return !ctx.targets.isEmpty();
    }
    bool pay(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != AskForRetrial) return true;
        const int id = ctx.extra_data.toInt();
        return ctx.owner && ctx.owner->handCards().contains(id)
            && !Sanguosha->getCard(id)->hasFlag("using")
            && !ctx.owner->isCardLimited(Sanguosha->getCard(id), Card::MethodResponse);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (event == AskForRetrial) {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            const int id = ctx.extra_data.toInt();
            if (judge && judge->who == target && ctx.owner->handCards().contains(id) && !Sanguosha->getCard(id)->hasFlag("using"))
                room->retrial(Sanguosha->getCard(id), ctx.owner, judge, objectName());
            return false;
        }
        QVariantList receipts = room->getTag("IfZhenshiReceipts").toList();
        if (event == TargetConfirmed) {
            const int index = receipts.indexOf(ctx.extra_data);
            if (index < 0) return false;
            // Reserve before acquisition callbacks to prevent a nested duplicate grant.
            QVariantMap receipt = receipts.at(index).toMap();
            receipt.insert("grant", -1);
            receipts[index] = receipt;
            room->setTag("IfZhenshiReceipts", receipts);
            SkillContext accepted = ctx;
            accepted.activationRef = dreamActivation(receipt);
            const int id = room->acquireSkillFromEffect(target, "ifjiusuo", accepted, [&](int committedId) {
                QVariantList committed = room->getTag("IfZhenshiReceipts").toList();
                const int position = committed.indexOf(receipt);
                if (position >= 0) {
                    receipt.insert("grant", committedId); committed[position] = receipt;
                    room->setTag("IfZhenshiReceipts", committed);
                }
            });
            receipts = room->getTag("IfZhenshiReceipts").toList();
            const int reserved = receipts.indexOf(receipt);
            if (reserved >= 0) {
                receipt.insert("grant", id);
                receipts[reserved] = receipt;
                room->setTag("IfZhenshiReceipts", receipts);
            } else if (id > 0) {
                room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName("ifjiusuo", id), false, true);
            }
        } else {
            receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                {"holder", ctx.owner->objectName()}, {"activation_owner", ctx.activationRef.ownerObjectName},
                {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
                {"target", target->objectName()}, {"event", room->currentHistoryEventId()},
                {"serial", nextDreamReceipt(target->getRoom())}, {"grant", 0}};
            room->setTag("IfZhenshiReceipts", receipts);
        }
        return false;
    }
};
class IfJiusuo : public TriggerSkillV2
{
public:
    IfJiusuo() : TriggerSkillV2("ifjiusuo")
    {
        events << TargetConfirmed;
        frequency = Skill::Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !use.card || !use.card->isDamageCard() || !use.from || use.from == player
            || !use.to.contains(player)) return result;
        for (ServerPlayer *target : use.to) {
            if (!target->hasSkill(objectName(), true)) return result;
        }
        addInstances(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card && use.from && use.to.contains(ctx.owner)) ctx.targets = {use.from};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "lose-hp") {
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
            return false;
        }
        if (ctx.choice == "obtain") {
            ServerPlayer *from = room->findPlayerByObjectName(ctx.extra_data.toString());
            if (!from) return false;
            for (int i = 0; i < getEffectiveAmount(ctx) && from->isAlive() && target->isAlive()
                && target->canGet(from, "he"); ++i) {
                const int id = room->askForCardChosen(target, from, "he", objectName(), false, Card::MethodGet);
                if (id >= 0 && room->getCardOwner(id) == from && !Sanguosha->getCard(id)->hasFlag("using") && target->canGet(from, id))
                    room->obtainCard(target, id, false);
            }
            return false;
        }
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        JudgeStruct judge;
        judge.reason = objectName();
        judge.who = target;
        judge.pattern = ".|.|0~8";
        judge.good = false;
        judge.negative = true;
        room->judge(judge);
        if (!judge.isBad() || !judge.card) return false;
        // Re-read after retrial callbacks so other modifications are preserved.
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains("_ALL_TARGETS")) use.nullified_list << "_ALL_TARGETS";
        ctx.original_data->setValue(use);
        const int number = judge.card->getNumber();
        if (target->isAlive() && number < target->getHp()) {
            SkillContext loss = ctx;
            loss.choice = "lose-hp";
            skillEffect(event, room, player, loss, target);
        }
        if (target->isAlive() && number < target->getCardCount()) {
            SkillContext obtain = ctx;
            obtain.choice = "obtain";
            obtain.extra_data = target->objectName();
            skillEffect(event, room, player, obtain, ctx.owner);
        }
        return false;
    }
};
class IfYinglveViewAs : public ViewAsSkillV2
{
public:
    IfYinglveViewAs() : ViewAsSkillV2("ifyinglve", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool willThrowSelectedCards() const override { return false; }
    SkillDialogInfo getDialogInfo() const override
    { return SkillDialogInfo::juguan(objectName(), "fire_slash,fire_attack"); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.activationRef.isValid()) return false;
        const bool prompt = request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "accepted_view_as_effect").toBool();
        if (request.pattern == "@@ifyinglve") return prompt && request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
        return !prompt && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return ownsHand(request.initiator, card) && request.selectedCardIds.isEmpty(); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return selectionAccepted(this, request); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        QString name = request.userString;
        if (request.pattern == "@@ifyinglve") name = request.initiator->getSkillInstanceStateValue(
            objectName(), request.activationRef.key.instanceID, "card_name").toString();
        else if (name.isEmpty()) {
            const Card *declared = request.initiator->getTag(objectName()).value<const Card *>();
            if (declared) name = declared->objectName();
        }
        if (name != "fire_slash" && name != "fire_attack") return nullptr;
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return nullptr;
        card->setSkillName(objectName()); card->addSubcards(request.selectedCardIds);
        card->setTag("IfYinglveSecondary", request.pattern == "@@ifyinglve");
        return card;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || !ctx.initiator || ctx.use_card->subcardsLength() != 1) return FinishSkill;
        Room *room = ctx.initiator->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand
            || Sanguosha->getCard(id)->hasFlag("using")) return FinishSkill;
        ctx.use_card->setTag("IfYinglveApplied", dreamReceipt(room, ctx));
        return ContinueEffects;
    }
};
class IfYinglve : public TriggerSkillV2
{
public:
    IfYinglve() : TriggerSkillV2("ifyinglve")
    {
        events << TargetConfirmed << CardFinished << EventSkillEffectFinished;
        global = true; frequency = Compulsory; view_as_skill = new IfYinglveViewAs;
    }
    SkillDialogInfo getDialogInfo() const override { return view_as_skill->getDialogInfo(); }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && ctx.choice == "finish" && ctx.original_data) {
                const Card *card = ctx.original_data->value<CardUseStruct>().card;
                if (card) card->removeTag("IfYinglveApplied");
            }
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != TargetConfirmed && event != CardFinished) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.from) return true;
        const QVariantMap receipt = use.card->getTag("IfYinglveApplied").toMap();
        if (receipt.isEmpty() || !dreamSource(receipt).isValid()) return true;
        SkillContext ctx = dreamContinuation(room, objectName(), event, actor, data, receipt);
        if (!ctx.owner || !ctx.initiator) return true;
        if (event == TargetConfirmed) {
            if (!actor || !actor->isAlive() || !use.to.contains(actor) || !actor->canDiscard(actor, "he")) return true;
            ctx.targets = {actor}; ctx.preferredTarget = actor;
        } else {
            if (use.card->getTag("IfYinglveSecondary").toBool()) { use.card->removeTag("IfYinglveApplied"); return true; }
            const QVariantMap damage = room->queryCardUseDamage(use.targetModReveal.useHistoryEventId);
            if (!damage.value("complete").toBool() || damage.contains("error") || !damage.value("items").toList().isEmpty()) {
                use.card->removeTag("IfYinglveApplied"); return true;
            }
            if (!ctx.initiator->isAlive()) return true;
            ctx.choice = "finish";
        }
        out << ctx; return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        const Card *card = ctx.original_data ? ctx.original_data->value<CardUseStruct>().card : nullptr;
        return ctx.sourceRef.isValid() && card && card->getTag("IfYinglveApplied").toMap() == ctx.extra_data.toMap();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != CardFinished) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        use.card->removeTag("IfYinglveApplied");
        if (use.card->getSubcards().isEmpty()) return false;
        for (int id : use.card->getSubcards()) if (room->getCardPlace(id) != Player::PlaceTable) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.initiator, room->getOtherPlayers(ctx.initiator), objectName(),
            "ifyinglve1:" + use.card->objectName(), true, true);
        if (target) ctx.targets = {target};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetConfirmed) {
            if (target->canDiscard(target, "he") && room->askForDiscard(target, objectName(), 2, 2, true, true,
                "ifyinglve0:" + use.card->objectName())) {
                // Re-read after callbacks so another target's changes are retained.
                use = ctx.original_data->value<CardUseStruct>();
                if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
                *ctx.original_data = QVariant::fromValue(use);
            }
            return false;
        }
        for (int id : use.card->getSubcards()) if (room->getCardPlace(id) != Player::PlaceTable) return false;
        room->obtainCard(target, use.card, objectName());
        SkillContext draw = ctx; draw.choice = "draw";
        skillEffect(event, room, ctx.owner, draw, ctx.initiator);
        if (!target->isAlive() || target->isKongcheng()) return false;
        SkillContext accepted = ctx; accepted.activationRef = dreamActivation(ctx.extra_data.toMap());
        Room::AcceptedViewAsEffectScope scope(room, target, objectName(), accepted);
        if (!scope.isValid()) return false;
        const QString name = use.card->objectName() == "fire_slash" ? "fire_attack" : "fire_slash";
        target->setSkillInstanceStateValue(objectName(), scope.activationRef().key.instanceID, "card_name", name);
        const QVariant previous = target->property("ifyinglveCn");
        const auto restore = qScopeGuard([&] { room->setPlayerProperty(target, "ifyinglveCn", previous); });
        room->setPlayerProperty(target, "ifyinglveCn", name); // Scoped legacy AI hint only.
        room->askForUseCard(target, "@@ifyinglve", "ifyinglve2:" + name);
        return false;
    }
};
class IfBihe : public TriggerSkillV2
{
public:
    IfBihe() : TriggerSkillV2("ifbihe") { events << TargetSpecifying << ConfirmDamage << Damage << CardFinished; global = true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) if (const Card *card = data.value<CardUseStruct>().card) card->removeTag("IfBiheApplied");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event == TargetSpecifying) return false;
        if (event == CardFinished) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.to) return true;
        const QVariantList receipts = damage.card->tag.value("IfBiheApplied").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            ServerPlayer *source = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
            if (!source || !holder || (event == Damage && !holder->isAlive())) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = holder; ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(source->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = receipt.value("activation_instance").toInt(); ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true;
            ctx.trigger_count = receipt.value("serial").toInt(); ctx.preferredTarget = event == ConfirmDamage ? damage.to : holder; ctx.targets = {ctx.preferredTarget};
            out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ctx.original_data->value<DamageStruct>().card : nullptr;
        return ctx.sourceRef.isValid() && card && card->getTag("IfBiheApplied").toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecifying) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isDamageCard() || use.to.size() != 1) return result;
        for (ServerPlayer *holder : room->getAlivePlayers())
            if (holder->hasFlag("CurrentPlayer") && holder->canDiscard(holder, "he")) addInstances(result, holder, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != TargetSpecifying) return true;
        const Card *selected = room->askForExchange(ctx.owner, objectName(), 2, 2, true, "ifbihe0", true);
        if (!selected || selected->subcardsLength() != 2) return false;
        QVariantList ids;
        for (int id : selected->getSubcards()) {
            if (!ctx.owner->canDiscard(ctx.owner, id) || Sanguosha->getCard(id)->hasFlag("using")) return false;
            ids << id;
        }
        ctx.extra_data = ids;
        ctx.choice = selected->isBlack() ? "black" : selected->isRed() ? "red" : "mixed";
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != TargetSpecifying) return true;
        DummyCard selected;
        for (const QVariant &entry : ctx.extra_data.toList()) {
            const int id = entry.toInt();
            if (selected.getSubcards().contains(id) || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
            selected.addSubcard(id);
        }
        if (selected.subcardsLength() != 2) return false;
        room->throwCard(&selected, ctx.owner, ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != TargetSpecifying) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from) return false;
        if (ctx.choice == "mixed") {
            ServerPlayer *extra = room->askForPlayerChosen(ctx.owner, room->getCardTargets(use.from, use.card, use.to), objectName(),
                "ifbihe1:" + use.card->objectName(), true);
            if (extra) ctx.targets = {extra};
        } else ctx.targets = use.to;
        Q_UNUSED(owner);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == ConfirmDamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (!damage.from || damage.to != target) return false;
            const int desired = qMin(qAbs(damage.from->getHandcardNum() - target->getHandcardNum()), 5) * amount;
            return target->damageRevises(*ctx.original_data, desired - damage.damage);
        }
        if (event == Damage) {
            const int draw = target->getMaxHp() - target->getHandcardNum();
            if (draw > 0) target->drawCards(draw, objectName());
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        if (ctx.choice == "black") {
            if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
        } else if (ctx.choice == "red") {
            QVariantList receipts = use.card->tag.value("IfBiheApplied").toList();
            receipts << QVariantMap{{"holder", ctx.owner->objectName()}, {"owner", ctx.sourceRef.ownerObjectName},
                {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
                {"amount", amount}, {"serial", nextDreamReceipt(room)}};
            use.card->tag.insert("IfBiheApplied", receipts);
        } else if (!use.to.contains(target)) {
            use.to << target; room->sortByActionOrder(use.to); use.extra_use += amount;
        }
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};
class IfShiji : public ViewAsSkillV2
{
public:
    IfShiji() : ViewAsSkillV2("ifshiji", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override { return ctx.initiator ? qMax(0, ctx.initiator->getHp()) : 0; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "IfShijiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return ownsHand(request.initiator, card) && request.selectedCardIds.isEmpty(); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return selectionAccepted(this, request); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return selected.isEmpty() && target && target->isAlive() && target != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return ContinueEffects; }
        if (ctx.choice == "recover") {
            target->getRoom()->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
            return ContinueEffects;
        }
        ServerPlayer *source = ctx.invoker;
        if (!source || !ctx.initiator || !ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        Room *room = source->getRoom();
        const int given = ctx.use_card->getSubcards().first();
        const Card *first = Sanguosha->getCard(given);
        if (room->getCardOwner(given) != ctx.initiator || room->getCardPlace(given) != Player::PlaceHand
            || first->hasFlag("using")) return ContinueEffects;
        const Card::Color color = first->getColor();
        const Card::CardType type = first->getTypeId();
        room->giveCard(ctx.initiator, target, ctx.use_card, objectName(), true);
        if (!source->isAlive() || !target->isAlive() || !source->canDiscard(target, "h")) return ContinueEffects;
        const int discarded = room->askForCardChosen(source, target, "h", objectName(), false, Card::MethodDiscard);
        if (discarded < 0 || room->getCardOwner(discarded) != target || room->getCardPlace(discarded) != Player::PlaceHand
            || !source->canDiscard(target, discarded) || Sanguosha->getCard(discarded)->hasFlag("using")) return ContinueEffects;
        const Card *second = Sanguosha->getCard(discarded);
        const bool recover = color != second->getColor(), draw = type == second->getTypeId();
        room->throwCard(discarded, objectName(), target, source);
        if (recover) { SkillContext child = ctx; child.choice = "recover"; skillEffect(child, target); }
        if (draw) for (ServerPlayer *recipient : {source, target}) {
            SkillContext child = ctx; child.choice = "draw"; skillEffect(child, recipient);
        }
        return ContinueEffects;
    }
};
IfAnjieCard::IfAnjieCard() { setSkillName("ifanjie"); }

bool IfAnjieCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (user_string.isEmpty())
        return targets.isEmpty() && to_select != Self && Self->getMark("ifanjieBf" + to_select->objectName()) > 0;
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    if (!card) return false;
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool IfAnjieCard::targetFixed() const
{
    if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) return true;
    if (user_string.isEmpty()) return false;
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    if (!card) return false;
    card->deleteLater();
    return card->targetFixed();
}

bool IfAnjieCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (user_string.isEmpty()) return !targets.isEmpty();
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    if (!card) return false;
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *IfAnjieCard::validateInResponse(ServerPlayer *) const { return nullptr; }
const Card *IfAnjieCard::validate(CardUseStruct &) const { return nullptr; }

class IfAnjieViewAs : public ViewAsSkillV2
{
public:
    IfAnjieViewAs() : ViewAsSkillV2("ifanjie") {}
    bool willThrowSelectedCards() const override { return false; }
    static QString banKey(int id) { return QString("ifanjie_%1_lun").arg(id); }
    static QStringList recovered(Room *room, const Player *player)
    {
        QStringList result;
        QVariantMap query{{"kind", "actual_recover"}, {"from", player->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool() || page.contains("error")) return {};
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap value = entry.toMap().value("data").toMap();
                if (value.value("amount").toInt() > 0) result << value.value("to").toString();
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        result.removeDuplicates(); return result;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.activationRef.isValid()) return false;
        const bool prompt = request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID,
            "accepted_view_as_effect").toBool();
        if (request.pattern == "@@ifanjie") return prompt;
        return !prompt && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && request.initiator->getMark(banKey(request.activationRef.key.instanceID)) == 0;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!target || !target->isAlive() || target == request.initiator || !selected.isEmpty()) return false;
        const auto *server = qobject_cast<const ServerPlayer *>(request.initiator);
        return !server || recovered(server->getRoom(), request.initiator).contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return nullptr;
        Card *card = nullptr;
        if (request.pattern == "@@ifanjie") {
            const QVariantMap chosen = request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "selection").toMap();
            const int id = chosen.value("id", -1).toInt();
            if (id < 0 || id >= Sanguosha->getCardCount()) return nullptr;
            const Card *physical = Sanguosha->getCard(id);
            card = Sanguosha->cloneCard(physical->objectName(), physical->getSuit(), physical->getNumber());
            if (card) { card->addSubcard(id); card->setTag("IfAnjieSelection", chosen); }
        } else if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            card = const_cast<Card *>(ViewAsSkillV2::createCard(request));
        } else {
            for (const QString &name : request.pattern.split('+')) {
                card = Sanguosha->cloneCard(name);
                if (card && card->getTypeId() != Card::TypeSkill) break;
                delete card; card = nullptr;
            }
        }
        if (card) {
            card->setSkillName(objectName());
            card->setTag("IfAnjieRequest", QVariantMap{{"pattern", request.pattern}, {"reason", int(request.reason)}});
        }
        return card;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.invoker || !ctx.use_card) return FinishSkill;
        Room *room = ctx.initiator->getRoom();
        const QVariantMap request = ctx.use_card->getTag("IfAnjieRequest").toMap();
        const bool play = ctx.use_card->getTypeId() == Card::TypeSkill;
        if (request.value("pattern").toString() != "@@ifanjie") {
            ServerPlayer *provider = play ? ctx.targets.value(0) : nullptr;
            const QStringList names = recovered(room, ctx.initiator);
            if (!play) {
                QList<ServerPlayer *> candidates;
                for (ServerPlayer *other : room->getOtherPlayers(ctx.initiator)) if (names.contains(other->objectName())) candidates << other;
                if (!candidates.isEmpty()) provider = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "ifanjie0", true, true);
            }
            if (!provider || !names.contains(provider->objectName())) {
                room->setPlayerMark(ctx.initiator, banKey(ctx.activationRef.key.instanceID), 1);
                return FinishSkill;
            }
            ctx.choice = "view";
            skillEffect(ctx, provider);
            const QVariantMap chosen = ctx.extra_data.toMap();
            const int id = chosen.value("id", -1).toInt();
            if (id < 0) { room->setPlayerMark(ctx.initiator, banKey(ctx.activationRef.key.instanceID), 1); return FinishSkill; }
            if (play) {
                Room::AcceptedViewAsEffectScope scope(room, ctx.initiator, objectName(), ctx);
                if (!scope.isValid()) return FinishSkill;
                ctx.initiator->setSkillInstanceStateValue(objectName(), scope.activationRef().key.instanceID, "selection", chosen);
                const int previous = ctx.initiator->getMark("ifanjieId");
                const auto restore = qScopeGuard([&] { room->setPlayerMark(ctx.initiator, "ifanjieId", previous); });
                room->setPlayerMark(ctx.initiator, "ifanjieId", id);
                if (!room->askForUseCard(ctx.initiator, "@@ifanjie", "ifanjie0:" + Sanguosha->getCard(id)->objectName()))
                    room->setPlayerMark(ctx.initiator, banKey(ctx.activationRef.key.instanceID), 1);
                return FinishSkill;
            }
            const Card *physical = Sanguosha->getCard(id);
            Card *replacement = Sanguosha->cloneCard(physical->objectName(), physical->getSuit(), physical->getNumber());
            if (!replacement) return FinishSkill;
            replacement->addSubcard(id); replacement->setSkillName(objectName()); replacement->setTag("IfAnjieSelection", chosen);
            replacement->deleteLater(); ctx.updated_card = replacement;
        }
        const Card *selected = ctx.updated_card ? ctx.updated_card : ctx.use_card;
        const QVariantMap selection = selected->getTag("IfAnjieSelection").toMap();
        ServerPlayer *provider = room->findPlayerByObjectName(selection.value("provider").toString(), true);
        const int id = selection.value("id", -1).toInt();
        if (!provider || id < 0 || selected->subcardsLength() != 1 || selected->getSubcards().first() != id
            || room->getCardOwner(id) != provider || room->getCardPlace(id) != Player::PlaceHand
            || Sanguosha->getCard(id)->hasFlag("using")) return FinishSkill;
        QVariantMap receipt = dreamReceipt(room, ctx); receipt.insert("provider", provider->objectName());
        selected->setTag("IfAnjieApplied", receipt);
        if (ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>(); use.m_isOwnerUse = false; *ctx.original_data = QVariant::fromValue(use);
        }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const QVariantMap request = ctx.use_card->getTag("IfAnjieRequest").toMap();
        const bool play = ctx.use_card->getTypeId() == Card::TypeSkill;
        QList<int> ids;
        for (const Card *card : target->getHandcards())
            if (!card->hasFlag("using") && !ctx.invoker->isCardLimited(card, Card::MethodUse)
                && (play ? card->isAvailable(ctx.invoker) : Sanguosha->matchPattern(request.value("pattern").toString(), ctx.invoker, card))) ids << card->getEffectiveId();
        const int id = room->doGongxin(ctx.invoker, target, ids, objectName());
        ctx.extra_data = QVariantMap{{"id", ids.contains(id) && room->getCardOwner(id) == target
            && room->getCardPlace(id) == Player::PlaceHand ? id : -1}, {"provider", target->objectName()}};
        return ContinueEffects;
    }
};
class IfAnjie : public TriggerSkillV2
{
public:
    IfAnjie() : TriggerSkillV2("ifanjie")
    { events << CardUsed << CardResponded << EventSkillEffectFinished; global = true; frequency = Compulsory; view_as_skill = new IfAnjieViewAs; }
    static const Card *usedCard(const SkillContext &ctx)
    {
        if (!ctx.original_data) return nullptr;
        return ctx.original_data->canConvert<CardResponseStruct>() ? ctx.original_data->value<CardResponseStruct>().m_card
            : ctx.original_data->value<CardUseStruct>().card;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && !ctx.activationRef.isValid()) if (const Card *card = usedCard(ctx)) card->removeTag("IfAnjieApplied");
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &out) const override
    {
        if (event != CardUsed && event != CardResponded) return true;
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        if (!card) return true;
        const QVariantMap receipt = card->getTag("IfAnjieApplied").toMap();
        if (receipt.isEmpty() || !dreamSource(receipt).isValid()) return true;
        ServerPlayer *provider = room->findPlayerByObjectName(receipt.value("provider").toString(), true);
        SkillContext ctx = dreamContinuation(room, objectName(), event, actor, data, receipt);
        if (provider && provider->isAlive() && ctx.owner) { ctx.targets = {provider}; out << ctx; }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { const Card *card = usedCard(ctx); return card && ctx.sourceRef.isValid() && card->getTag("IfAnjieApplied").toMap() == ctx.extra_data.toMap(); }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (const Card *card = usedCard(ctx)) card->removeTag("IfAnjieApplied"); return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};
class IfLitian : public TriggerSkillV2
{
public:
    IfLitian() : TriggerSkillV2("iflitian") { events << EventPhaseStart; waked_skills = "ifhuangchu,_iffeileijia"; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->getPhase() == Player::Start) addInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        // Freeze co-located native relations before invoke/reveal callbacks can retire the source.
        QVariantList anjie;
        const SkillInstance *original = ctx.owner->findSkillInstance(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        if (original && original->bindHead != 0) for (int id : ctx.owner->getSkillInstanceIds("ifanjie")) {
            const SkillInstance *candidate = ctx.owner->findSkillInstance("ifanjie", id);
            if (candidate && candidate->bindHead == original->bindHead) anjie << id;
        }
        ctx.extra_data = anjie;
        return ctx.owner->askForSkillInvoke(objectName());
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QList<int> ids = room->getNCards(3 * amount);
        if (ids.isEmpty()) return false;
        QList<int> pending = ids;
        const auto cleanup = qScopeGuard([&] {
            room->clearAG(target);
            QList<int> remaining;
            for (int id : pending) if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile) remaining << id;
            if (!remaining.isEmpty()) room->returnToTopDrawPile(remaining);
        });
        room->fillAG(ids, target);
        const int id = room->askForAG(target, ids, false, objectName());
        room->clearAG(target);
        room->returnToTopDrawPile(ids); pending.clear();
        if (!ids.contains(id) || room->getCardPlace(id) != Player::DrawPile) return false;
        const Card *card = Sanguosha->getCard(id);
        const bool spade = card->getSuit() == Card::Spade;
        room->moveCardTo(card, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), ""), true);
        if (room->getCardPlace(id) == Player::PlaceTable) room->obtainCard(target, id, true);
        if (!spade) return false;
        // Actual recovery facts include recoveries before this grant was acquired.
        QVariantMap query{{"kind", "actual_recover"}, {"from", target->objectName()}};
        QStringList healed;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool() || page.contains("error")) { healed.clear(); break; }
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap value = entry.toMap().value("data").toMap();
                const QString name = value.value("to").toString();
                if (value.value("amount").toInt() > 0 && !healed.contains(name)) healed << name;
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        for (ServerPlayer *recipient : room->getAlivePlayers()) {
            if (!healed.contains(recipient->objectName())) continue;
            SkillContext child = ctx; child.choice = "draw";
            skillEffect(event, room, owner, child, recipient);
        }
        // Replace only the original activation; another same-name grant remains independent.
        const QVariantList anjie = ctx.extra_data.toList();
        room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID), false, false, false);
        room->acquireSkillFromEffect(target, "iflitian2", ctx, [&](int committedId) {
            target->setSkillInstanceStateValue("iflitian2", committedId, "anjie_instances", anjie);
        }, true, true, false);
        return false;
    }
};
class IfLitian2 : public TriggerSkillV2
{
public:
    IfLitian2() : TriggerSkillV2("iflitian2")
    {
        events << EventPhaseStart << TargetSpecified << TargetConfirmed << PreHpRecover << CardFinished;
        shiming_skill = true; frequency = Compulsory; global = true; waked_skills = "ifhuangchu,_iffeileijia";
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) if (const Card *card = data.value<CardUseStruct>().card) card->removeTag("IfLitianRecovery");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event == CardFinished) return true;
        if (event != PreHpRecover) return false;
        const RecoverStruct recover = data.value<RecoverStruct>();
        if (!recover.card || !player) return true;
        const QVariantList receipts = recover.card->tag.value("IfLitianRecovery").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            SkillContext ctx = dreamContinuation(room, objectName(), event, player, data, receipt);
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.preferredTarget = player; ctx.targets = {player}; out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ctx.original_data->value<RecoverStruct>().card : nullptr;
        return ctx.sourceRef.isValid() && card && card->getTag("IfLitianRecovery").toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Start) return result;
            for (ServerPlayer *other : room->getOtherPlayers(player))
                if (other->getHandcardNum() > player->getHandcardNum() || other->getHp() > player->getHp()) return result;
        } else if (event == TargetSpecified || event == TargetConfirmed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.card->isKindOf("Peach")) return result;
            if (event == TargetSpecified ? use.from != player : use.from == player || !use.to.contains(player)) return result;
        } else return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            if (room->getShimingStatus(ref) < 1) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart) return true;
        // The unbound upgraded grant retains only the original mission's exact related instances.
        ctx.extra_data = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "anjie_instances").toList();
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart && !room->sendShimingLog(ctx.activationRef, true)) return false;
        if (event != PreHpRecover) ctx.targets = {ctx.owner};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == PreHpRecover) {
            RecoverStruct recover = ctx.original_data->value<RecoverStruct>(); recover.recover += amount;
            *ctx.original_data = QVariant::fromValue(recover);
        } else if (event == EventPhaseStart) {
            room->gainMaxHp(target, amount, objectName());
            for (const QVariant &id : ctx.extra_data.toList())
                room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName("ifanjie", id.toInt()));
            room->acquireSkillFromEffect(target, "ifhuangchu", ctx);
        } else {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.card || !use.card->isKindOf("Peach")) return false;
            if (event == TargetConfirmed) {
                if (use.to.contains(target) && !use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
                *ctx.original_data = QVariant::fromValue(use);
            } else {
                QVariantList receipts = use.card->tag.value("IfLitianRecovery").toList();
                QVariantMap receipt = dreamReceipt(room, ctx); receipt.insert("amount", amount);
                receipts << receipt;
                use.card->tag.insert("IfLitianRecovery", receipts);
            }
        }
        return false;
    }
};
class IfHuangchu : public TriggerSkillV2
{
public:
    IfHuangchu() : TriggerSkillV2("ifhuangchu$")
    { events << EventPhaseStart; frequency = Compulsory; waked_skills = "_iffeileijia"; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && (player->getPhase() == Player::Start || player->getPhase() == Player::Finish)
            && player->hasLordSkill(objectName())) addInstances(result, player, objectName());
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.owner->getPhase() == Player::Finish) {
            ctx.choice = "equip";
            return skillEffect(event, room, owner, ctx, ctx.owner);
        }
        QList<int> ids;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner)) {
            SkillContext child = ctx; child.choice = "discard"; child.extra_data = QVariantList();
            skillEffect(event, room, owner, child, other);
            for (const QVariant &value : child.extra_data.toList()) {
                const int id = value.toInt();
                if (room->getCardPlace(id) == Player::DiscardPile && !ids.contains(id)) ids << id;
            }
        }
        bool received = false;
        while (ctx.owner->isAlive() && !ids.isEmpty()) {
            for (int id : QList<int>(ids)) if (room->getCardPlace(id) != Player::DiscardPile) ids.removeOne(id);
            if (ids.isEmpty()) break;
            room->fillAG(ids, ctx.owner);
            int id = room->askForAG(ctx.owner, ids, false, objectName());
            room->clearAG(ctx.owner);
            if (!ids.contains(id)) id = ids.first();
            ServerPlayer *recipient = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "ifhuangchu0", false);
            if (!recipient) break;
            ids.removeOne(id); // A canceled recipient hook does not re-offer the same allocation.
            SkillContext child = ctx; child.choice = "obtain"; child.extra_data = id;
            skillEffect(event, room, owner, child, recipient);
            if (recipient == ctx.owner && child.extra_data.toBool()) received = true;
        }
        if (!received && ctx.owner->isAlive()) {
            SkillContext child = ctx; child.choice = "recover";
            skillEffect(event, room, owner, child, ctx.owner);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.choice == "discard") {
            const int extra = target->getHandcardNum() - qMin(3, target->getMaxHp());
            if (extra <= 0) return false;
            const Card *discarded = room->askForDiscard(target, objectName(), extra, extra);
            QVariantList ids;
            if (discarded) for (int id : discarded->getSubcards()) ids << id;
            ctx.extra_data = ids;
        } else if (ctx.choice == "obtain") {
            const int id = ctx.extra_data.toInt();
            ctx.extra_data = false;
            if (room->getCardPlace(id) != Player::DiscardPile || Sanguosha->getCard(id)->hasFlag("using")) return false;
            const qint64 cause = room->currentHistoryEventId();
            const qint64 before = room->queryHistoryMoves({{"limit", 1}}).value("watermark").toLongLong();
            room->obtainCard(target, id, true);
            ctx.extra_data = dreamMoved(room, before, cause, id, QString(), target->objectName(), Player::PlaceHand);
        } else if (ctx.choice == "recover") {
            room->recover(target, RecoverStruct(objectName(), ctx.owner, amount));
        } else if (ctx.choice == "equip") {
            const Card *treasure = target->getTreasure();
            if (!treasure || treasure->objectName() != "_iffeileijia") target->getDerivativeCard("_iffeileijia");
        }
        return false;
    }
};
class IfRenli : public TriggerSkillV2
{
public:
    IfRenli() : TriggerSkillV2("ifrenli")
    { events << Dying << HpRecover << QuitDying << EventPhaseChanging; global = true; }
    static qint64 dyingEvent(Room *room)
    { return room->historyParent(room->currentHistoryEventId(), "dying", true).value("id").toLongLong(); }
    static bool firstDying(Room *room, const ServerPlayer *target)
    {
        const qint64 current = dyingEvent(room);
        const qint64 round = room->historyScopes().value("round_id").toLongLong();
        if (!target || current <= 0 || round <= 0) return false;
        QVariantMap query{{"kind", "dying_start"}, {"round_id", round}, {"player", target->objectName()}};
        bool foundCurrent = false;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const qint64 id = entry.toMap().value("event_id").toLongLong();
                if (id < current) return false;
                if (id == current) foundCurrent = true;
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark"));
            query.insert("after", page.value("next_after"));
        }
        return foundCurrent;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == HpRecover && player) {
            const RecoverStruct recover = data.value<RecoverStruct>();
            QVariantMap pending = player->getTag("IfRenliRecover").toMap();
            if (!pending.isEmpty() && recover.reason == objectName() && recover.who
                && recover.who->objectName() == pending.value("actor").toString()
                && dyingEvent(room) == pending.value("dying").toLongLong()) {
                const qulonglong eventId = room->historyParent(room->currentHistoryEventId(), "recover", true).value("id").toULongLong();
                const QVariantMap page = eventId > 0 ? room->queryHistoryFacts(
                    {{"kind", "actual_recover"}, {"event_id", eventId}, {"player", player->objectName()}}) : QVariantMap();
                if (page.value("complete").toBool() && room->historyEvent(eventId).value("parent_id").toLongLong()
                    == pending.value("recover_parent").toLongLong()) for (const QVariant &entry : page.value("items").toList()) {
                    const QVariantMap fact = entry.toMap().value("data").toMap();
                    if (fact.contains("hp_before") && fact.contains("hp_after") && fact.value("hp_before").toInt() <= 0 && fact.value("hp_after").toInt() > 0) {
                        pending.insert("rescued", true);
                        player->setTag("IfRenliRecover", pending);
                    }
                }
            }
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            for (ServerPlayer *target : room->getAllPlayers(true)) {
                QVariantList retained;
                for (const QVariant &entry : target->getTag("IfRenliPending").toList()) {
                    const qint64 id = entry.toMap().value("dying").toLongLong();
                    // A nested extra turn ending must not erase an outer dying resolution.
                    if (room->historyEvent(id).value("status").toString() == "active") retained << entry;
                }
                target->setTag("IfRenliPending", retained);
            }
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != QuitDying) return false;
        const qint64 current = dyingEvent(room);
        if (!player || current <= 0) return true;
        const QVariantMap page = room->queryHistoryFacts({{"kind", "dying_result"}, {"event_id", current}});
        if (!page.value("complete").toBool()) return true;
        bool rescued = false;
        for (const QVariant &value : page.value("items").toList()) {
            const QVariantMap result = value.toMap().value("data").toMap();
            if (result.value("player").toString() == player->objectName()
                && result.value("alive").toBool() && result.value("hp").toInt() > 0) rescued = true;
        }
        if (!rescued) return true;
        for (const QVariant &value : player->getTag("IfRenliPending").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("dying").toLongLong() != current || receipt.value("consumed").toBool()) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("activation_instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill", objectName()).toString(), receipt.value("instance").toInt()));
            ctx.amount = receipt.value("amount", 1).toInt();
            ctx.trigger_count = receipt.value("serial").toInt();
            ctx.preferredTarget = player;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.is_forced = true; // Continue the already applied receipt without a second optional activation.
            ctx.extra_data = value;
            out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.invoker && ctx.invoker->getTag("IfRenliPending").toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Dying || !player || !player->isAlive()) return result;
        const DyingStruct dying = data.value<DyingStruct>();
        // Native Dying dispatch visits each player; this callback owns only its current holder.
        if (dying.who && dying.who != player && dying.who->getHp() <= 0 && firstDying(room, dying.who)
            && !player->isKongcheng()) addInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == QuitDying) return true;
        const DyingStruct dying = ctx.original_data->value<DyingStruct>();
        if (!ctx.owner || !dying.who || !firstDying(room, dying.who)
            || !ctx.owner->askForSkillInvoke(objectName(), dying.who)) return false;
        const int count = qMin(2, ctx.owner->getHandcardNum());
        if (count <= 0) return false;
        const Card *given = room->askForExchange(ctx.owner, objectName(), count, count, false, "ifrenli0");
        if (!given || given->subcardsLength() != count) return false;
        ctx.extra_data = ListI2V(given->getSubcards());
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == QuitDying) return true;
        const DyingStruct dying = ctx.original_data->value<DyingStruct>();
        const QList<int> ids = ListV2I(ctx.extra_data.toList());
        if (!dying.who || !dying.who->isAlive() || ids.isEmpty()) return false;
        QSet<int> seen;
        for (int id : ids) {
            if (seen.contains(id) || !ctx.owner->handCards().contains(id) || room->getCardOwner(id) != ctx.owner
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
            seen.insert(id);
        }
        DummyCard gift(ids);
        room->giveCard(ctx.owner, dying.who, &gift, objectName());
        return true;
    }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == QuitDying) {
            QVariantList pending = ctx.invoker->getTag("IfRenliPending").toList();
            const int index = pending.indexOf(ctx.extra_data);
            if (index < 0) return false;
            QVariantMap consumed = ctx.extra_data.toMap();
            consumed.insert("consumed", true);
            pending[index] = consumed;
            ctx.invoker->setTag("IfRenliPending", pending);
            ctx.targets = {ctx.invoker};
        } else ctx.targets = {ctx.original_data->value<DyingStruct>().who};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (event == QuitDying) {
            room->scheduleExtraTurn(target, ctx.sourceRef, {}, getEffectiveAmount(ctx));
            return false;
        }
        if (target->getHp() > 0) return false;
        const QVariant previous = target->getTag("IfRenliRecover");
        const auto restore = qScopeGuard([&] {
            if (previous.isValid()) target->setTag("IfRenliRecover", previous);
            else target->removeTag("IfRenliRecover");
        });
        const QVariantMap receipt{{"actor", ctx.owner->objectName()}, {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
            {"activation_instance", ctx.activationRef.key.instanceID}, {"holder", ctx.owner->objectName()},
            {"recover_parent", room->currentHistoryEventId()}, {"event", room->currentHistoryEventId()},
                {"serial", nextDreamReceipt(target->getRoom())}, {"dying", dyingEvent(room)}, {"amount", getEffectiveAmount(ctx)}};
        target->setTag("IfRenliRecover", receipt);
        room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        if (target->getTag("IfRenliRecover").toMap().value("rescued").toBool()) {
            QVariantList pending = target->getTag("IfRenliPending").toList();
            pending << receipt;
            target->setTag("IfRenliPending", pending);
        }
        return false;
    }
};
class IfMingduan : public TriggerSkillV2
{
public:
    IfMingduan() : TriggerSkillV2("ifmingduan") { events << EventPhaseChanging; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (player && (change.to == Player::NotActive || change.from == Player::NotActive))
            addInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->askForSkillInvoke(objectName()); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        int refused = 0;
        QList<ServerPlayer *> participants;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner)) {
            if (!other->isAlive()) continue;
            SkillContext offer = ctx;
            offer.choice = "offer";
            offer.extra_data = false;
            skillEffect(event, room, owner, offer, other);
            if (offer.extra_data.toBool()) participants << other;
            else ++refused;
        }
        if (!ctx.owner->isAlive()) return false;
        SkillContext reveal = ctx;
        reveal.choice = "reveal";
        reveal.extra_data = 0;
        skillEffect(event, room, owner, reveal, ctx.owner);
        const int red = reveal.extra_data.toInt();
        for (int i = participants.size() - 1; i >= 0; --i)
            if (participants.at(i)->isDead()) participants.removeAt(i);
        if (red != 0 && !participants.isEmpty() && ctx.owner->isAlive()) {
            const QString prompt = red > 0 ? "ifmingduan1" : "ifmingduan2";
            ServerPlayer *chosen = room->askForPlayerChosen(ctx.owner, participants, prompt, prompt);
            if (chosen) {
                SkillContext benefit = ctx;
                benefit.choice = red > 0 ? "recover" : "damage";
                skillEffect(event, room, owner, benefit, chosen);
            }
        }
        SkillContext draw = ctx;
        draw.choice = "draw";
        draw.extra_data = refused;
        skillEffect(event, room, owner, draw, ctx.owner);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "offer") {
            room->doAnimate(1, ctx.owner->objectName(), target->objectName());
            const Card *given = room->askForExchange(target, objectName(), 1, 1, true, "ifmingduan0", true);
            if (!given || given->subcardsLength() != 1) return false;
            const int id = given->getSubcards().first();
            if (room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand
                && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return false;
            // Resolve each participant separately; no callback can spend another player's card.
            const qint64 cause = room->currentHistoryEventId();
            const qint64 before = room->queryHistoryMoves({{"limit", 1}}).value("watermark").toLongLong();
            room->moveCardsInToDrawpile(target, id, objectName(), 1);
            ctx.extra_data = dreamMoved(room, before, cause, id, target->objectName(), QString(), Player::DrawPile);
        } else if (ctx.choice == "reveal") {
            const QList<int> shown = room->getNCards(target->aliveCount());
            bool cleanupNeeded = true;
            const auto cleanup = qScopeGuard([&] {
                if (!cleanupNeeded) return;
                QList<int> detached, table;
                for (int id : shown) {
                    if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) detached << id;
                    else if (room->getCardPlace(id) == Player::PlaceTable) table << id;
                }
                try {
                    if (!detached.isEmpty()) room->returnToTopDrawPile(detached);
                    if (!table.isEmpty()) room->throwCard(table, objectName(), nullptr);
                } catch (...) { /* Cleanup must preserve the original control exception. */ }
            });
            room->moveCardsAtomic(CardsMoveStruct(shown, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), "")), true);
            int red = 0;
            for (int id : shown) {
                const Card *card = Sanguosha->getCard(id);
                if (card->isRed()) ++red;
                else if (card->isBlack()) --red;
            }
            room->getThread()->delay();
            QList<int> remaining;
            for (int id : shown) if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
            if (!remaining.isEmpty()) room->throwCard(remaining, objectName(), nullptr);
            cleanupNeeded = false;
            ctx.extra_data = red;
        } else if (ctx.choice == "recover") {
            room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        } else if (ctx.choice == "damage") {
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        } else if (ctx.choice == "draw") {
            target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
        }
        return false;
    }
};
IfSixiangCard::IfSixiangCard() { setSkillName("ifsixiang"); }

bool IfSixiangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    if (!card) return false;
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool IfSixiangCard::targetFixed() const
{
    if (user_string.isEmpty() || Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
        return true;
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    if (!card) return false;
    card->deleteLater();
    return card->targetFixed();
}

bool IfSixiangCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (user_string.isEmpty()) return targets.isEmpty();
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    if (!card) return false;
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *IfSixiangCard::validateInResponse(ServerPlayer *) const { return nullptr; }
const Card *IfSixiangCard::validate(CardUseStruct &) const { return nullptr; }

class IfSixiangViewAs : public ViewAsSkillV2
{
public:
    IfSixiangViewAs() : ViewAsSkillV2("ifsixiang") {}
    LimitScope getLimitScope() const override { return Limit_Custom; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    bool willThrowSelectedCards() const override { return false; }
    static int quotaInstance(const ActiveSkillRequest &request)
    {
        return request.initiator->getSkillInstanceStateValue("ifsixiang", request.activationRef.key.instanceID,
            "quota_instance").toInt() > 0 ? request.initiator->getSkillInstanceStateValue("ifsixiang",
                request.activationRef.key.instanceID, "quota_instance").toInt() : request.activationRef.key.instanceID;
    }
    static QString quotaKey(int instance, const Card *card)
    { return QString("ifsixiang_%1_%2-Clear").arg(instance).arg(card->getType()); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.activationRef.isValid()) return false;
        const bool prompt = request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID,
            "accepted_view_as_effect").toBool();
        if (request.pattern == "@@ifsixiang") return prompt;
        if (prompt) return false;
        for (const Player *other : request.initiator->getSiblings()) if (other->isDead()) return true;
        return false;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !cardSelectionFeasible(request)) return nullptr;
        if (request.pattern == "@@ifsixiang") {
            const QVariant value = request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "selected_id");
            if (!value.isValid() || value.toInt() < 0 || value.toInt() >= Sanguosha->getCardCount()) return nullptr;
            const Card *physical = Sanguosha->getCard(value.toInt());
            Card *card = Sanguosha->cloneCard(physical->objectName(), physical->getSuit(), physical->getNumber());
            if (card) {
                card->addSubcard(value.toInt()); card->setSkillName(objectName());
                card->setTag("IfSixiangQuota", quotaKey(quotaInstance(request), card));
            }
            return card;
        }
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return ViewAsSkillV2::createCard(request);
        for (const QString &name : request.pattern.split('+')) {
            Card *card = Sanguosha->cloneCard(name);
            if (!card) continue;
            if (card->getTypeId() == Card::TypeSkill || request.initiator->getMark(quotaKey(quotaInstance(request), card)) > 0) { delete card; continue; }
            card->setSkillName(objectName()); return card;
        }
        return nullptr;
    }
    static bool validMaterial(Room *room, const SkillContext &ctx, const Card *card)
    {
        if (!ctx.initiator || !card || card->subcardsLength() != 1) return false;
        const int id = card->getSubcards().first();
        return room->getDrawPile().mid(0, 3).contains(id) && !room->getCardOwner(id)
            && room->getCardPlace(id) == Player::DrawPile && !Sanguosha->getCard(id)->hasFlag("using");
    }
    static int choose(Room *room, ServerPlayer *player, int instance, const QString &pattern, CardUseStruct::CardUseReason reason)
    {
        const QList<int> cards = room->getDrawPile().mid(0, 3);
        QList<int> enabled, disabled;
        for (int id : cards) {
            const Card *card = Sanguosha->getCard(id);
            const Card::HandlingMethod method = reason == CardUseStruct::CARD_USE_REASON_RESPONSE ? Card::MethodResponse : Card::MethodUse;
            if (!card->hasFlag("using") && player->getMark(quotaKey(instance, card)) == 0
                && !player->isCardLimited(card, method)
                && (reason == CardUseStruct::CARD_USE_REASON_PLAY ? card->isAvailable(player) : Sanguosha->matchPattern(pattern, player, card))) enabled << id;
            else disabled << id;
        }
        room->fillAG(cards, player, disabled);
        const auto clear = qScopeGuard([&] { room->clearAG(player); });
        const int id = room->askForAG(player, enabled, true, "ifsixiang");
        return enabled.contains(id) && room->getDrawPile().mid(0, 3).contains(id) ? id : -1;
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern == "@@ifsixiang") return true;
        const int id = choose(room, ctx.initiator, quotaInstance(request), request.pattern, request.reason);
        if (id < 0) return false;
        const Card *physical = Sanguosha->getCard(id);
        Card *card = Sanguosha->cloneCard(physical->objectName(), physical->getSuit(), physical->getNumber());
        if (!card) return false;
        card->addSubcard(id); card->setSkillName(objectName()); card->deleteLater();
        card->setTag("IfSixiangQuota", quotaKey(quotaInstance(request), card));
        ctx.updated_card = card; return true;
    }
    static void commitAccepted(SkillContext &ctx)
    {
        if (!ctx.initiator || ctx.extra_data.toMap().value("committed").toBool()) return;
        const Card *card = ctx.updated_card ? ctx.updated_card : ctx.use_card;
        const QString key = card ? card->getTag("IfSixiangQuota").toString() : QString();
        if (key.isEmpty() || ctx.initiator->getMark(key) > 0 || !validMaterial(ctx.initiator->getRoom(), ctx, card)) return;
        QVariantMap receipt = ctx.extra_data.toMap(); receipt.insert("committed", true); ctx.extra_data = receipt;
        ctx.initiator->getRoom()->addPlayerMark(ctx.initiator, key);
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        const Card *card = ctx.updated_card ? ctx.updated_card : ctx.use_card;
        if (card && card->getTypeId() == Card::TypeSkill) return true;
        const QString key = card ? card->getTag("IfSixiangQuota").toString() : QString();
        if (key.isEmpty() || !validMaterial(room, ctx, card) || ctx.initiator->getMark(key) > 0) return false;
        commitAccepted(ctx); return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.use_card) return FinishSkill;
        if (ctx.use_card->getTypeId() != Card::TypeSkill)
            return ctx.extra_data.toMap().value("committed").toBool() && validMaterial(ctx.initiator->getRoom(), ctx, ctx.use_card) ? ContinueEffects : FinishSkill;
        skillEffect(ctx, ctx.invoker); return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const int id = choose(room, target, ctx.activationRef.key.instanceID, QString(), CardUseStruct::CARD_USE_REASON_PLAY);
        if (id < 0) return ContinueEffects;
        Room::AcceptedViewAsEffectScope scope(room, target, objectName(), ctx);
        if (!scope.isValid()) return ContinueEffects;
        target->setSkillInstanceStateValue(objectName(), scope.activationRef().key.instanceID, "selected_id", id);
        target->setSkillInstanceStateValue(objectName(), scope.activationRef().key.instanceID, "quota_instance", ctx.activationRef.key.instanceID);
        const int previous = target->getMark("ifsixiangId");
        const auto restore = qScopeGuard([&] { room->setPlayerMark(target, "ifsixiangId", previous); });
        room->setPlayerMark(target, "ifsixiangId", id);
        room->askForUseCard(target, "@@ifsixiang", "ifsixiang0:" + Sanguosha->getCard(id)->objectName());
        return ContinueEffects;
    }
};
class IfSixiang : public TriggerSkillV2
{
public:
    IfSixiang() : TriggerSkillV2("ifsixiang") { events << EventSkillInvoking; global = true; view_as_skill = new IfSixiangViewAs; }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        SkillContext ctx = data.value<SkillContext>();
        if (ctx.activationRef.key.skillName == objectName()) {
            IfSixiangViewAs::commitAccepted(ctx); data = QVariant::fromValue(ctx);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override { return true; }
};
IfJizhiCard::IfJizhiCard() { target_fixed = false; setSkillName("ifjizhivs"); }

bool IfJizhiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *slash = Sanguosha->cloneCard("slash");
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

const Card *IfJizhiCard::validateInResponse(ServerPlayer *) const { return nullptr; }
const Card *IfJizhiCard::validate(CardUseStruct &) const { return nullptr; }

class IfJizhiViewAs : public ViewAsSkillV2
{
public:
    IfJizhiViewAs() : ViewAsSkillV2("ifjizhivs&") {}
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool willThrowSelectedCards() const override { return false; }
    static SkillInstanceRef providerRef(const Player *player, const SkillInstanceRef &activation)
    {
        const SkillInstance *instance = player ? player->findSkillInstance(activation.key.skillName, activation.key.instanceID) : nullptr;
        return instance ? instance->parentRef : SkillInstanceRef();
    }
    SkillInstanceRef getUsageRef(const SkillContext &ctx) const override
    { return providerRef(ctx.initiator, ctx.activationRef); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->getKingdom() != "shu") return false;
        const SkillInstanceRef parent = providerRef(request.initiator, request.activationRef);
        const Player *lord = nullptr;
        for (const Player *other : request.initiator->getAliveSiblings()) if (other->objectName() == parent.ownerObjectName) lord = other;
        if (!lord || !lord->hasLordSkill("ifjizhi") || !lord->hasSkillInstance(parent.key.skillName, parent.key.instanceID)) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? Slash::IsAvailable(request.initiator) : request.pattern.contains("slash");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        Slash *card = new Slash(Card::NoSuit, 0); card->setSkillName(objectName());
        card->setTag("IfJizhiProvider", providerRef(request.initiator, request.activationRef).ownerObjectName);
        return card;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.initiator ? ctx.initiator->getRoom() : nullptr;
        if (!room) return FinishSkill;
        ServerPlayer *lord = ctx.use_card ? room->findPlayerByObjectName(ctx.use_card->getTag("IfJizhiProvider").toString(), true) : nullptr;
        if (!lord || !lord->isAlive()) return FinishSkill;
        skillEffect(ctx, lord);
        return ctx.updated_card ? ContinueEffects : FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *lord) const override
    {
        Room *room = lord->getRoom();
        lord->drawCards(getEffectiveAmount(ctx), "ifjizhi");
        if (!lord->isAlive() || !ctx.invoker || !ctx.invoker->isAlive()) return ContinueEffects;
        const Card *provided = room->askForCard(lord, "slash", "ifjizhi1:" + ctx.invoker->objectName(),
            QVariant::fromValue(ctx.invoker), Card::MethodResponse, ctx.invoker, false, "ifjizhi", true);
        if (!provided) return ContinueEffects;
        Card *slash = Sanguosha->cloneCard(provided->objectName(), provided->getSuit(), provided->getNumber());
        if (!slash) return ContinueEffects;
        slash->addSubcard(provided); slash->setSkillName(objectName()); slash->setFlags("YUANBEN");
        slash->deleteLater(); ctx.updated_card = slash;
        if (ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>(); use.m_isOwnerUse = false;
            *ctx.original_data = QVariant::fromValue(use);
        }
        return ContinueEffects;
    }
};
class IfJizhi : public TriggerSkillV2
{
public:
    IfJizhi() : TriggerSkillV2("ifjizhi$") { events << GameStart << EventAcquireSkill << Revived; global = true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        // This is the native attachment graph: one helper per exact living lord grant.
        // Parent removal cascades only its own children; another lord's helper is unaffected.
        for (ServerPlayer *lord : room->getAlivePlayers()) {
            if (!lord->hasLordSkill(objectName(), true)) continue;
            for (int id : lord->getSkillInstanceIds(objectName()))
                for (ServerPlayer *receiver : room->getOtherPlayers(lord))
                    room->attachSkillToPlayer(receiver, "ifjizhivs", SkillInstanceRef(lord->objectName(), SkillInstanceKey(objectName(), id)));
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override { return true; }
};
class IfHaitian : public TriggerSkillV2
{
public:
    IfHaitian() : TriggerSkillV2("ifhantian") { events << TargetConfirmed << TargetSpecified << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetConfirmed && event != TargetSpecified) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !use.from || use.to.size() != 1 || !use.card || use.card->getTypeId() == Card::TypeSkill) return result;
        if (event == TargetConfirmed ? use.to.first() != player || use.from == player
            : use.from != player || use.to.first() == player) return result;
        addInstances(result, player, objectName()); return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "ban") {
            room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", true,
                "ifhantian:" + QString::number(nextDreamReceipt(room)));
            return false;
        }
        if (ctx.choice == "discard") {
            if (!ctx.owner->canDiscard(target, "he")) return false;
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (id < 0 || room->getCardOwner(id) != target
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using") || !ctx.owner->canDiscard(target, id)) return false;
            const bool equip = Sanguosha->getCard(id)->getTypeId() == Card::TypeEquip;
            room->throwCard(id, objectName(), target, ctx.owner);
            if (equip) ctx.extra_data = true;
            return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *other = event == TargetConfirmed ? use.from : use.to.value(0);
        if (!other || getEffectiveAmount(ctx) <= 0) return false;
        JudgeStruct judge; judge.who = target; judge.reason = objectName();
        judge.pattern = ".|spade|2~9"; judge.good = false;
        room->judge(judge);
        if (judge.isBad() && target->isAlive()) {
            const qint64 cause = room->currentHistoryEventId();
            QVariantMap query{{"kind", "actual_damage"}};
            const QVariantMap before = room->queryHistoryFacts(query);
            const qint64 frontier = before.value("watermark").toLongLong();
            room->damage(DamageStruct(objectName(), nullptr, target, 3, DamageStruct::Thunder));
            query.insert("after", frontier);
            bool damaged = false;
            for (;;) {
                const QVariantMap page = room->queryHistoryFacts(query);
                if (!page.value("complete").toBool() || page.contains("error") || cause <= 0) return false;
                for (const QVariant &entry : page.value("items").toList()) {
                    const QVariantMap fact = entry.toMap(), value = fact.value("data").toMap();
                    if (room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause
                        && value.value("to").toString() == target->objectName() && value.value("amount").toInt() > 0) damaged = true;
                }
                if (!page.value("has_more").toBool()) break;
                query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
            }
            if (damaged) return false;
        }
        bool discardedEquip = false;
        for (int i = 0; i < 2 && target->isAlive() && other->isAlive(); ++i) {
            QList<ServerPlayer *> choices;
            if (target->canDiscard(target, "he")) choices << target;
            if (target->canDiscard(other, "he")) choices << other;
            if (choices.isEmpty()) break;
            ServerPlayer *chosen = room->askForPlayerChosen(target, choices, objectName(), "ifhantian0", false);
            if (!chosen) break;
            SkillContext discard = ctx; discard.choice = "discard"; discard.extra_data = false;
            skillEffect(event, room, ctx.owner, discard, chosen);
            discardedEquip = discardedEquip || discard.extra_data.toBool();
        }
        if (discardedEquip) { SkillContext ban = ctx; ban.choice = "ban"; skillEffect(event, room, ctx.owner, ban, other); }
        return false;
    }
};
class IfShenfengViewAs : public ViewAsSkillV2
{
public:
    IfShenfengViewAs() : ViewAsSkillV2("ifshenfeng") {}
    bool willThrowSelectedCards() const override { return false; }
    static QList<int> materials(const Player *player)
    {
        QList<int> ids;
        if (player) for (const Card *card : player->getHandcards())
            if (!card->isDamageCard() && !card->hasFlag("using")) ids << card->getEffectiveId();
        return ids;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || materials(request.initiator).isEmpty()) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            const Card *slash = createCard(request);
            if (!slash) return false;
            CardLifetimeManager &manager = globalCardLifetimeManager();
            const CardLifetimeLease lease(manager, manager.observeCard(const_cast<Card *>(slash)));
            const_cast<Card *>(slash)->deleteLater();
            return Slash::IsAvailable(request.initiator, slash);
        }
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern.contains("slash");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const QList<int> ids = materials(request.initiator);
        if (ids.isEmpty()) return nullptr;
        Card *slash = Sanguosha->cloneCard("slash");
        slash->setSkillName(objectName()); slash->addSubcards(ids);
        QStringList types;
        for (int id : ids) if (!types.contains(Sanguosha->getCard(id)->getType())) types << Sanguosha->getCard(id)->getType();
        slash->setTag("IfShenfengAllTypes", types.size() == 3);
        return slash;
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.use_card || !ctx.initiator) return false;
        const QList<int> ids = materials(ctx.initiator);
        const QList<int> submitted = ctx.use_card->getSubcards();
        return !ids.isEmpty() && qsanToSet(ids) == qsanToSet(submitted);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || !ctx.initiator) return FinishSkill;
        const QList<int> ids = materials(ctx.initiator), submitted = ctx.use_card->getSubcards();
        // A waived payment does not authorize stale or partial conversion materials.
        if (ids.isEmpty() || qsanToSet(ids) != qsanToSet(submitted)) return FinishSkill;
        ctx.use_card->setTag("IfShenfengApplied", dreamReceipt(ctx.initiator->getRoom(), ctx));
        return ContinueEffects;
    }
};
class IfShenfeng : public TriggerSkillV2
{
public:
    IfShenfeng() : TriggerSkillV2("ifshenfeng")
    {
        events << PreCardUsed << TargetSpecified << Damage << CardFinished;
        frequency = Compulsory; global = true; view_as_skill = new IfShenfengViewAs;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card) use.card->removeTag("IfShenfengApplied");
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event == CardFinished) return true;
        const Card *card = event == Damage ? data.value<DamageStruct>().card : data.value<CardUseStruct>().card;
        if (!card) return true;
        const QVariantMap receipt = card->getTag("IfShenfengApplied").toMap();
        if (receipt.isEmpty() || !dreamSource(receipt).isValid()) return true;
        SkillContext ctx = dreamContinuation(room, objectName(), event, actor, data, receipt);
        if (!ctx.owner || !ctx.initiator) return true;
        if (event == Damage) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.to && damage.to->isAlive() && damage.to->canDiscard(damage.to, "h")) ctx.targets = {damage.to};
        } else if (event == TargetSpecified) ctx.targets = data.value<CardUseStruct>().to;
        else ctx.targets = {ctx.initiator};
        if (!ctx.targets.isEmpty()) out << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.original_data || !ctx.sourceRef.isValid()) return false;
        const Card *card = ctx.original_data->canConvert<DamageStruct>()
            ? ctx.original_data->value<DamageStruct>().card : ctx.original_data->value<CardUseStruct>().card;
        return card && card->getTag("IfShenfengApplied").toMap() == ctx.extra_data.toMap();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "obtain") {
            DummyCard cards;
            for (const QVariant &value : ctx.extra_data.toMap().value("chosen").toList()) {
                const int id = value.toInt();
                if (room->getCardPlace(id) == Player::DiscardPile && !Sanguosha->getCard(id)->hasFlag("using")) cards.addSubcard(id);
            }
            if (cards.subcardsLength()) room->obtainCard(target, &cards, objectName());
            return false;
        }
        if (event != Damage) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.card) return false;
            if (event == TargetSpecified) target->addQinggangTag(use.card);
            else if (use.card->getTag("IfShenfengAllTypes").toBool()) {
                use.m_addHistory = false; *ctx.original_data = QVariant::fromValue(use);
            }
            return false;
        }
        const int count = target->getHandcardNum() / 2;
        if (count <= 0 || getEffectiveAmount(ctx) <= 0) return false;
        const Card *discarded = room->askForDiscard(target, objectName(), count, count);
        if (!discarded || !ctx.initiator->isAlive()) return false;
        QList<int> ids;
        for (int id : discarded->getSubcards()) if (room->getCardPlace(id) == Player::DiscardPile) ids << id;
        QVariantList chosen;
        room->fillAG(ids, ctx.initiator);
        const auto clear = qScopeGuard([&] { room->clearAG(ctx.initiator); });
        while (!ids.isEmpty() && ctx.initiator->isAlive()) {
            const int id = room->askForAG(ctx.initiator, ids, false, objectName());
            if (!ids.contains(id)) break;
            chosen << id;
            const QString name = Sanguosha->getEngineCard(id)->objectName(false);
            for (int candidate : QList<int>(ids)) if (Sanguosha->getEngineCard(candidate)->objectName(false) == name) {
                room->takeAG(ctx.initiator, candidate, false, {ctx.initiator}); ids.removeOne(candidate);
            }
        }
        SkillContext obtain = ctx; obtain.choice = "obtain";
        QVariantMap receipt = ctx.extra_data.toMap(); receipt.insert("chosen", chosen); obtain.extra_data = receipt;
        skillEffect(event, room, ctx.owner, obtain, ctx.initiator);
        return false;
    }
};
class IfXiechang : public TriggerSkillV2
{
public:
    IfXiechang(const QString &name = "ifxiechang") : TriggerSkillV2(name)
    {
        global = true; frequency = Compulsory;
        if (name == "ifxiechang") events << EventPhaseChanging;
        else events << Dying;
    }
    Frequency getFrequency(const Player *target) const override
    { return objectName() == "ifxiechang" && target && !target->getTag("IfTianminUpgrades").toList().isEmpty() ? NotFrequent : Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != Dying) return false;
        const DyingStruct dying = data.value<DyingStruct>();
        const Card *card = dying.damage ? dying.damage->card : nullptr;
        if (!card || !dying.who || actor != dying.who || dying.who->isNude()) return true;
        const QVariantMap receipt = card->getTag("IfXiechangApplied").toMap();
        if (receipt.isEmpty() || !dreamSource(receipt).isValid()) return true;
        SkillContext ctx = dreamContinuation(room, objectName(), event, actor, data, receipt);
        if (!ctx.owner || !ctx.owner->isAlive()) return true;
        ctx.targets = {dying.who}; out << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const DyingStruct dying = ctx.original_data ? ctx.original_data->value<DyingStruct>() : DyingStruct();
        return ctx.sourceRef.isValid() && dying.damage && dying.damage->card
            && dying.damage->card->getTag("IfXiechangApplied").toMap() == ctx.extra_data.toMap();
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            addInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Dying) return true;
        FireSlash probe(Card::NoSuit, 0);
        QList<ServerPlayer *> targets;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if (other->getEquips().size() >= ctx.owner->getEquips().size() && ctx.owner->canSlash(other, &probe, false)) targets << other;
        if (getFrequency(ctx.owner) != Compulsory) {
            if (targets.isEmpty()) return false;
            targets = room->askForPlayersChosen(ctx.owner, targets, objectName(), 0, targets.size(), "ifxiechang0", true, true);
            if (targets.isEmpty()) return false;
        }
        ctx.targets = targets; return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) {
            SkillContext loss = ctx; loss.choice = "lose";
            skillEffect(event, room, player, loss, ctx.owner);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "lose") { room->loseHp(target, getEffectiveAmount(ctx), true, ctx.owner, "ifxiechang"); return false; }
        if (ctx.choice == "obtain") {
            const QVariantMap value = ctx.extra_data.toMap();
            ServerPlayer *victim = room->findPlayerByObjectName(value.value("victim").toString(), true);
            const int id = value.value("id", -1).toInt();
            if (victim && id >= 0 && room->getCardOwner(id) == victim
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using")) room->obtainCard(target, id, "ifxiechang", false);
            return false;
        }
        if (ctx.choice == "recover") { room->recover(target, RecoverStruct("ifxiechang", ctx.owner, getEffectiveAmount(ctx))); return false; }
        if (event == EventPhaseChanging) {
            if (!ctx.owner->isAlive()) return false;
            Card *slash = Sanguosha->cloneCard("fire_slash");
            slash->setSkillName("_ifxiechang");
            QVariantMap receipt = dreamReceipt(room, ctx);
            receipt.insert("upgraded", !ctx.owner->getTag("IfTianminUpgrades").toList().isEmpty());
            slash->setTag("IfXiechangApplied", receipt);
            CardUseStruct generated(slash, ctx.owner, target); generated.setOwnedCard(slash);
            if (ctx.owner->canSlash(target, slash, false)) room->useCardFromSkillEffect(generated, ctx, true);
            return false;
        }
        if (target->isNude()) return false;
        const int id = room->askForCardChosen(ctx.owner, target, "he", "ifxiechang");
        SkillContext obtain = ctx; obtain.choice = "obtain";
        QVariantMap receipt = ctx.extra_data.toMap(); receipt.insert("id", id); receipt.insert("victim", target->objectName());
        obtain.extra_data = receipt; skillEffect(event, room, ctx.owner, obtain, ctx.owner);
        if (ctx.extra_data.toMap().value("upgraded").toBool()) {
            SkillContext heal = ctx; heal.choice = "recover"; skillEffect(event, room, ctx.owner, heal, ctx.owner);
        }
        return false;
    }
};
class IfTianmin : public TriggerSkillV2
{
public:
    IfTianmin() : TriggerSkillV2("iftianmin")
    { events << Dying << EventSkillInvoking; limit_mark = "@iftianmin"; frequency = Limited; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    static bool neverKilled(Room *room, const ServerPlayer *player)
    {
        if (!player) return false;
        const QVariantMap history = room->queryHistoryFacts({{"kind", "death"}, {"from", player->objectName()}, {"limit", 1}});
        return history.value("complete").toBool() && history.value("items").toList().isEmpty();
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == Dying && player && data.value<DyingStruct>().who == player && neverKilled(room, player))
            addInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && isUsable(ctx) && neverKilled(room, ctx.owner)
        && ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data); }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        room->removePlayerMark(ctx.owner, limit_mark); // Display only; the exact instance owns the quota.
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->doSuperLightbox(ctx.owner, objectName());
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        room->recover(target, RecoverStruct(objectName(), ctx.owner, target->getMaxHp() - target->getHp()));
        // The permanent upgrade is already applied and is not revoked with the original grant.
        QVariantList upgrades = target->getTag("IfTianminUpgrades").toList();
        upgrades << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"serial", nextDreamReceipt(room)}, {"amount", amount}};
        target->setTag("IfTianminUpgrades", upgrades);
        room->addPlayerMark(target, "ifxiechangUp", amount);
        room->changeTranslation(target, "ifxiechang", 1);
        return false;
    }
};
class IfJianxiao : public TriggerSkillV2
{
public:
    IfJianxiao() : TriggerSkillV2("ifjianxiao")
    { events << TargetSpecified << ConfirmDamage << EventPhaseChanging; global = true; }
    static qint64 useEvent(Room *room)
    { return room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong(); }
    static bool previousTarget(Room *room, ServerPlayer *actor, ServerPlayer *target)
    {
        const qint64 current = useEvent(room);
        const QVariant turn = room->historyScopes().value("turn_id");
        if (current <= 0 || turn.toLongLong() <= 0) return false;
        QVariantMap query{{"turn_id", turn}, {"from", actor->objectName()}};
        QList<QVariantMap> uses;
        qint64 sequence = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap();
                const QVariantMap value = fact.value("data").toMap();
                const QString kind = fact.value("kind").toString();
                if (kind != "use_card" && !(kind == "respond_card" && value.value("is_use").toBool())) continue;
                const QVariantMap card = value.value("card").toMap();
                if (!card.contains("type")) return false;
                if (card.value("type").toInt() == Card::TypeSkill) continue;
                if (kind == "use_card" && fact.value("event_id").toLongLong() == current) sequence = fact.value("sequence").toLongLong();
                uses << fact;
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark"));
            query.insert("after", page.value("next_after"));
        }
        QVariantMap previous;
        for (const QVariantMap &fact : uses) {
            const qint64 order = fact.value("sequence").toLongLong();
            if (order < sequence && order > previous.value("sequence").toLongLong()) previous = fact;
        }
        if (previous.isEmpty() || previous.value("kind").toString() != "use_card") return false;
        const QVariantMap targets = room->queryHistoryFacts({{"kind", "use_card_targets"}, {"event_id", previous.value("event_id")}});
        if (!targets.value("complete").toBool()) return false;
        for (const QVariant &entry : targets.value("items").toList())
            if (entry.toMap().value("data").toMap().value("targets").toStringList().contains(target->objectName())) return true;
        return false;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        QVariantList retained;
        for (const QVariant &entry : room->getTag("IfJianxiaoApplied").toList())
            if (room->historyEvent(entry.toMap().value("use").toLongLong()).value("status").toString() == "active") retained << entry;
        room->setTag("IfJianxiaoApplied", retained);
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != ConfirmDamage) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        const qint64 current = useEvent(room);
        if (!damage.to || !damage.card || !damage.card->isKindOf("Slash") || current <= 0) return true;
        const QVariantList receipts = room->getTag("IfJianxiaoApplied").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("use").toLongLong() != current) continue;
            SkillContext ctx = dreamContinuation(room, objectName(), event, player, data, receipt);
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.preferredTarget = damage.to; ctx.targets = {damage.to};
            out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
        : room->getTag("IfJianxiaoApplied").toList().contains(ctx.extra_data); }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecified || !player || !player->hasTurn()) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from == player && use.card && use.card->isKindOf("Slash") && use.to.size() == 1
            && use.to.first() != player && previousTarget(room, player, use.to.first())) addInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return event == ConfirmDamage || ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data); }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == TargetSpecified) ctx.targets = ctx.original_data->value<CardUseStruct>().to;
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == TargetSpecified) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.to.contains(target)) return false;
            QVariantList receipts = room->getTag("IfJianxiaoApplied").toList();
            QVariantMap receipt = dreamReceipt(room, ctx);
            receipt.insert("use", useEvent(room)); receipt.insert("amount", getEffectiveAmount(ctx));
            receipts << receipt;
            room->setTag("IfJianxiaoApplied", receipts);
        } else {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to == target) { damage.damage += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(damage); }
        }
        return false;
    }
};
class IfShenwu : public TriggerSkillV2
{
public:
    IfShenwu() : TriggerSkillV2("ifshenwu") { events << EventPhaseStart << DamageCaused; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (!TriggerSkillV2::prepareSource(room, ctx)) return false;
        // Only the phase-start discard is optional; the damage increase is mandatory.
        if (ctx.current_event == DamageCaused) ctx.is_forced = true;
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == EventPhaseStart) {
            if (player->getPhase() == Player::Play && player->canDiscard(player, "h"))
                addInstances(result, player, objectName());
            return result;
        }
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || damage.card->getTypeId() <= 0) return result;
        for (const Card *hand : player->getHandcards())
            if (!hand->isDamageCard()) return result;
        addInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return event != EventPhaseStart || (ctx.owner && ctx.owner->askForSkillInvoke(objectName()));
    }
    bool pay(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Discarding the hand is the accepted invocation's payment.
        if (event == EventPhaseStart) {
            if (!ctx.owner || !ctx.owner->canDiscard(ctx.owner, "h")) return false;
            ctx.owner->throwAllHandCards(objectName());
        }
        return true;
    }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) ctx.targets = {ctx.owner};
        else ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (event == EventPhaseStart) {
            DummyCard obtained;
            QMap<QString, int> remaining;
            for (const QString &name : {QString("slash"), QString("fire_slash"), QString("thunder_slash"), QString("duel")})
                remaining.insert(name, getEffectiveAmount(ctx));
            for (int id : room->getDrawPile()) {
                const QString name = Sanguosha->getCard(id)->objectName();
                if (remaining.value(name) <= 0) continue;
                --remaining[name];
                obtained.addSubcard(id);
            }
            if (!obtained.getSubcards().isEmpty()) target->obtainCard(&obtained);
            return false;
        }
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        damage.damage += getEffectiveAmount(ctx);
        ctx.original_data->setValue(damage);
        return false;
    }
};
class IfBashi : public TriggerSkillV2
{
public:
    IfBashi() : TriggerSkillV2("ifbashi") { events << CardUsed; frequency = Compulsory; }
    static bool firstName(Room *room, ServerPlayer *actor, const Card *card)
    {
        const QVariantMap history = room->queryCardHistory(actor);
        if (!card || !history.value("complete").toBool()) return false;
        int count = 0;
        for (const QVariant &entry : history.value("items").toList()) {
            const QVariantMap used = entry.toMap();
            const QString name = used.value("classes").toStringList().contains("Slash") ? "slash" : used.value("name").toString();
            if (name == card->objectName(false)) ++count;
        }
        return count == 1;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (player && use.from == player && use.card && use.card->isDamageCard() && firstName(room, player, use.card))
            addInstances(result, player, objectName());
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !ctx.invoker || getEffectiveAmount(ctx) <= 0) return false;
        use.m_addHistory = false;
        use.to.clear();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.invoker))
            if (!ctx.invoker->isProhibited(other, use.card)) candidates << other;
        *ctx.original_data = QVariant::fromValue(use);
        ctx.manual_effect = true;
        QStringList suits{"spade", "club", "diamond", "heart", "no_suit"};
        // Each target is added only inside its accepted recipient hook.
        for (ServerPlayer *target : candidates) {
            SkillContext choice = ctx;
            choice.choice = "choose";
            choice.extra_data = suits;
            skillEffect(event, room, owner, choice, target);
            suits = choice.extra_data.toStringList();
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "obtain") {
            ServerPlayer *from = room->findPlayerByObjectName(ctx.extra_data.toString());
            if (!from || !from->isAlive()) return false;
            QList<int> ids;
            const int count = qMin(2 * getEffectiveAmount(ctx), from->getCardCount());
            for (int i = 0; i < count; ++i) {
                const int id = room->askForCardChosen(target, from, "he", objectName(), false, Card::MethodNone, ids, true);
                if (id < 0 || room->getCardOwner(id) != from || ids.contains(id) || Sanguosha->getCard(id)->hasFlag("using")) break;
                ids << id;
            }
            for (int id : QList<int>(ids)) if (room->getCardOwner(id) != from
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using")) ids.removeOne(id);
            if (!ids.isEmpty()) { DummyCard taken(ids); room->obtainCard(target, &taken, false); }
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        CardUseStruct admitted = ctx.original_data->value<CardUseStruct>();
        if (!admitted.card || ctx.invoker->isProhibited(target, admitted.card)) return false;
        if (!admitted.to.contains(target)) admitted.to << target;
        room->sortByActionOrder(admitted.to); *ctx.original_data = QVariant::fromValue(admitted);
        QStringList suits = ctx.extra_data.toStringList();
        QStringList choices{"1"};
        for (const Card *card : target->getHandcards())
            if (suits.contains(card->getSuitString()) && target->canDiscard(target, card->getEffectiveId())) {
                choices << "2"; break;
            }
        if (ctx.invoker && ctx.invoker->isAlive()) choices << "3=" + ctx.invoker->objectName();
        const QString chosen = room->askForChoice(target, objectName(), choices.join("+"), *ctx.original_data);
        if (chosen == "1") {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        } else if (chosen == "2") {
            for (int i = 0; i < getEffectiveAmount(ctx) && !suits.isEmpty() && target->isAlive(); ++i) {
                const Card *discarded = room->askForDiscard(target, objectName(), 1, 1, false, false, "", ".|" + suits.join(","));
                if (!discarded || discarded->subcardsLength() != 1) break;
                const Card *physical = Sanguosha->getEngineCard(discarded->getSubcards().first());
                if (physical) suits.removeOne(physical->getSuitString());
            }
        } else if (chosen.startsWith("3=")) {
            SkillContext obtain = ctx;
            obtain.choice = "obtain";
            obtain.extra_data = target->objectName();
            skillEffect(event, room, owner, obtain, ctx.invoker);
        }
        ctx.extra_data = suits;
        return false;
    }
};
class IfYinjue : public TriggerSkillV2
{
public:
    IfYinjue() : TriggerSkillV2("ifyinjue")
    {
        events << TurnedOver << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        for (ServerPlayer *holder : room->getAllPlayers()) {
            if (!holder->isAlive() || !holder->hasSkill(objectName())) continue;
            if (event == TurnedOver && player->faceUp() && !holder->canDiscard(player, "he")) continue;
            if (event == DamageCaused && player->faceUp()) continue;
            addInstances(result, holder, objectName());
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!ctx.owner || !player) return false;
        if (event == TurnedOver) ctx.targets = {player};
        else ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *owner = ctx.owner;
        room->sendCompulsoryTriggerLog(owner, this);
        const int amount = getEffectiveAmount(ctx);
        if (event == TurnedOver) {
            room->doAnimate(1, owner->objectName(), target->objectName());
            if (target->faceUp()) {
                for (int i = 0; i < amount && owner->isAlive() && target->isAlive()
                    && owner->canDiscard(target, "he"); ++i) {
                    const int id = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodDiscard);
                    if (id >= 0 && room->getCardOwner(id) == target && !Sanguosha->getCard(id)->hasFlag("using") && owner->canDiscard(target, id))
                        room->throwCard(id, objectName(), target, owner);
                }
            } else target->drawCards(amount, objectName());
            return false;
        }
        if (!player || player->faceUp()) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target) return false;
        damage.damage += target->faceUp() ? amount : -amount;
        ctx.original_data->setValue(damage);
        return damage.damage < 1;
    }
};
class IfBaqiViewAs : public ViewAsSkillV2
{
public:
    IfBaqiViewAs() : ViewAsSkillV2("ifbaqi") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.activationRef.isValid()) return false;
        const bool prompt = request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "accepted_view_as_effect").toBool();
        if (request.pattern == "@@ifbaqi") return prompt && request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
        return !prompt && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    { return request.pattern == "@@ifbaqi" ? QString("Slash") : QString("IfBaqiCard"); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.pattern != "@@ifbaqi") return ViewAsSkillV2::createCard(request);
        Card *slash = Sanguosha->cloneCard("slash");
        if (slash) slash->setSkillName("_ifbaqi");
        return slash;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
        const Player *target) const override
    { return selected.isEmpty() && target && target->isAlive() && target != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        // Turning over is the chosen payment for this prompted ordinary Slash.
        if (request.pattern == "@@ifbaqi") ctx.initiator->turnOver();
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || ctx.use_card->getTypeId() != Card::TypeSkill) return ContinueEffects;
        if (!ctx.invoker || ctx.targets.size() != 1) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        ServerPlayer *excluded = ctx.targets.first();
        for (ServerPlayer *other : room->getAllPlayers()) {
            if (other == excluded || !other->isAlive()) continue;
            SkillContext prompt = ctx;
            skillEffect(prompt, other);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        Room::AcceptedViewAsEffectScope scope(room, target, objectName(), ctx);
        if (scope.isValid()) room->askForUseCard(target, "@@ifbaqi", "ifbaqi0");
        return ContinueEffects;
    }
};
class IfBaqi : public TriggerSkillV2
{
public:
    IfBaqi() : TriggerSkillV2("ifbaqi") { view_as_skill = new IfBaqiViewAs; }
};
class IfHuanghuang : public TriggerSkillV2
{
public:
    IfHuanghuang() : TriggerSkillV2("ifhuanghuang$")
    {
        events << Death;
        frequency = Skill::Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DeathStruct death = data.value<DeathStruct>();
        if (player && death.who && death.who->getKingdom() == "qun" && death.who->getHandcardNum() > 0
            && player->hasLordSkill(objectName()))
            addInstances(result, player, objectName());
        return result;
    }

    bool allowsDeadTarget(const SkillContext &ctx, const ServerPlayer *target) const override
    {
        return ctx.choice != "obtain" && ctx.original_data
            && ctx.original_data->value<DeathStruct>().who == target;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.original_data->value<DeathStruct>().who};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        if (!death.who || !ctx.owner) return false;
        if (ctx.choice != "obtain") {
            SkillContext obtain = ctx; obtain.choice = "obtain";
            skillEffect(event, room, ctx.owner, obtain, ctx.owner);
            return false;
        }
        QList<int> cards;
        for (int id : death.who->handCards())
            if (room->getCardOwner(id) == death.who && room->getCardPlace(id) == Player::PlaceHand
                && !Sanguosha->getCard(id)->hasFlag("using")) cards << id;
        if (!cards.isEmpty()) { DummyCard obtained(cards); target->obtainCard(&obtained, false); }
        return false;
    }
};
class IfTunshi : public TriggerSkillV2
{
public:
    IfTunshi() : TriggerSkillV2("iftunshi")
    {
        events << Death << RoundEnd;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event != RoundEnd) return false;
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            const QVariantList receipts = holder->getTag("IfTunshiGrants").toList();
            // Applied round-long grants expire even after their granting instance is gone.
            holder->removeTag("IfTunshiGrants");
            for (const QVariant &value : receipts) {
                const QVariantMap grant = value.toMap();
                room->detachSkillFromPlayer(holder, SkillInstanceUtils::formatName(
                    grant.value("skill").toString(), grant.value("instance").toInt()), false, true);
            }
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Death || !player || !player->isAlive() || !player->hasSkill(objectName())
            || !data.value<DeathStruct>().who) return result;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (!ctx.original_data) return false;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        if (!death.who) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        // Tom ruling B (2026-09-29): one applied grant per dead skill instance, even when
        // the beneficiary already has that skill name.
        for (const Skill *skill : death.who->getVisibleSkillList()) {
            if (!target->isAlive()) break;
            if (skill->isAttachedLordSkill()) continue;
            const int grantCount = death.who->getSkillInstanceIds(skill->objectName()).size();
            for (int i = 0; i < grantCount; ++i) {
                if (!target->isAlive()) break;
                QVariantMap receipt = dreamReceipt(room, ctx);
                receipt.insert("skill", skill->objectName());
                const int id = room->acquireSkillFromEffect(target, skill->objectName(), ctx, [&](int committedId) {
                    // Publish expiry ownership before acquisition observers can end the round.
                    receipt.insert("instance", committedId);
                    QVariantList receipts = target->getTag("IfTunshiGrants").toList();
                    receipts << receipt; target->setTag("IfTunshiGrants", receipts);
                });
                if (id > 0 && !target->getTag("IfTunshiGrants").toList().contains(receipt))
                    room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(skill->objectName(), id), false, true);
            }
        }
        return false;
    }
};
class IfTianwei : public TriggerSkillV2
{
public:
    IfTianwei() : TriggerSkillV2("iftianwei") { events << TargetSpecified << RoundEnd; global = true; }
    static QStringList skills(const Player *player)
    {
        QStringList result;
        for (const Skill *skill : player->getVisibleSkillList()) {
            if (skill->isAttachedLordSkill()) continue;
            for (int id : player->getSkillInstanceIds(skill->objectName()))
                result << SkillInstanceUtils::formatName(skill->objectName(), id);
        }
        return result;
    }
    static bool belowCap(Room *room, const SkillContext &ctx, ServerPlayer *target)
    {
        const QVariant round = room->historyScopes().value("round_id");
        if (round.toLongLong() <= 0) return false;
        QVariantMap query{{"round_id", round}, {"from", ctx.owner->objectName()}, {"to", target->objectName()}};
        int total = 0;
        while (true) {
            const QVariantMap page = room->queryActualDamage(query);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap value = entry.toMap().value("data").toMap();
                if (value.value("reason_skill").toString() != "iftianwei") continue;
                // The cap belongs to this exact invoking grant, not another leaf with the same root source.
                if (!value.value("attribution_complete").toBool()) return false;
                if (value.value("activation_owner").toString() == ctx.activationRef.ownerObjectName
                    && value.value("activation_skill").toString() == ctx.activationRef.key.skillName
                    && value.value("activation_instance_id").toInt() == ctx.activationRef.key.instanceID)
                    total += value.value("amount").toInt();
            }
            if (total >= 2) return false;
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark"));
            query.insert("after", page.value("next_after"));
        }
        return true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event != RoundEnd) return false;
        const qint64 round = room->historyScopes().value("round_id").toLongLong();
        QVariantList kept, expired;
        for (const QVariant &entry : room->getTag("IfTianweiInvalidity").toList()) {
            if (entry.toMap().value("round").toLongLong() == round) expired << entry;
            else kept << entry;
        }
        room->setTag("IfTianweiInvalidity", kept);
        for (const QVariant &entry : expired) {
            const QVariantMap receipt = entry.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            if (target) room->removeSkillInvalidity(target, receipt.value("disabled_skill").toString(),
                receipt.value("token").toString(), objectName(), receipt.value("disabled_instance").toInt());
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecified || !player) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from == player && use.card && use.card->isBlack() && use.card->getTypeId() != Card::TypeSkill)
            addInstances(result, player, objectName());
        return result;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : ctx.original_data->value<CardUseStruct>().to)
            if (target != ctx.owner) ctx.targets << target;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!belowCap(room, ctx, target)) return false;
        const QStringList candidates = skills(target);
        const int gap = skills(ctx.owner).size() - candidates.size();
        if (gap < 0 || getEffectiveAmount(ctx) <= 0 || !ctx.owner->askForSkillInvoke(objectName(), target)) return false;
        QStringList choices;
        for (const QString &skill : candidates) choices << "1=" + skill;
        const int damage = qBound(1, gap, 3) * getEffectiveAmount(ctx);
        choices << QString("2=%1").arg(damage);
        const QString selected = room->askForChoice(target, objectName(), choices.join("+"), *ctx.original_data);
        if (!choices.contains(selected)) return false;
        if (selected.startsWith("1=")) {
            QString name;
            const int id = SkillInstanceUtils::parseName(selected.mid(2), name);
            if (id <= 0 || !target->hasSkillInstance(name, id)) return false;
            const QString token = objectName() + ":" + QString::number(nextDreamReceipt(room));
            QVariantList receipts = room->getTag("IfTianweiInvalidity").toList();
            receipts << QVariantMap{{"target", target->objectName()}, {"disabled_skill", name}, {"disabled_instance", id},
                {"round", room->historyScopes().value("round_id")}, {"token", token},
                {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}};
            room->setTag("IfTianweiInvalidity", receipts);
            room->addSkillInvalidity(target, name, token, objectName(), id);
            target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        } else room->damage(DamageStruct(objectName(), ctx.owner, target, damage));
        return false;
    }
};
class IfXiongzhengViewAs : public ViewAsSkillV2
{
public:
    IfXiongzhengViewAs() : ViewAsSkillV2("ifxiongzheng", 1) {}
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.activationRef.isValid() && request.pattern == "@@ifxiongzheng"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID,
                "accepted_view_as_effect").toBool();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return ownsHand(request.initiator, card) && request.selectedCardIds.isEmpty(); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return selectionAccepted(this, request); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Card *slash = Sanguosha->cloneCard("slash");
        if (!slash) return nullptr;
        slash->addSubcards(request.selectedCardIds); slash->setSkillName(objectName());
        slash->setTag("IfXiongzhengTarget", request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "target"));
        return slash;
    }
};
class IfXiongzheng : public TriggerSkillV2
{
public:
    IfXiongzheng(const QString &name = "ifxiongzheng") : TriggerSkillV2(name)
    {
        global = true;
        if (name == "ifxiongzheng") { events << RoundStart << Damaged; view_as_skill = new IfXiongzhengViewAs; }
        else { events << CardFinished << RoundEnd; frequency = Compulsory; }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event == RoundEnd) room->removeTag("IfXiongzhengApplied");
        return false;
    }
    static QList<ServerPlayer *> candidates(Room *room, const SkillContext &ctx)
    {
        QList<ServerPlayer *> result = room->getOtherPlayers(ctx.owner);
        for (const QVariant &entry : room->getTag("IfXiongzhengApplied").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (dreamActivation(receipt) != ctx.activationRef) continue;
            for (ServerPlayer *target : QList<ServerPlayer *>(result))
                if (target->objectName() == receipt.value("target").toString()) result.removeOne(target);
        }
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event == RoundStart || event == Damaged) return false;
        if (event != CardFinished) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.from) return true;
        for (const QVariant &entry : room->getTag("IfXiongzhengApplied").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("target").toString() != use.from->objectName() || !receipt.contains("type")) continue;
            SkillContext ctx = dreamContinuation(room, objectName(), event, actor, data, receipt);
            if (!ctx.owner || !ctx.owner->isAlive() || !ctx.sourceRef.isValid()) continue;
            ctx.choice = use.card->getType() == receipt.value("type").toString() ? "draw" : "slash";
            ctx.targets = {ctx.owner}; ctx.preferredTarget = ctx.owner; out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.sourceRef.isValid() && room->getTag("IfXiongzhengApplied").toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == RoundStart || event == Damaged) addInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardFinished) return true;
        const QList<ServerPlayer *> targets = candidates(room, ctx);
        if (targets.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "ifxiongzheng0", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (event == CardFinished) {
            if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), "ifxiongzheng"); return false; }
            ServerPlayer *victim = room->findPlayerByObjectName(ctx.extra_data.toMap().value("target").toString());
            if (!victim || !victim->isAlive() || target->isKongcheng()) return false;
            SkillContext accepted = ctx; accepted.activationRef = dreamActivation(ctx.extra_data.toMap());
            Room::AcceptedViewAsEffectScope scope(room, target, "ifxiongzheng", accepted);
            if (!scope.isValid()) return false;
            target->setSkillInstanceStateValue("ifxiongzheng", scope.activationRef().key.instanceID, "target", victim->objectName());
            const QVariant previous = target->property("ifxiongzhengSlash");
            const auto restore = qScopeGuard([&] { room->setPlayerProperty(target, "ifxiongzhengSlash", previous); });
            room->setPlayerProperty(target, "ifxiongzhengSlash", victim->objectName());
            room->askForUseCard(target, "@@ifxiongzheng", "ifxiongzheng1:" + victim->objectName());
            return false;
        }
        if (!candidates(room, ctx).contains(target)) return false;
        QVariantMap receipt = dreamReceipt(room, ctx);
        receipt.insert("target", target->objectName());
        // Selection itself is spent even when the selected character has no hand.
        QVariantList receipts = room->getTag("IfXiongzhengApplied").toList();
        receipts << receipt; room->setTag("IfXiongzhengApplied", receipts);
        if (target->isKongcheng()) return false;
        const int id = room->askForCardChosen(ctx.owner, target, "h", objectName());
        if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) return false;
        const QString type = Sanguosha->getCard(id)->getType();
        receipt.insert("type", type);
        receipts = room->getTag("IfXiongzhengApplied").toList();
        for (QVariant &entry : receipts) if (entry.toMap().value("serial") == receipt.value("serial")) entry = receipt;
        room->setTag("IfXiongzhengApplied", receipts);
        room->showCard(target, id);
        room->setPlayerMark(target, "&ifxiongzheng+:+" + type + "+#" + ctx.owner->objectName() + "_lun", 1);
        return false;
    }
};
class IfXiongzhengBf : public ProhibitSkill
{
public:
    IfXiongzhengBf() : ProhibitSkill("#IfXiongzhengBf") {}
    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return card && to && card->getSkillName() == "ifxiongzheng"
            && card->getTag("IfXiongzhengTarget").toString() != to->objectName();
    }
};
IfEjiangCard::IfEjiangCard() { setSkillName("ifejiang"); }
bool IfEjiangCard::targetFilter(const QList<const Player *> &targets, const Player *candidate, const Player *self) const
{
    if (targets.isEmpty() && self->property("ifejiangTo").toString() != candidate->objectName()) return false;
    IronChain chain(Card::NoSuit, 0);
    return chain.targetFilter(targets, candidate, self);
}
const Card *IfEjiangCard::validate(CardUseStruct &) const
{ return nullptr; } // V2 rebuild owns quota, target admission and the ordinary-card replacement.

class IfEjiangViewAs : public ViewAsSkillV2
{
public:
    IfEjiangViewAs() : ViewAsSkillV2("ifejiang") {}
    LimitScope getLimitScope() const override { return Limit_Custom; }
    static QString quotaKey(int id) { return QString("ifejiang_%1-Clear").arg(id); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.pattern != "@@ifejiang" || request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return false;
        const QString key = request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "quota_key").toString();
        return !key.isEmpty() && request.initiator->getMark(key) == 0;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        Card *card = const_cast<Card *>(ViewAsSkillV2::createCard(request));
        card->setTag("IfEjiangQuota", request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "quota_key"));
        card->setTag("IfEjiangTarget", request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "target"));
        return card;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive() || selected.contains(target)) return false;
        const QString forced = request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "target").toString();
        if (selected.isEmpty() && target->objectName() != forced) return false;
        IronChain chain(Card::NoSuit, 0);
        return chain.targetFilter(selected, target, request.initiator) && !request.initiator->isProhibited(target, &chain, selected);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        if (targets.isEmpty()) return false;
        QList<const Player *> prefix;
        for (const Player *target : targets) { if (!canSelectTarget(request, prefix, target)) return false; prefix << target; }
        IronChain chain(Card::NoSuit, 0); return chain.targetsFeasible(targets, request.initiator);
    }
    static void commitAccepted(SkillContext &ctx)
    {
        if (!ctx.initiator || !ctx.use_card || ctx.extra_data.toMap().value("committed").toBool()) return;
        const QString key = ctx.use_card->getTag("IfEjiangQuota").toString();
        if (key.isEmpty() || ctx.initiator->getMark(key) > 0) return;
        QVariantMap receipt = ctx.extra_data.toMap(); receipt.insert("committed", true); ctx.extra_data = receipt;
        ctx.initiator->getRoom()->addPlayerMark(ctx.initiator, key);
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        QList<const Player *> targets; for (ServerPlayer *target : ctx.targets) targets << target;
        if (!canActivate(request) || !targetsFeasible(request, targets)) return false;
        commitAccepted(ctx); return ctx.extra_data.toMap().value("committed").toBool();
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.use_card || !ctx.extra_data.toMap().value("committed").toBool()) return FinishSkill;
        if (ctx.targets.isEmpty() || ctx.targets.first()->objectName() != ctx.use_card->getTag("IfEjiangTarget").toString()) return FinishSkill;
        QList<ServerPlayer *> targets;
        for (ServerPlayer *target : ctx.targets) {
            SkillContext selected = ctx; selected.choice.clear();
            skillEffect(selected, target);
            if (selected.choice == "accepted") targets << target;
        }
        if (targets.isEmpty()) return FinishSkill;
        IronChain *chain = new IronChain(Card::NoSuit, 0); chain->setSkillName(objectName()); chain->setCanRecast(false);
        CardUseStruct use(chain, ctx.invoker, targets); use.setOwnedCard(chain);
        ctx.invoker->getRoom()->useCardFromSkillEffect(use, ctx);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *) const override { if (getEffectiveAmount(ctx) <= 0) return ContinueEffects; ctx.choice = "accepted"; return ContinueEffects; }
};
class IfEjiang : public TriggerSkillV2
{
public:
    IfEjiang() : TriggerSkillV2("ifejiang") { events << Damaged << EventSkillInvoking; global = true; view_as_skill = new IfEjiangViewAs; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            SkillContext accepted = data.value<SkillContext>();
            if (accepted.activationRef.key.skillName == objectName() && accepted.use_card && accepted.use_card->getTypeId() == Card::TypeSkill) {
                IfEjiangViewAs::commitAccepted(accepted); data = QVariant::fromValue(accepted);
            }
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == Damaged && player && player->isAlive()) for (ServerPlayer *holder : room->getAlivePlayers()) addInstances(result, holder, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner->getMark(IfEjiangViewAs::quotaKey(ctx.activationRef.key.instanceID)) > 0) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (!ctx.invoker || !ctx.invoker->isAlive()) return false;
        Room::AcceptedViewAsEffectScope scope(room, target, objectName(), ctx);
        if (!scope.isValid()) return false;
        target->setSkillInstanceStateValue(objectName(), scope.activationRef().key.instanceID, "target", ctx.invoker->objectName());
        target->setSkillInstanceStateValue(objectName(), scope.activationRef().key.instanceID, "quota_key", IfEjiangViewAs::quotaKey(ctx.activationRef.key.instanceID));
        const QVariant previous = target->property("ifejiangTo");
        const auto restore = qScopeGuard([&] { room->setPlayerProperty(target, "ifejiangTo", previous); });
        room->setPlayerProperty(target, "ifejiangTo", ctx.invoker->objectName());
        room->askForUseCard(target, "@@ifejiang", "ifejiang0:" + ctx.invoker->objectName());
        return false;
    }
};
class IfPini : public TriggerSkillV2
{
public:
    IfPini() : TriggerSkillV2("ifpini") { events << ChainStateChanged << Dying; }
    static QList<ServerPlayer *> recipients(Room *room, ServerPlayer *owner, ServerPlayer *from, int id)
    {
        QList<ServerPlayer *> result;
        if (!owner || !from || room->getCardOwner(id) != from) return result;
        const Card *card = Sanguosha->getCard(id);
        const Player::Place place = room->getCardPlace(id);
        if (!card || card->hasFlag("using") || (place != Player::PlaceHand && place != Player::PlaceEquip && place != Player::PlaceJudge)) return result;
        for (ServerPlayer *to : room->getOtherPlayers(from)) {
            if (!to->isAlive() || to->isChained() != from->isChained()) continue;
            if (place != Player::PlaceHand && owner->isProhibited(to, card)) continue;
            if (place == Player::PlaceEquip) {
                const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
                if (!equip || !to->hasEquipArea(equip->location()) || to->getEquip(equip->location())) continue;
            }
            if (place == Player::PlaceJudge && to->containsTrick(card->objectName())) continue;
            result << to;
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || (event == ChainStateChanged && player->isChained())
            || (event == Dying && data.value<DyingStruct>().who != player)) return result;
        for (ServerPlayer *holder : room->getAllPlayers()) addInstances(result, holder, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.invoker;
        if (!from || !ctx.owner) return false;
        QList<int> banned;
        for (const Card *card : from->getCards("hej"))
            if (recipients(room, ctx.owner, from, card->getEffectiveId()).isEmpty()) banned << card->getEffectiveId();
        if (banned.size() >= from->getCardCount(true, true) || !ctx.owner->askForSkillInvoke(objectName(), from)) return false;
        const int id = room->askForCardChosen(ctx.owner, from, "hej", objectName(), false, Card::MethodNone, banned);
        if (id < 0) return false;
        const QList<ServerPlayer *> choices = recipients(room, ctx.owner, from, id);
        if (choices.isEmpty()) return false;
        const int previous = ctx.owner->getMark("ifpiniId");
        const auto restore = qScopeGuard([&] { ctx.owner->setMark("ifpiniId", previous); });
        ctx.owner->setMark("ifpiniId", id);
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, choices, objectName(), "ifpini0");
        if (!target) return false;
        ctx.targets = {from};
        ctx.extra_data = QVariantMap{{"to", target->objectName()}, {"from", from->objectName()}, {"card", id}, {"place", int(room->getCardPlace(id))}};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        const QVariantMap selection = ctx.extra_data.toMap();
        if (ctx.choice != "move") {
            ServerPlayer *recipient = room->findPlayerByObjectName(selection.value("to").toString());
            if (!recipient) return false;
            SkillContext move = ctx; move.choice = "move";
            return skillEffect(event, room, ctx.owner, move, recipient);
        }
        ServerPlayer *from = room->findPlayerByObjectName(selection.value("from").toString());
        const int id = selection.value("card").toInt();
        if (!from || int(room->getCardPlace(id)) != selection.value("place").toInt()
            || !recipients(room, ctx.owner, from, id).contains(target)) return false;
        // Selection is a physical-card receipt; interception cannot spend a newly moved card.
        room->moveCardTo(Sanguosha->getCard(id), target, room->getCardPlace(id),
            CardMoveReason(CardMoveReason::S_REASON_TRANSFER, ctx.owner->objectName(), objectName(), ""), false);
        return false;
    }
};
class IfLianque : public TriggerSkillV2
{
public:
    IfLianque() : TriggerSkillV2("iflianque$") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Start || player->getKingdom() != "wei") return result;
        for (ServerPlayer *lord : room->getOtherPlayers(player)) {
            if (lord->hasLordSkill(objectName())) addInstances(result, lord, objectName());
        }
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        return player && ctx.owner && player->askForSkillInvoke(objectName(), "0:" + ctx.owner->objectName(), false);
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !ctx.owner) return false;
        player->skillInvoked(objectName(), -1, ctx.owner);
        SkillContext child = ctx;
        child.choice = "damage";
        skillEffect(event, room, player, child, player);
        child = ctx;
        child.choice = "draw";
        skillEffect(event, room, player, child, player);
        child = ctx;
        child.choice = "draw";
        skillEffect(event, room, player, child, ctx.owner);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (ctx.choice == "damage")
            room->damage(DamageStruct(objectName(), player, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        else target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class IfMoran : public TriggerSkillV2
{
public:
    IfMoran(const QString &name = "ifmoran") : TriggerSkillV2(name)
    {
        global = true;
        if (name == "ifmoran") events << CardUsed << PreCardUsed;
        else { events << CardFinished << EventSkillEffectFinished; frequency = Compulsory; }
    }
    static void consume(const SkillContext &ctx)
    {
        const Card *card = ctx.original_data ? ctx.original_data->value<CardUseStruct>().card : nullptr;
        if (!card) return;
        QVariantList receipts = card->getTag("IfMoranApplied").toList();
        receipts.removeAll(ctx.extra_data);
        card->setTag("IfMoranApplied", receipts);
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == PreCardUsed) {
            const Card *card = data.value<CardUseStruct>().card;
            if (card) card->removeTag("IfMoranApplied");
        } else if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && !ctx.activationRef.isValid()) consume(ctx);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event == CardUsed) return false;
        if (event != CardFinished) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card) return true;
        const QVariantMap damage = room->queryCardUseDamage(use.targetModReveal.useHistoryEventId);
        if (!damage.value("complete").toBool() || damage.contains("error") || damage.value("items").toList().isEmpty()) {
            use.card->removeTag("IfMoranApplied"); return true;
        }
        for (const QVariant &entry : use.card->getTag("IfMoranApplied").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("use_event").toLongLong() != use.targetModReveal.useHistoryEventId) continue;
            SkillContext ctx = dreamContinuation(room, objectName(), event, actor, data, receipt);
            if (!ctx.owner || !ctx.owner->isAlive() || !ctx.sourceRef.isValid()) continue;
            ctx.targets = {ctx.owner}; out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ctx.original_data->value<CardUseStruct>().card : nullptr;
        return ctx.sourceRef.isValid() && card && card->getTag("IfMoranApplied").toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (event != CardUsed || !actor || !actor->isWounded() || !use.card
            || !use.card->isKindOf("NatureSlash") || use.targetModReveal.useHistoryEventId <= 0) return result;
        for (ServerPlayer *holder : room->getAlivePlayers()) addInstances(result, holder, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardFinished) return true;
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Arrival consumes the retained opportunity even if its recipient hook cancels.
        if (event == CardFinished) consume(ctx);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (event == CardFinished) { target->drawCards(2 * getEffectiveAmount(ctx), "ifmoran"); return false; }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        QVariantMap receipt = dreamReceipt(room, ctx);
        receipt.insert("use_event", use.targetModReveal.useHistoryEventId);
        QVariantList receipts = use.card->getTag("IfMoranApplied").toList();
        receipts << receipt; use.card->setTag("IfMoranApplied", receipts);
        // Native single-turn limitations expire independently of the granting source.
        room->setPlayerCardLimitation(target, "use", ".", true,
            "ifmoran:" + QString::number(receipt.value("serial").toLongLong()));
        return false;
    }
};
class IfJilveViewAs : public ViewAsSkillV2
{
public:
    IfJilveViewAs() : ViewAsSkillV2("ifjilve", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "IfJilveCard"; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ownsHand(request.initiator, card) && request.selectedCardIds.isEmpty();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return selectionAccepted(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return selected.isEmpty() && target && target->isAlive() && target->getPile("ifji_bing").isEmpty();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (!ctx.use_card || !ctx.invoker || ctx.use_card->subcardsLength() != 1 || !target->getPile("ifji_bing").isEmpty()) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        if (!ctx.initiator || !ctx.initiator->handCards().contains(id)
            || target->getRoom()->getCardOwner(id) != ctx.initiator || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        // The placed card retains its original grant independently of the current skill population.
        QVariantMap receipt = dreamReceipt(target->getRoom(), ctx); receipt.insert("card", id);
        target->setTag("IfJilveApplied", receipt);
        target->addToPile("ifji_bing", {id}, false);
        return ContinueEffects;
    }
};

class IfJilve : public TriggerSkillV2
{
public:
    IfJilve() : TriggerSkillV2("ifjilve")
    { events << DamageInflicted; global = true; view_as_skill = new IfJilveViewAs; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (!player) return true;
        const QVariantMap receipt = player->getTag("IfJilveApplied").toMap();
        if (receipt.isEmpty() || !player->getPile("ifji_bing").contains(receipt.value("card").toInt())) return true;
        SkillContext ctx = dreamContinuation(room, objectName(), event, player, data, receipt);
        if (!ctx.owner || !ctx.sourceRef.isValid()) return true;
        ctx.targets = {player};
        out << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->getTag("IfJilveApplied") == ctx.extra_data; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "transfer") {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            ServerPlayer *original = damage.to;
            damage.to = target; damage.transfer = true; damage.transfer_reason = objectName();
            *ctx.original_data = QVariant::fromValue(damage);
            original->setTag("TransferDamage", *ctx.original_data);
            return true;
        }
        const int id = ctx.extra_data.toMap().value("card").toInt();
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target || !target->getPile("ifji_bing").contains(id)) return false;
        target->removeTag("IfJilveApplied");
        const Card::Color color = Sanguosha->getEngineCard(id)->getColor();
        const bool transfer = damage.from && damage.card && damage.card->getColor() != color;
        room->throwCard(QList<int>{id}, objectName(), nullptr);
        if (transfer) {
            SkillContext child = ctx; child.choice = "transfer";
            return skillEffect(event, room, ctx.owner, child, damage.from);
        }
        return false;
    }
};
class IfBujia : public TriggerSkillV2
{
public:
    IfBujia() : TriggerSkillV2("ifbujia") { events << EventPhaseChanging; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->hasFlag("CurrentPlayer")
            || data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (turn <= 0) return false;
        QVariantMap query{{"turn_id", turn}};
        int net = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool()) return false;
            // Explicit endpoints establish hand changes independently of causing skill ownership.
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap();
                if (!move.contains("from_place") || !move.contains("to_place")) return false;
                if (move.value("to").toString() == ctx.owner->objectName()
                    && move.value("to_place").toInt() == Player::PlaceHand) ++net;
                if (move.value("from").toString() == ctx.owner->objectName()
                    && move.value("from_place").toInt() == Player::PlaceHand) --net;
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark"));
            query.insert("after", page.value("next_after"));
        }
        ctx.choice = net < 0 ? "lose-hp" : "recover";
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        if (ctx.choice == "lose-hp") room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
        else room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        return false;
    }
};
DreamPackage::DreamPackage()
    : Package("qs_dream")
{
    General *if_liuxie = new General(this, "if_liuxie", "qun", 3);
    if_liuxie->addSkill(new IfAnxu);
    if_liuxie->addSkill(new IfMishou);
    if_liuxie->addSkill(new IfMishouBf);
    if_liuxie->addSkill(new IfDianbian);
    if_liuxie->addSkill(new IfDianbian("#ifdianbian-effect"));
    related_skills.insert("ifdianbian", "#ifdianbian-effect");
    skills << new IfPiyong;

    General *if_pangtong = new General(this, "if_pangtong", "shu", 3);
    if_pangtong->addSkill(new IfXiance);
    if_pangtong->addSkill(new IfZhenshi);
    skills << new IfJiusuo;

    General *if_zhouyu = new General(this, "if_zhouyu", "wu", 4);
    if_zhouyu->addSkill(new IfYinglve);
    if_zhouyu->addSkill(new IfBihe);

    General *if_zhangjiao = new General(this, "if_zhangjiao", "qun", 3);
    if_zhangjiao->addSkill(new IfShiji);
    if_zhangjiao->addSkill(new IfAnjie);
    if_zhangjiao->addSkill(new IfLitian);
    skills << new IfLitian2 << new IfHuangchu;
    addMetaObject<IfAnjieCard>();

    General *if_liushan = new General(this, "if_liushan$", "shu", 3);
    if_liushan->addSkill(new IfRenli);
    if_liushan->addSkill(new IfMingduan);
    if_liushan->addSkill(new IfSixiang);
    if_liushan->addSkill(new IfJizhi);
    skills << new IfJizhiViewAs;
    addMetaObject<IfSixiangCard>();
    addMetaObject<IfJizhiCard>();

    General *if_guanyu = new General(this, "if_guanyu", "shu", 4);
    if_guanyu->addSkill(new IfHaitian);
    if_guanyu->addSkill(new IfShenfeng);

    General *if_liubei = new General(this, "if_liubei", "shu", 4);
    if_liubei->addSkill(new IfXiechang);
    if_liubei->addSkill(new IfXiechang("#ifxiechang-effect"));
    related_skills.insert("ifxiechang", "#ifxiechang-effect");
    if_liubei->addSkill(new IfTianmin);
    if_liubei->addSkill(new IfJianxiao);

    General *if_lvbu = new General(this, "if_lvbu", "qun", 5);
    if_lvbu->addSkill(new IfShenwu);
    if_lvbu->addSkill(new IfBashi);

    General *if_liuhong = new General(this, "if_liuhong$", "qun", 4);
    if_liuhong->addSkill(new IfYinjue);
    if_liuhong->addSkill(new IfBaqi);
    if_liuhong->addSkill(new IfHuanghuang);

    General *if_caopi = new General(this, "if_caopi", "wei", 3);
    if_caopi->addSkill(new IfTunshi);
    if_caopi->addSkill(new IfTianwei);
    if_caopi->addSkill(new IfXiongzheng);
    if_caopi->addSkill(new IfXiongzheng("#ifxiongzheng-effect"));
    related_skills.insert("ifxiongzheng", "#ifxiongzheng-effect");
    if_caopi->addSkill(new IfXiongzhengBf);

    General *if_caocao = new General(this, "if_caocao$", "wei", 4);
    if_caocao->addSkill(new IfEjiang);
    if_caocao->addSkill(new IfPini);
    if_caocao->addSkill(new IfLianque);
    addMetaObject<IfEjiangCard>();

    General *if_fazheng = new General(this, "if_fazheng", "shu", 4, true, false, false, 2);
    if_fazheng->addSkill(new IfMoran);
    if_fazheng->addSkill(new IfMoran("#ifmoran-effect"));
    related_skills.insert("ifmoran", "#ifmoran-effect");
    if_fazheng->addSkill(new IfJilve);
    if_fazheng->addSkill(new IfBujia);
}

ADD_PACKAGE(Dream)

