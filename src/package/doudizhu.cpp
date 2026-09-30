#include "doudizhu.h"
#include "qt-collection-utils.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
#include "wind.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
#include <memory>

namespace {
qint64 ddzCurrentUse(Room *room)
{
    qint64 id = room->currentHistoryEventId();
    while (id > 0) {
        const QVariantMap event = room->historyEvent(id);
        const QString kind = event.value("kind").toString();
        if (kind == "use_card" || kind == "respond_card") return id;
        id = event.value("parent_id").toLongLong();
    }
    return 0;
}

qint64 ddzProvidedResponse(Room *room)
{
    const QVariantMap event = room->historyEvent(ddzCurrentUse(room));
    if (event.value("kind").toString() != "respond_card") return 0;
    const qint64 original = event.value("data").toMap().value("provenance").toMap()
        .value("paid_provision").toMap().value("response_event_id").toLongLong();
    return original > 0 && room->historyEvent(original).value("kind").toString() == "respond_card" ? original : 0;
}

QVariantMap ddzAppliedReceipt(Room *room, const SkillContext &ctx, int amount)
{
    const qint64 serial = room->getTag("DdzAppliedReceiptSequence").toLongLong() + 1;
    room->setTag("DdzAppliedReceiptSequence", serial);
    // Dispatch identity is per application; provenance keeps the original grant.
    return {{"serial", serial}, {"owner", ctx.activationRef.ownerObjectName},
        {"instance", ctx.activationRef.key.instanceID}, {"activation_skill", ctx.activationRef.key.skillName},
        {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
        {"source_instance", ctx.sourceRef.key.instanceID}, {"amount", amount},
        {"turn", room->historyScopes().value("turn_id")}, {"phase", room->historyScopes().value("phase_id")}};
}

SkillInstanceRef ddzReceiptSource(const QVariantMap &receipt)
{
    return SkillInstanceRef(receipt.value("source_owner").toString(),
        SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
}

bool ddzSameActivation(const QVariantMap &left, const QVariantMap &right)
{
    return left.value("owner") == right.value("owner") && left.value("activation_skill") == right.value("activation_skill")
        && left.value("instance") == right.value("instance");
}

void ddzExpireReceipts(Room *room, ServerPlayer *eventPlayer, TriggerEvent event, const QVariant &data,
    const QString &tag, bool playPhase = false)
{
    const bool broken = event == TurnBroken;
    const PhaseChangeStruct change = event == EventPhaseChanging ? data.value<PhaseChangeStruct>() : PhaseChangeStruct();
    const bool turnEnd = broken || (event == EventPhaseChanging && change.to == Player::NotActive);
    if (!turnEnd && !(playPhase && event == EventPhaseChanging && change.from == Player::Play)) return;
    const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
    if (turn <= 0) return;
    for (ServerPlayer *holder : room->getAllPlayers(true)) {
        QVariantList kept;
        for (const QVariant &value : holder->getTag(tag).toList()) {
            const QVariantMap receipt = value.toMap();
            const QVariantMap phase = room->historyEvent(receipt.value("phase").toLongLong());
            // Changing already entered the next scope; inspect the saved previous phase.
            const bool leftPlay = playPhase && eventPlayer && phase.value("status").toString() == "finished"
                && phase.value("data").toMap().value("player").toString() == eventPlayer->objectName();
            if (receipt.value("turn").toLongLong() != turn || (!turnEnd && !leftPlay)) kept << value;
        }
        if (kept.isEmpty()) holder->removeTag(tag);
        else holder->setTag(tag, kept);
    }
}

bool ddzPaidMaterial(Room *room, ServerPlayer *payer, int id, Player::Place place,
    const QString &skill, qint64 skillEvent, const QVariant &after, ServerPlayer *receiver = nullptr)
{
    QVariantMap filter{{"from", payer->objectName()}, {"after", after}};
    bool paid = false;
    for (;;) {
        const QVariantMap page = room->queryHistoryMoves(filter);
        if (!page.value("complete").toBool()) return false;
        for (const QVariant &value : page.value("items").toList()) {
            const QVariantMap fact = value.toMap(), move = fact.value("data").toMap();
            if (move.value("card_id").toInt() == id && move.value("from_place").toInt() == place
                && move.value("to_place").toInt() == (receiver ? Player::PlaceHand : Player::DiscardPile)
                && (!receiver || move.value("to").toString() == receiver->objectName())
                && move.value("reason_skill").toString() == skill
                && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON)
                    == (receiver ? CardMoveReason::S_REASON_GIVE : CardMoveReason::S_REASON_DISCARD)
                && room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() == skillEvent)
                paid = true;
        }
        if (!page.value("has_more").toBool()) return paid;
        filter.insert("watermark", page.value("watermark"));
        filter.insert("after", page.value("next_after"));
    }
}

bool ddzDiscard(Room *room, ServerPlayer *payer, ServerPlayer *chooser, int id, const QString &skill)
{
    const Card *card = Sanguosha->getCard(id);
    const Player::Place place = room->getCardPlace(id);
    if (!payer || !chooser || !payer->isAlive() || !card || card->hasFlag("using")
        || room->getCardOwner(id) != payer || (place != Player::PlaceHand && place != Player::PlaceEquip)
        || !chooser->canDiscard(payer, id)) return false;
    const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
    const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
    if (!before.value("complete").toBool() || skillEvent <= 0) return false;
    room->throwCard(id, skill, payer, chooser);
    return ddzPaidMaterial(room, payer, id, place, skill, skillEvent, before.value("watermark"));
}

QVariantList ddzCardReceipts(Room *room, const Card *card, const QString &key)
{
    if (!card) return {};
    const qint64 use = ddzCurrentUse(room);
    if (use <= 0) return {}; // Missing history identity cannot identify a pending card effect.
    const qint64 provided = ddzProvidedResponse(room);
    QVariantList result;
    for (const QVariant &value : card->getTag(key).toList())
        if (value.toMap().value("use_event").toLongLong() == use
            || (provided > 0 && value.toMap().value("use_event").toLongLong() == provided)) result << value;
    return result;
}

void ddzSetCardReceipts(Room *room, const Card *card, const QString &key, const QVariantList &receipts)
{
    if (!card) return;
    const qint64 use = ddzCurrentUse(room);
    if (use <= 0) return;
    const qint64 provided = ddzProvidedResponse(room);
    // A physical card can be reused inside its own discard callback. Keep only
    // live ancestor uses; never erase their receipts or apply them to this use.
    QSet<qint64> ancestors;
    qint64 parent = room->historyEvent(use).value("parent_id").toLongLong();
    while (parent > 0 && !ancestors.contains(parent)) {
        ancestors.insert(parent);
        parent = room->historyEvent(parent).value("parent_id").toLongLong();
    }
    QVariantList saved;
    for (const QVariant &value : card->getTag(key).toList())
        if (ancestors.contains(value.toMap().value("use_event").toLongLong())
            && value.toMap().value("use_event").toLongLong() != provided) saved << value;
    for (const QVariant &value : receipts) {
        QVariantMap receipt = value.toMap();
        // Middle providers retain the first paid response identity, just as the native handoff does.
        receipt.insert("use_event", provided > 0 ? provided : use);
        saved << receipt;
    }
    if (saved.isEmpty()) card->removeTag(key);
    else card->setTag(key, saved);
}

void ddzResetCardReceipts(Room *room, const Card *card, const QString &key)
{
    QVariantList linked;
    const qint64 provided = ddzProvidedResponse(room);
    if (provided > 0)
        for (const QVariant &value : ddzCardReceipts(room, card, key))
            if (value.toMap().value("use_event").toLongLong() == provided) linked << value;
    ddzSetCardReceipts(room, card, key, linked);
}
}

FeiyangCard::FeiyangCard()
{
    //mute = true;
    target_fixed = true;
    setSkillName("feiyang");
    //will_throw = false;
}

void FeiyangCard::onUse(Room *, CardUseStruct &) const
{
}

class FeiyangVS : public ViewAsSkillV2
{
public:
    static QList<int> judgingCards(const Player *player)
    {
        // The server retains the judging area; the client temporarily displays a virtual pile.
        QList<int> ids = player->getJudgingAreaID();
        for (int id : player->getPile("#feiyang")) if (!ids.contains(id)) ids << id;
        return ids;
    }
    FeiyangVS() : ViewAsSkillV2("feiyang", 3)
    {
        response_pattern = "@@feiyang";
        expand_pile = "#feiyang";
        attached_lord_skill = true;
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.initiator->getPhase() == Player::Judge
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern == "@@feiyang";
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || request.selectedCardIds.size() >= 3) return false;
        const int id = card->getEffectiveId();
        if (request.selectedCardIds.contains(id) || !request.initiator->canDiscard(request.initiator, id)) return false;
        const QList<int> judging = judgingCards(request.initiator);
        int selectedJudging = 0;
        for (int selected : request.selectedCardIds) if (judging.contains(selected)) ++selectedJudging;
        if (judging.contains(id)) return selectedJudging == 0;
        return request.initiator->handCards().contains(id)
            && request.selectedCardIds.size() - selectedJudging < 2;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 3) return false;
        int judging = 0, hand = 0;
        QSet<int> unique;
        for (int id : request.selectedCardIds) {
            if (!Sanguosha->getCard(id) || Sanguosha->getCard(id)->hasFlag("using") || unique.contains(id) || !request.initiator->canDiscard(request.initiator, id)) return false;
            unique.insert(id);
            if (judgingCards(request.initiator).contains(id)) ++judging;
            else if (request.initiator->handCards().contains(id)) ++hand;
            else return false;
        }
        return judging == 1 && hand == 2;
    }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "FeiyangCard"; }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        for (int id : request.selectedCardIds)
            if (judgingCards(request.initiator).contains(id)) ctx.extra_data = id;
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !cardSelectionFeasible(request)) return false;
        DummyCard payment;
        for (int id : request.selectedCardIds)
            if (id != ctx.extra_data.toInt()) payment.addSubcard(id);
        // The delayed trick is the effect's object, never part of the two-card payment.
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
        if (!before.value("complete").toBool() || skillEvent <= 0) return false;
        room->throwCard(&payment, objectName(), ctx.initiator);
        for (int id : payment.getSubcards())
            if (!ddzPaidMaterial(room, ctx.initiator, id, Player::PlaceHand, objectName(), skillEvent, before.value("watermark"))) return false;
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.extra_data.isValid() && ctx.initiator && ctx.use_card)
            for (int id : ctx.use_card->getSubcards())
                if (judgingCards(ctx.initiator).contains(id)) ctx.extra_data = id;
        if (ctx.initiator && ctx.extra_data.isValid()) skillEffect(ctx, ctx.initiator);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceDelayedTrick
            && target->canDiscard(target, id))
            room->throwCard(id, objectName(), target);
        return ContinueEffects;
    }
};

class Feiyang : public TriggerSkillV2
{
public:
    Feiyang() : TriggerSkillV2("feiyang")
    {
        events << EventPhaseStart;
        view_as_skill = new FeiyangVS;
        attached_lord_skill = true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Judge || !player->canDiscard(player, "j")) return {};
        int hand = 0;
        for (int id : player->handCards()) if (player->canDiscard(player, id)) ++hand;
        return hand >= 2 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const QList<int> ids = owner->getJudgingAreaID();
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = owner->getMark(selector);
        room->setPlayerMark(owner, selector, ctx.activationRef.key.instanceID);
        room->notifyMoveToPile(owner, ids, objectName(), Player::PlaceDelayedTrick, true);
        const auto restoreRequest = qScopeGuard([room, owner, ids, selector, previous] {
            room->notifyMoveToPile(owner, ids, "feiyang", Player::PlaceDelayedTrick, false);
            room->setPlayerMark(owner, selector, previous);
        });
        // The nested active skill owns quota, payment and target effects.
        room->askForUseCard(owner, "@@feiyang", "@feiyang");
        return false;
    }
};

class Feiyang2 : public Feiyang {};

class Bahu : public TriggerSkillV2
{
public:
    Bahu() : TriggerSkillV2("bahu")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
        attached_lord_skill = true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
    {
        return event == EventPhaseStart && target && target->isAlive() && target->hasSkill(objectName())
            && target->getPhase() == Player::Start ? TriggerList{{target, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, objectName(), true, true);
        ctx.targets << owner;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Bahu2 : public Bahu {};

class BahuTargetMod : public TargetModSkillV2
{
public:
    BahuTargetMod() : TargetModSkillV2("#bahu-target", "Slash") { attached_lord_skill = true; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.primary && ctx.primary->hasSkill("bahu") && ctx.modType == TargetModSkill::Residue
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class DdzCombatModifier : public TargetModSkillV2
{
public:
    DdzCombatModifier() : TargetModSkillV2("#ddz-combat-modifier", "Slash") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary) return CorrectSkillResult::noEffect();
        const Weapon *weapon = ctx.primary->hasWeapon("ddz_jingubang")
            ? Sanguosha->findChild<const Weapon *>("ddz_jingubang") : nullptr;
        if (ctx.modType == TargetModSkill::Residue) {
            if (weapon && weapon->getRange(ctx.primary) == 1) return CorrectSkillResult::unlimitedResidue();
            // Nutao already applied its per-instance amount when granting this turn-local effect.
            const int amount = ctx.primary->getMark("&nutao-PlayClear");
            return amount > 0 ? CorrectSkillResult::useAmount(amount) : CorrectSkillResult::noEffect();
        }
        return ctx.modType == TargetModSkill::ExtraTarget && weapon && weapon->getRange(ctx.primary) == 4
            ? CorrectSkillResult::useAmount(1) : CorrectSkillResult::noEffect();
    }
};

class Huoyan : public TriggerSkillV2
{
public:
    Huoyan() : TriggerSkillV2("huoyan")
    {
        events << GameStart << CardsMoveOneTime << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventLoseSkill || !player || player->hasSkill(objectName())) return false;
        // Loss callbacks run after the last instance is gone; projection cleanup cannot require that instance.
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            const QString receipt = "huoyan" + target->objectName();
            if (player->getMark(receipt) <= 0) continue;
            player->setMark(receipt, 0);
            room->removePlayerMark(player, "HandcardVisible_" + target->objectName());
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event != EventLoseSkill && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : room->getOtherPlayers(owner))
            if (owner->getMark("huoyan" + target->objectName()) == 0) ctx.targets << target;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &, ServerPlayer *target) const override
    {
        const QString receipt = "huoyan" + target->objectName();
        if (owner->getMark(receipt) == 0) {
            owner->setMark(receipt, 1);
            room->addPlayerMark(owner, "HandcardVisible_" + target->objectName());
        }
        return false;
    }
};

class Huoyan2 : public Huoyan {};

class RuyiBf : public ViewAsEquipSkill
{
public:
    RuyiBf() : ViewAsEquipSkill("#ruyibf")
    {
    }

    QString viewAsEquip(const Player *target) const
    {
        if (target->hasEquipArea(0)&&target->hasSkill("ruyi"))
            return "ddz_jingubang";
        return "";
    }
};

class Ruyi : public FilterSkill
{
public:
    Ruyi() : FilterSkill("ruyi")
    {
		waked_skills = "ddz_jingubang,#ruyibf";
        frequency = Compulsory;
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->isKindOf("Weapon")
		&&Sanguosha->getCardPlace(to_select->getEffectiveId())==Player::PlaceHand;
    }

    const Card *viewAs(const Card *c) const
    {
        Card *slash = Sanguosha->cloneCard("slash",c->getSuit(),c->getNumber());
        slash->setSkillName("ruyi");/*
        WrappedCard *card = Sanguosha->getWrappedCard(c->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class DdzCibei : public TriggerSkillV2
{
public:
    DdzCibei() : TriggerSkillV2("ddzcibei") { events << DamageCaused << EventPhaseChanging << TurnBroken << EventSkillInvoking; m_baseAmount = 5; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const SkillInstanceRef ref = getUsageRef(ctx);
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return ref.isValid() && damage.to && damage.to != ctx.owner
            && !ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
                                                     QString("targets_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString())).toStringList().contains(damage.to->objectName());
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid() || !ctx.original_data) return;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to) return;
        QStringList used = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("targets_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString())).toStringList();
        if (!used.contains(damage.to->objectName())) used << damage.to->objectName();
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("targets_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString()), used);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (ctx.owner && ref.isValid()) ctx.owner->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("targets_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString()));
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventSkillInvoking && ctx.original_data) {
            const SkillContext accepted = ctx.original_data->value<SkillContext>();
            if (accepted.bypass_cost && accepted.activationRef == ctx.activationRef
                && parseSkillName(accepted.skill_name) == objectName()) addUsage(accepted);
            return;
        }

        if (event == TurnBroken || (event == EventPhaseChanging && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive))
            resetUsage(ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DamageCaused || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.to || damage.to == player || damage.damage <= 0) return {};
        return TriggerList{{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        return isUsable(ctx) && owner->askForSkillInvoke(objectName() + "$-1",
            QVariant::fromValue(ctx.original_data->value<DamageStruct>().to));
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ctx.choice = "prevent";
        const bool prevented = skillEffect(event, room, owner, ctx, ctx.original_data->value<DamageStruct>().to);
        ctx.choice = "draw";
        skillEffect(event, room, owner, ctx, owner);
        return prevented;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            return damage.to == target && target->damageRevises(*ctx.original_data, -damage.damage);
        }
        return false;
    }
};

class DdzCibei2 : public DdzCibei {};

JingubangCard::JingubangCard()
{
    target_fixed = true;
	setSkillName("ddz_jingubang");
}

void JingubangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QString choice = room->askForChoice(source,"ddz_jingubang","1+2+3+4");
	room->notifyWeaponRange("ddz_jingubang",choice.toInt());
}

class JingubangVs : public ViewAsSkillV2
{
public:
    JingubangVs() : ViewAsSkillV2("ddz_jingubang&") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->hasWeapon(objectName());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JingubangCard"; }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        ctx.choice = room->askForChoice(ctx.initiator, objectName(), "1+2+3+4");
        return QStringList{"1", "2", "3", "4"}.contains(ctx.choice);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker) {
            if (ctx.choice.isEmpty()) ctx.choice = ctx.invoker->getRoom()->askForChoice(ctx.invoker, objectName(), "1+2+3+4");
            if (QStringList{"1", "2", "3", "4"}.contains(ctx.choice)) skillEffect(ctx, ctx.invoker);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        target->getRoom()->notifyWeaponRange(objectName(), ctx.choice.toInt());
        return ContinueEffects;
    }
};

class JingubangSkill : public WeaponSkillV2
{
public:
    JingubangSkill() : WeaponSkillV2("ddz_jingubang&", "ddz_jingubang")
    {
        events << Predamage << CardUsed;
        frequency = Compulsory;
        view_as_skill = new JingubangVs;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !WeaponSkillV2::triggerable(player)) return {};
        const Weapon *weapon = Sanguosha->findChild<const Weapon *>(objectName());
        if (!weapon) return {};
        const Card *card = event == Predamage ? data.value<DamageStruct>().card : data.value<CardUseStruct>().card;
        if (!card || !card->isKindOf("Slash")) return {};
        const int range = weapon->getRange(player);
        return (event == Predamage && range == 2) || (event == CardUsed && range == 3)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        if (event == Predamage) ctx.targets << ctx.original_data->value<DamageStruct>().to;
        else ctx.targets = ctx.original_data->value<CardUseStruct>().to;
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == Predamage) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            return damage.to == target && target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class JingubangSkill2 : public JingubangSkill {};

Jingubang::Jingubang(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("ddz_jingubang");
}

class Longgong : public TriggerSkillV2
{
public:
    Longgong() : TriggerSkillV2("longgong") { events << DamageInflicted << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == DamageInflicted && player && player->isAlive() && player->hasTurn()
            && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        return owner && isUsable(ctx) && owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data);
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && parseSkillName(accepted.skill_name) == objectName()) addUsage(accepted);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ctx.choice = "prevent";
        const bool prevented = skillEffect(event, room, owner, ctx, damage.to);
        if (damage.from && damage.from->isAlive()) {
            ctx.choice = "equip";
            skillEffect(event, room, owner, ctx, damage.from);
        }
        return prevented;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "prevent") {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            return damage.to == target && target->damageRevises(*ctx.original_data, -damage.damage);
        }
        QList<int> ids;
        for (int id : room->getDrawPile()) {
            if (ids.size() >= getEffectiveAmount(ctx)) break;
            if (Sanguosha->getCard(id)->isKindOf("EquipCard")) ids << id;
        }
        if (!ids.isEmpty()) {
            DummyCard cards(ids);
            room->obtainCard(target, &cards);
        }
        return false;
    }
};

class Longgong2 : public Longgong {};

SitianCard::SitianCard()
{
    target_fixed = true;
    setSkillName("sitian");
}

void SitianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QStringList choices;
	choices << "lieri" << "leidian" << "dalang" << "baoyu" << "dawu";
	qsanShuffle(choices);
	QString choice = room->askForChoice(source,getSkillName(),choices.first()+"+"+choices.last());
	room->addPlayerMark(source,"&"+choice+"-PlayClear");
	if(choice=="lieri"){
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			room->doAnimate(1,source->objectName(),p->objectName());
		}
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			room->damage(DamageStruct(getSkillName(),source,p,1,DamageStruct::Fire));
		}
	}else if(choice=="leidian"){
		foreach (ServerPlayer *p, room->getOtherPlayers(source))
			room->doAnimate(1,source->objectName(),p->objectName());
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			JudgeStruct judge;
			judge.who = p;
			judge.reason = "lightning";
			judge.pattern = ".|spade|2~9";
			judge.negative = true;
			judge.good = false;
			room->judge(judge);
			if(judge.isBad())
				room->damage(DamageStruct("lightning",nullptr,p,3,DamageStruct::Thunder));
		}
	}else if(choice=="dalang"){
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			room->doAnimate(1,source->objectName(),p->objectName());
		}
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			DummyCard*dc = new DummyCard;
			foreach (int e, p->getEquipsId()) {
				if(source->canDiscard(p,e))
					dc->addSubcard(e);
			}
			if(dc->subcardsLength()>0)
				room->throwCard(dc,choice,p,source);
			else if(!p->hasEquip())
				room->loseHp(p,1,true,source,choice);
			dc->deleteLater();
		}
	}else if(choice=="baoyu"){
		ServerPlayer *tp = room->askForPlayerChosen(source,room->getOtherPlayers(source),choice,"baoyu0");
		if(tp){
			room->doAnimate(1,source->objectName(),tp->objectName());
			if(tp->getHandcardNum()>0){
				room->throwCard(tp->handCards(),choice,tp,source);
			}else
				room->loseHp(tp,1,true,source,choice);
		}
	}
}

class SitianVs : public ViewAsSkillV2
{
public:
    SitianVs() : ViewAsSkillV2("sitian", 2) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || request.selectedCardIds.size() >= 2
            || request.selectedCardIds.contains(card->getEffectiveId())
            || !request.initiator->handCards().contains(card->getEffectiveId())
            || !request.initiator->canDiscard(card->getEffectiveId())) return false;
        return request.selectedCardIds.isEmpty()
            || Sanguosha->getCard(request.selectedCardIds.first())->getColor() != card->getColor();
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 2) return false;
        ActiveSkillRequest selected = request;
        selected.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(selected, Sanguosha->getCard(id))) return false;
            selected.selectedCardIds << id;
        }
        return true;
    }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "SitianCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !actor->isAlive()) return FinishSkill;
        Room *room = actor->getRoom();
        QStringList choices{"lieri", "leidian", "dalang", "baoyu", "dawu"};
        qsanShuffle(choices);
        ctx.choice = room->askForChoice(actor, objectName(), choices.first() + "+" + choices.last());
        if (ctx.choice != choices.first() && ctx.choice != choices.last()) return FinishSkill;
        room->addPlayerMark(actor, "&" + ctx.choice + "-PlayClear");
        QList<ServerPlayer *> targets = room->getOtherPlayers(actor);
        if (ctx.choice == "baoyu") {
            ServerPlayer *target = room->askForPlayerChosen(actor, targets, ctx.choice, "baoyu0");
            targets.clear();
            if (target) targets << target;
        }
        for (ServerPlayer *target : targets) {
            room->doAnimate(1, actor->objectName(), target->objectName());
            skillEffect(ctx, target);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        Room *room = target->getRoom();
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "lieri")
            room->damage(DamageStruct(objectName(), actor, target, amount, DamageStruct::Fire));
        else if (ctx.choice == "leidian") {
            for (int i = 0; i < amount && target->isAlive(); ++i) {
                JudgeStruct judge;
                judge.who = target;
                judge.reason = "lightning";
                judge.pattern = ".|spade|2~9";
                judge.negative = true;
                judge.good = false;
                room->judge(judge);
                if (target->isAlive() && judge.isBad())
                    room->damage(DamageStruct("lightning", nullptr, target, 3, DamageStruct::Thunder));
            }
        } else if (ctx.choice == "dalang" || ctx.choice == "baoyu") {
            const QList<int> candidates = ctx.choice == "dalang" ? target->getEquipsId() : target->handCards();
            if (candidates.isEmpty()) room->loseHp(target, amount, true, actor, ctx.choice);
            else {
                DummyCard cards;
                for (int id : candidates) if (!Sanguosha->getCard(id)->hasFlag("using") && actor->canDiscard(target, id)) cards.addSubcard(id);
                if (cards.subcardsLength() > 0) room->throwCard(&cards, ctx.choice, target, actor);
            }
        } else if (ctx.choice == "dawu" && amount > 0) {
            // Applied weather survives losing Sitian; keep only its primitive source and remaining effect.
            QVariantList receipts = target->getTag("SitianDawuReceipts").toList();
            QVariantMap applied = ddzAppliedReceipt(room, ctx, amount);
            applied.insert("holder", target->objectName());
            applied.insert("remaining", amount);
            bool merged = false;
            for (QVariant &value : receipts) {
                QVariantMap receipt = value.toMap();
                if (!ddzSameActivation(receipt, applied) || receipt.value("phase") != applied.value("phase")) continue;
                receipt.insert("remaining", receipt.value("remaining").toInt() + amount);
                value = receipt;
                merged = true;
                break;
            }
            if (!merged) receipts << applied;
            target->setTag("SitianDawuReceipts", receipts);
        }
        return ContinueEffects;
    }
};

class Sitian : public TriggerSkillV2
{
public:
    Sitian() : TriggerSkillV2("sitian")
    {
        events << CardUsed << EventPhaseChanging << TurnBroken << EventSkillEffectFinished;
        view_as_skill = new SitianVs;
        global = true;
    }
    Frequency getFrequency(const Player *player = nullptr) const override { return player ? Compulsory : NotFrequent; }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    static void consume(Room *room, const SkillContext &ctx)
    {
        const QVariantMap original = ctx.extra_data.toMap();
        ServerPlayer *holder = room->findPlayerByObjectName(original.value("holder").toString(), true);
        if (!holder) return;
        QVariantList receipts = holder->getTag("SitianDawuReceipts").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return;
        QVariantMap receipt = receipts.at(index).toMap();
        const int remaining = receipt.value("remaining").toInt() - 1;
        if (remaining <= 0) receipts.removeAt(index);
        else { receipt.insert("remaining", remaining); receipts[index] = receipt; }
        holder->setTag("SitianDawuReceipts", receipts);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (parseSkillName(finished.skill_name) == objectName() && !finished.activationRef.isValid()) consume(room, finished);
        } else ddzExpireReceipts(room, player, event, data, "SitianDawuReceipts", true);
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardUsed || !player || !player->isAlive()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() != Card::TypeBasic) return true;
        for (const QVariant &value : player->getTag("SitianDawuReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("remaining").toInt() <= 0) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("serial").toInt();
            ctx.sourceRef = ddzReceiptSource(receipt);
            if (!ctx.sourceRef.isValid() || ctx.instanceID <= 0) continue;
            ctx.is_forced = true;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.extra_data = value;
            ctx.targets << player;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("holder").toString(), true);
        return holder && holder->getTag("SitianDawuReceipts").toList().contains(ctx.extra_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        consume(room, ctx);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->objectName() != ctx.extra_data.toMap().value("holder").toString()) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains("_ALL_TARGETS")) use.nullified_list << "_ALL_TARGETS";
        *ctx.original_data = QVariant::fromValue(use);
        room->sendCompulsoryTriggerLog(owner, "dawu");
        return false;
    }
};

class SitianBf2 : public Sitian {};

class Nutao : public TriggerSkillV2
{
public:
    Nutao() : TriggerSkillV2("nutao") { events << TargetSpecifying << Damage << EventPhaseChanging << TurnBroken; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ddzExpireReceipts(room, player, event, data, "NutaoReceipts", true);
        if (event == EventPhaseChanging || event == TurnBroken)
            for (ServerPlayer *holder : room->getAllPlayers(true)) {
                int amount = 0;
                for (const QVariant &value : holder->getTag("NutaoReceipts").toList()) amount += value.toMap().value("amount").toInt();
                room->setPlayerMark(holder, "&nutao-PlayClear", amount);
            }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((event != Damage && event != TargetSpecifying) || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == Damage) {
            const DamageStruct damage = data.value<DamageStruct>();
            return damage.nature == DamageStruct::Thunder && player->getPhase() == Player::Play
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("TrickCard")) return {};
        for (ServerPlayer *target : use.to) if (target != player) return TriggerList{{player, {objectName()}}};
        return {};
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == Damage) ctx.targets << owner;
        else {
            QList<ServerPlayer *> targets = ctx.original_data->value<CardUseStruct>().to;
            targets.removeAll(owner);
            if (targets.isEmpty()) return false;
            qsanShuffle(targets);
            ctx.targets << targets.first();
            room->sendCompulsoryTriggerLog(owner, this);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == Damage) {
            QVariantList receipts = target->getTag("NutaoReceipts").toList();
            receipts << ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
            target->setTag("NutaoReceipts", receipts);
            room->addPlayerMark(target, "&nutao-PlayClear", getEffectiveAmount(ctx));
        }
        else room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
        return false;
    }
};

class Nutao2 : public Nutao {};
class Nutaobf2 : public Nutao {};

class Jiuxian : public ViewAsSkillV2
{
public:
    Jiuxian() : ViewAsSkillV2("jiuxian", 1) { setResponseOrUse(true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return Analeptic(Card::NoSuit, 0).isAvailable(request.initiator);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && (request.pattern == "analeptic" || request.pattern == "peach+analeptic");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && request.initiator->handCards().contains(card->getEffectiveId())
            && card->isNDTrick() && !card->isSingleTargetCard();
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest empty = request; empty.selectedCardIds.clear();
        return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Analeptic"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Analeptic *card = new Analeptic(Card::NoSuit, 0);
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
};

class JiuxianMod : public TargetModSkillV2
{
public:
    JiuxianMod() : TargetModSkillV2("#jiuxian_mod", "Analeptic")
    {
		setHolderSelector(CorrectSkill_Primary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
		const Player *from = context.getPrimary();
		return from && from->hasSkill("jiuxian") && context.getModType() == TargetModSkill::Residue
			? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
    }
};

static QString shixianRhyme(const QString &name)
{
    static const QHash<QString, QStringList> rhymes = [] {
        QHash<QString, QStringList> yun;
        yun["a ia ua"] << "杀" << "发" << "甲" << "下" << "法";
        yun["o e uo"] << "槊" << "渴" << "车";
        yun["ie ve"] << "劫";
        yun["ai uai"] << "铠" << "海";
        yun["ei ui"] << "梅" << "倍" << "雷" << "锐";
        yun["ao iao"] << "桃" << "刀" << "矛" << "桥" << "壳" << "灶" << "劳";
        yun["ou iu"] << "有" << "斗" << "骝" << "酒" << "走";
        yun["an ian uan van"] << "闪" << "电" << "剑" << "宛" << "断" << "扇" << "环" << "链" << "远" << "砖";
        yun["en in un vn"] << "人" << "侵" << "阵" << "盾" << "军" << "尘" << "金";
        yun["ang iang uang"] << "羊" << "僵" << "枪" << "粮";
        yun["eng ing ong ung"] << "登" << "弓" << "影" << "骍" << "攻" << "镜" << "生" << "兵" << "纵";
        yun["i er v"] << "义" << "击" << "戟" << "子" << "意" << "机" << "计" << "利" << "西" << "移" << "彼";
        yun["u"] << "蜀" << "弩" << "斧" << "兔" << "卢" << "图" << "符" << "毒" << "柱" << "腹" << "武" << "五" << "术" << "梳" << "葫" << "鹄" << "入";
        return yun;
    }();
    const QString translated = Sanguosha->translate(name);
    for (auto it = rhymes.cbegin(); it != rhymes.cend(); ++it)
        for (const QString &ending : it.value()) if (translated.endsWith(ending)) return it.key();
    return QString();
}

static bool shixianRhymesWithPrevious(Room *room, ServerPlayer *player, const Card *card)
{
    const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
    const qint64 currentUse = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
    if (turn <= 0 || currentUse <= 0 || !card) return false;
    const QVariantMap snapshot = room->queryHistoryFacts({{"limit", 1}});
    if (!snapshot.value("complete").toBool()) return false;
    QList<QPair<qint64, QString>> uses;
    qint64 currentSequence = 0;
    for (const QString &kind : {QString("use_card"), QString("respond_card")}) {
        QVariantMap filter{{"kind", kind}, {"turn_id", turn}, {"watermark", snapshot.value("watermark")},
                           {kind == "use_card" ? "from" : "player", player->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), data = fact.value("data").toMap();
                if (kind == "respond_card" && !data.value("is_use").toBool()) continue;
                const QVariantMap saved = data.value("card").toMap();
                if (!saved.contains("type") || !saved.contains("name")) return false;
                if (saved.value("type").toInt() == Card::TypeSkill) continue;
                const qint64 sequence = fact.value("sequence").toLongLong();
                if (kind == "use_card" && fact.value("event_id").toLongLong() == currentUse) currentSequence = sequence;
                uses.append({sequence, saved.value("name").toString()});
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
    }
    // Use the accepted use event, not the last physical card: nested pre-use actions may be newer.
    qint64 previousSequence = 0;
    QString previous;
    for (const auto &use : uses)
        if (use.first < currentSequence && use.first > previousSequence) { previousSequence = use.first; previous = use.second; }
    const QString rhyme = shixianRhyme(card->objectName());
    return previousSequence > 0 && !rhyme.isEmpty() && rhyme == shixianRhyme(previous);
}

class Shixian : public TriggerSkillV2
{
public:
    Shixian() : TriggerSkillV2("shixian") { events << CardUsed << CardFinished; global = true; }
    Frequency getFrequency(const Player *player = nullptr) const override
    { return player ? Compulsory : NotFrequent; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card) ddzSetCardReceipts(room, use.card, "ShixianReceipts", {});
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.card && use.card->getTypeId() != Card::TypeSkill && shixianRhymesWithPrevious(room, player, use.card)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !use.card) return true;
        for (const QVariant &value : ddzCardReceipts(room, use.card, "ShixianReceipts")) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("serial").toInt();
            ctx.sourceRef = ddzReceiptSource(receipt);
            if (!ctx.sourceRef.isValid() || ctx.instanceID <= 0) continue;
            ctx.is_forced = true;
            ctx.extra_data = value;
            ctx.amount = receipt.value("amount", 1).toInt();
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.targets << player;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const CardUseStruct use = ctx.original_data ? ctx.original_data->value<CardUseStruct>() : CardUseStruct();
        return use.card && ddzCardReceipts(room, use.card, "ShixianReceipts").contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == CardFinished) return true;
        if (!owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
        ctx.targets << owner;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardFinished) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            QVariantList receipts = ddzCardReceipts(room, use.card, "ShixianReceipts");
            receipts.removeOne(ctx.extra_data);
            ddzSetCardReceipts(room, use.card, "ShixianReceipts", receipts);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "repeat_target") {
            ctx.targets << target;
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        if (event == CardUsed) {
            QVariantList receipts = ddzCardReceipts(room, use.card, "ShixianReceipts");
            receipts << ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
            ddzSetCardReceipts(room, use.card, "ShixianReceipts", receipts);
            target->drawCards(getEffectiveAmount(ctx), objectName());
        } else {
            const int repeats = getEffectiveAmount(ctx);
            for (int i = 0; i < repeats && target->isAlive(); ++i) {
                SkillContext admission = ctx;
                admission.choice = "repeat_target";
                admission.targets.clear();
                for (ServerPlayer *recipient : use.to) skillEffect(event, room, owner, admission, recipient);
                if (use.to.isEmpty() || !admission.targets.isEmpty())
                    use.card->use(room, ctx.invoker, admission.targets);
            }
        }
        return false;
    }
};

class Shixian2 : public Shixian {};
class Shixianbf2 : public Shixian {};

class Santou : public TriggerSkillV2
{
public:
    Santou() : TriggerSkillV2("santou") { events << DamageInflicted; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == DamageInflicted && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        bool preventedBefore = false;
        if (damage.from) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            if (turn <= 0) return false; // An unknown turn cannot establish first damage from this source.
            {
                QVariantMap filter{{"kind", "skill"}, {"turn_id", turn}, {"skill_name", ctx.sourceRef.key.skillName},
                                   {"skill_owner", ctx.sourceRef.ownerObjectName}};
                for (;;) {
                    const QVariantMap page = room->queryHistoryEvents(filter);
                    if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) {
                        qWarning() << "Santou prevention history is incomplete";
                        return false;
                    }
                    if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
                    for (const QVariant &value : page.value("items").toList()) {
                        const QVariantMap recorded = value.toMap().value("data").toMap();
                        if (recorded.value("activation_owner").toString() == ctx.activationRef.ownerObjectName
                            && recorded.value("activation_skill").toString() == ctx.activationRef.key.skillName
                            && recorded.value("activation_instance_id").toInt() == ctx.activationRef.key.instanceID
                            && recorded.value("santou_prevented_from").toString() == damage.from->objectName()
                            && recorded.value("santou_prevented_to").toString() == damage.to->objectName()) preventedBefore = true;
                    }
                    if (!page.value("has_more").toBool()) break;
                    filter.insert("after", page.value("next_after"));
                }
            }
        }
        ctx.extra_data = preventedBefore;
        ctx.targets << owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        target->damageRevises(*ctx.original_data, -damage.damage);
        // Record the actual prevention on this execution, not a package-maintained damage counter.
        const qint64 event = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
        if (room->historyRecordingEnabled() && event > 0 && damage.from)
            room->resolutionHistory().updateEvent(event, {{"santou_prevented_from", damage.from->objectName()},
                                                         {"santou_prevented_to", target->objectName()}});
        if (ctx.extra_data.toBool() && (target->getHp() >= 3
            || (target->getHp() == 2 && damage.nature != DamageStruct::Normal)
            || (target->getHp() == 1 && damage.card && damage.card->isRed())))
            room->loseHp(target, getEffectiveAmount(ctx), true, owner, objectName());
        return true;
    }
};

class FaqiVs : public ViewAsSkillV2
{
public:
    FaqiVs() : ViewAsSkillV2("faqi") { response_pattern = "@@faqi"; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern == "@@faqi" && request.activationRef.key.instanceID > 0
            && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID,
                                                            "pendingCardId", -1).toInt() >= 0;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }
    SkillInstanceRef quotaRef(const SkillContext &ctx) const
    {
        const SkillInstanceRef activation = getUsageRef(ctx);
        if (!ctx.owner || !activation.isValid()) return {};
        const QVariantMap original = ctx.owner->getSkillInstanceStateValue(activation.key.skillName,
            activation.key.instanceID, "quotaOrigin").toMap();
        return original.isEmpty() ? activation : SkillInstanceRef(original.value("owner").toString(),
            SkillInstanceKey(objectName(), original.value("instance").toInt()));
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = quotaRef(ctx);
        if (!ctx.owner || !ref.isValid()) return false;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder) return false;
        const int id = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "pendingCardId", -1).toInt();
        const Card *card = id >= 0 ? Sanguosha->getEngineCard(id) : nullptr;
        return card && !holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
                                                             QString("usedNames_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString())).toStringList().contains(card->objectName());
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = quotaRef(ctx);
        if (!ctx.owner || !ref.isValid() || !ctx.use_card) return;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder) return;
        QStringList names = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("usedNames_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString())).toStringList();
        if (!names.contains(ctx.use_card->objectName())) names << ctx.use_card->objectName();
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("usedNames_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString()), names);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = quotaRef(ctx);
        if (ctx.owner && ref.isValid()) ctx.owner->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("usedNames_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString()));
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx); // Custom quotas are author-owned; the generic reservation path only commits built-in scopes.
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        const int id = request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID,
                                                                    "pendingCardId", -1).toInt();
        const Card *original = id >= 0 ? Sanguosha->getEngineCard(id) : nullptr;
        if (!original || !original->isNDTrick()) return nullptr;
        Card *card = Sanguosha->cloneCard(original->objectName());
        if (card) card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        const int id = request.initiator ? request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "pendingCardId", -1).toInt() : -1;
        const Card *card = id >= 0 ? Sanguosha->getEngineCard(id) : nullptr;
        return card ? card->getClassName() : QString();
    }
};

class Faqi : public TriggerSkillV2
{
public:
    Faqi() : TriggerSkillV2("faqi")
    {
        events << CardFinished << EventPhaseChanging << TurnBroken << EventSkillInvoking;
        view_as_skill = new FaqiVs;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return event == CardFinished && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Play && use.card && use.card->isKindOf("EquipCard")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventSkillInvoking && ctx.original_data) {
            const SkillContext accepted = ctx.original_data->value<SkillContext>();
            if (accepted.bypass_cost && accepted.activationRef == ctx.activationRef
                && parseSkillName(accepted.skill_name) == objectName()) view_as_skill->addUsage(accepted);
        } else if (event == TurnBroken || (event == EventPhaseChanging && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive))
            view_as_skill->resetUsage(ctx);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const QStringList used = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID,
            QString("usedNames_%1").arg(room->historyScopes().value("turn_id").toString())).toStringList();
        QStringList names;
        QList<int> ids;
        for (int id : Sanguosha->getRandomCards()) {
            const Card *original = Sanguosha->getEngineCard(id);
            if (!original || !original->isNDTrick() || names.contains(original->objectName())
                || used.contains(original->objectName())) continue;
            names << original->objectName();
            const std::unique_ptr<Card> card(Sanguosha->cloneCard(original->objectName()));
            if (!card) continue;
            card->setSkillName(objectName());
            if (card->isAvailable(owner)) ids << id;
        }
        if (ids.isEmpty()) return false;
        int chosen = -1;
        {
            room->fillAG(ids, owner);
            const auto clearSelection = qScopeGuard([room, owner] { room->clearAG(owner); });
            chosen = room->askForAG(owner, ids, ids.size() < 2, objectName(), "faqi0");
        }
        if (!ids.contains(chosen)) return false;
        ctx.extra_data = chosen;
        ctx.targets << owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int chosen = ctx.extra_data.toInt();
        Room::AcceptedViewAsEffectScope response(room, target, objectName(), ctx);
        if (!response.isValid()) return false;
        const int id = response.activationRef().key.instanceID;
        target->setSkillInstanceStateValue(objectName(), id, "pendingCardId", chosen);
        target->setSkillInstanceStateValue(objectName(), id, "quotaOrigin",
            QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID}});
        const int previousAiCard = target->getMark("faqiUse");
        const auto restore = qScopeGuard([room, target, previousAiCard] { room->setPlayerMark(target, "faqiUse", previousAiCard); });
        room->setPlayerMark(target, "faqiUse", chosen);
        room->askForUseCard(target, "@@faqi", "faqi1:" + Sanguosha->getEngineCard(chosen)->objectName());
        return false;
    }
};

class Faqi2 : public Faqi {};

class Zhanjian : public TriggerSkillV2
{
public:
    Zhanjian() : TriggerSkillV2("zhanjian") { events << EventPhaseStart; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Start) return true;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (ServerPlayer *target : room->getAlivePlayers()) {
                const Card *weapon = target->getWeapon();
                if (!weapon || weapon->objectName() != "qinggang_sword" || !player->canGet(target, weapon->getEffectiveId())) continue;
                SkillContext ctx;
                ctx.skill_name = objectName() + "->" + target->objectName();
                ctx.owner = ctx.invoker = ctx.initiator = player;
                ctx.instanceID = id;
                ctx.amount = room->getSkillInstanceAmount(SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id)));
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                if (!ctx.sourceRef.isValid()) continue;
                ctx.preferredTarget = target;
                ctx.preferredTargetSeat = target->getSeat();
                ctx.targets << target;
                ctx.extra_data = weapon->getEffectiveId();
                ctx.current_event = event;
                ctx.original_data = &data;
                contexts << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { return owner->askForSkillInvoke(objectName() + "$-1", ctx.preferredTarget); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "receive") {
            const QVariantMap material = ctx.extra_data.toMap();
            const int id = material.value("id").toInt();
            ServerPlayer *giver = room->findPlayerByObjectName(material.value("giver").toString(), true);
            if (giver && room->getCardOwner(id) == giver && room->getCardPlace(id) == Player::PlaceEquip
                && !Sanguosha->getCard(id)->hasFlag("using") && target->canGet(giver, id)) room->obtainCard(target, id);
            return false;
        }
        const int id = ctx.extra_data.toInt();
        if (!ctx.invoker || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip
            || Sanguosha->getCard(id)->hasFlag("using") || !ctx.invoker->canGet(target, id)) return false;
        ctx.extra_data = QVariantMap{{"id", id}, {"giver", target->objectName()}};
        ctx.choice = "receive";
        skillEffect(event, room, owner, ctx, ctx.invoker);
        ctx.choice.clear();
        return false;
    }

};

class Zhanjian2 : public Zhanjian {};

class DdzBenxi : public TriggerSkillV2
{
public:
    DdzBenxi() : TriggerSkillV2("ddzbenxi")
    {
        events << CardsMoveOneTime << EventPhaseChanging;
        frequency = Compulsory;
        change_skill = true;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::RoundStart) return false;
        const QVariantList grants = player->getTag("DdzBenxiGrants").toList();
        player->removeTag("DdzBenxiGrants");
        for (const QVariant &value : grants) {
            const QVariantMap grant = value.toMap();
            if (grant.value("grant").toInt() > 0) room->detachSkillFromPlayer(player, SkillInstanceUtils::formatName(
                grant.value("skill").toString(), grant.value("grant").toInt()), false, true);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return move.from == player && move.from_places.contains(Player::PlaceHand)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int state = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "state", 1).toInt();
        if (state == 1) {
            static const QHash<QString, int> voices{{"qizhi", 2}, {"daiyan", 2}, {"ny_10th_quanmou", 1},
                {"xiaowu", 1}, {"tenyearyonglve", 2}, {"ny_10th_fangdu", 2}, {"ny_tenth_xiuwen", 2},
                {"spyoudi", 1}, {"ny_10th_qingbei", 2}, {"yisuan", 1}, {"duorui", 2}, {"qingtan", 1},
                {"weiwu", 2}, {"fujian", 2}, {"xiantu", 2}, {"ny_10th_sijun", 2}, {"zongfan", 2}};
            QStringList names = voices.keys();
            qsanShuffle(names);
            for (const QString &name : names) {
                if (!Sanguosha->getSkill(name)) continue;
                ctx.choice = "recite";
                ctx.extra_data = QVariantMap{{"skill", name}, {"voice", voices.value(name)}};
                ctx.targets << owner;
                return true;
            }
            return false;
        }
        const QString skill = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "skill").toString();
        if (skill.isEmpty() || !Sanguosha->getSkill(skill)) return false;
        ctx.extra_data = QVariantMap{{"skill", skill}};
        if (owner->hasSkill(skill, true)) {
            ctx.choice = "damage";
            ServerPlayer *target = room->askForPlayerChosen(owner, room->getAlivePlayers(), objectName(), "ddzbenxi0:");
            if (!target) return false;
            ctx.targets << target;
        } else {
            ctx.choice = "gain";
            ctx.targets << owner;
        }
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int state = ctx.choice == "recite" ? 2 : 1;
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "state", state);
        if (ctx.choice == "recite") owner->setSkillInstanceStateValue(objectName(), ctx.instanceID,
            "skill", ctx.extra_data.toMap().value("skill"));
        room->setChangeSkillState(owner, objectName(), state);
        room->sendCompulsoryTriggerLog(owner, this);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap data = ctx.extra_data.toMap();
        const QString skill = data.value("skill").toString();
        if (ctx.choice == "recite") {
            room->broadcastSkillInvoke(skill, data.value("voice").toInt(), target);
            target->setTag("ddz_benxiSkill", skill);
        } else if (ctx.choice == "damage") {
            room->doAnimate(1, owner->objectName(), target->objectName());
            room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        } else if (!target->hasSkill(skill, true)) {
            QVariantMap receipt = ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
            receipt.insert("skill", skill);
            receipt.insert("grant", 0);
            const qint64 serial = receipt.value("serial").toLongLong();
            QVariantList grants = target->getTag("DdzBenxiGrants").toList();
            grants << receipt;
            target->setTag("DdzBenxiGrants", grants);
            room->acquireSkillFromEffect(target, skill, ctx, [target, serial](int id) {
                // Publish the exact grant before acquisition signals can expire it.
                QVariantList pending = target->getTag("DdzBenxiGrants").toList();
                for (QVariant &value : pending) {
                    QVariantMap saved = value.toMap();
                    if (saved.value("serial").toLongLong() != serial) continue;
                    saved.insert("grant", id); value = saved; break;
                }
                target->setTag("DdzBenxiGrants", pending);
            });
        }
        return false;
    }
};

class DdzBenxi2 : public DdzBenxi {};

class Qiusuo : public TriggerSkillV2
{
public:
    Qiusuo() : TriggerSkillV2("qiusuo") { events << Damage << Damaged; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return (event == Damage || event == Damaged) && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        ctx.targets << owner;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> chosen;
        for (int id : Sanguosha->getRandomCards()) {
            if (chosen.size() >= getEffectiveAmount(ctx)) break;
            const Player::Place place = room->getCardPlace(id);
            if ((place == Player::DrawPile || place == Player::DiscardPile) && Sanguosha->getCard(id)->isKindOf("IronChain"))
                chosen << id;
        }
        if (!chosen.isEmpty()) {
            DummyCard cards(chosen);
            room->obtainCard(target, &cards);
        }
        return false;
    }
};

LisaoCard::LisaoCard()
{
	will_throw = false;
    setSkillName("lisao");
}

bool LisaoCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length()<2;
}

void LisaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	int n = qsanRandomBounded(69)+1;
	QString question = QString("lisaoQuestion%1").arg(n);
	QString optionA = QString("lisaoOptionA%1").arg(n);
	QString optionB = QString("lisaoOptionB%1").arg(n);
	QList<ServerPlayer *> tps;
	foreach (ServerPlayer *p, targets) {
		QElapsedTimer timer;
		timer.start();
		QString choice = room->askForChoice(p,"lisao",optionA+"+"+optionB,QVariant::fromValue(source),question);
		p->setMark("lisaoTimer",timer.elapsed());
		if(n>35){
			if(choice==optionB)
				tps << p;
		}else{
			if(choice==optionA)
				tps << p;
		}
	}
	if(tps.length()>1){
		int t1 = tps.first()->getMark("lisaoTimer"), t2 = tps.last()->getMark("lisaoTimer");
		if(t1>t2){
			tps.removeOne(tps.first());
		}else if(t2>t1){
			tps.removeOne(tps.last());
		}
	}
	foreach (ServerPlayer *p, tps) {
		room->showAllCards(p);
	}
	foreach (ServerPlayer *p, targets) {
		if(tps.contains(p)) continue;
		room->setPlayerMark(p,"&lisao+#"+source->objectName()+"-Clear",1);
	}
}

class LisaoVs : public ViewAsSkillV2
{
public:
    LisaoVs() : ViewAsSkillV2("lisao") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && selected.size() < 2 && !selected.contains(candidate); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return !targets.isEmpty() && targets.size() <= 2; }
    QString historyKey(const ActiveSkillRequest &) const override { return "LisaoCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker) return FinishSkill;
        const int question = qsanRandomBounded(69) + 1;
        ctx.extra_data = QVariantMap{{"question", question}, {"answers", QVariantMap()}};
        ctx.choice = "ask";
        const QList<ServerPlayer *> targets = ctx.targets;
        for (ServerPlayer *target : targets) skillEffect(ctx, target);
        const QVariantMap answers = ctx.extra_data.toMap().value("answers").toMap();
        qint64 best = -1;
        for (const QVariant &value : answers) {
            const QVariantMap answer = value.toMap();
            if (answer.value("correct").toBool() && (best < 0 || answer.value("time").toLongLong() < best))
                best = answer.value("time").toLongLong();
        }
        for (ServerPlayer *target : targets) {
            if (!answers.contains(target->objectName())) continue;
            const QVariantMap answer = answers.value(target->objectName()).toMap();
            ctx.choice = answer.value("correct").toBool() && answer.value("time").toLongLong() == best ? "win" : "lose";
            skillEffect(ctx, target);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "ask") {
            QVariantMap data = ctx.extra_data.toMap();
            const int n = data.value("question").toInt();
            const QString optionA = QString("lisaoOptionA%1").arg(n), optionB = QString("lisaoOptionB%1").arg(n);
            QElapsedTimer timer;
            timer.start();
            const QString answer = room->askForChoice(target, objectName(), optionA + "+" + optionB,
                QVariant::fromValue(ctx.invoker), QString("lisaoQuestion%1").arg(n));
            QVariantMap answers = data.value("answers").toMap();
            answers.insert(target->objectName(), QVariantMap{{"correct", answer == (n > 35 ? optionB : optionA)},
                                                           {"time", timer.elapsed()}});
            data.insert("answers", answers);
            ctx.extra_data = data;
        } else if (ctx.choice == "win") room->showAllCards(target);
        else {
            QVariantList receipts = target->getTag("LisaoReceipts").toList();
            QVariantMap receipt = ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
            receipt.insert("actor", ctx.invoker->objectName());
            receipt.insert("holder", target->objectName());
            for (int i = receipts.size() - 1; i >= 0; --i) {
                const QVariantMap old = receipts.at(i).toMap();
                if (ddzSameActivation(old, receipt) && old.value("turn") == receipt.value("turn"))
                    receipts.removeAt(i);
            }
            receipts << receipt;
            target->setTag("LisaoReceipts", receipts);
            room->setPlayerMark(target, "&lisao+#" + ctx.invoker->objectName() + "-Clear", 1);
        }
        return ContinueEffects;
    }
};

class Lisao : public TriggerSkillV2
{
public:
    Lisao() : TriggerSkillV2("lisao")
    {
        events << CardUsed << DamageInflicted << EventPhaseChanging << TurnBroken;
        view_as_skill = new LisaoVs;
        global = true;
    }
    Frequency getFrequency(const Player *player = nullptr) const override { return player ? Compulsory : NotFrequent; }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ddzExpireReceipts(room, player, event, data, "LisaoReceipts");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive() || (event != CardUsed && event != DamageInflicted)) return true;
        const CardUseStruct use = event == CardUsed ? data.value<CardUseStruct>() : CardUseStruct();
        if (event == CardUsed && (!use.card || use.card->getTypeId() == Card::TypeSkill)) return true;
        QMap<QString, SkillContext> grouped;
        for (ServerPlayer *target : room->getAlivePlayers()) {
            if (event == DamageInflicted && target != player) continue;
            for (const QVariant &value : target->getTag("LisaoReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                if (event == CardUsed && receipt.value("actor").toString() != player->objectName()) continue;
                ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
                if (!owner) continue;
                const int instance = receipt.value("serial").toInt();
                const QString key = owner->objectName() + ":" + QString::number(instance);
                if (!grouped.contains(key)) {
                    SkillContext ctx;
                    ctx.skill_name = objectName();
                    ctx.owner = owner;
                    ctx.invoker = ctx.initiator = player;
                    ctx.instanceID = instance;
                    ctx.sourceRef = ddzReceiptSource(receipt);
                    if (!ctx.sourceRef.isValid() || instance <= 0) continue;
                    ctx.extra_data = receipt;
                    ctx.is_forced = true;
                    ctx.original_data = &data;
                    ctx.current_event = event;
                    ctx.amount = receipt.value("amount", 1).toInt();
                    grouped.insert(key, ctx);
                }
                grouped[key].targets << target;
            }
        }
        for (const SkillContext &ctx : grouped) contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("holder").toString(), true);
        return holder && holder->getTag("LisaoReceipts").toList().contains(ctx.extra_data);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == CardUsed) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        } else {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to != target) return false;
            target->damageRevises(*ctx.original_data, damage.damage * getEffectiveAmount(ctx));
        }
        room->sendCompulsoryTriggerLog(owner, this);
        return false;
    }
};

class Lisao2 : public Lisao {};

class Chushan : public TriggerSkillV2
{
public:
    Chushan() : TriggerSkillV2("chushan") { events << GameStart; frequency = Compulsory; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == GameStart && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        ctx.targets << owner;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QSet<QString> excluded{"ddz_wuming"};
        for (ServerPlayer *player : room->getAlivePlayers()) {
            excluded << player->getGeneralName() << player->getGeneral2Name();
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            const QStringList generals = Sanguosha->getRandomGenerals(3, excluded);
            if (generals.isEmpty()) break;
            const QString selected = room->askForGeneral(target, generals);
            if (!generals.contains(selected)) break;
            const General *general = Sanguosha->getGeneral(selected);
            if (!general) break;
            excluded << selected;
            QStringList skills;
            for (const Skill *skill : general->getVisibleSkillList()) skills << skill->objectName();
            if (skills.isEmpty()) continue;
            const QString skill = room->askForChoice(target, objectName(), skills.join("+"));
            if (skills.contains(skill) && target->isAlive() && !target->hasSkill(skill, true)) room->acquireSkillFromEffect(target, skill, ctx);
        }
        return false;
    }
};

class Juanlv : public TriggerSkillV2
{
public:
    Juanlv() : TriggerSkillV2("juanlv") { events << TargetSpecified; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != TargetSpecified || !player || !player->isAlive()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() == Card::TypeSkill) return true;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (ServerPlayer *target : use.to) {
                if (!target->isAlive() || target->getGender() == player->getGender()) continue;
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
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { return owner->askForSkillInvoke(objectName() + "$-1", ctx.preferredTarget); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
        if (!before.value("complete").toBool() || skillEvent <= 0) return false;
        if (target->canDiscard(target, "h"))
            room->askForDiscard(target, objectName(), amount, amount, true, false, "juanlv0:" + owner->objectName());
        QVariantMap filter{{"from", target->objectName()}, {"after", before.value("watermark")}};
        QSet<int> paid;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), move = fact.value("data").toMap();
                if (move.value("from_place").toInt() == Player::PlaceHand && move.value("to_place").toInt() == Player::DiscardPile
                    && move.value("reason_skill").toString() == objectName()
                    && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
                    && room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() == skillEvent)
                    paid.insert(move.value("card_id").toInt());
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark")); filter.insert("after", page.value("next_after"));
        }
        if (paid.size() < amount) {
            const QString previous = ctx.choice;
            ctx.choice = "draw";
            skillEffect(event, room, owner, ctx, owner);
            ctx.choice = previous;
        }
        return false;
    }
};

QixinCard::QixinCard()
{
    target_fixed = true;
    setSkillName("qixin");
}

void QixinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int n = source->getChangeSkillState(getSkillName());
	if(n==1){
		room->setChangeSkillState(source, getSkillName(), 2);
		const General*g = Sanguosha->getGeneral("caojie");
		source->setGender(g->getGender());
		n = source->getTag("qixinCaojieHp").toInt();
		source->setTag("qixinLiuxieHp", source->getHp());
		if(n>0) room->setPlayerProperty(source,"hp",n);
		else room->setPlayerProperty(source,"hp",g->getStartHp());
		source->setAvatarIcon("caojie",source->getGeneral2()&&source->getGeneral2()->hasSkill(getSkillName()));
	}else{
		room->setChangeSkillState(source, getSkillName(), 1);
		const General*g = Sanguosha->getGeneral("liuxie");
		source->setGender(g->getGender());
		n = source->getTag("qixinLiuxieHp").toInt();
		source->setTag("qixinCaojieHp", source->getHp());
		if(n>0) room->setPlayerProperty(source,"hp",n);
		else room->setPlayerProperty(source,"hp",g->getStartHp());
		source->setAvatarIcon("liuxie",source->getGeneral2()&&source->getGeneral2()->hasSkill(getSkillName()));
	}
}

class Qixin : public ViewAsSkillV2
{
public:
    Qixin() : ViewAsSkillV2("qixin") { change_skill = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "QixinCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (card && request.initiator && request.activationRef.isValid())
            card->setTag("qixinForms", request.initiator->getSkillInstanceStateValue(objectName(),
                request.activationRef.key.instanceID, "forms"));
        return card;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker) skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const SkillInstanceRef ref = ctx.activationRef;
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        // The rebuilt proxy freezes state before interception can retire the source.
        QVariantMap forms = ctx.use_card ? ctx.use_card->getTag("qixinForms").toMap() : QVariantMap();
        QVariantMap form = forms.value(target->objectName()).toMap();
        const bool toCaojie = form.value("state", 1).toInt() == 1;
        const General *general = Sanguosha->getGeneral(toCaojie ? "caojie" : "liuxie");
        if (!general) return ContinueEffects;
        const QString saved = toCaojie ? "liuxie_hp" : "caojie_hp";
        const QString restored = toCaojie ? "caojie_hp" : "liuxie_hp";
        form.insert(saved, target->getHp());
        target->setTag(toCaojie ? "qixinLiuxieHp" : "qixinCaojieHp", target->getHp());
        form.insert("state", toCaojie ? 2 : 1);
        forms.insert(target->objectName(), form);
        if (holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID))
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "forms", forms);
        const qint64 serial = ddzAppliedReceipt(room, ctx, 0).value("serial").toLongLong();
        target->setTag("QixinMutation", serial);
        const auto unchanged = [&] { return target->getTag("QixinMutation").toLongLong() == serial; };
        room->setChangeSkillState(target, objectName(), toCaojie ? 2 : 1);
        if (!unchanged()) return ContinueEffects;
        target->setGender(general->getGender());
        if (!unchanged()) return ContinueEffects;
        room->setPlayerProperty(target, "hp", form.value(restored, general->getStartHp()).toInt());
        if (!unchanged()) return ContinueEffects;
        target->setAvatarIcon(general->objectName(), target->getGeneral2() && target->getGeneral2()->hasSkill(objectName()));
        return ContinueEffects;
    }
};

class Zhinang : public TriggerSkillV2
{
public:
    Zhinang() : TriggerSkillV2("zhinang") { events << CardFinished; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return event == CardFinished && player && player->isAlive() && player->hasSkill(objectName())
            && use.card && (use.card->isKindOf("TrickCard") || use.card->isKindOf("EquipCard"))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const bool voice = use.card->isKindOf("TrickCard");
        for (const QString &name : Sanguosha->getSkillNames()) {
            const Skill *skill = Sanguosha->getSkill(name);
            if (!skill || owner->hasSkill(name, true)) continue;
            bool eligible = !voice && Sanguosha->translate(name).contains("谋");
            if (voice) for (const QString &source : skill->getSources())
                if (Sanguosha->translate("$" + source.split("/").last().split(".").first()).contains("谋")) { eligible = true; break; }
            if (!eligible || !owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) continue;
            ctx.choice = voice ? "voice_grant" : "name_grant";
            ctx.extra_data = name;
            ctx.targets << owner;
            return true;
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QString name = ctx.extra_data.toString();
        if (target->hasSkill(name, true)) return false;
        const QVariantMap previous = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, ctx.choice).toMap();
        const int instance = room->acquireSkillFromEffect(target, name, ctx, [&](int id) {
            owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, ctx.choice,
                QVariantMap{{"holder", target->objectName()}, {"skill", name}, {"instance", id}});
        });
        if (instance <= 0) return false;
        if (owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, ctx.choice).toMap().value("instance").toInt() == instance)
            target->setTag(ctx.choice == "voice_grant" ? "zhinangSkill1" : "zhinangSkill2", name);
        // Replacing a grant must not remove an independently acquired copy with the same name.
        ServerPlayer *previousHolder = room->findPlayerByObjectName(previous.value("holder").toString(), true);
        if (previousHolder && previous.value("instance").toInt() > 0)
            room->detachSkillFromPlayer(previousHolder, SkillInstanceUtils::formatName(
                previous.value("skill").toString(), previous.value("instance").toInt()), false, true);
        return false;
    }
};

class Gouzhu : public TriggerSkillV2
{
public:
    Gouzhu() : TriggerSkillV2("gouzhu") { events << EventLoseSkill; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Skill *lost = Sanguosha->getSkill(SkillInstanceUtils::baseName(data.toString()));
        if (event != EventLoseSkill || !player || !player->isAlive() || !player->hasSkill(objectName()) || !lost) return {};
        const Frequency f = lost->getFrequency(player);
        return f == Compulsory || f == Wake || f == Limited || lost->isChangeSkill() || lost->isLordSkill()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const Skill *lost = Sanguosha->getSkill(SkillInstanceUtils::baseName(ctx.original_data->toString()));
        if (!lost) return false;
        ctx.manual_effect = true;
        const Frequency f = lost->getFrequency(owner);
        room->sendCompulsoryTriggerLog(owner, this);
        if (f == Compulsory || f == Wake) {
            ctx.choice = f == Compulsory ? "recover" : "basic";
            skillEffect(event, room, owner, ctx, owner);
        } else if (f == Limited) {
            QList<ServerPlayer *> targets = room->getOtherPlayers(owner);
            if (!targets.isEmpty()) {
                qsanShuffle(targets);
                ctx.choice = "damage";
                skillEffect(event, room, owner, ctx, targets.first());
            }
        }
        if (lost->isChangeSkill()) { ctx.choice = "maxcards"; skillEffect(event, room, owner, ctx, owner); }
        if (lost->isLordSkill()) { ctx.choice = "maxhp"; skillEffect(event, room, owner, ctx, owner); }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "recover") room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        else if (ctx.choice == "damage") room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        else if (ctx.choice == "maxcards") room->addMaxCards(target, amount, false);
        else if (ctx.choice == "maxhp") room->gainMaxHp(target, amount, objectName());
        else {
            QList<int> chosen;
            for (int id : Sanguosha->getRandomCards()) {
                if (chosen.size() >= amount) break;
                const Player::Place place = room->getCardPlace(id);
                if ((place == Player::DrawPile || place == Player::DiscardPile) && Sanguosha->getCard(id)->isKindOf("BasicCard")) chosen << id;
            }
            if (!chosen.isEmpty()) { DummyCard cards(chosen); room->obtainCard(target, &cards); }
        }
        return false;
    }
};

class Huyi : public TriggerSkillV2
{
public:
    Huyi() : TriggerSkillV2("huyi") { events << CardFinished << CardResponded << GameStart << EventPhaseChanging; }
    Frequency getFrequency(const Player *player = nullptr) const override { return player ? Compulsory : NotFrequent; }
    static QStringList candidates()
    {
        QStringList skills;
        for (const QString &name : Sanguosha->getLimitedGeneralNames()) {
            if (!name.endsWith("guanyu") && !name.endsWith("zhangfei") && !name.endsWith("zhaoyun")
                && !name.endsWith("huangzhong") && !name.endsWith("machao")) continue;
            const General *general = Sanguosha->getGeneral(name);
            if (!general) continue;
            for (const Skill *skill : general->getVisibleSkillList())
                if (!skills.contains(skill->objectName())) skills << skill->objectName();
        }
        return skills;
    }
    QVariantList liveGrants(Room *room, ServerPlayer *owner, int instance) const
    {
        QVariantList result;
        for (const QVariant &value : owner->getSkillInstanceStateValue(objectName(), instance, "grants").toList()) {
            const QVariantMap grant = value.toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(grant.value("holder").toString(), true);
            if (holder && holder->hasSkillInstance(grant.value("skill").toString(), grant.value("instance").toInt())) result << value;
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return {};
        } else if (event != GameStart) {
            const Card *card = event == CardFinished ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
            if (!card || !card->isKindOf("BasicCard")) return {};
        }
        return TriggerList{{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const QVariantList grants = liveGrants(room, owner, ctx.instanceID);
        QStringList names;
        for (const QVariant &value : grants) names << value.toMap().value("skill").toString();
        if (event == EventPhaseChanging) {
            if (names.isEmpty() || !owner->askForSkillInvoke(this, "huyi0")) return false;
            const QString chosen = room->askForChoice(owner, objectName(), names.join("+"));
            for (const QVariant &value : grants) {
                const QVariantMap grant = value.toMap();
                if (grant.value("skill").toString() != chosen) continue;
                ServerPlayer *holder = room->findPlayerByObjectName(grant.value("holder").toString(), true);
                if (!holder) return false;
                ctx.choice = "lose";
                ctx.extra_data = grant;
                ctx.targets << holder;
                return true;
            }
            return false;
        }
        if (event != GameStart && grants.size() >= 5) return false;
        QStringList available = candidates();
        qsanShuffle(available);
        qsanRemoveIf(available, [&names](const QString &name) { return names.contains(name); });
        QString chosen;
        if (event == GameStart) {
            const QStringList options = available.mid(0, 3);
            if (options.isEmpty()) return false;
            chosen = room->askForChoice(owner, objectName(), options.join("+"));
            if (!options.contains(chosen)) return false;
        } else {
            const Card *card = event == CardFinished ? ctx.original_data->value<CardUseStruct>().card
                                                   : ctx.original_data->value<CardResponseStruct>().m_card;
            const QString name = "【" + Sanguosha->translate(card->isKindOf("Slash") ? "slash" : card->objectName()) + "】";
            for (const QString &skill : available)
                if (Sanguosha->translate(":" + skill).contains(name)) { chosen = skill; break; }
            if (chosen.isEmpty()) return false;
        }
        ctx.choice = "gain";
        ctx.extra_data = chosen;
        ctx.targets << owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList grants = liveGrants(room, owner, ctx.instanceID);
        if (ctx.choice == "lose") {
            const QVariantMap grant = ctx.extra_data.toMap();
            if (grant.value("holder").toString() != target->objectName() || !grants.removeOne(grant)) return false;
            // Update private ownership before EventLoseSkill can cause a nested Huyi activation.
            owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "grants", grants);
            room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(
                grant.value("skill").toString(), grant.value("instance").toInt()), false, true);
        } else {
            if (grants.size() >= 5) return false;
            const QString skill = ctx.extra_data.toString();
            room->acquireSkillFromEffect(target, skill, ctx, [&](int id) {
                QVariantList committed = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "grants").toList();
                committed << QVariantMap{{"holder", target->objectName()}, {"skill", skill}, {"instance", id}};
                owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "grants", committed);
            });
            room->sendCompulsoryTriggerLog(owner, this);
        }
        QStringList projection;
        for (int id : owner->getSkillInstanceIds(objectName()))
            for (const QVariant &value : liveGrants(room, owner, id)) projection << value.toMap().value("skill").toString();
        owner->setTag("huyiSkills", projection);
        return false;
    }
};

class Fengzhu : public TriggerSkillV2
{
public:
    Fengzhu() : TriggerSkillV2("fengzhu") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Start) return {};
        for (ServerPlayer *target : room->getOtherPlayers(player)) if (target->isMale()) return TriggerList{{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(owner)) if (target->isMale()) candidates << target;
        ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(), "fengzhu0", false, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") {
            target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
            return false;
        }
        owner->peiyin(this);
        room->setPlayerMark(target, "&ddz_yifu", 1);
        ctx.choice = "draw";
        ctx.extra_data = qMax(0, target->getHp());
        skillEffect(event, room, owner, ctx, owner);
        ctx.choice.clear();
        return false;
    }
};

class Yuyu : public TriggerSkillV2
{
public:
    Yuyu() : TriggerSkillV2("yuyu") { events << Damaged << CardsMoveOneTime << EventPhaseChanging; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive) return {};
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || (!move.from_places.contains(Player::PlaceHand) && !move.from_places.contains(Player::PlaceEquip))) return {};
        }
        for (ServerPlayer *target : room->getOtherPlayers(player))
            if (target->getMark("&ddz_yifu") > 0 && (event == EventPhaseChanging
                || (target->getMark("&ddz_hen") > 0 && target->hasFlag("CurrentPlayer"))))
                return TriggerList{{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : room->getOtherPlayers(owner))
            if (target->getMark("&ddz_yifu") > 0 && (event == EventPhaseChanging
                || (target->getMark("&ddz_hen") > 0 && target->hasFlag("CurrentPlayer")))) ctx.targets << target;
        if (event == EventPhaseChanging) {
            ServerPlayer *target = room->askForPlayerChosen(owner, ctx.targets, objectName(), "yuyu0", false, true);
            if (!target) return false;
            ctx.targets = {target};
            ctx.extra_data = 1;
        } else if (event == Damaged) ctx.extra_data = ctx.original_data->value<DamageStruct>().damage;
        else {
            int count = 0;
            for (Player::Place place : ctx.original_data->value<CardsMoveOneTimeStruct>().from_places)
                if (place == Player::PlaceHand || place == Player::PlaceEquip) ++count;
            ctx.extra_data = count;
        }
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        target->gainMark("&ddz_hen", ctx.extra_data.toInt() * getEffectiveAmount(ctx));
        return false;
    }
};

class DdzZhiji : public TriggerSkillV2
{
public:
    DdzZhiji() : TriggerSkillV2("ddzzhiji")
    {
        events << PreCardUsed << TargetSpecifying << ConfirmDamage;
        waked_skills = "shenji,wushuang";
        frequency = Compulsory;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card) ddzSetCardReceipts(room, use.card, "DdzZhijiReceipts", {});
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecifying || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() == Card::TypeSkill) return {};
        for (ServerPlayer *target : use.to)
            if (target->getMark("&ddz_yifu") > 0 && target->getMark("&ddz_hen") > 0) return TriggerList{{player, {objectName()}}};
        return {};
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != ConfirmDamage) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.to) return true;
        for (const QVariant &value : ddzCardReceipts(room, damage.card, "DdzZhijiReceipts")) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("serial").toInt();
            ctx.sourceRef = ddzReceiptSource(receipt);
            if (!ctx.sourceRef.isValid() || ctx.instanceID <= 0) continue;
            ctx.is_forced = true;
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt;
            ctx.targets << damage.to;
            ctx.current_event = event;
            ctx.original_data = &data;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const DamageStruct damage = ctx.original_data ? ctx.original_data->value<DamageStruct>() : DamageStruct();
        return damage.card && ddzCardReceipts(room, damage.card, "DdzZhijiReceipts").contains(ctx.extra_data);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card->isDamageCard()) {
            for (ServerPlayer *target : use.to)
                if (target->getMark("&ddz_yifu") > 0 && target->getMark("&ddz_hen") > 0) ctx.targets << target;
        } else {
            int count = 0;
            for (ServerPlayer *target : use.to)
                if (target->getMark("&ddz_yifu") > 0) count += target->getMark("&ddz_hen");
            ctx.extra_data = count;
            ctx.targets << owner;
        }
        room->sendCompulsoryTriggerLog(owner, this);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == ConfirmDamage) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            return damage.to == target && target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card->isDamageCard()) {
            const int hatred = target->getMark("&ddz_hen");
            if (hatred <= 0) return false;
            target->loseAllMarks("&ddz_hen");
            QVariantList receipts = ddzCardReceipts(room, use.card, "DdzZhijiReceipts");
            const QVariantMap applied = ddzAppliedReceipt(room, ctx, hatred * getEffectiveAmount(ctx));
            bool merged = false;
            for (QVariant &value : receipts) {
                QVariantMap receipt = value.toMap();
                if (!ddzSameActivation(receipt, applied)) continue;
                receipt.insert("amount", receipt.value("amount").toInt() + hatred * getEffectiveAmount(ctx));
                value = receipt;
                merged = true;
                break;
            }
            if (!merged) receipts << applied;
            ddzSetCardReceipts(room, use.card, "DdzZhijiReceipts", receipts);
        } else {
            const int count = ctx.extra_data.toInt() * getEffectiveAmount(ctx);
            for (int i = 0; i < count && target->isAlive(); ++i) {
                JudgeStruct judge;
                judge.who = target;
                judge.reason = objectName();
                judge.pattern = "Slash,Duel,EquipCard";
                room->judge(judge);
                if (!judge.card || !target->isAlive()) continue;
                if (judge.card->isKindOf("EquipCard") && !target->hasSkill("shenji", true)) room->acquireSkillFromEffect(target, "shenji", ctx);
                if (judge.card->isKindOf("Slash") || judge.card->isKindOf("Duel")) {
                    if (!target->hasSkill("wushuang", true)) room->acquireSkillFromEffect(target, "wushuang", ctx);
                    if (room->getCardPlace(judge.card->getEffectiveId()) == Player::DiscardPile)
                        room->obtainCard(target, judge.card);
                }
            }
        }
        return false;
    }
};

class Jiejiuvs : public ViewAsSkillV2
{
public:
    Jiejiuvs() : ViewAsSkillV2("jiejiu", 1) { setResponseOrUse(true); }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && card->isKindOf("Analeptic")
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getHandPile().contains(card->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.size() == 1; }
protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        const std::unique_ptr<Card> card(Sanguosha->cloneCard(name));
        return card && card->isKindOf("BasicCard") && !card->isKindOf("Analeptic");
    }
};

class Jiejiu : public TriggerSkillV2
{
public:
    Jiejiu() : TriggerSkillV2("jiejiu") { events << GameStart; view_as_skill = new Jiejiuvs; frequency = Compulsory; }
    SkillDialogInfo getDialogInfo() const override { return view_as_skill->getDialogInfo(); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == GameStart && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        for (ServerPlayer *target : room->getOtherPlayers(owner)) if (target->isFemale()) ctx.targets << target;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            QList<SkillInstance> innate;
            for (const SkillInstance &instance : target->getSkillInstances()) {
                const Skill *skill = Sanguosha->getSkill(instance.skillName);
                if (instance.source == SourceInnate && skill && skill->isVisible()) innate << instance;
            }
            if (innate.isEmpty()) break;
            qsanShuffle(innate);
            const SkillInstance removed = innate.first();
            room->doAnimate(1, owner->objectName(), target->objectName());
            // Only the selected innate copy is replaced; acquired siblings remain intact.
            room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(removed.skillName, removed.instanceID));
            if (target->isAlive() && !target->hasSkill("lijian", true)) room->acquireSkillFromEffect(target, "lijian", ctx);
        }
        return false;
    }
};

class JiejiuLimit : public CardLimitSkill
{
public:
    JiejiuLimit() : CardLimitSkill("#jiejiu-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        if (target->hasSkill("jiejiu"))
            return "Analeptic";
        return "";
    }
};

class Dingxi : public TriggerSkillV2
{
public:
    Dingxi() : TriggerSkillV2("dingxi") { events << CardsMoveOneTime; }
    QList<int> candidates(Room *room, ServerPlayer *owner, const CardsMoveOneTimeStruct &move) const
    {
        QList<int> result;
        if (!owner || !owner->isAlive() || move.from != owner || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE) return result;
        ServerPlayer *previous = owner->getNextAlive(owner->getAliveSiblings().length());
        const QVariantList pending = owner->getTag("DingxiPending").toList();
        for (int id : move.card_ids) {
            const Card *card = Sanguosha->getCard(id);
            if (!pending.contains(id) && room->getCardPlace(id) == Player::DiscardPile
                && card->isDamageCard() && owner->canUse(card, previous)) result << id;
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->hasSkill(objectName()) && !candidates(room, player, data.value<CardsMoveOneTimeStruct>()).isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const QList<int> ids = candidates(room, owner, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (ids.isEmpty()) return false;
        ServerPlayer *previous = owner->getNextAlive(owner->getAliveSiblings().length());
        room->fillAG(ids, owner);
        auto clear = qScopeGuard([&] { room->clearAG(owner); });
        if (!owner->askForSkillInvoke(objectName() + "$-1", previous)) return false;
        const int id = room->askForAG(owner, ids, false, objectName());
        if (!ids.contains(id)) return false;
        ctx.extra_data = id;
        ctx.targets << previous;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.extra_data.toInt();
        if (ctx.choice == "store") {
            if (room->getCardPlace(id) == Player::DiscardPile) target->addToPile("dingxi", id);
            return false;
        }
        const Card *card = Sanguosha->getCard(id);
        if (!ctx.invoker || !ctx.invoker->isAlive() || room->getCardPlace(id) != Player::DiscardPile || !ctx.invoker->canUse(card, target)) return false;
        // Suppress only this physical card during the nested use, including its discard move.
        const QVariant saved = owner->getTag("DingxiPending");
        QVariantList pending = saved.toList();
        if (pending.contains(id)) return false;
        pending << id;
        owner->setTag("DingxiPending", pending);
        auto restore = qScopeGuard([&] {
            if (saved.isValid()) owner->setTag("DingxiPending", saved);
            else owner->removeTag("DingxiPending");
        });
        if (room->useCardFromSkillEffect(CardUseStruct(card, ctx.invoker, target), ctx) && owner->isAlive()) {
            ctx.choice = "store";
            skillEffect(event, room, owner, ctx, owner);
            ctx.choice.clear();
        }
        return false;
    }
};

class Nengchen : public TriggerSkillV2
{
public:
    Nengchen() : TriggerSkillV2("nengchen") { events << Damaged; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = data.value<DamageStruct>().card;
        if (player && player->isAlive() && player->hasSkill(objectName()) && card)
            for (int id : player->getPile("dingxi"))
                if (Sanguosha->getCard(id)->sameNameWith(card)) return TriggerList{{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const Card *card = ctx.original_data->value<DamageStruct>().card;
        if (!card) return false;
        if (getEffectiveAmount(ctx) <= 0) return false;
        QList<int> ids = target->getPile("dingxi"), obtained;
        qsanShuffle(ids);
        for (int id : ids) {
            if (Sanguosha->getCard(id)->sameNameWith(card)) obtained << id;
            if (obtained.size() >= getEffectiveAmount(ctx)) break;
        }
        if (!obtained.isEmpty()) {
            room->sendCompulsoryTriggerLog(owner, this);
            DummyCard cards(obtained);
            room->obtainCard(target, &cards);
        }
        return false;
    }
};

class Huojie : public TriggerSkillV2
{
public:
    Huojie() : TriggerSkillV2("huojie") { events << EventPhaseStart << Damaged; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Play && !player->getPile("dingxi").isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != Damaged) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        const QVariantMap pending = room->getTag("HuojiePending").toMap();
        for (const QString &tip : damage.tips) {
            if (!pending.contains(tip)) continue;
            const QVariantMap receipt = pending.value(tip).toMap();
            if (!damage.to || damage.to->objectName() != receipt.value("target").toString()) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("serial").toInt();
            ctx.sourceRef = ddzReceiptSource(receipt);
            if (!ctx.sourceRef.isValid() || ctx.instanceID <= 0) continue;
            ctx.is_forced = true;
            ctx.amount = receipt.value("amount").toInt();
            ctx.choice = tip;
            ctx.targets << damage.to;
            ctx.current_event = event;
            ctx.original_data = &data;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : room->getTag("HuojiePending").toMap().contains(ctx.choice);
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            ctx.extra_data = owner->getPile("dingxi").size();
            ctx.targets << owner;
        }
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == Damaged) {
            const QList<int> ids = target->getPile("dingxi");
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards); }
            return false;
        }
        room->sendCompulsoryTriggerLog(owner, this);
        QVariantMap receipt = ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
        receipt.insert("target", target->objectName());
        const QString token = "huojie_application_" + receipt.value("serial").toString();
        QVariantMap pending = room->getTag("HuojiePending").toMap();
        pending.insert(token, receipt);
        room->setTag("HuojiePending", pending);
        // The applied lightning keeps its source through skill loss; unwind never leaves a receipt behind.
        auto clear = qScopeGuard([&] {
            QVariantMap remaining = room->getTag("HuojiePending").toMap();
            remaining.remove(token);
            if (remaining.isEmpty()) room->removeTag("HuojiePending");
            else room->setTag("HuojiePending", remaining);
        });
        for (int i = 0; i < ctx.extra_data.toInt() * getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            JudgeStruct judge;
            judge.who = target;
            judge.reason = "lightning";
            judge.pattern = ".|spade|2~9";
            judge.negative = true;
            judge.good = false;
            room->judge(judge);
            if (judge.isBad() && target->isAlive()) {
                DamageStruct damage("lightning", nullptr, target, 3, DamageStruct::Thunder);
                damage.tips << "huojie_lightning" << token;
                room->damage(damage);
            }
        }
        return false;
    }
};

class Huiwan : public TriggerSkillV2
{
public:
    Huiwan() : TriggerSkillV2("huiwan") { events << DrawNCards << EventPhaseChanging << TurnBroken << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return false;
        const QStringList used = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("usedNames_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString())).toStringList();
        for (const QVariant &value : ctx.extra_data.toList())
            if (used.contains(Sanguosha->getCard(value.toInt())->objectName())) return false;
        return true;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return;
        QStringList used = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("usedNames_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString())).toStringList();
        for (const QVariant &value : ctx.extra_data.toList()) {
            const QString name = Sanguosha->getCard(value.toInt())->objectName();
            if (!used.contains(name)) used << name;
        }
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("usedNames_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString()), used);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (ctx.owner && ref.isValid()) ctx.owner->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, QString("usedNames_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString()));
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventSkillInvoking && ctx.original_data) {
            const SkillContext accepted = ctx.original_data->value<SkillContext>();
            if (accepted.bypass_cost && accepted.activationRef == ctx.activationRef
                && parseSkillName(accepted.skill_name) == objectName()) addUsage(accepted);
            return;
        }

        if (event == TurnBroken || (event == EventPhaseChanging && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive)) resetUsage(ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DrawNCards || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const DrawStruct draw = data.value<DrawStruct>();
        return draw.num > 0 && draw.reason != "InitialHandCards" ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const int count = ctx.original_data->value<DrawStruct>().num;
        QList<int> pile = room->getDrawPile(), candidates;
        qsanShuffle(pile);
        QStringList names = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, QString("usedNames_%1").arg(ctx.owner->getRoom()->historyScopes().value("turn_id").toString())).toStringList();
        for (int id : pile) {
            const Card *card = Sanguosha->getCard(id);
            if ((card->isNDTrick() || card->isKindOf("BasicCard")) && !names.contains(card->objectName())) {
                names << card->objectName();
                candidates << id;
            }
        }
        if (candidates.isEmpty() || !owner->askForSkillInvoke(objectName() + "$-1", "huiwan0:" + QString::number(count))) return false;
        room->fillAG(candidates, owner);
        auto clear = qScopeGuard([&] { room->clearAG(owner); });
        QVariantList selected;
        for (int i = 0; i < count && !candidates.isEmpty(); ++i) {
            const int id = room->askForAG(owner, candidates, i > 0, objectName());
            if (!candidates.contains(id)) break;
            selected << id;
            candidates.removeOne(id);
            room->takeAG(owner, id, false, {owner});
        }
        ctx.extra_data = selected;
        ctx.targets << owner;
        return !selected.isEmpty();
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariantList selected = ctx.extra_data.toList();
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        if (selected.isEmpty() || selected.size() > draw.num || !isUsable(ctx)) return false;
        for (const QVariant &value : selected) if (room->getCardPlace(value.toInt()) != Player::DrawPile) return false;
        // Reducing the pending draw is the cost. Reserve names before any obtain-card triggers can re-enter.
        addUsage(ctx);
        draw.num -= selected.size();
        ctx.original_data->setValue(draw);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> obtained;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int selected = value.toInt();
            const QString name = Sanguosha->getCard(selected)->objectName();
            int count = 0;
            if (getEffectiveAmount(ctx) > 0 && room->getCardPlace(selected) == Player::DrawPile) { obtained << selected; ++count; }
            for (int id : room->getDrawPile()) {
                if (count >= getEffectiveAmount(ctx)) break;
                if (!obtained.contains(id) && Sanguosha->getCard(id)->objectName() == name) { obtained << id; ++count; }
            }
        }
        if (!obtained.isEmpty()) { DummyCard cards(obtained); room->obtainCard(target, &cards); }
        return false;
    }
};

class Huanli : public TriggerSkillV2
{
public:
    Huanli() : TriggerSkillV2("huanli") { events << EventPhaseStart << EventPhaseChanging << TurnBroken; global = true; }
    static void projectGrants(Room *room, ServerPlayer *target)
    {
        QMap<QString, int> allowed;
        int effects = 0;
        for (const QVariant &value : target->getTag("HuanliGrants").toList()) {
            const QVariantMap receipt = value.toMap();
            if (!receipt.value("invalidate").toBool()) continue;
            ++effects;
            for (const QString &name : receipt.value("allowed").toStringList()) ++allowed[name];
        }
        room->setPlayerMark(target, "huanliInvalidity", effects);
        for (const QString &name : {QString("zhijian"), QString("guzheng"), QString("yingzi"), QString("fanjian")})
            room->setPlayerMark(target, name + "huanliSkill", allowed.value(name));
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || (event != TurnBroken
            && (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive))) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        QVariantList remaining, expired;
        for (const QVariant &value : player->getTag("HuanliGrants").toList()) {
            if (value.toMap().value("turn").toLongLong() == turn) remaining << value;
            else expired << value;
        }
        if (remaining.isEmpty()) player->removeTag("HuanliGrants");
        else player->setTag("HuanliGrants", remaining);
        // Remove the receipt before detaching, so nested skill-loss callbacks see the remaining grants only.
        projectGrants(room, player);
        for (const QVariant &value : expired)
            for (const QVariant &entry : value.toMap().value("skills").toList()) {
                const QVariantMap grant = entry.toMap();
                room->detachSkillFromPlayer(player, SkillInstanceUtils::formatName(grant.value("name").toString(),
                    grant.value("instance").toInt()), false, true);
            }
        return false;
    }
    static bool targetCounts(Room *room, ServerPlayer *owner, QVariantMap &counts)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (turn <= 0) return false;
        // Count the recipients after target-changing skills have finished.
        QVariantMap filter{{"kind", "use_card_targets"}, {"turn_id", turn}, {"from", owner->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap().value("data").toMap();
                const QVariantMap card = fact.value("card").toMap();
                if (!card.contains("type") || !fact.contains("targets")) return false;
                if (card.value("type").toInt() == Card::TypeSkill) continue;
                QStringList seen;
                for (const QVariant &target : fact.value("targets").toList()) {
                    const QString name = target.toString();
                    if (!seen.contains(name)) { counts[name] = counts.value(name).toInt() + 1; seen << name; }
                }
            }
            if (!page.value("has_more").toBool()) return true;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QVariantMap counts;
        if (!targetCounts(room, owner, counts)) return false;
        QVariantList effects;
        const bool self = counts.value(owner->objectName()).toInt() >= 3;
        bool other = false;
        if (self) {
            ServerPlayer *target = room->askForPlayerChosen(owner, room->getOtherPlayers(owner), objectName(), "huanli0", true, true);
            if (target) effects << QVariantMap{{"target", target->objectName()}, {"skills", QStringList{"zhijian", "guzheng"}}, {"invalidate", true}};
        }
        for (ServerPlayer *target : room->getOtherPlayers(owner)) {
            if (counts.value(target->objectName()).toInt() < 3) continue;
            other = true;
            if (owner->askForSkillInvoke(objectName() + "$-1", target))
                effects << QVariantMap{{"target", target->objectName()}, {"skills", QStringList{"yingzi", "fanjian"}}, {"invalidate", true}};
        }
        if (self && other) effects << QVariantMap{{"target", owner->objectName()}, {"skills", QStringList{"zhiheng"}}, {"invalidate", false}};
        ctx.extra_data = effects;
        return !effects.isEmpty();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const QVariantList effects = ctx.extra_data.toList();
        for (const QVariant &value : effects) {
            SkillContext child = ctx;
            child.extra_data = value;
            ServerPlayer *target = room->findPlayerByObjectName(value.toMap().value("target").toString());
            if (target) skillEffect(event, room, owner, child, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap effect = ctx.extra_data.toMap();
        if (getEffectiveAmount(ctx) <= 0) return false;
        QVariantMap receipt = ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
        receipt.insert("skills", QVariantList());
        receipt.insert("allowed", effect.value("skills"));
        receipt.insert("invalidate", effect.value("invalidate"));
        const qint64 serial = receipt.value("serial").toLongLong();
        QVariantList receipts = target->getTag("HuanliGrants").toList();
        receipts << receipt;
        target->setTag("HuanliGrants", receipts);
        for (const QString &name : effect.value("skills").toStringList()) {
            if (!target->isAlive()) break;
            bool present = false;
            for (const QVariant &value : target->getTag("HuanliGrants").toList())
                present |= value.toMap().value("serial").toLongLong() == serial;
            if (!present) break; // A previous acquisition callback already crossed expiry.
            room->acquireSkillFromEffect(target, name, ctx, [target, serial, name](int id) {
                QVariantList pending = target->getTag("HuanliGrants").toList();
                for (QVariant &value : pending) {
                    QVariantMap saved = value.toMap();
                    if (saved.value("serial").toLongLong() != serial) continue;
                    QVariantList grants = saved.value("skills").toList();
                    grants << QVariantMap{{"name", name}, {"instance", id}};
                    saved.insert("skills", grants); value = saved; break;
                }
                target->setTag("HuanliGrants", pending);
            });
        }
        projectGrants(room, target);
        owner->peiyin(this);
        return false;
    }
};

class HuanliInvalidity : public InvaliditySkill
{
public:
    HuanliInvalidity() : InvaliditySkill("#huanli-invalidity") {}
    bool isSkillValid(const Player *player, const Skill *skill) const override
    {
        return player->getMark("huanliInvalidity") <= 0 || player->getMark(skill->objectName() + "huanliSkill") > 0;
    }
};

class Yinfeng : public TriggerSkillV2
{
public:
    Yinfeng() : TriggerSkillV2("yinfeng") { events << GameStart << CardsMoveOneTime; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == GameStart) return TriggerList{{player, {objectName()}}};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::DiscardPile
            && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
            for (int id : move.card_ids)
                if (room->getCardPlace(id) == Player::DiscardPile && Sanguosha->getCard(id)->isKindOf("GodSword"))
                    return TriggerList{{player, {objectName()}}};
        }
        if (move.from == player && move.to && move.to != player && move.from_places.contains(Player::PlaceHand)
            && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip)) {
            for (const Card *card : player->getHandcards()) if (card->isKindOf("GodSword")) return TriggerList{{player, {objectName()}}};
            for (int i = 0; i < move.card_ids.size(); ++i)
                if (move.from_places.value(i) == Player::PlaceHand && Sanguosha->getCard(move.card_ids.at(i))->isKindOf("GodSword"))
                    return TriggerList{{player, {objectName()}}};
        }
        return {};
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        room->sendCompulsoryTriggerLog(owner, this);
        if (event == GameStart) {
            ctx.choice = "initial";
            skillEffect(event, room, owner, ctx, owner);
            return false;
        }
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::DiscardPile) {
            ctx.choice = "retrieve";
            for (int id : move.card_ids) {
                if (!Sanguosha->getCard(id)->isKindOf("GodSword")) continue;
                ctx.extra_data = id;
                skillEffect(event, room, owner, ctx, owner);
            }
        } else {
            ServerPlayer *recipient = room->findPlayerByObjectName(move.to->objectName());
            if (!recipient) return false;
            bool kept = false, given = false;
            for (const Card *card : owner->getHandcards()) kept = kept || card->isKindOf("GodSword");
            for (int i = 0; i < move.card_ids.size(); ++i)
                given = given || (move.from_places.value(i) == Player::PlaceHand && Sanguosha->getCard(move.card_ids.at(i))->isKindOf("GodSword"));
            if (kept) { ctx.choice = "damage"; ctx.extra_data = owner->objectName(); skillEffect(event, room, owner, ctx, recipient); }
            if (given) { ctx.choice = "damage"; ctx.extra_data = recipient->objectName(); skillEffect(event, room, owner, ctx, owner); }
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "initial") {
            for (int id : room->getDrawPile())
                if (Sanguosha->getCard(id)->isKindOf("GodSword")) { room->obtainCard(target, id); break; }
        } else if (ctx.choice == "retrieve") {
            const int id = ctx.extra_data.toInt();
            if (room->getCardPlace(id) != Player::DiscardPile) return false;
            room->loseHp(target, getEffectiveAmount(ctx), true, owner, objectName());
            if (target->isAlive() && room->getCardPlace(id) == Player::DiscardPile) room->obtainCard(target, id);
        } else {
            ServerPlayer *from = room->findPlayerByObjectName(ctx.extra_data.toString());
            room->damage(DamageStruct(objectName(), from, target, getEffectiveAmount(ctx)));
        }
        return false;
    }
};

class DdzFulu : public TriggerSkillV2
{
public:
    DdzFulu() : TriggerSkillV2("ddzfulu") { events << PreCardUsed << CardFinished; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != PreCardUsed) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card) return false;
        ddzSetCardReceipts(room, use.card, "DdzFuluSources", {});
        if (!use.card->isKindOf("Slash") || !use.from) return false;
        QVariantList sources;
        for (ServerPlayer *owner : room->getAlivePlayers()) {
            const bool attacker = owner == use.from;
            if (!attacker && (!use.to.contains(owner) || use.from->getHp() >= owner->getHp())) continue;
            for (int id : owner->getValidSkillInstanceIds(objectName()))
                sources << QVariantMap{{"owner", owner->objectName()}, {"instance", id}, {"attacker", attacker}};
        }
        ddzSetCardReceipts(room, use.card, "DdzFuluSources", sources);
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.from || !use.from->isAlive() || use.from->isKongcheng()) return true;
        for (const QVariant &value : ddzCardReceipts(room, use.card, "DdzFuluSources")) {
            const QVariantMap source = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(source.value("owner").toString());
            if (!owner || (!source.value("attacker").toBool() && !use.to.contains(owner))) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = source.value("instance").toInt();
            ctx.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), ctx.instanceID));
            ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
            if (!ctx.sourceRef.isValid()) continue;
            ctx.amount = room->getSkillInstanceAmount(ctx.activationRef);
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.extra_data = source;
            contexts << ctx;
        }
        return true;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *actor = use.from;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : use.to)
            if (target->isAlive() && target != actor && (owner == actor || target == owner)) candidates << target;
        if (candidates.isEmpty() || !actor->isAlive() || actor->isKongcheng()) return false;
        ServerPlayer *target = owner == actor
            ? room->askForPlayerChosen(actor, candidates, objectName(), "ddzfulu0", true, true) : owner;
        if (!target) return false;
        const std::unique_ptr<const Card> selected(room->askForExchange(actor, objectName(), 1, 1, false,
            "ddzfulu1:" + target->objectName(), true));
        if (!selected || selected->subcardsLength() != 1) return false;
        ctx.extra_data = selected->getSubcards().first();
        ctx.targets << target;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *actor = ctx.original_data->value<CardUseStruct>().from;
        ServerPlayer *target = ctx.targets.value(0);
        const int id = ctx.extra_data.toInt();
        if (!actor || !actor->isAlive() || !target || !target->isAlive() || !actor->handCards().contains(id)
            || Sanguosha->getCard(id)->hasFlag("using")) return false;
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
        if (!before.value("complete").toBool() || skillEvent <= 0) return false;
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, actor->objectName(), target->objectName(), objectName(), QString());
        room->obtainCard(target, Sanguosha->getCard(id), reason, false);
        return ddzPaidMaterial(room, actor, id, Player::PlaceHand, objectName(), skillEvent, before.value("watermark"), target);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "receive") {
            const QVariantMap selected = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(selected.value("giver").toString(), true);
            QList<int> ids = ListV2I(selected.value("ids").toList());
            qsanRemoveIf(ids, [&](int id) { return !giver || room->getCardOwner(id) != giver || room->getCardPlace(id) != Player::PlaceHand
                || Sanguosha->getCard(id)->hasFlag("using") || !target->canGet(giver, id); });
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); }
            return false;
        }
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !actor->isAlive()) return false;
        owner->peiyin(this);
        QList<int> selected;
        for (int i = 0; i < 2 * getEffectiveAmount(ctx) && actor->canGet(target, "h"); ++i) {
            if (selected.size() >= target->getHandcardNum()) break;
            const int id = room->askForCardChosen(actor, target, "h", objectName(), false, Card::MethodGet, selected, i > 0);
            if (id < 0 || selected.contains(id) || !target->handCards().contains(id) || Sanguosha->getCard(id)->hasFlag("using") || !actor->canGet(target, id)) break;
            selected << id;
        }
        if (!selected.isEmpty()) {
            ctx.extra_data = QVariantMap{{"giver", target->objectName()}, {"ids", ListI2V(selected)}};
            ctx.choice = "receive";
            skillEffect(event, room, owner, ctx, actor);
            ctx.choice.clear();
        }
        return false;
    }
};

static const Card *ddzEffectCard(TriggerEvent event, const QVariant &data)
{
    if (event == ConfirmDamage) return data.value<DamageStruct>().card;
    if (event == PreHpRecover) return data.value<RecoverStruct>().card;
    if (event == CardResponded || event == PostCardResponded) return data.value<CardResponseStruct>().m_card;
    return data.value<CardUseStruct>().card;
}

static void ddzCollectCardReceipts(const QString &skill, const QString &tag, TriggerEvent event,
    Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts)
{
    const Card *card = ddzEffectCard(event, data);
    if (!card) return;
    ServerPlayer *eventTarget = event == ConfirmDamage ? data.value<DamageStruct>().to : player;
    for (const QVariant &value : ddzCardReceipts(room, card, tag)) {
        const QVariantMap receipt = value.toMap();
        ServerPlayer *target = eventTarget;
        if ((event == CardFinished || event == PostCardResponded) && receipt.contains("recipient"))
            target = room->findPlayerByObjectName(receipt.value("recipient").toString(), true);
        if (!target || !target->isAlive()) continue;
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner) continue;
        SkillContext ctx;
        ctx.skill_name = skill;
        ctx.owner = owner;
        ctx.invoker = ctx.initiator = player;
        ctx.instanceID = receipt.value("serial").toInt();
        // A retained effect never reactivates its possibly retired grant.
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        if (!ctx.sourceRef.isValid() || ctx.instanceID <= 0) continue;
        ctx.amount = receipt.value("amount").toInt();
        ctx.extra_data = receipt;
        ctx.targets << target;
        ctx.preferredTarget = target;
        ctx.is_forced = true;
        ctx.current_event = event;
        ctx.original_data = &data;
        contexts << ctx;
    }
}

class Juejue : public TriggerSkillV2
{
public:
    Juejue() : TriggerSkillV2("juejue")
    {
        events << PreCardUsed << CardUsed << ConfirmDamage << PreHpRecover << CardFinished << CardResponded << PostCardResponded;
        frequency = Compulsory;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == PreCardUsed || event == CardResponded) ddzResetCardReceipts(room, ddzEffectCard(event, data), "JuejueReceipts");
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if ((event != CardUsed && event != CardResponded) || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == CardResponded && (!data.value<CardResponseStruct>().m_isUse || data.value<CardResponseStruct>().nullified)) return {};
        const Card *card = ddzEffectCard(event, data);
        if (!card || !(card->isKindOf("Slash") || card->isKindOf("Jink") || card->isKindOf("Peach") || card->isKindOf("Analeptic"))) return {};
        const QString kind = card->isKindOf("Slash") ? "Slash" : card->getClassName();
        return room->countHistoryCards(player, "game", kind) == 1 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event == CardUsed || event == PreCardUsed || event == CardResponded) return false;
        if (event == PostCardResponded && room->historyEvent(ddzCurrentUse(room)).value("data").toMap().value("is_provision").toBool()) return true;
        ddzCollectCardReceipts(objectName(), "JuejueReceipts", event, room, player, data, contexts);
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ddzEffectCard(ctx.current_event, *ctx.original_data) : nullptr;
        return card && ddzCardReceipts(room, card, "JuejueReceipts").contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == CardUsed || event == CardResponded) ctx.targets << owner;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardFinished || event == PostCardResponded) {
            const Card *card = ddzEffectCard(event, *ctx.original_data);
            QVariantList receipts = ddzCardReceipts(room, card, "JuejueReceipts");
            receipts.removeOne(ctx.extra_data);
            ddzSetCardReceipts(room, card, "JuejueReceipts", receipts);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == CardUsed || event == CardResponded) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            const Card *card = ddzEffectCard(event, *ctx.original_data);
            QVariantList receipts = ddzCardReceipts(room, card, "JuejueReceipts");
            QVariantMap receipt = ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
            receipt.insert("recipient", target->objectName());
            receipts << receipt;
            ddzSetCardReceipts(room, card, "JuejueReceipts", receipts);
            if (event == CardUsed) { use.m_addHistory = false; ctx.original_data->setValue(use); }
            room->sendCompulsoryTriggerLog(owner, this);
        } else if (event == ConfirmDamage) {
            if (ctx.original_data->value<DamageStruct>().to == target) target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        } else if (event == PreHpRecover) {
            if (target != ctx.preferredTarget) return false;
            RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
            recover.recover += getEffectiveAmount(ctx);
            ctx.original_data->setValue(recover);
        } else {
            const Card *card = ddzEffectCard(event, *ctx.original_data);
            QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
            qsanRemoveIf(ids, [&](int id) { return room->getCardPlace(id) != Player::DiscardPile && room->getCardPlace(id) != Player::PlaceTable; });
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards); }
        }
        return false;
    }
};

class Pimi : public TriggerSkillV2
{
public:
    Pimi() : TriggerSkillV2("pimi")
    {
        events << PreCardUsed << TargetSpecified << TargetConfirmed << ConfirmDamage << PreHpRecover << EventPhaseChanging << TurnBroken;
        global = true;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        return ctx.owner && ref.isValid() && !ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "disabled_turns").toList().contains(ctx.owner->getRoom()->historyScopes().value("turn_id"));
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return;
        QVariantList turns = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "disabled_turns").toList();
        const QVariant turn = ctx.owner->getRoom()->historyScopes().value("turn_id");
        if (!turns.contains(turn)) turns << turn;
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "disabled_turns", turns);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return;
        QVariantList turns = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "disabled_turns").toList();
        turns.removeAll(ctx.owner->getRoom()->historyScopes().value("turn_id"));
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "disabled_turns", turns);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == TurnBroken || (event == EventPhaseChanging && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive)) resetUsage(ctx);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == PreCardUsed && data.value<CardUseStruct>().card) ddzSetCardReceipts(room, data.value<CardUseStruct>().card, "PimiReceipts", {});
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if ((event != TargetSpecified && event != TargetConfirmed) || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.from || use.to.size() != 1 || use.to.first() == use.from) return {};
        if ((event == TargetSpecified && use.from != player) || (event == TargetConfirmed && use.to.first() != player)) return {};
        return player->canDiscard(use.from, "he") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != ConfirmDamage && event != PreHpRecover) return false;
        ddzCollectCardReceipts(objectName(), "PimiReceipts", event, room, player, data, contexts);
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ddzEffectCard(ctx.current_event, *ctx.original_data) : nullptr;
        return card && ddzCardReceipts(room, card, "PimiReceipts").contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage || event == PreHpRecover) return true;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!isUsable(ctx) || !owner->canDiscard(use.from, "he") || !owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
        const int id = room->askForCardChosen(owner, use.from, "he", objectName(), false, Card::MethodDiscard);
        if (id < 0) return false;
        ctx.extra_data = id;
        ctx.targets << use.from;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage || event == PreHpRecover) return true;
        if (!isUsable(ctx)) return false;
        ServerPlayer *source = ctx.original_data->value<CardUseStruct>().from;
        const int id = ctx.extra_data.toInt();
        if (!source || !source->isAlive() || room->getCardOwner(id) != source || !owner->canDiscard(source, id)) return false;
        return source != owner || ddzDiscard(room, owner, owner, id, objectName());
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == ConfirmDamage) {
            if (ctx.original_data->value<DamageStruct>().to == target) target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
            return false;
        }
        if (event == PreHpRecover) {
            if (target != ctx.preferredTarget) return false;
            RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
            recover.recover += getEffectiveAmount(ctx);
            ctx.original_data->setValue(recover);
            return false;
        }
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (target != use.from) return false;
        if (target != owner) {
            const int id = ctx.extra_data.toInt();
            if (!ddzDiscard(room, target, owner, id, objectName())) return false;
        }
        QVariantList receipts = ddzCardReceipts(room, use.card, "PimiReceipts");
        receipts << ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
        ddzSetCardReceipts(room, use.card, "PimiReceipts", receipts);
        bool maximum = true, minimum = true;
        for (ServerPlayer *player : room->getAlivePlayers()) {
            maximum = maximum && player->getHandcardNum() <= target->getHandcardNum();
            minimum = minimum && player->getHandcardNum() >= target->getHandcardNum();
        }
        if (maximum || minimum) {
            addUsage(ctx);
            ctx.choice = "draw";
            skillEffect(event, room, owner, ctx, owner);
            ctx.choice.clear();
        }
        return false;
    }
};

class Duanti : public TriggerSkillV2
{
public:
    Duanti() : TriggerSkillV2("duanti") { events << CardsMoveOneTime; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return move.to == player && move.to_place == Player::PlaceHand && !move.card_ids.isEmpty()
            && move.reason.m_skillName != "InitialHandCards" && move.reason.m_reason == CardMoveReason::S_REASON_DRAW
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        room->damage(DamageStruct(objectName(), nullptr, target, getEffectiveAmount(ctx)));
        return false;
    }
};

class Lianwu : public TriggerSkillV2
{
public:
    Lianwu() : TriggerSkillV2("lianwu") { events << TargetSpecified << TargetConfirmed; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash") || !use.from || use.to.size() != 1) return {};
        if ((event == TargetSpecified && use.from != player) || (event == TargetConfirmed && use.to.first() != player)) return {};
        return use.from->isAlive() && use.from->canDiscard(use.to.first(), "he")
            && (use.from->getWeapon() || use.card->getTag("drank").toInt() > 0) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ctx.extra_data = int(use.from->getWeapon() != nullptr) + int(use.card->getTag("drank").toInt() > 0);
        if (!use.from->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
        ctx.targets << use.to.first();
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.original_data->value<CardUseStruct>().from;
        for (int i = 0; i < ctx.extra_data.toInt() * getEffectiveAmount(ctx) && actor && actor->isAlive()
            && target->isAlive() && actor->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(actor, target, "he", objectName(), false, Card::MethodDiscard);
            if (id < 0 || !ddzDiscard(room, target, actor, id, objectName())) break;
        }
        return false;
    }
};

class DdzChengxiang : public TriggerSkillV2
{
public:
    DdzChengxiang() : TriggerSkillV2("ddzchengxiang") { events << Damaged; m_baseAmount = 4; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int damage = data.value<DamageStruct>().damage;
        return player && player->isAlive() && player->hasSkill(objectName()) && damage > 0
            ? TriggerList{{player, {objectName() + '*' + QString::number(damage)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
        ctx.targets << owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int extra = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "extraReveal").toInt();
        owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "extraReveal");
        const int count = qMax(0, getEffectiveAmount(ctx) + extra);
        if (count == 0) return false;
        QList<int> ids = room->getNCards(count), returned;
        const QList<int> removedFromPile = ids;
        auto returnUnrevealed = qScopeGuard([&] {
            QList<int> remaining;
            for (int id : removedFromPile)
                if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id;
            if (!remaining.isEmpty()) room->returnToTopDrawPile(remaining);
        });
        if (target->hasSkill("duanti")) {
            bool maximum = true, minimum = true;
            for (ServerPlayer *player : room->getAlivePlayers()) {
                maximum = maximum && player->getHp() <= target->getHp();
                minimum = minimum && player->getHp() >= target->getHp();
            }
            QList<int> required;
            // Keep the top cards whenever they already meet Duanti's guarantees.
            for (int kind = 0; kind < 2; ++kind) {
                if ((kind == 0 && !maximum) || (kind == 1 && !minimum)) continue;
                auto matches = [kind](int id) {
                    const Card *card = Sanguosha->getCard(id);
                    return kind == 0 ? card->isKindOf("Weapon") || card->isDamageCard()
                                     : card->isKindOf("Peach") || card->isKindOf("Analeptic");
                };
                int selected = -1;
                for (int id : ids) if (matches(id)) { selected = id; break; }
                if (selected < 0)
                    for (int id : room->getDrawPile()) if (matches(id)) { selected = id; break; }
                if (selected < 0) continue;
                if (!ids.contains(selected)) {
                    int replace = ids.size() - 1;
                    while (replace >= 0 && required.contains(ids.at(replace))) --replace;
                    if (replace < 0) continue;
                    returned.prepend(ids.at(replace));
                    ids[replace] = selected;
                }
                required << selected;
            }
        }
        if (!returned.isEmpty()) room->returnToTopDrawPile(returned);
        auto clear = qScopeGuard([&] {
            room->clearAG();
            QList<int> remaining;
            for (int id : ids) if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
            if (!remaining.isEmpty()) room->throwCard(remaining, objectName(), nullptr);
        });
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), QString())), true);
        if (!target->isAlive()) return false;
        QList<int> available;
        for (int id : ids) if (room->getCardPlace(id) == Player::PlaceTable) available << id;
        room->fillAG(available);
        QList<int> obtained;
        int sum = 0;
        while (!available.isEmpty() && target->isAlive()) {
            const QList<int> snapshot = available;
            for (int id : snapshot)
                if (room->getCardPlace(id) != Player::PlaceTable || sum + Sanguosha->getCard(id)->getNumber() > 13) {
                    available.removeOne(id);
                    room->takeAG(nullptr, id, false);
                }
            if (available.isEmpty()) break;
            const int id = room->askForAG(target, available, true, objectName());
            if (!available.contains(id)) break;
            obtained << id;
            sum += Sanguosha->getCard(id)->getNumber();
            available.removeOne(id);
            room->takeAG(target, id, false);
        }
        room->clearAG();
        qsanRemoveIf(obtained, [&](int id) { return room->getCardPlace(id) != Player::PlaceTable; });
        if (!obtained.isEmpty() && target->isAlive()) {
            // Save this copy's next reveal bonus before obtain-card triggers can activate it again.
            sum = 0;
            for (int id : obtained) sum += Sanguosha->getCard(id)->getNumber();
            if (sum == 13) owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "extraReveal", 1);
            DummyCard cards(obtained);
            room->obtainCard(target, &cards);
        }
        return false;
    }
};

class Lieti : public TriggerSkillV2
{
public:
    Lieti() : TriggerSkillV2("lieti")
    { events << CardsMoveOneTime << DrawNCards; waked_skills = "#LietiBf,#LietiBf2"; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == DrawNCards) {
            const DrawStruct draw = data.value<DrawStruct>();
            return draw.reason == "InitialHandCards" && draw.num > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return move.to == player && move.to_place == Player::PlaceHand && !move.card_ids.isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DrawNCards) {
            if (target != ctx.owner) return false;
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += draw.num * getEffectiveAmount(ctx);
            ctx.original_data->setValue(draw);
        } else {
            const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            const bool initial = move.reason.m_skillName == "InitialHandCards";
            const QString current = target->property("lietiGN").toString();
            if (!initial && current.isEmpty()) return false;
            int index = 0;
            for (int id : move.card_ids) {
                if (!target->handCards().contains(id)) continue;
                const QString general = initial ? (index++ < move.card_ids.size() / 2 ? "lt_yuanshao" : "lt_yuanshu") : current;
                room->setCardTip(id, "-lt_yuanshao");
                room->setCardTip(id, "-lt_yuanshu");
                room->setCardTip(id, general);
            }
        }
        room->sendCompulsoryTriggerLog(owner, this);
        return false;
    }
};

class LietiBf : public CardLimitSkill
{
public:
    LietiBf() : CardLimitSkill("#LietiBf") {}
    QString limitList(const Player *target) const override
    {
        // The exception is for using a first hand card, not for a pure response.
        return target->hasSkill("shigong") && target->getMark("shigongFirstHandUsed-Clear") == 0 ? "response" : "use,response";
    }
    QString limitPattern(const Player *target, const Card *card) const override
    {
        if (!target->hasSkill("lieti")) return QString();
        const QString general = target->property("lietiGN").toString();
        if (target->handCards().contains(card->getId()) && !card->hasTip(general)) return card->toString();
        return QString();
    }
};

class LietiBf2 : public CardLimitSkill
{
public:
    LietiBf2() : CardLimitSkill("#LietiBf2") {}
    QString limitList(const Player *) const override { return "ignore"; }
    QString limitPattern(const Player *target, const Card *card) const override
    {
        return target->hasSkill("lieti") && target->handCards().contains(card->getId())
            && !card->hasTip(target->property("lietiGN").toString()) ? card->toString() : QString();
    }
};

class Shigong : public TriggerSkillV2
{
public:
    Shigong() : TriggerSkillV2("shigong") { events << CardFinished << PreCardUsed << EventSkillInvoking << CardResponded << PostCardResponded; frequency = Compulsory; global = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && (!ctx.activationRef.isValid() || isUsable(ctx)); }
    static bool firstHandUse(Room *room, ServerPlayer *owner)
    {
        const qint64 current = ddzCurrentUse(room);
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (current <= 0 || turn <= 0) return false;
        QVariantMap filter{{"from", owner->objectName()}, {"turn_id", turn}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), data = fact.value("data").toMap();
                if (fact.value("kind").toString() != "use_card" && (fact.value("kind").toString() != "respond_card" || !data.value("is_use").toBool())) continue;
                if (!data.contains("is_handcard")) return false;
                if (data.value("card").toMap().value("type").toInt() == Card::TypeSkill || !data.value("is_handcard").toBool()) continue;
                return fact.value("event_id").toLongLong() == current;
            }
            if (!page.value("has_more").toBool()) return false;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardResponded) {
            const CardResponseStruct response = data.value<CardResponseStruct>();
            ddzResetCardReceipts(room, response.m_card, "ShigongReceipts");
            if (response.m_isUse && response.m_isHandcard && response.m_card && response.m_card->getTypeId() != Card::TypeSkill)
                room->setPlayerMark(player, "shigongFirstHandUsed-Clear", 1);
            return false;
        }
        if (event != PreCardUsed) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card) return false;
        ddzSetCardReceipts(room, use.card, "ShigongReceipts", {});
        // Public legality projection only. Activation still queries the accepted use facts, including before skill acquisition.
        if (use.from && use.m_isHandcard && use.card->getTypeId() != Card::TypeSkill)
            room->setPlayerMark(use.from, "shigongFirstHandUsed-Clear", 1);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if ((event != PreCardUsed && event != CardResponded) || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == CardResponded) {
            const CardResponseStruct response = data.value<CardResponseStruct>();
            return response.m_card && response.m_isUse && response.m_isHandcard && !response.nullified
                && response.m_card->getTypeId() != Card::TypeSkill && firstHandUse(room, player)
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.card && use.card->getTypeId() != Card::TypeSkill && use.m_isHandcard && firstHandUse(room, player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished && event != PostCardResponded) return false;
        if (event == PostCardResponded && room->historyEvent(ddzCurrentUse(room)).value("data").toMap().value("is_provision").toBool()) return true;
        ddzCollectCardReceipts(objectName(), "ShigongReceipts", event, room, player, data, contexts);
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ddzEffectCard(ctx.current_event, *ctx.original_data) : nullptr;
        return card && ddzCardReceipts(room, card, "ShigongReceipts").contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == CardFinished || event == PostCardResponded) return true;
        if (!isUsable(ctx)) return false;
        const Card *card = ddzEffectCard(event, *ctx.original_data);
        QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        for (int id : ids) {
            const Card *material = Sanguosha->getCard(id);
            for (const QString &general : {QString("lt_yuanshao"), QString("lt_yuanshu")})
                if (material->hasTip(general)) { ctx.choice = general; break; }
            if (!ctx.choice.isEmpty()) break;
        }
        if (ctx.choice.isEmpty()) return false;
        ctx.targets << owner;
        return true;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && parseSkillName(accepted.skill_name) == objectName()) addUsage(accepted);
    }
    bool pay(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardFinished || event == PostCardResponded) return true;
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardFinished || event == PostCardResponded) {
            const Card *card = ddzEffectCard(event, *ctx.original_data);
            QVariantList receipts = ddzCardReceipts(room, card, "ShigongReceipts");
            receipts.removeOne(ctx.extra_data);
            ddzSetCardReceipts(room, card, "ShigongReceipts", receipts);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const Card *used = ddzEffectCard(event, *ctx.original_data);
        if (event == PreCardUsed || event == CardResponded) {
            QVariantList receipts = ddzCardReceipts(room, used, "ShigongReceipts");
            QVariantMap receipt = ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
            receipt.insert("general", ctx.choice);
            receipt.insert("recipient", target->objectName());
            receipts << receipt;
            ddzSetCardReceipts(room, used, "ShigongReceipts", receipts);
            return false;
        }
        const QString general = ctx.extra_data.toMap().value("general").toString();
        if (!Sanguosha->getGeneral(general)) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        room->setPlayerProperty(target, "lietiGN", general);
        room->setPlayerProperty(target, "ChangeHeroMaxHp", target->getMaxHp() + 1);
        room->changeHero(target, general, false, false);
        if (general == "lt_yuanshao") {
            for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
                Card *card = Sanguosha->cloneCard("archery_attack");
                if (!card) break;
                card->setSkillName("_shigong");
                CardUseStruct use(card, target);
                use.setOwnedCard(card);
                // Keep accepted provenance after changeHero retires the granting skill.
                if (target->canUse(card)) {
                    SkillContext accepted = ctx;
                    const QVariantMap receipt = ctx.extra_data.toMap();
                    accepted.activationRef = SkillInstanceRef(receipt.value("owner").toString(),
                        SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("instance").toInt()));
                    room->useCardFromSkillEffect(use, accepted, true);
                }
            }
        } else if (target->isAlive()) target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Luankui : public TriggerSkillV2
{
public:
    Luankui() : TriggerSkillV2("luankui")
    { events << Damage << CardsMoveOneTime << ConfirmDamage << DrawNCards << EventPhaseChanging << TurnBroken << EventSkillEffectFinished; global = true; }
    Frequency getFrequency(const Player *player) const override { return player ? Compulsory : NotFrequent; }
    static bool secondOccurrence(Room *room, ServerPlayer *owner, bool damage)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        const qint64 current = room->historyParent(room->currentHistoryEventId(), damage ? "damage" : "move_cards", true).value("id").toLongLong();
        if (turn <= 0 || current <= 0) return false;
        QVariantMap filter{{"turn_id", turn}, {damage ? "from" : "to", owner->objectName()}};
        QList<qint64> occurred;
        for (;;) {
            const QVariantMap page = damage ? room->queryActualDamage(filter) : room->queryHistoryMoves(filter);
            if (!page.value("error").toString().isEmpty() || !page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), data = fact.value("data").toMap();
                if (!damage && (data.value("to_place").toInt() != Player::PlaceHand
                    || data.value("reason").toInt() != CardMoveReason::S_REASON_DRAW
                    || data.value("reason_skill").toString() == "InitialHandCards")) continue;
                const qint64 event = fact.value("event_id").toLongLong();
                if (!occurred.contains(event)) occurred << event;
                if (event == current) return occurred.size() == 2;
            }
            if (!page.value("has_more").toBool()) return false;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (parseSkillName(finished.skill_name) == objectName() && !finished.activationRef.isValid()) consume(room, finished);
        } else ddzExpireReceipts(room, player, event, data, "LuankuiPending");
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if ((event != Damage && event != CardsMoveOneTime) || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to != player || move.to_place != Player::PlaceHand || move.reason.m_reason != CardMoveReason::S_REASON_DRAW
                || move.reason.m_skillName == "InitialHandCards") return {};
        }
        const QString tip = event == Damage ? "lt_yuanshao" : "lt_yuanshu";
        bool available = false;
        for (const Card *card : player->getHandcards()) if (card->hasTip(tip) && !card->hasFlag("using") && player->canDiscard(card->getEffectiveId())) available = true;
        return available && secondOccurrence(room, player, event == Damage) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != ConfirmDamage && event != DrawNCards) return false;
        if (!player || !player->isAlive()) return true;
        const QString kind = event == ConfirmDamage ? "damage" : "draw";
        for (const QVariant &value : player->getTag("LuankuiPending").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("kind").toString() != kind) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.initiator = owner;
            ctx.invoker = player;
            ctx.instanceID = receipt.value("serial").toInt();
            ctx.sourceRef = ddzReceiptSource(receipt);
            if (!ctx.sourceRef.isValid() || ctx.instanceID <= 0) continue;
            ctx.is_forced = true;
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt;
            ctx.targets << (event == ConfirmDamage ? data.value<DamageStruct>().to : player);
            ctx.current_event = event;
            ctx.original_data = &data;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("holder").toString(), true);
        return holder && holder->getTag("LuankuiPending").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage || event == DrawNCards) return true;
        const QString tip = event == Damage ? "lt_yuanshao" : "lt_yuanshu";
        QStringList ids;
        for (const Card *card : owner->getHandcards()) if (card->hasTip(tip) && !card->hasFlag("using") && owner->canDiscard(card->getEffectiveId())) ids << card->toString();
        const Card *selected = ids.isEmpty() ? nullptr : room->askForCard(owner, ids.join(','), event == Damage ? "luankui0" : "luankui1",
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!selected) return false;
        ctx.extra_data = selected->getEffectiveId();
        ctx.targets << owner;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage || event == DrawNCards) return true;
        const int id = ctx.extra_data.toInt();
        return owner->handCards().contains(id) && ddzDiscard(room, owner, owner, id, objectName());
    }
    static void consume(Room *room, const SkillContext &ctx)
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
        if (!holder) return;
        QVariantList pending = holder->getTag("LuankuiPending").toList();
        if (!pending.removeOne(ctx.extra_data)) return;
        holder->setTag("LuankuiPending", pending);
        bool more = false;
        for (const QVariant &value : pending) more = more || value.toMap().value("kind") == receipt.value("kind");
        room->setPlayerMark(holder, "&luankui+" + receipt.value("kind").toString() + "-Clear", int(more));
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage || event == DrawNCards) consume(room, ctx);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == ConfirmDamage) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (target == damage.to) target->damageRevises(*ctx.original_data, damage.damage * getEffectiveAmount(ctx));
        } else if (event == DrawNCards) {
            if (target->objectName() != ctx.extra_data.toMap().value("holder").toString()) return false;
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += draw.num * getEffectiveAmount(ctx);
            ctx.original_data->setValue(draw);
        } else {
            const QString kind = event == Damage ? "damage" : "draw";
            QVariantList pending = target->getTag("LuankuiPending").toList();
            QVariantMap receipt = ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
            receipt.insert("holder", target->objectName());
            receipt.insert("kind", kind);
            pending << receipt;
            target->setTag("LuankuiPending", pending);
            room->setPlayerMark(target, "&luankui+" + kind + "-Clear", 1);
        }
        return false;
    }
};

class Huaquan : public TriggerSkillV2
{
public:
    Huaquan() : TriggerSkillV2("huaquan")
    { events << PreCardUsed << TargetSpecified << CardFinished << ConfirmDamage; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == PreCardUsed && data.value<CardUseStruct>().card) ddzSetCardReceipts(room, data.value<CardUseStruct>().card, "HuaquanReceipts", {});
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && use.card->getTypeId() != Card::TypeSkill && use.card->isBlack())
            for (ServerPlayer *target : use.to) if (target != player) return TriggerList{{player, {objectName()}}};
        return {};
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished && event != ConfirmDamage) return false;
        QList<SkillContext> applied;
        ddzCollectCardReceipts(objectName(), "HuaquanReceipts", event, room, player, data, applied);
        const QString choice = event == CardFinished ? "huaquan2" : "huaquan1";
        for (const SkillContext &ctx : applied) if (ctx.extra_data.toMap().value("choice").toString() == choice) contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ddzEffectCard(ctx.current_event, *ctx.original_data) : nullptr;
        return card && ddzCardReceipts(room, card, "HuaquanReceipts").contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != TargetSpecified) return true;
        ctx.choice = room->askForChoice(owner, objectName(), "huaquan1+huaquan2", *ctx.original_data);
        ctx.targets << owner;
        return ctx.choice == "huaquan1" || ctx.choice == "huaquan2";
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == ConfirmDamage) {
            if (ctx.original_data->value<DamageStruct>().to == target) target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        } else if (event == CardFinished) target->drawCards(getEffectiveAmount(ctx), objectName());
        else if (ctx.choice == "guess") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            const QVariant saved = room->getTag("HuaquanGuess");
            room->setTag("HuaquanGuess", QVariantMap{{"owner", owner->objectName()}, {"target", target->objectName()},
                                                    {"choice", receipt.value("choice")}});
            auto restore = qScopeGuard([&] {
                if (saved.isValid()) room->setTag("HuaquanGuess", saved);
                else room->removeTag("HuaquanGuess");
            });
            // Sanou observes the existing ChoiceMade event, with its own instance and target hooks.
            room->askForChoice(target, "huaquan0", "huaquan1+huaquan2", *ctx.original_data);
        } else {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            QVariantMap receipt = ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
            receipt.insert("choice", ctx.choice);
            QVariantList receipts = ddzCardReceipts(room, use.card, "HuaquanReceipts");
            receipts << receipt;
            ddzSetCardReceipts(room, use.card, "HuaquanReceipts", receipts);
            room->sendCompulsoryTriggerLog(owner, this);
            ctx.extra_data = receipt;
            ctx.choice = "guess";
            for (ServerPlayer *other : use.to) if (other != owner) skillEffect(event, room, owner, ctx, other);
            ctx.choice = receipt.value("choice").toString();
        }
        return false;
    }
};

class Sanou : public TriggerSkillV2
{
public:
    Sanou() : TriggerSkillV2("sanou")
    { events << CardsMoveOneTime << Damaged << MarkChanged << EventPhaseChanging << ChoiceMade; frequency = Compulsory; global = true; }
    static bool recovered(Room *room, const QVariantMap &receipt)
    {
        QVariantMap filter{{"after", receipt.value("after")}};
        int moved = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap data = value.toMap().value("data").toMap();
                if (!data.contains("from_place") || !data.contains("to_place")) return false;
                if (data.value("from_place").toInt() == Player::DrawPile || data.value("to_place").toInt() == Player::DiscardPile) ++moved;
            }
            if (moved >= 10) return true;
            if (!page.value("has_more").toBool()) return false;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *owner = nullptr;
        if (event == Damaged) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.to != damage.from && damage.damage > 0) owner = damage.from;
        } else if (event == ChoiceMade && player) {
            const QVariantMap pending = room->getTag("HuaquanGuess").toMap();
            const QStringList decision = data.toString().split(':');
            if (decision.size() == 3 && decision[0] == "skillChoice" && decision[1] == "huaquan0"
                && pending.value("target").toString() == player->objectName()
                && decision[2] != pending.value("choice").toString())
                owner = room->findPlayerByObjectName(pending.value("owner").toString());
        }
        return owner && owner->isAlive() && owner->hasSkill(objectName()) ? TriggerList{{owner, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event == Damaged || event == ChoiceMade) return false;
        QList<ServerPlayer *> candidates;
        QString action;
        if (event == MarkChanged) {
            const MarkStruct mark = data.value<MarkStruct>();
            if (player && mark.name == "&so_jidao" && mark.gain > 0 && mark.count >= 3) { candidates << player; action = "knockdown"; }
        } else if (event == EventPhaseChanging) {
            if (player && data.value<PhaseChangeStruct>().to == Player::Play) { candidates << player; action = "skip"; }
        } else { candidates = room->getAlivePlayers(); action = "release"; }
        for (ServerPlayer *target : candidates) {
            const QVariantMap receipt = target->getTag(action == "knockdown" ? "SanouSource" : "SanouKnockdown").toMap();
            if (receipt.isEmpty() || (action == "release" && !recovered(room, receipt))) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName() + "->" + target->objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player ? player : target;
            ctx.instanceID = receipt.value("serial").toInt();
            ctx.sourceRef = ddzReceiptSource(receipt);
            if (!ctx.sourceRef.isValid() || ctx.instanceID <= 0) continue;
            ctx.is_forced = true;
            ctx.choice = action;
            ctx.extra_data = receipt;
            ctx.targets << target;
            ctx.preferredTarget = target;
            ctx.preferredTargetSeat = target->getSeat();
            ctx.current_event = event;
            ctx.original_data = &data;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        ServerPlayer *target = ctx.targets.value(0);
        return target && target->getTag(ctx.choice == "knockdown" ? "SanouSource" : "SanouKnockdown") == ctx.extra_data;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Damaged) ctx.targets << ctx.original_data->value<DamageStruct>().to;
        else if (event == ChoiceMade) ctx.targets << ctx.invoker;
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.activationRef.isValid() && target != ctx.preferredTarget) return false;
        if (ctx.choice == "skip") target->skip(Player::Play);
        else if (ctx.choice == "release") {
            target->removeTag("SanouKnockdown");
            room->setPlayerMark(target, "&is_jidao", 0);
            owner->peiyin(this, 4);
        } else if (ctx.choice == "knockdown") {
            QVariantMap receipt = ctx.extra_data.toMap();
            const QVariantMap snapshot = room->queryHistoryMoves({{"limit", 1}});
            if (!snapshot.value("complete").toBool()) return false;
            receipt.insert("after", snapshot.value("watermark"));
            target->setTag("SanouKnockdown", receipt);
            target->loseAllMarks("&so_jidao");
            room->setPlayerMark(target, "&is_jidao", 1);
            room->sendCompulsoryTriggerLog(owner, this, 3);
        } else {
            target->setTag("SanouSource", ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx)));
            room->sendCompulsoryTriggerLog(owner, this, qsanRandomBounded(2) + 1);
            target->gainMark("&so_jidao", getEffectiveAmount(ctx));
        }
        return false;
    }
};

class Jimi : public TriggerSkillV2
{
public:
    Jimi() : TriggerSkillV2("jimi") { events << GameStart << CardsMoveOneTime; frequency = Compulsory; }
    static int discardedCount(Room *room)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (turn <= 0) return -1;
        QVariantMap filter{{"turn_id", turn}};
        int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool()) return -1;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap data = value.toMap().value("data").toMap();
                if (!data.contains("to_place")) return -1;
                if (data.value("to_place").toInt() != Player::DiscardPile) continue;
                const QVariantMap card = data.value("card").toMap();
                if (!card.contains("class_name")) return -1;
                if (card.value("class_name").toString() == "Peach" || card.value("class_name").toString() == "Analeptic") ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == GameStart) return TriggerList{{player, {objectName()}}};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE) return {};
        for (int id : move.card_ids) {
            const Card *card = Sanguosha->getCard(id);
            if (card->isKindOf("Peach") || card->isKindOf("Analeptic")) return TriggerList{{player, {objectName()}}};
        }
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == GameStart) ctx.targets = room->getAlivePlayers();
        else {
            const int count = discardedCount(room);
            if (count < 1) return false;
            ctx.extra_data = count;
            ctx.targets << owner;
        }
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == GameStart) {
            QList<int> candidates;
            for (int id : room->getDrawPile()) {
                const Card *card = Sanguosha->getCard(id);
                if (card->isKindOf("Peach") || card->isKindOf("Analeptic")) candidates << id;
            }
            qsanShuffle(candidates);
            QList<CardsMoveStruct> moves;
            CardMoveReason reason(CardMoveReason::S_REASON_OVERRIDE, owner->objectName(), objectName(), QString());
            for (const Card *card : target->getHandcards()) {
                if (candidates.isEmpty()) break;
                if (card->isKindOf("Peach") || card->isKindOf("Analeptic")) continue;
                moves << CardsMoveStruct(candidates.takeFirst(), target, Player::PlaceHand, reason);
                moves << CardsMoveStruct(card->getEffectiveId(), nullptr, Player::DrawPile, reason);
            }
            if (!moves.isEmpty()) { room->sendCompulsoryTriggerLog(owner, this); room->moveCardsAtomic(moves, true); }
        } else {
            QList<int> obtained;
            for (int id : Sanguosha->getRandomCards(true)) {
                if (obtained.size() >= getEffectiveAmount(ctx)) break;
                if (room->getCardPlace(id) != Player::DrawPile && room->getCardPlace(id) != Player::DiscardPile) continue;
                const Card *card = Sanguosha->getCard(id);
                if (card->isDamageCard() && card->nameLength() == ctx.extra_data.toInt()) obtained << id;
            }
            if (!obtained.isEmpty()) {
                room->sendCompulsoryTriggerLog(owner, this);
                DummyCard cards(obtained);
                room->obtainCard(target, &cards);
            }
        }
        return false;
    }
};

class Maodie : public TriggerSkillV2
{
public:
    Maodie() : TriggerSkillV2("maodie") { events << CardFinished << CardUsed << EventPhaseChanging << TurnBroken << EventSkillInvoking; frequency = Compulsory; global = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *eventPlayer, QVariant &data) const override
    {
        if (event == TurnBroken || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)) {
            ddzExpireReceipts(room, eventPlayer, event, data, "MaodiePending");
            for (ServerPlayer *player : room->getAllPlayers(true)) {
                int minimum = 0;
                for (const QVariant &value : player->getTag("MaodiePending").toList()) minimum = qMax(minimum, value.toMap().value("minimum").toInt());
                room->setPlayerMark(player, "&maodie-Clear", minimum);
            }
        } else if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && use.card && use.card->isDamageCard()) {
                use.from->removeTag("MaodiePending");
                room->setPlayerMark(use.from, "&maodie-Clear", 0);
            }
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = event == CardFinished ? data.value<CardUseStruct>().card : nullptr;
        return card && card->getTypeId() != Card::TypeSkill && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    static QList<int> initialCards(ServerPlayer *owner, ServerPlayer *target)
    {
        QList<int> result;
        for (int id : ListV2I(target->property("InitialHandCards").toList()))
            if (target->handCards().contains(id) && !Sanguosha->getCard(id)->hasFlag("using") && owner->canGet(target, id)) result << id;
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const QVariantMap damage = room->queryCardUseDamage();
        if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()) return false;
        if (!damage.value("items").toList().isEmpty()) {
            ctx.choice = "restrict";
            ctx.targets << owner;
            return true;
        }
        if (!isUsable(ctx)) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : ctx.original_data->value<CardUseStruct>().to)
            if (target->isAlive() && !initialCards(owner, target).isEmpty()) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(), "maodie0", false, true);
        if (!target) return false;
        ctx.choice = "obtain";
        ctx.targets << target;
        return true;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.bypass_cost && accepted.choice == "obtain" && accepted.activationRef == ctx.activationRef
            && parseSkillName(accepted.skill_name) == objectName()) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice != "obtain") return true;
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "receive") {
            QList<int> ids = ListV2I(ctx.extra_data.toMap().value("ids").toList());
            const QString giver = ctx.extra_data.toMap().value("giver").toString();
            qsanRemoveIf(ids, [&](int id) { return room->getCardPlace(id) != Player::PlaceHand
                || !room->getCardOwner(id) || room->getCardOwner(id)->objectName() != giver || !target->canGet(room->getCardOwner(id), id) || Sanguosha->getCard(id)->hasFlag("using"); });
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); }
        } else if (ctx.choice == "restrict") {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            QVariantList receipts = target->getTag("MaodiePending").toList();
            const int minimum = card->nameLength() + getEffectiveAmount(ctx);
            QVariantMap receipt = ddzAppliedReceipt(room, ctx, getEffectiveAmount(ctx));
            receipt.insert("minimum", minimum);
            receipts << receipt;
            target->setTag("MaodiePending", receipts);
            room->setPlayerMark(target, "&maodie-Clear", qMax(target->getMark("&maodie-Clear"), minimum));
        } else {
            QList<int> ids = initialCards(owner, target);
            qsanShuffle(ids);
            ids = ids.mid(0, qMax(0, getEffectiveAmount(ctx)));
            if (ids.isEmpty() || !ctx.invoker || !ctx.invoker->isAlive()) return false;
            // The selected card holder and the actual recipient are independently intercepted.
            ctx.extra_data = QVariantMap{{"ids", ListI2V(ids)}, {"giver", target->objectName()}};
            ctx.choice = "receive";
            skillEffect(ctx.current_event, room, owner, ctx, ctx.invoker);
            ctx.choice = "obtain";
        }
        return false;
    }
};

class MaodieLimit : public CardLimitSkill
{
public:
    MaodieLimit() : CardLimitSkill("#MaodieLimit") {}
    QString limitList(const Player *) const override { return "use"; }
    QString limitPattern(const Player *target, const Card *card) const override
    {
        // This restriction was already applied; losing Maodie does not erase it before the next damage card.
        return card->isDamageCard() && card->nameLength() < target->getMark("&maodie-Clear") ? card->toString() : QString();
    }
};

DoudizhuPackage::DoudizhuPackage()
    : Package("Doudizhu")
{
    related_skills.insert("bahu", "#bahu-target");

    skills << new Feiyang << new Bahu << new BahuTargetMod << new DdzCombatModifier;

    addMetaObject<FeiyangCard>();


    General *ddz_sunwukong = new General(this, "ddz_sunwukong", "qun", 3);
    ddz_sunwukong->addSkill(new Huoyan);
    ddz_sunwukong->addSkill(new Ruyi);
    ddz_sunwukong->addSkill(new RuyiBf);
    ddz_sunwukong->addSkill(new DdzCibei);

    skills << new JingubangSkill;
    addMetaObject<JingubangCard>();
    Weapon *jgb = new Jingubang(Card::Heart,9);
	jgb->setParent(this);

    General *ddz_longwang = new General(this, "ddz_longwang", "qun", 3);
    ddz_longwang->addSkill(new Longgong);
    ddz_longwang->addSkill(new Sitian);
    addMetaObject<SitianCard>();

    General *ddz_taoshen = new General(this, "ddz_taoshen", "qun", 3);
    ddz_taoshen->addSkill(new Nutao);

    General *ddz_libai = new General(this, "ddz_libai", "qun", 3);
    ddz_libai->addSkill(new Jiuxian);
    ddz_libai->addSkill(new JiuxianMod);
    ddz_libai->addSkill(new Shixian);

    General *ddz_nezha = new General(this, "ddz_nezha", "qun", 3);
    ddz_nezha->addSkill(new Santou);
    ddz_nezha->addSkill(new Faqi);

    General *ddz_shenzhaoyun = new General(this, "ddz_shenzhaoyun", "god", 1);
    ddz_shenzhaoyun->addSkill("gdjuejing");
    ddz_shenzhaoyun->addSkill("gdlonghun");
    ddz_shenzhaoyun->addSkill(new Zhanjian);

    General *ddz_wuyi = new General(this, "ddz_wuyi", "shu", 4);
    ddz_wuyi->addSkill(new DdzBenxi);

    General *ddz_quyuan = new General(this, "ddz_quyuan", "qun", 3);
    ddz_quyuan->addSkill(new Qiusuo);
    ddz_quyuan->addSkill(new Lisao);
    addMetaObject<LisaoCard>();

    General *ddz_wuming = new General(this, "ddz_wuming", "qun", 3);
    ddz_wuming->addSkill(new Chushan);

    General *ddz_sunquan = new General(this, "ddz_sunquan", "wu", 3);
    ddz_sunquan->addSkill(new Huiwan);
    ddz_sunquan->addSkill(new Huanli);
    ddz_sunquan->addSkill(new HuanliInvalidity);

    General *ddz_liuxiecaojie = new General(this, "ddz_liuxiecaojie", "qun", 6);
	ddz_liuxiecaojie->setStartHp(3);
    ddz_liuxiecaojie->addSkill(new Juanlv);
    ddz_liuxiecaojie->addSkill(new Qixin);
    addMetaObject<QixinCard>();

    General *ddz_erxun = new General(this, "ddz_erxun", "wei", 3);
    ddz_erxun->addSkill(new Zhinang);
    ddz_erxun->addSkill(new Gouzhu);

    General *ddz_wuhu = new General(this, "ddz_wuhu", "shu", 4);
    ddz_wuhu->addSkill(new Huyi);

    General *ddz_caocao = new General(this, "ddz_caocao", "qun", 4);
    ddz_caocao->addSkill(new Dingxi);
    ddz_caocao->addSkill(new Nengchen);
    ddz_caocao->addSkill(new Huojie);

    General *ddz_lvbu = new General(this, "ddz_lvbu", "qun", 5);
    ddz_lvbu->addSkill(new Fengzhu);
    ddz_lvbu->addSkill(new Yuyu);
    ddz_lvbu->addSkill(new DdzZhiji);
    ddz_lvbu->addSkill(new Jiejiu);
    ddz_lvbu->addSkill(new JiejiuLimit);

    General *ddz_hanwuhu = new General(this, "ddz_hanwuhu", "wei", 5);
    ddz_hanwuhu->addSkill(new Juejue);
    ddz_hanwuhu->addSkill(new Pimi);

    General *ddz_xiahouen = new General(this, "ddz_xiahouen", "wei", 4);
    ddz_xiahouen->addSkill(new Yinfeng);
    ddz_xiahouen->addSkill(new DdzFulu);

    General *ddz_caochong = new General(this, "ddz_caochong", "wei", 3);
    ddz_caochong->addSkill(new Duanti);
    ddz_caochong->addSkill(new Lianwu);
    ddz_caochong->addSkill(new DdzChengxiang);

    General *ddz_yuanshaoyuanshu = new General(this, "ddz_yuanshaoyuanshu", "qun", 4);
    ddz_yuanshaoyuanshu->addSkill(new Lieti);
    ddz_yuanshaoyuanshu->addSkill(new LietiBf);
    ddz_yuanshaoyuanshu->addSkill(new LietiBf2);
    ddz_yuanshaoyuanshu->addSkill(new Shigong);
    ddz_yuanshaoyuanshu->addSkill(new Luankui);

    General *lt_yuanshao = new General(this, "lt_yuanshao", "qun", 4, true, true, true);
    lt_yuanshao->addSkill("lieti");
    lt_yuanshao->addSkill("shigong");
    lt_yuanshao->addSkill("luankui");

    General *lt_yuanshu = new General(this, "lt_yuanshu", "qun", 4, true, true, true);
    lt_yuanshu->addSkill("lieti");
    lt_yuanshu->addSkill("shigong");
    lt_yuanshu->addSkill("luankui");

    General *ddz_liru = new General(this, "ddz_liru", "qun", 4);
    ddz_liru->addSkill(new Huaquan);
    ddz_liru->addSkill(new Sanou);

    General *ddz_yuanshu = new General(this, "ddz_yuanshu", "qun", 4);
    ddz_yuanshu->addSkill(new Jimi);
    ddz_yuanshu->addSkill(new Maodie);
    ddz_yuanshu->addSkill(new MaodieLimit);








}
ADD_PACKAGE(Doudizhu)
