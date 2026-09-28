#include "maotu.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
//#include "standard-generals.h"
#include "engine.h"
#include "maneuvering.h"
//#include "json.h"
#include "settings.h"
#include "clientplayer.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
//#include "clientstruct.h"
#include "mobile.h"
#include "wind.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
#include <QScopedPointer>
#include <algorithm>

namespace {

// Offer only live instances whose own quota remains available.
QStringList usableTriggerInstances(const TriggerSkillV2 *skill, ServerPlayer *owner)
{
    QStringList result;
    for (int id : owner->getValidSkillInstanceIds(skill->objectName())) {
        SkillContext candidate;
        candidate.owner = candidate.invoker = owner;
        candidate.skill_name = skill->objectName(); candidate.instanceID = id;
        candidate.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(skill->objectName(), id));
        if (skill->isUsable(candidate)) result << SkillInstanceUtils::formatName(skill->objectName(), id);
    }
    return result;
}
// Publish expiry before acquisition callbacks; a nested expiry must also retire a late returned ID.
void acquireTimedSkill(Room *room, ServerPlayer *target, const QString &skill, const SkillContext &ctx,
                       const QString &tag, const QString &expires = QString())
{
    const QString serialTag = tag + "Serial";
    const int serial = target->getTag(serialTag).toInt() + 1;
    target->setTag(serialTag, serial);
    QVariantMap receipt{{"serial", serial}, {"expires", expires}, {"skill", skill}, {"instance", 0},
        {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
        {"source_instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
        {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
    QVariantList grants = target->getTag(tag).toList(); grants << receipt; target->setTag(tag, grants);
    const int id = room->acquireSkillFromEffect(target, skill, ctx, [&](int committedId) {
        // Store the exact committed grant before the first signal can expire it or throw.
        QVariantList pending = target->getTag(tag).toList();
        for (int i = 0; i < pending.size(); ++i) {
            QVariantMap current = pending.at(i).toMap();
            if (current.value("serial").toInt() != serial) continue;
            current["instance"] = committedId;
            pending[i] = current;
            target->setTag(tag, pending);
            break;
        }
    });
    grants = target->getTag(tag).toList();
    int index = -1;
    for (int i = 0; i < grants.size(); ++i)
        if (grants.at(i).toMap().value("serial").toInt() == serial) { index = i; break; }
    if (id > 0 && index >= 0) {
        receipt["instance"] = id; grants[index] = receipt; target->setTag(tag, grants);
    } else if (id > 0) {
        room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(skill, id), false, true);
    } else if (index >= 0) {
        grants.removeAt(index); target->setTag(tag, grants);
    }
}
// MethodNone prompts carry UNKNOWN but still require the exact selector.
bool isPromptRequest(const ActiveSkillRequest &request, const QString &pattern)
{
    return request.initiator && request.pattern == pattern
        && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
}

bool matchesFilter(const ActiveSkillRequest &request, const Card *card, const QString &pattern)
{
    return request.initiator && card && card->getEffectiveId() >= 0 && !card->hasFlag("using")
        && !request.selectedCardIds.contains(card->getEffectiveId())
        && Sanguosha->matchExpPattern(pattern, request.initiator, card);
}

// Replay the selection in order, exactly as the server rebuilds it.
bool replaySelection(const ViewAsSkillV2 *skill, const ActiveSkillRequest &request)
{
    ActiveSkillRequest prefix = request;
    prefix.selectedCardIds.clear();
    foreach (int id, request.selectedCardIds) {
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (!card || !skill->canSelectCard(prefix, card)) return false;
        prefix.selectedCardIds << id;
    }
    return true;
}

// V2 records history under the activation skill; conversions keep the card's own key.
QString cardHistoryKey(const QString &cardName, const QString &fallback)
{
    Card *card = Sanguosha->cloneCard(cardName);
    if (!card) return fallback;
    const QString key = card->getClassName();
    card->deleteLater();
    return key;
}

}

class MTLiaoshi : public TriggerSkillV2
{
public:
    MTLiaoshi() : TriggerSkillV2("mtliaoshi")
    { events << GameStart << CardsMoveOneTime << HpChanged << EventForDiy; }
    static void DOSTH(ServerPlayer *player, const QString &choice, bool, const QString &reason)
    {
        Room *room = player->getRoom();
        if (choice == "discard") room->askForDiscard(player, reason, 2, 2, false, true);
        else if (choice == "lose") room->loseHp(HpLostStruct(player, 1, reason, player));
        else if (choice == "draw") player->drawCards(2, reason);
        else room->recover(player, RecoverStruct(reason, player));
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == GameStart) return TriggerList{{player, {objectName()}}};
        if (event == EventForDiy && !data.toString().startsWith("mtliaoshi_number:")) return {};
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!((move.to == player && move.to_place == Player::PlaceHand)
                || (move.from == player && move.from_places.contains(Player::PlaceHand)))) return {};
        }
        QStringList names;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            if (event == EventForDiy && data.toString() != "mtliaoshi_number:" + QString::number(id)) continue;
            const int number = player->getSkillInstanceStateValue(objectName(), id, "number", 0).toInt();
            if (number > 0 && ((event != CardsMoveOneTime && number == player->getHp())
                || (event != HpChanged && number == player->getHandcardNum())))
                names << SkillInstanceUtils::formatName(objectName(), id);
        }
        return names.isEmpty() ? TriggerList() : TriggerList{{player, names}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == GameStart) {
            QStringList choices;
            for (int number = 1; number <= 8; ++number)
                if (number != player->getHp() && number != player->getHandcardNum()) choices << QString::number(number);
            ctx.choice = "initialize";
            ctx.extra_data = room->askForChoice(player, "mtliaoshi_num", choices.join("+")).toInt();
            ctx.targets = {player}; return true;
        }
        int discardable = 0;
        for (int id : player->handCards() + player->getEquipsId()) if (player->canDiscard(player, id)) ++discardable;
        QStringList choices;
        if (discardable >= 2) choices << "discard";
        choices << "lose" << "draw";
        if (player->isWounded()) choices << "recover";
        choices << "cancel";
        ctx.choice = room->askForChoice(player, objectName(), choices.join("+"));
        if (ctx.choice == "cancel") return false;
        ctx.extra_data = player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "number", 1);
        ctx.targets = {player}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "initialize") {
            const int number = qBound(1, ctx.extra_data.toInt(), 8);
            player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "number", number);
            room->setPlayerMark(player, "&mtliaoshi_num", number);
            return false;
        }
        player->skillInvoked(this, 0);
        DOSTH(target, ctx.choice, false, objectName());
        if (!player->isAlive()) return false;
        int number = ctx.extra_data.toInt() + ((ctx.choice == "discard" || ctx.choice == "lose") ? 1 : -1);
        if (number > 8) number = 1;
        if (number < 1) number = 8;
        // The public mark is a compatibility display; each instance owns its actual number.
        player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "number", number);
        room->setPlayerMark(player, "&mtliaoshi_num", number);
        QVariant changed = "mtliaoshi_number:" + QString::number(ctx.instanceID);
        room->getThread()->trigger(EventForDiy, room, player, changed);
        QVariant choice = "mtliaoshi_choice_" + ctx.choice;
        room->getThread()->trigger(EventForDiy, room, player, choice);
        return false;
    }
};


class MTTongyi : public TriggerSkillV2
{
public:
    MTTongyi() : TriggerSkillV2("mttongyi") { events << EventForDiy; }
    static QString liaoshiChoice(const QVariant &data)
    { const QString text = data.toString(); return text.startsWith("mtliaoshi_choice_") ? text.split("_").last() : QString(); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && !liaoshiChoice(data).isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QStringList used = player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients").toStringList();
        ctx.choice = liaoshiChoice(*ctx.original_data);
        QList<ServerPlayer *> targets;
        for (ServerPlayer *target : room->getOtherPlayers(player)) {
            if (used.contains(target->objectName()) || (ctx.choice == "recover" && !target->isWounded())
                || (ctx.choice == "discard" && target->getCardCount() < 2)) continue;
            targets << target;
        }
        if (targets.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@mttongyi-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        QStringList used = player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients").toStringList();
        used << target->objectName();
        player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients", used);
        player->peiyin(this);
        MTLiaoshi::DOSTH(target, ctx.choice, false, objectName());
        return false;
    }
};

class MTXianzhengVS : public ViewAsSkillV2
{
public:
    MTXianzhengVS() : ViewAsSkillV2("mtxianzheng", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtxianzheng") && request.activationRef.isValid()
            && request.initiator->getMark(borrowedActivationMarkName(objectName())) == request.activationRef.key.instanceID;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !matchesFilter(request, card, ".")) return false;
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcard(card);
        slash.setSkillName(objectName());
        return slash.isAvailable(request.initiator);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return cardHistoryKey("slash", "Slash");
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        return slash;
    }
};

class MTXianzheng : public TriggerSkillV2
{
public:
    MTXianzheng() : TriggerSkillV2("mtxianzheng")
    {
        events << EventPhaseStart << Damage;
        view_as_skill = new MTXianzhengVS;
    }

    static bool canMove(Room *room, ServerPlayer *to)
    {
        if (!to || to->isDead() || (to->getEquips().isEmpty() && to->getJudgingArea().isEmpty())) return false;
        return room->canMoveField("ej", QList<ServerPlayer *>{to}, room->getOtherPlayers(to));
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Start || (player->isNude() && player->getHandPile().isEmpty()))
                return TriggerList();
        } else {
            const DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash") || !canMove(room, damage.to)) return TriggerList();
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }

    // At the start phase the Slash is the invocation; declining it never invoked Xianzheng.
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
            const int previous = player->getMark(selector);
            auto restore = qScopeGuard([&] { room->setPlayerMark(player, selector, previous); });
            room->setPlayerMark(player, selector, ctx.instanceID);
            room->askForUseCard(player, "@@mtxianzheng", "@mtxianzheng");
            return false; // The ordinary Slash owns its accepted activation.
        }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ctx.targets = {damage.to};
        return canMove(room, damage.to)
            && player->askForSkillInvoke(this, "mtxianzheng:" + damage.to->objectName());
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *to) const override
    {
        if (event != Damage) return false;

        player->peiyin(this);
        room->moveField(player, objectName(), false, "ej", QList<ServerPlayer *>{to}, room->getOtherPlayers(to));
        return false;
    }
};

class MTNianchou : public TriggerSkillV2
{
public:
    MTNianchou() : TriggerSkillV2("mtnianchou")
    {
        events << EventPhaseStart << Death << EventPhaseChanging; global = true;
        shiming_skill = true;
        waked_skills = "tenyearshensu,baobian,#mtnianchou";
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *target : room->getAllPlayers(true))
                room->setPlayerMark(target, "mtnianchouFrom_" + player->objectName(), 0);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging || !player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart || room->getOtherPlayers(player).isEmpty()) return TriggerList();
        } else {
            const DeathStruct death = data.value<DeathStruct>();
            if (!death.who || death.who == player || !death.damage || death.damage->from != player) return TriggerList();
        }
        // Each pending mission instance resolves on its own.
        QStringList names;
        foreach (int id, player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            if (room->getShimingStatus(ref) == 0)
                names << SkillInstanceUtils::formatName(objectName(), id);
        }
        return names.isEmpty() ? TriggerList() : TriggerList{{player, names}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (room->getShimingStatus(ctx.activationRef) > 0) return false;
        if (event != EventPhaseStart) { ctx.targets = {player}; return true; }
        ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@mtnianchou-target", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        const SkillInstanceRef ref = ctx.activationRef;
        if (event == EventPhaseStart) {
            if (ctx.targets.isEmpty()) return false;
            player->peiyin(this);
            room->setPlayerMark(target, "mtnianchouFrom_" + player->objectName(), 1);

            if (player->getHp() != 1) return false;
            if (!room->sendShimingLog(ref, false)) return false;
            room->detachSkillFromPlayer(player, ref.key.toString());
            room->discardSkillInstance(player, player, "mtxianzheng");
            room->acquireSkillFromEffect(player, "baobian", ctx);
        } else {
            if (!room->sendShimingLog(ref)) return false;

            QString choices = "draw";
            if (player->isWounded()) choices = "recover+draw";
            QString choice = room->askForChoice(player, objectName(), choices);

            if (choice == "draw")
                player->drawCards(2, objectName());
            else
                room->recover(player, RecoverStruct("mtnianchou", player));

            room->acquireSkillFromEffect(player, "tenyearshensu", ctx);
        }
        return false;
    }
};

class MTNianchouTargetMod : public TargetModSkillV2
{
public:
    MTNianchouTargetMod() : TargetModSkillV2("#mtnianchou", "^SkillCard")
    {
        frequency = NotFrequent;
        shiming_skill = true;
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.primary && ctx.secondary
            && ctx.secondary->getMark("mtnianchouFrom_" + ctx.primary->objectName()) > 0)
            return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

class MTJieliVS : public ViewAsSkillV2
{
public:
    MTJieliVS() : ViewAsSkillV2("mtjieli")
    {
    }

    // Every hand card of the colour declared in the dialog, as one Duel.
    static Duel *colourDuel(const Player *player, const QString &colour)
    {
        if (!player || (colour != "red" && colour != "black")) return nullptr;
        Duel *duel = new Duel(Card::SuitToBeDecided, -1);
        duel->setSkillName("mtjieli");
        foreach (const Card *c, player->getHandcards()) {
            if ((c->isRed() && colour == "red") || (c->isBlack() && colour == "black"))
                duel->addSubcard(c);
        }
        if (duel->subcardsLength() > 0) return duel;
        delete duel;
        return nullptr;
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override
    { return 1 + (ctx.owner ? ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "extra_limit", 0).toInt() : 0); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || request.initiator->isKongcheng())
            return false;
        return true;
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        Duel *duel = colourDuel(request.initiator, request.userString);
        const bool ok = duel && candidate && duel->targetFilter(selected, candidate, request.initiator)
            && !request.initiator->isProhibited(candidate, duel, selected);
        delete duel;
        return ok;
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        Duel *duel = colourDuel(request.initiator, request.userString);
        const bool ok = duel && duel->targetsFeasible(selected, request.initiator);
        delete duel;
        return ok;
    }

    // The colour's cards are the Duel's material, not a discarded price.
    bool willThrowSelectedCards() const override
    {
        return false;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTJieliCard";
    }

    // The proxy carries the declared colour to the server.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Duel *duel = colourDuel(request.initiator, request.userString);
        const bool usable = duel && duel->isAvailable(request.initiator);
        delete duel;
        return usable ? ViewAsSkillV2::createCard(request) : nullptr;
    }

    // The accepted activation is used as the ordinary Duel over the colour's cards.
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        Duel *duel = colourDuel(request.initiator, request.userString);
        if (!duel) return false;
        duel->deleteLater();
        duel->setActivationSkill(objectName(), request.getActivationInstanceId());
        if (ctx.use_card)
            duel->setSourceSkill(ctx.use_card->getSourceSkillName(), ctx.use_card->getSourceSkillInstanceId());
        ctx.updated_card = duel;
        return true;
    }
};

class MTJieli : public TriggerSkillV2
{
public:
    MTJieli() : TriggerSkillV2("mtjieli")
    { events << CardFinished << EventPhaseChanging; view_as_skill = new MTJieliVS; waked_skills = "#mtjieli"; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::tiansuan("mtjieli", "red,black"); }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (int id : player->getSkillInstanceIds(objectName()))
                player->removeSkillInstanceStateValue(objectName(), id, "extra_limit");
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished || !player || !player->isAlive() || !player->hasSkill(objectName()) || !player->hasTurn()) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Duel")) return {};
        const QVariantMap history = room->queryCardUseDamage();
        if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return {};
        int amount = 0;
        for (const QVariant &value : history.value("items").toList()) {
            const QVariantMap fact = value.toMap().value("data").toMap();
            if (fact.value("to").toString() != player->objectName()) amount += fact.value("amount").toInt();
        }
        return amount >= 2 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        const int bonus = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "extra_limit", 0).toInt();
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "extra_limit", bonus + getEffectiveAmount(ctx));
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        return false;
    }
};

class MTJieliTargetMod : public TargetModSkillV2
{
public:
    MTJieliTargetMod() : TargetModSkillV2("#mtjieli", "Duel")
    {
        frequency = NotFrequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::ExtraTarget && ctx.primary && ctx.primary->hasSkill("mtjieli"))
            return CorrectSkillResult::useAmount(ctx.getCurrentAmount());
        return CorrectSkillResult::noEffect();
    }
};

class MTFuyi : public TriggerSkillV2
{
public:
    MTFuyi() : TriggerSkillV2("mtfuyi")
    {
        events << Death << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "#mtfuyi,#mtfuyi-turn";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }
    static bool killedBy(const DeathStruct &death, const ServerPlayer *player)
    {
        return death.damage && death.damage->from && death.damage->from == player;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            if (isUsable(accepted)) addUsage(accepted);
            else {
                accepted.is_canceled = true;
                *ctx.original_data = QVariant::fromValue(accepted);
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Death) return {};
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        // canWake() consumes its grant, so selection only peeks at it.
        if (killedBy(data.value<DeathStruct>(), player)
            && player->getTag(objectName() + "_SKILLCANWAKE").toStringList().isEmpty())
            return TriggerList();
        return TriggerList{{player, usableTriggerInstances(this, player)}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; ctx.targets = {player}; return isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {

        if (killedBy(ctx.original_data->value<DeathStruct>(), player) && !player->canWake(objectName()))
            return false;
        room->sendCompulsoryTriggerLog(player, this);
        room->doSuperLightbox(player, "mtfuyi");
        room->setPlayerMark(player, "mtfuyi", 1);
        if (room->changeMaxHpForAwakenSkill(player, 1, objectName())) {
            room->recover(player, RecoverStruct(objectName(), player));
            QVariantList receipts = player->getTag("MTFuyiReceipts").toList();
            receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.instanceID},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)},
                {"pending_turn", true}, {"turn", room->historyScopes().value("turn_id")}};
            player->setTag("MTFuyiReceipts", receipts);
            room->addPlayerMark(player, "&mtfuyi_buff", getEffectiveAmount(ctx));
        }
        return false;
    }
};

class MTFuyiDamage : public TriggerSkillV2
{
public:
    MTFuyiDamage() : TriggerSkillV2("#mtfuyi") { events << ConfirmDamage; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.to || !damage.to->isAlive()) return true;
        QList<ServerPlayer *> holders{damage.to};
        if (damage.from && damage.from != damage.to) holders << damage.from;
        for (ServerPlayer *holder : holders) {
            for (const QVariant &value : holder->getTag("MTFuyiReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                SkillContext ctx;
                ctx.skill_name = objectName(); ctx.owner = holder; ctx.invoker = player; ctx.initiator = holder;
                ctx.instanceID = receipt.value("instance").toInt();
                ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                    SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
                ctx.amount = receipt.value("amount").toInt() * (damage.from == damage.to ? 2 : 1);
                ctx.extra_data = receipt; ctx.targets = {damage.to}; ctx.original_data = &data; ctx.current_event = event;
                contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getTag("MTFuyiReceipts").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    { ctx.owner->damageRevises(*ctx.original_data, getEffectiveAmount(ctx)); return false; }
};

class MTFuyiTurn : public TriggerSkillV2
{
public:
    MTFuyiTurn() : TriggerSkillV2("#mtfuyi-turn") { events << EventPhaseStart << EventSkillInvoking << EventSkillEffectFinished; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking && event != EventSkillEffectFinished) return false;
        const SkillContext finished = data.value<SkillContext>();
        if (finished.skill_name != objectName() || !finished.owner) return false;
        QVariantList receipts = finished.owner->getTag("MTFuyiReceipts").toList();
        const int index = receipts.indexOf(finished.extra_data);
        if (index >= 0) {
            QVariantMap receipt = receipts.at(index).toMap(); receipt["pending_turn"] = false;
            receipts[index] = receipt; finished.owner->setTag("MTFuyiReceipts", receipts);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::NotActive) return true;
        for (ServerPlayer *holder : room->getAlivePlayers()) {
            for (const QVariant &value : holder->getTag("MTFuyiReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                if (!receipt.value("pending_turn").toBool()
                    || receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
                SkillContext ctx;
                ctx.skill_name = objectName(); ctx.owner = holder; ctx.invoker = player; ctx.initiator = holder;
                ctx.instanceID = receipt.value("instance").toInt();
                ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                    SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
                ctx.amount = receipt.value("amount", 1).toInt();
                ctx.extra_data = receipt; ctx.targets = {holder}; ctx.original_data = &data; ctx.current_event = event;
                contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getTag("MTFuyiReceipts").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantList receipts = ctx.owner->getTag("MTFuyiReceipts").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return false;
        QVariantMap receipt = receipts.at(index).toMap(); receipt.insert("pending_turn", false); receipts[index] = receipt;
        ctx.owner->setTag("MTFuyiReceipts", receipts);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->scheduleExtraTurn(target, ctx.sourceRef, {}, getEffectiveAmount(ctx));
        return false;
    }
};

class MTZhongyi : public TriggerSkillV2
{
public:
    MTZhongyi() : TriggerSkillV2("mtzhongyi")
    { events << TargetConfirming << EventPhaseStart << EventPhaseChanging; global = true; }

    static QVariant startingHp(Room *room, ServerPlayer *player)
    {
        QVariantMap filter{{"kind", "turn_hp_snapshot"}, {"turn_id", room->historyScopes().value("turn_id")}};
        QVariant hp;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return {};
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap body = value.toMap().value("data").toMap();
                if (body.value("boundary") == "start" && body.value("player") == player->objectName()
                    && body.contains("hp")) hp = body.value("hp");
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        return hp;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        for (ServerPlayer *owner : room->getAllPlayers(true)) {
            QVariantList kept;
            for (const QVariant &value : owner->getTag("MTZhongyiReceipts").toList())
                if (value.toMap().value("turn").toLongLong() != turn) kept << value;
            owner->setTag("MTZhongyiReceipts", kept);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetConfirming || !player || player->isDead()) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.to.length() != 1 || use.to.first() != player
            || !(use.card->isKindOf("Slash") || (use.card->isDamageCard() && !use.card->isKindOf("DelayedTrick")))) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner == player || player->getHandcardNum() < owner->getHandcardNum()) result[owner] << objectName();
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart) return false;
        if (!player || player->getPhase() != Player::Finish || player->isDead() || player->isNude()) return true;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        for (ServerPlayer *owner : room->getAllPlayers()) {
            if (owner == player && player->getEquips().isEmpty()) continue;
            for (const QVariant &value : owner->getTag("MTZhongyiReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                ServerPlayer *recipient = room->findPlayerByObjectName(receipt.value("recipient").toString());
                if (receipt.value("turn").toLongLong() != turn || !recipient || recipient->isDead()
                    || recipient->getHp() != receipt.value("start_hp").toInt()) continue;
                SkillContext ctx;
                ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player; ctx.initiator = player;
                ctx.instanceID = receipt.value("dispatch").toInt(); ctx.extra_data = receipt;
                ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                    SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
                ctx.original_data = &data; ctx.current_event = event; ctx.targets = {player};
                contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        // Paid follow-up belongs to its receipt, even if the granting instance disappears.
        return ctx.owner && ctx.owner->getTag("MTZhongyiReceipts").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || ctx.invoker->isDead()) return false;
        ctx.targets = {ctx.invoker};
        return ctx.owner->askForSkillInvoke(this,
            (event == TargetConfirming ? "draw:" : "current:") + ctx.invoker->objectName());
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != TargetConfirming) {
            QVariantList receipts = ctx.owner->getTag("MTZhongyiReceipts").toList();
            receipts.removeAll(ctx.extra_data); ctx.owner->setTag("MTZhongyiReceipts", receipts);
        }
        return TriggerSkillV2::effect(event, room, owner, ctx);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || owner->isDead() || !target || target->isDead()) return false;
        if (ctx.choice == "obtain") {
            const QVariantMap transfer = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(transfer.value("giver").toString());
            const int id = transfer.value("card", -1).toInt();
            if (id >= 0 && giver && room->getCardOwner(id) == giver
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using"))
                room->obtainCard(target, Sanguosha->getCard(id),
                    CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, target->objectName()), false);
            return false;
        }
        if (event == TargetConfirming) {
            const QVariant hp = startingHp(room, target);
            if (hp.isValid()) {
                const int dispatch = owner->getTag("MTZhongyiNextReceipt").toInt() + 1;
                owner->setTag("MTZhongyiNextReceipt", dispatch);
                QVariantList receipts = owner->getTag("MTZhongyiReceipts").toList();
                receipts << QVariantMap{{"dispatch", dispatch}, {"recipient", target->objectName()},
                    {"turn", room->historyScopes().value("turn_id")}, {"start_hp", hp},
                    {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                    {"source_instance", ctx.sourceRef.key.instanceID}};
                owner->setTag("MTZhongyiReceipts", receipts);
            }
            owner->peiyin(this);
            target->drawCards(getEffectiveAmount(ctx), objectName());
        } else {
            QVariantList receipts = owner->getTag("MTZhongyiReceipts").toList();
            receipts.removeAll(ctx.extra_data);
            owner->setTag("MTZhongyiReceipts", receipts);
            if (target->isNude() || (owner == target && target->getEquips().isEmpty())) return false;
            const int id = room->askForCardChosen(owner, target, owner == target ? "e" : "he", objectName());
            if (id >= 0) {
                ctx.choice = "obtain";
                ctx.extra_data = QVariantMap{{"giver", target->objectName()}, {"card", id}};
                skillEffect(event, room, owner, ctx, owner);
            }
        }
        return false;
    }
};

class MTWeiqieVS : public ViewAsSkillV2
{
public:
    MTWeiqieVS() : ViewAsSkillV2("mtweiqie")
    {
        expand_pile = "#mtweiqie";
    }

    TargetMode targetMode() const override { return NoTarget; }
    bool willThrowSelectedCards() const override { return false; }
    static int gcd(int a, int b)
    {
        return b == 0 ? a : gcd(b, a % b);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtweiqie") && request.activationRef.isValid()
            && request.initiator->getMark(borrowedActivationMarkName(objectName())) == request.activationRef.key.instanceID;
    }

    // Every pair of chosen shown cards must have coprime numbers.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || request.selectedCardIds.contains(card->getEffectiveId())
            || !getExpandPileCardIds(request.initiator).contains(card->getEffectiveId())) return false;
        foreach (int id, request.selectedCardIds) {
            if (gcd(card->getNumber(), Sanguosha->getCard(id)->getNumber()) != 1) return false;
        }
        return true;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }

    // Only the selection; Weiqie itself moves the chosen cards.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        DummyCard *dummy = new DummyCard;
        dummy->setSkillName(objectName());
        dummy->addSubcards(request.selectedCardIds);
        return dummy;
    }
};

class MTWeiqie : public TriggerSkillV2
{
public:
    MTWeiqie() : TriggerSkillV2("mtweiqie")
    {
        events << DrawNCards;
        setBaseAmount(4);
        view_as_skill = new MTWeiqieVS;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.targets = {player};
        return player->askForSkillInvoke(this);
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>(); draw.num = 0;
        *ctx.original_data = QVariant::fromValue(draw);
        return TriggerSkillV2::effect(event, room, player, ctx);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {
        player->peiyin(this);

        const Room::AcceptedViewAsEffectScope prompt(room, player, objectName(), ctx);
        QList<int> cards = room->showDrawPile(player, getEffectiveAmount(ctx), objectName());
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previousSelector = player->getMark(selector);
        const QVariant previousAI = player->getTag("mtweiqieForAI");
        player->setTag("mtweiqieForAI", QVariant::fromValue(cards));
        room->setPlayerMark(player, selector, prompt.activationRef().key.instanceID);
        room->notifyMoveToPile(player, cards, objectName(), Player::PlaceTable, true);
        const QList<int> shown = cards;
        auto restore = qScopeGuard([&] {
            room->notifyMoveToPile(player, shown, objectName(), Player::PlaceTable, false);
            room->setPlayerMark(player, selector, previousSelector);
            if (previousAI.isValid()) player->setTag("mtweiqieForAI", previousAI);
            else player->removeTag("mtweiqieForAI");
        });
        const auto cleanTable = [&]() {
            QList<int> ids; for (int id : shown) if (room->getCardPlace(id) == Player::PlaceTable) ids << id;
            if (!ids.isEmpty()) { DummyCard rest(ids); room->throwCard(&rest, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), ""), nullptr); }
        };
        try {
        const Card *selected = prompt.isValid() ? room->askForCard(player, "@@mtweiqie", "@mtweiqie", QVariant(), Card::MethodNone) : nullptr;
        QList<int> obtain;
        if (selected)
            for (int id : selected->getSubcards())
                if (cards.contains(id) && room->getCardPlace(id) == Player::PlaceTable) { obtain << id; cards.removeOne(id); }
        if (!obtain.isEmpty()) { DummyCard chosen(obtain); room->obtainCard(player, &chosen); }
        QList<int> discard;
        for (int id : cards) if (room->getCardPlace(id) == Player::PlaceTable) discard << id;
        if (!discard.isEmpty()) {
            DummyCard rest(discard);
            room->throwCard(&rest, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                player->objectName(), objectName(), ""), nullptr);
        }
        } catch (...) { try { cleanTable(); } catch (...) {} throw; }
        return false;
    }
};

class MTGuanda : public TriggerSkillV2
{
public:
    MTGuanda() : TriggerSkillV2("mtguanda")
    {
        events << CardsMoveOneTime;
    }

    // Whether the move put a Slash into the discard pile, and whether one of them is red.
    static bool movedSlash(const CardsMoveOneTimeStruct &move, bool *red = nullptr)
    {
        if (move.to_place != Player::DiscardPile) return false;
        bool slash = false;
        if (red) *red = false;
        foreach (int id, move.card_ids) {
            const Card *c = Sanguosha->getCard(id);
            if (c->isKindOf("Slash")) {
                slash = true;
                if (c->isRed() && red)
                    *red = true;
            }
        }
        return slash;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && movedSlash(data.value<CardsMoveOneTimeStruct>())
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.targets = {player}; return player->askForSkillInvoke(this);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {
        bool red = false;
        movedSlash(ctx.original_data->value<CardsMoveOneTimeStruct>(), &red);
        player->peiyin(this);

        QList<int> two = room->getNCards(2 * getEffectiveAmount(ctx));
        bool returned = false;
        const auto restoreTop = qScopeGuard([&]() { room->clearAG(player); if (!returned) room->returnToTopDrawPile(two); });
        LogMessage log;
        log.from = player;
        log.type = "$ViewDrawPile";
        log.card_str = ListI2S(two).join("+");
        room->sendLog(log, player);

        log.type = "#ViewDrawPile";
        log.arg = QString::number(two.size());
        room->sendLog(log, room->getOtherPlayers(player, true));

        room->fillAG(two, player);
        int id = room->askForAG(player, two, true, objectName(), red ? "@mtguanda-get" : "@mtguanda-see");
        room->clearAG(player);

        room->returnToTopDrawPile(two); returned = true;
        if (red && two.contains(id) && room->getCardPlace(id) == Player::DrawPile)
            room->obtainCard(player, id, false);
        return false;
    }
};

class MTZhilie : public ViewAsSkillV2
{
public:
    MTZhilie() : ViewAsSkillV2("mtzhilie")
    {
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return selected.isEmpty() && candidate && !candidate->isKongcheng() && candidate != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTZhilieCard";
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *to) const override
    {
        ServerPlayer *from = ctx.invoker;
        if (!from || from->isDead() || to->isKongcheng()) return ContinueEffects;

        Room *room = from->getRoom();

        int id = room->askForCardChosen(from, to, "h", "mtzhilie");
        room->showCard(to, id);
        const Card *show = Sanguosha->getCard(id);

        QStringList choices;
        int same = 0, dis = 0;

        foreach (const Card *c, from->getEquips()) {
            if (c->sameColorWith(show)) {
                same++;
                if (from->canDiscard(from, c->getEffectiveId()))
                    dis++;
            }
        }
        if (same > 0 && dis > 0) {
            same = qMin(dis, 2);
            choices << QString("damage=%1=%2=%3").arg(show->getColorString()).arg(to->objectName()).arg(same);
        }

        Card *usecard = Sanguosha->cloneCard(show->objectName(), Card::NoSuit, 0);
        if (usecard) {
            usecard->setSkillName("_mtzhilie");
            usecard->deleteLater();
            if ((usecard->isKindOf("BasicCard") || usecard->isNDTrick()) && from->canUse(usecard, to, true))
                choices << "use=" + to->objectName() + "=" + show->objectName();
        }

        choices << "cancel";

        CardUseStruct use;
        use.from = to;
        use.card = usecard;
        QVariant data = QVariant::fromValue(use);  //For AI

        QString choice = room->askForChoice(from, "mtzhilie", choices.join("+"), data);

        if (choice == "cancel")
            return ContinueEffects;
        else if (choice.startsWith("damage")) {
            same = 0;
            DummyCard *discard = new DummyCard;

            foreach (const Card *c, from->getEquips()) {
                if (c->sameColorWith(show)) {
                    same++;
                    if (from->canDiscard(from, c->getEffectiveId()))
                        discard->addSubcard(c);
                }
            }

            same = qMin(discard->subcardsLength(), 2);
            if (discard->subcardsLength() > 0) {
                room->throwCard(discard, from);
                room->damage(DamageStruct("mtzhilie", from, to, same));
            }
            discard->deleteLater();
        } else {
            if (usecard && from->canUse(usecard, to, true))
            {
                CardUseStruct use(usecard, from, to); use.setOwnedCard(usecard);
                room->useCardFromSkillEffect(use, ctx, false);
            }
        }
        return ContinueEffects;
    }
};

class MTChuanjiu : public TriggerSkillV2
{
public:
    MTChuanjiu() : TriggerSkillV2("mtchuanjiu")
    {
        events << CardUsed << EventPhaseStart;
        frequency = Compulsory;
        waked_skills = "#mtchuanjiu";
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!player->hasFlag("CurrentPlayer") || !use.card || !use.card->isKindOf("TrickCard")) return TriggerList();
            Analeptic ana(Card::NoSuit, 0);
            ana.setSkillName("_mtchuanjiu");
            if (!Analeptic::IsAvailable(player, &ana)) return TriggerList();
        } else if (player->getPhase() != Player::Finish || !player->isWounded()
                   || room->countHistoryCards(player, "turn", "Analeptic") <= 1) {
            return TriggerList();
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { ctx.targets = {player}; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {
        if (event == CardUsed) {
            Analeptic *ana = new Analeptic(Card::NoSuit, 0);
            ana->setSkillName("_mtchuanjiu");
            ana->deleteLater();

            if (!Analeptic::IsAvailable(player, ana)) return false;
            room->sendCompulsoryTriggerLog(player, this);
            CardUseStruct use(ana, player); use.setOwnedCard(ana);
            room->useCardFromSkillEffect(use, ctx, !player->isWounded());
        } else {
            room->sendCompulsoryTriggerLog(player, objectName());
            room->loseHp(HpLostStruct(player, getEffectiveAmount(ctx), objectName(), player));
        }
        return false;
    }
};


class MTDianpei : public TriggerSkillV2
{
public:
    MTDianpei() : TriggerSkillV2("mtdianpei")
    {
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::RoundStart && player->getHandcardNum() <= player->getHp()
            && !room->getOtherPlayers(player).isEmpty()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@mtdianpei-invoke", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *t) const override
    {
        if (ctx.choice == "draw") { t->drawCards(qMax(t->getHp() - t->getHandcardNum(), 0), objectName()); return false; }
        if (ctx.choice == "give") {
            const QVariantMap gift = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(gift.value("giver").toString());
            if (!giver || giver->isDead()) return false;
            QList<int> ids;
            foreach (const QVariant &value, gift.value("ids").toList()) {
                const int id = value.toInt();
                if (room->getCardOwner(id) != giver || Sanguosha->getCard(id)->hasFlag("using")
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
                ids << id;
            }
            if (!ids.isEmpty()) { DummyCard cards(ids); room->giveCard(giver, t, &cards, objectName()); }
            return false;
        }
        player->peiyin(this);

        QList<int> placed;
        for (int id : player->handCards()) if (!Sanguosha->getCard(id)->hasFlag("using")) placed << id;
        const auto cleanup = [&]() {
            QList<int> stranded;
            for (int id : placed) if (t->getPile(objectName()).contains(id)) stranded << id;
            if (!stranded.isEmpty()) { DummyCard cards(stranded); room->throwCard(&cards,
                CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, t->objectName(), objectName(), ""), nullptr); }
        };
        try {
        if (!placed.isEmpty()) t->addToPile(objectName(), placed);

        if (player->isAlive() && t->isAlive() && player->getHandcardNum() < player->getHp()) {

        int num = qMax(player->getLostHp(), 1), draw = qMax(player->getHp() - player->getHandcardNum(), 0);
        QString prompt = QString("@mtdianpei-give:%1:%2:%3").arg(player->objectName()).arg(num).arg(draw);
        const Card *ex = t->isNude() ? nullptr : room->askForExchange(t, objectName(), num, num, true, prompt, true);
        if (!ex) ctx.choice = "draw";
        else {
            QVariantList ids; for (int id : ex->getSubcards()) ids << id;
            ctx.extra_data = QVariantMap{{"giver", t->objectName()}, {"ids", ids}}; ctx.choice = "give";
        }
        skillEffect(event, room, player, ctx, player);

        }
        QList<int> pile;
        for (int id : placed) if (t->getPile(objectName()).contains(id)) pile << id;
        if (t->isAlive() && !pile.isEmpty()) {
            LogMessage log;
            log.type = "$KuangbiGet";
            log.from = t;
            log.arg = objectName();
            log.card_str = ListI2S(pile).join("+");
            room->sendLog(log, t);

            log.type = "#MTDianpeiGet";
            log.from = t;
            log.arg2 = QString::number(pile.length());
            room->sendLog(log, room->getOtherPlayers(t, true));

            DummyCard get(pile);
            room->obtainCard(t, &get, false);
        }
        } catch (...) { try { cleanup(); } catch (...) {} throw; }
        cleanup();
        return false;
    }
};

class MTRenyiVS : public ViewAsSkillV2
{
public:
    MTRenyiVS() : ViewAsSkillV2("mtrenyi")
    {
    }

    // @@mtrenyi1 distributes the drawn cards; @@mtrenyi2 uses the chosen basic card.
    static bool isDistribution(const ActiveSkillRequest &request)
    {
        return request.pattern == "@@mtrenyi1";
    }

    static Card *basicCard(const ActiveSkillRequest &request)
    {
        const int id = request.initiator ? request.initiator->getSkillInstanceStateValue("mtrenyi", request.activationRef.key.instanceID, "basic_id", -1).toInt() : -1;
        if (id < 0) return nullptr;
        Card *card = Sanguosha->cloneCard(Sanguosha->getEngineCard(id)->objectName());
        if (card) card->setSkillName("_mtrenyi");
        return card;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return (isPromptRequest(request, "@@mtrenyi1") || isPromptRequest(request, "@@mtrenyi2"))
            && request.activationRef.isValid()
            && request.initiator->getMark(borrowedActivationMarkName(objectName())) == request.activationRef.key.instanceID;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return isDistribution(request) && matchesFilter(request, card, ".")
            && request.selectedCardIds.length() < request.initiator->getSkillInstanceStateValue("mtrenyi", request.activationRef.key.instanceID, "distribution_count", 0).toInt();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!isDistribution(request)) return request.selectedCardIds.isEmpty();
        return !request.selectedCardIds.isEmpty()
            && request.selectedCardIds.length() == request.initiator->getSkillInstanceStateValue("mtrenyi", request.activationRef.key.instanceID, "distribution_count", 0).toInt()
            && replaySelection(this, request);
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return candidate && candidate != request.initiator && selected.length() < request.selectedCardIds.length();
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.length() == request.selectedCardIds.length();
    }

    TargetEffectMode targetEffectMode() const override
    {
        return WholeTargetGroup;
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (isDistribution(request)) return "MTRenyiCard";
        const int id = request.initiator ? request.initiator->getSkillInstanceStateValue("mtrenyi", request.activationRef.key.instanceID, "basic_id", -1).toInt() : -1;
        return id < 0 ? objectName() : cardHistoryKey(Sanguosha->getEngineCard(id)->objectName(), objectName());
    }

    bool willThrowSelectedCards() const override
    {
        return false;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        if (isDistribution(request)) return ViewAsSkillV2::createCard(request);
        return basicCard(request);
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !ctx.use_card) return FinishSkill;
        Room *room = source->getRoom();
        const Room::AcceptedViewAsEffectScope followup(room, source, objectName(), ctx);

        ctx.extra_data = QVariant::fromValue(ctx.use_card->getSubcards());
        for (ServerPlayer *target : targets) skillEffect(ctx, target);
        if (source->isDead() || source->getPhase() == Player::NotActive || !followup.isValid()) return FinishSkill;

        QList<int> list = room->getAvailableCardList(source, "basic", "mtrenyi");
        if (list.isEmpty()) return FinishSkill;

        room->fillAG(list, source);
        int id = room->askForAG(source, list, true, "mtrenyi", "@mtrenyi-use");
        room->clearAG(source);
        if (id < 0) return FinishSkill;

        const QString name = Sanguosha->getEngineCard(id)->objectName();
        source->setSkillInstanceStateValue(objectName(), followup.activationRef().key.instanceID, "basic_id", id);
        room->askForUseCard(source, "@@mtrenyi2", "@mtrenyi2:" + name, 2, Card::MethodUse, false);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *giver = ctx.initiator;
        if (!giver || !target || target->isDead()) return ContinueEffects;
        Room *room = giver->getRoom();
        QList<int> ids = ctx.extra_data.value<QList<int>>();
        for (int i = ids.size() - 1; i >= 0; --i)
            if (room->getCardOwner(ids[i]) != giver || room->getCardPlace(ids[i]) != Player::PlaceHand
                || Sanguosha->getCard(ids[i])->hasFlag("using")) ids.removeAt(i);
        if (ids.isEmpty()) return ContinueEffects;
        room->fillAG(ids, target);
        const auto clear = qScopeGuard([&]() { room->clearAG(target); });
        const int id = room->askForAG(target, ids, false, objectName());
        if (!ids.contains(id) || room->getCardOwner(id) != giver || room->getCardPlace(id) != Player::PlaceHand
            || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        ids.removeOne(id); ctx.extra_data = QVariant::fromValue(ids);
        room->giveCard(giver, target, Sanguosha->getCard(id), objectName());
        return ContinueEffects;
    }
};

class MTRenyi : public TriggerSkillV2
{
public:
    MTRenyi() : TriggerSkillV2("mtrenyi")
    {
        events << CardsMoveOneTime;
        view_as_skill = new MTRenyiVS;
        waked_skills = "#mtrenyi";
    }

    static int drawnCount(ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        if (move.to != player || move.to_place != Player::PlaceHand || !move.from_places.contains(Player::DrawPile)) return 0;
        int show = 0;
        for (int i = 0; i < move.card_ids.length(); i++) {
            int id = move.card_ids.at(i);
            if (!player->hasCard(id) || move.from_places.at(i) != Player::DrawPile) continue;
            show++;
        }
        return show;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && !room->getTag("FirstRound").toBool()
            && drawnCount(player, data.value<CardsMoveOneTimeStruct>()) > 0
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        const int count = drawnCount(player, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (count <= 0) return false;
        ctx.extra_data = count; ctx.targets = {player}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        const Room::AcceptedViewAsEffectScope prompt(room, player, objectName(), ctx);
        if (!prompt.isValid()) return false;
        const int count = ctx.extra_data.toInt();
        player->setSkillInstanceStateValue(objectName(), prompt.activationRef().key.instanceID, "distribution_count", count);
        room->askForUseCard(player, "@@mtrenyi1", "@mtrenyi1:" + QString::number(count), 1, Card::MethodNone);
        return false;
    }
};
class MTRenyiTargetMod : public TargetModSkillV2
{
public:
    MTRenyiTargetMod() : TargetModSkillV2("#mtrenyi", "BasicCard")
    {
        frequency = NotFrequent; setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::Residue && ctx.card && ctx.card->getSkillName() == "mtrenyi")
            return CorrectSkillResult::unlimitedResidue();
        return CorrectSkillResult::noEffect();
    }
};

class MTFeirenVS : public ViewAsSkillV2
{
public:
    MTFeirenVS() : ViewAsSkillV2("mtfeiren", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(request.initiator);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && (request.pattern.contains("slash") || request.pattern.contains("Slash"));
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !matchesFilter(request, card, ".")
            || card->getTypeId() != Card::TypeEquip) return false;
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcard(card->getEffectiveId());
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return slash.isAvailable(request.initiator);
        return !request.initiator->isLocked(&slash);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return cardHistoryKey("slash", "Slash");
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard);
        slash->setSkillName(objectName());
        return slash;
    }
};

class MTFeiren : public TriggerSkillV2
{
public:
    MTFeiren() : TriggerSkillV2("mtfeiren") { events << CardFinished << EventSkillInvoking; view_as_skill = new MTFeirenVS; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            if (isUsable(accepted)) addUsage(accepted);
            else {
                accepted.is_canceled = true;
                *ctx.original_data = QVariant::fromValue(accepted);
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished) return {};
        if (event != CardFinished || !player || !player->isAlive() || !player->hasSkill(objectName()) || player->isKongcheng()) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !(use.card->isKindOf("Slash") || (use.card->isDamageCard() && use.card->isNDTrick()))) return {};
        const QVariantMap history = room->queryCardUseDamage();
        if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return {};
        for (const QVariant &value : history.value("items").toList())
            if (value.toMap().value("data").toMap().value("amount").toInt() > 0)
                return TriggerList{{player, usableTriggerInstances(this, player)}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<int> ids = use.card->isVirtualCard() ? use.card->getSubcards() : QList<int>{use.card->getEffectiveId()};
        room->fillAG(ids, player);
        auto clear = qScopeGuard([&] { room->clearAG(player); });
        const Card *card = room->askForCard(player, ".|.|.|hand", "@mtfeiren", *ctx.original_data, Card::MethodNone);
        if (!card) return false;
        ctx.extra_data = card->getEffectiveId(); ctx.targets = {player};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand
            || Sanguosha->getCard(id)->hasFlag("using") || player->isCardLimited(Sanguosha->getCard(id), Card::MethodMove)) return false;
        // This is a placed card, not a discard cost; reserve this instance before move callbacks.
        addUsage(ctx);
        room->throwCard(Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
            player->objectName(), objectName(), ""), nullptr);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        player->peiyin(this);
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        const QList<int> materials = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        QList<int> ids;
        for (int id : materials)
            if (room->getCardPlace(id) == Player::DiscardPile || room->getCardPlace(id) == Player::PlaceTable) ids << id;
        if (!ids.isEmpty()) { DummyCard returned(ids); room->obtainCard(target, &returned); }
        return false;
    }
};

class MTFeirenTargetMod : public TargetModSkillV2
{
public:
    MTFeirenTargetMod() : TargetModSkillV2("#mtfeiren")
    {
        frequency = NotFrequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->getSkillName() == "mtfeiren")
            return CorrectSkillResult::useAmount(999);
        return CorrectSkillResult::noEffect();
    }
};

class MTFuzhan : public TriggerSkillV2
{
public:
    MTFuzhan() : TriggerSkillV2("mtfuzhan")
    {
        events << EventPhaseStart;
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *p)
    {
        QSet<QString> opponents;
        QVariantMap filter{{"turn_id", room->historyScopes().value("turn_id")}};
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return {};
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap().value("data").toMap();
                if (fact.value("amount").toInt() <= 0) continue;
                if (fact.value("from").toString() == p->objectName()) opponents.insert(fact.value("to").toString());
                if (fact.value("to").toString() == p->objectName()) opponents.insert(fact.value("from").toString());
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        QList<ServerPlayer *> targets;
        for (ServerPlayer *target : room->getOtherPlayers(p))
            if (opponents.contains(target->objectName()) && p->canPindian(target)) targets << target;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Finish) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()) && !candidates(room, p).isEmpty())
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(p, candidates(room, p), objectName(), "@mtfuzhan-pindian", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &, ServerPlayer *t) const override
    {
        p->peiyin(this);

        PindianStruct *pindian = p->PinDian(t, objectName());
        if (pindian->from_number == pindian->to_number) return false;

        ServerPlayer *winner = t, *loser = p;
        if (pindian->success) {
            winner = p;
            loser = t;
        }

        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcard(pindian->from_card);
        if (pindian->from_card->getEffectiveId() != pindian->to_card->getEffectiveId())
            slash->addSubcard(pindian->to_card);
        slash->setSkillName("_mtfuzhan");
        slash->deleteLater();

        Duel *duel = new Duel(Card::SuitToBeDecided, -1);
        duel->addSubcard(pindian->from_card);
        if (pindian->from_card->getEffectiveId() != pindian->to_card->getEffectiveId())
            duel->addSubcard(pindian->to_card);
        duel->setSkillName("_mtfuzhan");
        duel->deleteLater();

        QStringList choices;
        if (winner->canSlash(loser, slash, false))
            choices << "slash=" + loser->objectName();
        if (winner->canUse(duel, loser, true))
            choices << "duel=" + loser->objectName();
        if (choices.isEmpty()) return false;

        QString choice = room->askForChoice(winner, objectName(), choices.join("+"), QVariant::fromValue(pindian));
        Card *selected = choice.startsWith("slash") ? static_cast<Card *>(slash) : static_cast<Card *>(duel);
        for (int id : selected->getSubcards())
            if (room->getCardPlace(id) != Player::DiscardPile && room->getCardPlace(id) != Player::PlaceTable) return false;
        CardUseStruct use(selected, winner, loser); use.setOwnedCard(selected);
        room->useCardFromSkillEffect(use, ctx, true);
        return false;
    }
};


class MTRenyu : public TriggerSkillV2
{
public:
    MTRenyu() : TriggerSkillV2("mtrenyu")
    {
        events << TargetSpecified;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !(use.card->isKindOf("Slash") || use.card->isNDTrick())) return TriggerList();
        foreach (ServerPlayer *p, use.to) {
            if (p != player && p->isAlive())
                return TriggerList{{player, QStringList{objectName()}}};
        }
        return TriggerList();
    }

    // Each target answers separately: the owner discards from allies, other kingdoms choose for themselves.
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : ctx.original_data->value<CardUseStruct>().to)
            if (target != player && target->isAlive()) ctx.targets << target;
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *p) const override
    {
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
            if (player->isDead()) return false;
            if (p->isDead() || !use.to.contains(p) || p == player) return false;

            QString kingdom1 = player->getKingdom(), kingdom2 = p->getKingdom();
            if (kingdom1 == kingdom2) {
                if (!player->canDiscard(p, "he")) return false;
                const QVariant previous = player->getTag("MTRenyuData");
                const auto restore = qScopeGuard([&]() { if (previous.isValid()) player->setTag("MTRenyuData", previous); else player->removeTag("MTRenyuData"); });
                player->setTag("MTRenyuData", data);
                bool invoke = player->askForSkillInvoke(this, p);
                player->removeTag("MTRenyuData");

                if (!invoke) return false;
                player->peiyin(this);

                int id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                if (id < 0 || room->getCardOwner(id) != p || !player->canDiscard(p, id)
                    || Sanguosha->getCard(id)->hasFlag("using")
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
                room->throwCard(id, p, player);

                use = data.value<CardUseStruct>();
                use.nullified_list << p->objectName();
                data = QVariant::fromValue(use);
            } else {
                const QVariant previous = p->getTag("MTRenyuData");
                const auto restore = qScopeGuard([&]() { if (previous.isValid()) p->setTag("MTRenyuData", previous); else p->removeTag("MTRenyuData"); });
                p->setTag("MTRenyuData", data);
                bool invoke = p->askForSkillInvoke("mtrenyu_jin", "mtrenyu_jin");
                p->removeTag("MTRenyuData");
                if (!invoke) return false;

                LogMessage log;
                log.type = "#InvokeOthersSkill";
                log.from = p;
                log.to << player;
                log.arg = objectName();
                room->sendLog(log);
                player->peiyin(this);
                room->notifySkillInvoked(player, objectName());

                p->drawCards(getEffectiveAmount(ctx), objectName());

                log.type = "#ChangeKingdom2";
                log.arg = p->getKingdom();
                log.arg2 = "jin";
                room->sendLog(log);
                room->setPlayerProperty(p, "kingdom", "jin");

                use = data.value<CardUseStruct>();
                use.nullified_list << p->objectName();
                data = QVariant::fromValue(use);
            }

        return false;
    }
};

class MTFengshang : public TriggerSkillV2
{
public:
    MTFengshang() : TriggerSkillV2("mtfengshang")
    {
        events << CardFinished << EventSkillInvoking;
        waked_skills = "#mtfengshang";
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override
    {
        if (!ctx.owner) return 0;
        int count = 0;
        for (const Player *player : ctx.owner->getAliveSiblings(true))
            if (player->getKingdom() == ctx.owner->getKingdom()) ++count;
        return count;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            if (isUsable(accepted)) addUsage(accepted);
            else {
                accepted.is_canceled = true;
                *ctx.original_data = QVariant::fromValue(accepted);
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished) return {};
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard")) return TriggerList();
        return TriggerList{{player, usableTriggerInstances(this, player)}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ServerPlayer *t = room->askForPlayerChosen(player, room->getAllPlayers(), objectName(), "@mtfengshang-invoke", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *t) const override
    {
        player->peiyin(this);

        t->drawCards(getEffectiveAmount(ctx), objectName());

        if (player->isAlive() && t->getHandcardNum() > player->getHandcardNum()) {
            QString phase = QString::number((int)Player::Finish);
            room->addPlayerMark(player, "&mtfengshang_debuff-Self" + phase + "Clear");
        }
        return false;
    }
};

class MTFengshangKeep : public MaxCardsSkillV2
{
public:
    MTFengshangKeep() : MaxCardsSkillV2("#mtfengshang")
    {
        frequency = NotFrequent; setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const int debuff = ctx.primary
            ? ctx.primary->getMark("&mtfengshang_debuff-Self" + QString::number((int)Player::Finish) + "Clear") : 0;
        return debuff > 0 ? CorrectSkillResult::signedAmount(-debuff) : CorrectSkillResult::noEffect();
    }
};

class MTJiawei : public TriggerSkillV2
{
public:
    MTJiawei() : TriggerSkillV2("mtjiawei$")
    {
        events << CardUsed << EventSkillInvoking;
    }

    LimitScope getLimitScope() const override { return Limit_Turn; }
    static bool canBecomeUser(Room *room, ServerPlayer *player, ServerPlayer *simayan, const CardUseStruct &use)
    {
        if (simayan->isDead() || !simayan->hasLordSkill("mtjiawei")) return false;
        foreach (ServerPlayer *p, use.to) {
            if (!use.card->isAvailable(simayan) || simayan->isLocked(use.card) || room->isProhibited(simayan, p, use.card) ||
                    !use.card->targetFilter(QList<const Player *>(), p, simayan))
                return false;
        }
        return true;
    }

    // The liege's card use triggers the lord; the liege decides whether to hand it over.
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            if (isUsable(accepted)) addUsage(accepted);
            else {
                accepted.is_canceled = true;
                *ctx.original_data = QVariant::fromValue(accepted);
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed) return {};
        TriggerList result;
        if (!player || !player->isAlive() || player->getKingdom() != "jin" || player->getPhase() == Player::NotActive)
            return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || (!use.card->isKindOf("Slash") && !use.card->isNDTrick())) return result;
        foreach (ServerPlayer *simayan, room->getOtherPlayers(player)) {
            if (simayan->hasSkill(objectName()) && canBecomeUser(room, player, simayan, use))
                result[simayan] << usableTriggerInstances(this, simayan);
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *simayan, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ServerPlayer *player = ctx.invoker;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!player || !canBecomeUser(room, player, simayan, use)) return false;
        ctx.targets = {player};
        return room->askForPlayerChosen(player, QList<ServerPlayer *>{simayan}, objectName(),
            "@mtjiawei-invoke:" + use.card->objectName(), true) == simayan;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *simayan, SkillContext &ctx, ServerPlayer *) const override
    {
        ServerPlayer *player = ctx.invoker;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();

        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = player;
        log.to << simayan;
        log.arg = simayan->isWeidi() ? "weidi" : objectName();
        room->sendLog(log);
        room->doAnimate(1, player->objectName(), simayan->objectName());
        if (simayan->isWeidi()) {
            simayan->peiyin("weidi");
            room->notifySkillInvoked(simayan, "weidi");
        } else {
            simayan->peiyin(this);
            room->notifySkillInvoked(simayan, objectName());
        }

        log.type = "#BecomeUser";
        log.from = simayan;
        log.card_str = use.card->toString();
        room->sendLog(log);

        use.from = simayan;
        *ctx.original_data = QVariant::fromValue(use);

        if (simayan->isDead() || player->isDead()) return false;
        if (!simayan->askForSkillInvoke("mtjiawei", "mtjiawei:" + player->objectName(), false)) return false;

        log.type = "#ChoosePlayerWithSkill";
        log.from = simayan;
        log.to.clear();
        log.to << player;
        log.arg = "mtfengshang";
        room->sendLog(log);
        room->doAnimate(1, simayan->objectName(), player->objectName());
        simayan->peiyin("mtfengshang");
        room->notifySkillInvoked(simayan, "mtfengshang");

        player->drawCards(1, "mtfengshang");

        if (simayan->isAlive() && player->getHandcardNum() > simayan->getHandcardNum()) {
            QString phase = QString::number((int)Player::Finish);
            room->addPlayerMark(simayan, "&mtfengshang_debuff-Self" + phase + "Clear");
        }
        return false;
    }
};

class MTGuzhaoVS : public ViewAsSkillV2
{
public:
    MTGuzhaoVS() : ViewAsSkillV2("mtguzhao")
    {
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng();
    }

    // Up to three consecutive other players with hand cards.
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *to_select) const override
    {
        if (!to_select || to_select->isKongcheng() || to_select == request.initiator || targets.length() > 2) return false;
        if (targets.isEmpty()) return true;
        return to_select->isAdjacentTo(targets.last()) || to_select->isAdjacentTo(targets.first());
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return !targets.isEmpty();
    }

    TargetEffectMode targetEffectMode() const override
    {
        return WholeTargetGroup;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTGuzhaoCard";
    }

    // One simultaneous pindian: the source's single card against each target's card.
    static int pindian(ServerPlayer *from, ServerPlayer *target, const Card *card1, const Card *card2)
    {
        if (!card1 || !card2 || from->isDead() || target->isDead()) return -2;

        Room *room = from->getRoom();

        PindianStruct selection;
        PindianStruct *pindian_struct = &selection;
        pindian_struct->from = from;
        pindian_struct->to = target;
        pindian_struct->from_card = card1;
        pindian_struct->to_card = card2;
        pindian_struct->from_number = card1->getNumber();
        pindian_struct->to_number = card2->getNumber();
        pindian_struct->reason = "mtguzhao";
        QVariant data = QVariant::fromValue(pindian_struct);

        LogMessage log;
        log.type = "$PindianResult";
        log.from = pindian_struct->from;
        log.card_str = QString::number(pindian_struct->from_card->getEffectiveId());
        room->sendLog(log);

        log.type = "$PindianResult";
        log.from = pindian_struct->to;
        log.card_str = QString::number(pindian_struct->to_card->getEffectiveId());
        room->sendLog(log);

        RoomThread *thread = room->getThread();
        thread->trigger(PindianVerifying, room, from, data);

        pindian_struct = data.value<PindianStruct *>();

        pindian_struct->success = pindian_struct->from_number > pindian_struct->to_number;

        log.type = pindian_struct->success ? "#PindianSuccess" : "#PindianFailure";
        log.from = from;
        log.to.clear();
        log.to << target;
        log.card_str.clear();
        room->sendLog(log);

        JsonArray arg;
        arg << QSanProtocol::S_GAME_EVENT_REVEAL_PINDIAN << pindian_struct->from->objectName() << pindian_struct->from_card->getEffectiveId()
            << target->objectName() << pindian_struct->to_card->getEffectiveId() << pindian_struct->success << "mtguzhao";
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

        data = QVariant::fromValue(pindian_struct);
        thread->trigger(Pindian, room, from, data);

        QVariant decisionData = QVariant::fromValue(QString("pindian:%1:%2:%3:%4:%5")
            .arg("mtguzhao").arg(from->objectName()).arg(pindian_struct->from_card->getEffectiveId())
            .arg(target->objectName()).arg(pindian_struct->to_card->getEffectiveId()));
        thread->trigger(ChoiceMade, room, from, decisionData);

        if (pindian_struct->success) return 1;
        else if (pindian_struct->from_number == pindian_struct->to_number) return 0;
        else if (pindian_struct->from_number < pindian_struct->to_number) return -1;
        return -2;
    }

    EffectFlow effect(SkillContext &ctx) const override
    { return effectOnTargetGroup(ctx, ctx.targets); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        QStringList admitted = ctx.extra_data.toStringList();
        admitted << target->objectName(); ctx.extra_data = admitted;
        return ContinueEffects;
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source) return FinishSkill;
        Room *room = source->getRoom();

        // Admit the common source and every participant before committing their pindian cards.
        ctx.extra_data = QStringList();
        skillEffect(ctx, source);
        if (!ctx.extra_data.toStringList().contains(source->objectName())) return FinishSkill;
        const SkillContext accepted = ctx;
        const QList<ServerPlayer *> selected = targets;
        for (ServerPlayer *target : selected) skillEffect(ctx, target);
        const QStringList admittedNames = ctx.extra_data.toStringList();
        QHash<ServerPlayer *, int> show;
        QList<ServerPlayer *> new_targets;
        foreach (ServerPlayer *target, targets) {
            if (!admittedNames.contains(target->objectName()) || target->isDead() || target->isKongcheng()) continue;
            int id = target->getRandomHandCardId();
            show[target] = id;
            new_targets << target;
            room->showCard(target, id, source, false);
        }

        if (new_targets.isEmpty() || !source->canPindian()) return FinishSkill;

        LogMessage log;
        log.type = "#Pindian";
        log.from = source;
        log.to = new_targets;
        room->sendLog(log);

        const Card *cardss = nullptr;
        QHash<ServerPlayer *, const Card *> hash;
        foreach (ServerPlayer *target, new_targets) {
            if (!source->canPindian(target, false)) continue;

            PindianStruct selection;
            PindianStruct *pindian = &selection;
            pindian->from = source;
            pindian->to = target;
            pindian->from_card = cardss;
            pindian->to_card = nullptr;
            pindian->reason = "mtguzhao";

            RoomThread *thread = room->getThread();
            QVariant data = QVariant::fromValue(pindian);
            thread->trigger(AskforPindianCard, room, source, data);

            pindian = data.value<PindianStruct *>();

            if (!pindian->from_card && !pindian->to_card) {
                QList<const Card *> cards = room->askForPindianRace(source, target, "mtguzhao");
                if (cards.size() < 2) continue;
                pindian->from_card = cards.first();
                pindian->to_card = cards.last();
            } else if (!pindian->to_card) {
                if (pindian->from_card->isVirtualCard())
                    pindian->from_card = Sanguosha->getCard(pindian->from_card->getEffectiveId());
                pindian->to_card = room->askForPindian(target, source, "mtguzhao");
            } else if (!pindian->from_card) {
                if (pindian->to_card->isVirtualCard())
                    pindian->to_card = Sanguosha->getCard(pindian->to_card->getEffectiveId());
                pindian->from_card = room->askForPindian(source, source, "mtguzhao");
            }
            cardss = pindian->from_card;
            hash[target] = pindian->to_card;
        }

        if (!cardss || room->getCardOwner(cardss->getEffectiveId()) != source
            || room->getCardPlace(cardss->getEffectiveId()) != Player::PlaceHand || cardss->hasFlag("using")) return FinishSkill;
        QList<CardsMoveStruct> moves;
        QList<int> materials{cardss->getEffectiveId()};
        QList<ServerPlayer *> admitted;
        moves << CardsMoveStruct(materials, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_PINDIAN, source->objectName(), objectName(), ""));
        for (ServerPlayer *target : new_targets) {
            const Card *card = hash.value(target);
            if (!card || target->isDead() || room->getCardOwner(card->getEffectiveId()) != target
                || room->getCardPlace(card->getEffectiveId()) != Player::PlaceHand || card->hasFlag("using")) continue;
            admitted << target; materials << card->getEffectiveId();
            moves << CardsMoveStruct(QList<int>{card->getEffectiveId()}, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_PINDIAN, target->objectName(), objectName(), ""));
        }
        if (admitted.isEmpty()) return FinishSkill;
        const auto cleanup = [&]() {
            QList<int> remaining;
            for (int id : materials) if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
            if (!remaining.isEmpty()) {
                DummyCard cards(remaining);
                room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_PINDIAN,
                    source->objectName(), objectName(), ""), nullptr);
            }
        };

        FireSlash *fire_slash = new FireSlash(Card::NoSuit, 0);
        CardUseStruct use; use.setOwnedCard(fire_slash);
        use.sourceRef = ctx.sourceRef; use.skillExecutionID = ctx.executionID;
        fire_slash->setSkillName("_mtguzhao");


        QList<ServerPlayer *> slash_targets;
        bool all_win = true;
        try {
            // All participants commit together; the common source card is never recycled between comparisons.
            room->moveCardsAtomic(moves, true);
            for (ServerPlayer *target : admitted) {
                const int result = pindian(source, target, cardss, hash.value(target));
                if (result != 1) all_win = false;
                if (result == -2 || !hash.value(target)) continue;
                if (show.value(target, -1) < 0 || show.value(target) == hash.value(target)->getEffectiveId()
                    || !source->canSlash(target, fire_slash, false)) continue;
                slash_targets << target;
            }
            cleanup();
        } catch (...) {
            try { cleanup(); } catch (...) {}
            throw;
        }
        if (slash_targets.isEmpty()) return FinishSkill;
        if (all_win) fire_slash->setTag("MTGuzhaoReceipt", QVariantMap{{"owner", ctx.activationRef.ownerObjectName},
            {"instance", ctx.activationRef.key.instanceID}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID},
            {"amount", getEffectiveAmount(accepted)}});
        use.from = source; use.to = slash_targets;
        if (source->isAlive() && !source->isLocked(fire_slash)) room->useCardFromSkillEffect(use, accepted, false);
        return FinishSkill;
    }
};

class MTGuzhao : public TriggerSkillV2
{
public:
    MTGuzhao() : TriggerSkillV2("mtguzhao")
    { events << ConfirmDamage; global = true; frequency = Compulsory; view_as_skill = new MTGuzhaoVS; waked_skills = "#mtguzhao"; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.to) return true;
        const QVariantMap receipt = damage.card->getTag("MTGuzhaoReceipt").toMap();
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner || receipt.value("instance").toInt() <= 0) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player; ctx.initiator = damage.from;
        ctx.instanceID = receipt.value("instance").toInt(); ctx.extra_data = receipt;
        ctx.amount = receipt.value("amount").toInt(); ctx.targets = {damage.to};
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.card && damage.card->getTag("MTGuzhaoReceipt") == ctx.extra_data;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx)); return false; }
};
class MTGuzhaoTargetMod : public TargetModSkillV2
{
public:
    MTGuzhaoTargetMod() : TargetModSkillV2("#mtguzhao")
    {
        frequency = NotFrequent; setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::Residue && ctx.card && ctx.card->getSkillName() == "mtguzhao")
            return CorrectSkillResult::unlimitedResidue();
        return CorrectSkillResult::noEffect();
    }
};

class MTGuquVS : public ViewAsSkillV2
{
public:
    MTGuquVS() : ViewAsSkillV2("mtguqu")
    {
    }

    static QStringList suits(const ActiveSkillRequest &request)
    {
        return request.initiator->getSkillInstanceStateValue("mtguqu",
            request.activationRef.key.instanceID, "missing_suits").toStringList();
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtguqu") && request.activationRef.isValid()
            && request.initiator->getMark(borrowedActivationMarkName(objectName())) == request.activationRef.key.instanceID;
    }

    // One card of each missing suit.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!matchesFilter(request, card, ".") || request.initiator->isJilei(card) || !card->hasSuit()) return false;
        if (!suits(request).contains(card->getSuitString())) return false;
        foreach (int id, request.selectedCardIds) {
            if (Sanguosha->getCard(id)->getSuit() == card->getSuit())
                return false;
        }
        return true;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && request.selectedCardIds.length() == suits(request).length()
            && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcards(request.selectedCardIds);
        return card;
    }
};

class MTGuqu : public TriggerSkillV2
{
public:
    MTGuqu() : TriggerSkillV2("mtguqu")
    {
        events << EventPhaseStart;
        view_as_skill = new MTGuquVS;
    }

    static QStringList missingSuits(ServerPlayer *player)
    {
        const QVariantMap history = player->getRoom()->queryCardHistory(player, "turn");
        if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return {};
        QStringList suits{"heart", "diamond", "spade", "club"};
        for (const QVariant &value : history.value("items").toList()) {
            const QVariantMap card = value.toMap().value("data").toMap().value("card").toMap();
            if (!card.contains("suit")) return {};
            suits.removeAll(Card::Suit2String(static_cast<Card::Suit>(card.value("suit").toInt())));
        }
        return suits;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::NotActive) return result;
        const QStringList all_suits = missingSuits(player);
        if (all_suits.isEmpty()) return result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isAlive() && p->hasSkill(objectName()) && p->getCardCount() >= all_suits.length())
                result[p] << objectName();
        }
        return result;
    }

    // Discarding the missing suits is the invocation; declining never invoked Guqu.
    bool cost(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        const QStringList all_suits = missingSuits(ctx.invoker);
        if (all_suits.isEmpty()) return false;

        QStringList records;
        records << QString("@mtguqu-discard1%1").arg(all_suits.length());
        foreach (QString suit, all_suits)
            records << "<img src='image/system/cardsuit/" + suit + ".png' height=17/>";

        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previousSelector = p->getMark(selector);
        const QVariant previous = p->getSkillInstanceStateValue(objectName(), ctx.instanceID, "missing_suits");
        auto restore = qScopeGuard([&] {
            room->setPlayerMark(p, selector, previousSelector);
            if (previous.isValid()) p->setSkillInstanceStateValue(objectName(), ctx.instanceID, "missing_suits", previous);
            else p->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "missing_suits");
        });
        room->setPlayerMark(p, selector, ctx.instanceID);
        p->setSkillInstanceStateValue(objectName(), ctx.instanceID, "missing_suits", all_suits);
        const Card *selected = room->askForCard(p, "@@mtguqu", records.join(""), QVariant(), Card::MethodNone, nullptr, false, objectName());
        if (!selected) return false;
        QVariantList ids;
        for (int id : selected->getSubcards()) ids << id;
        ctx.extra_data = ids; ctx.targets = {p};
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (room->getCardOwner(id) != player || !player->canDiscard(player, id)
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
            ids << id;
        }
        if (ids.isEmpty()) return false;
        room->throwCard(ids, objectName(), player);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx, ServerPlayer *target) const override
    {
        p->peiyin(this);
        room->scheduleExtraTurn(target, ctx.sourceRef, {}, getEffectiveAmount(ctx));
        return false;
    }
};

class MTLunhuanVS : public ViewAsSkillV2
{
public:
    MTLunhuanVS() : ViewAsSkillV2("mtlunhuan")
    {
    }

    // Counts of each suit, in the order heart, diamond, spade, club.
    static QList<int> suitCounts(const QStringList &suits)
    {
        QList<int> counts{0, 0, 0, 0};
        const QStringList order{"heart", "diamond", "spade", "club"};
        foreach (QString suit, suits) {
            const int i = order.indexOf(suit);
            if (i >= 0) counts[i]++;
        }
        return counts;
    }

    static QStringList shownSuits(const ActiveSkillRequest &request)
    {
        return request.initiator->getSkillInstanceStateValue("mtlunhuan",
            request.activationRef.key.instanceID, "shown_suits").toStringList();
    }

    static QStringList selectedSuits(const ActiveSkillRequest &request)
    {
        QStringList suits;
        foreach (int id, request.selectedCardIds)
            suits << Sanguosha->getCard(id)->getSuitString();
        return suits;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtlunhuan") && request.activationRef.isValid()
            && request.initiator->getMark(borrowedActivationMarkName(objectName())) == request.activationRef.key.instanceID;
    }

    // Hand cards matching the shown suits, no more of a suit than was shown.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!matchesFilter(request, card, ".|.|.|hand") || request.initiator->isJilei(card) || !card->hasSuit()) return false;
        const QStringList shown = shownSuits(request);
        if (shown.isEmpty()) return false;
        const QStringList order{"heart", "diamond", "spade", "club"};
        const int i = order.indexOf(card->getSuitString());
        return i >= 0 && suitCounts(selectedSuits(request)).at(i) < suitCounts(shown).at(i);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const QStringList shown = shownSuits(request);
        return !request.selectedCardIds.isEmpty() && !shown.isEmpty()
            && suitCounts(selectedSuits(request)) == suitCounts(shown) && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcards(request.selectedCardIds);
        return card;
    }
};

class MTLunhuan : public TriggerSkillV2
{
public:
    MTLunhuan() : TriggerSkillV2("mtlunhuan")
    {
        events << EventPhaseEnd;
        view_as_skill = new MTLunhuanVS;
    }

    static bool mayDamage(Room *room, ServerPlayer *player, const SkillInstanceRef &activation)
    {
        QVariantMap filter{{"from", player->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap body = value.toMap().value("data").toMap();
                if (body.value("activation_owner") == activation.ownerObjectName
                    && body.value("activation_skill") == activation.key.skillName
                    && body.value("activation_instance_id").toInt() == activation.key.instanceID
                    && body.value("amount").toInt() > 0) return false;
            }
            if (!page.value("has_more").toBool()) return true;
            filter.insert("after", page.value("next_after"));
        }
    }
    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isKongcheng())
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            && !candidates(room, player).isEmpty()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(player, candidates(room, player), objectName(), "@mtlunhuan-invoke", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *t) const override
    {
        player->peiyin(this);

        QList<int> show_ids;
        if (t->getHandcardNum() <= 4)
            show_ids = t->handCards();
        else {
            for (int i = 0; i < 4; ++i) {
                if (t->getHandcardNum() <= i) break;
                int id = room->askForCardChosen(player, t, "h", objectName(), false, Card::MethodNone, show_ids);
                if (id < 0) break;
                show_ids << id;
            }
        }
        if (show_ids.isEmpty()) return false;
        QStringList suits;
        foreach (int id, show_ids) suits << Sanguosha->getCard(id)->getSuitString();
        const Room::AcceptedViewAsEffectScope prompt(room, player, objectName(), ctx);
        room->showCard(t, show_ids);
        if (player->isDead() || player->getCardCount() < show_ids.length() || !mayDamage(room, player, ctx.activationRef)
            || !prompt.isValid()) return false;
        player->setSkillInstanceStateValue(objectName(), prompt.activationRef().key.instanceID, "shown_suits", suits);
        if (!room->askForCard(player, "@@mtlunhuan", QString("@mtlunhuan-discard:%1:%2").arg(t->objectName()).arg(show_ids.length()),
            QVariant::fromValue(t), objectName())) return false;
        player->peiyin(this);
        room->damage(DamageStruct(objectName(), player, t, show_ids.length() * getEffectiveAmount(ctx), DamageStruct::Fire));
        return false;
    }
};

class MTJiyeVS : public ViewAsSkillV2
{
public:
    MTJiyeVS() : ViewAsSkillV2("mtjiye")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtjiye") && request.activationRef.isValid()
            && request.initiator->getMark(borrowedActivationMarkName(objectName())) == request.activationRef.key.instanceID;
    }

    // Only suits missing from the Ye pile, one card of each.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!matchesFilter(request, card, ".") || !card->hasSuit()) return false;
        foreach (int id, request.initiator->getPile("yhjyye")) {
            if (Sanguosha->getCard(id)->getSuit() == card->getSuit())
                return false;
        }
        foreach (int id, request.selectedCardIds) {
            if (Sanguosha->getCard(id)->getSuit() == card->getSuit())
                return false;
        }
        return true;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty() && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }

    // Only the selection; Jiye itself puts the cards on the pile.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        DummyCard *dummy = new DummyCard;
        dummy->setSkillName(objectName());
        dummy->addSubcards(request.selectedCardIds);
        return dummy;
    }
};

class MTJiye : public TriggerSkillV2
{
public:
    MTJiye() : TriggerSkillV2("mtjiye")
    {
        events << RoundStart;
        frequency = Compulsory;
        view_as_skill = new MTJiyeVS;
    }

    static int getQueshaoSuitsNum(const Player *player)
    {
        QList<int> ye = player->getPile("yhjyye");
        QList<Card::Suit> all_suits;
        all_suits << Card::Heart << Card::Diamond << Card::Spade << Card::Club;
        foreach (int id, ye) {
            Card::Suit suit = Sanguosha->getCard(id)->getSuit();
            if (!all_suits.contains(suit)) continue;
            all_suits.removeOne(suit);
        }
        return all_suits.length();
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && getQueshaoSuitsNum(player) > 0
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        int num = getQueshaoSuitsNum(player);
        if (num <= 0) return false;
        room->sendCompulsoryTriggerLog(player, this);
        player->drawCards(num * getEffectiveAmount(ctx), objectName());
        if (player->isNude()) return false;
        const Room::AcceptedViewAsEffectScope prompt(room, player, objectName(), ctx);
        if (!prompt.isValid()) return false;
        const Card *c = room->askForCard(player, "@@mtjiye", "@mtjiye", QVariant(), Card::MethodNone);
        if (c && c->subcardsLength() > 0 && player->isAlive())
            player->addToPile("yhjyye", c);
        return false;
    }
};

class MTZhiheVS : public ViewAsSkillV2
{
public:
    MTZhiheVS() : ViewAsSkillV2("mtzhihe")
    {
        response_or_use = true;
    }

    static int needed(const Player *player)
    {
        return qMax(1, MTJiye::getQueshaoSuitsNum(player));
    }

    // Basic and non-delayed trick names that Zhihe may declare, from the Ye pile.
    static QStringList pileNames(const Player *player)
    {
        QStringList names;
        foreach (int id, player->getPile("yhjyye")) {
            const Card *c = Sanguosha->getCard(id);
            if ((c->isKindOf("BasicCard") || c->isNDTrick()) && !names.contains(c->objectName()))
                names << c->objectName();
        }
        return names;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->getPile("yhjyye").isEmpty()) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return !pileNames(request.initiator).isEmpty();
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
        if (request.pattern == "peach" && request.initiator->getMark("Global_PreventPeach") > 0) return false;
        return !usableNames(request).isEmpty();
    }

    // X hand cards of one suit.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!matchesFilter(request, card, ".|.|.|hand")) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE) {
            if (request.initiator->isCardLimited(card, Card::MethodResponse)) return false;
        } else if (request.initiator->isLocked(card)) {
            return false;
        }
        if (request.selectedCardIds.length() >= needed(request.initiator)) return false;
        return request.selectedCardIds.isEmpty()
            || Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == card->getSuit();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.length() == needed(request.initiator) && replaySelection(this, request);
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !ctx.use_card || !ViewAsSkillV2::pay(room, ctx, request)) return false;
        for (int id : ctx.initiator->getPile("yhjyye")) {
            if (!Sanguosha->getCard(id)->sameNameWith(ctx.use_card, true)) continue;
            ctx.use_card->setTag("MTZhiheReceipt", QVariantMap{{"owner", ctx.activationRef.ownerObjectName},
                {"actor", ctx.initiator->objectName()}, {"instance", ctx.activationRef.key.instanceID}, {"card", id},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_instance", ctx.sourceRef.key.instanceID}});
            return true;
        }
        return false;
    }
protected:
    bool allowDeclaration(const Player *player, const QString &name) const override
    {
        return pileNames(player).contains(name);
    }
};

class MTZhihe : public TriggerSkillV2
{
public:
    MTZhihe() : TriggerSkillV2("mtzhihe")
    { events << CardFinished << CardResponded; global = true; frequency = Compulsory; view_as_skill = new MTZhiheVS; }
    SkillDialogInfo getDialogInfo() const override
    { return SkillDialogInfo::guhuo("mtzhihe", true, true, true); }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 0; }
    static const Card *resolvedCard(TriggerEvent event, const QVariant &data)
    {
        if (event == CardFinished) return data.value<CardUseStruct>().card;
        const CardResponseStruct response = data.value<CardResponseStruct>();
        return response.m_card;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const Card *card = resolvedCard(event, data);
        if (!card) return true;
        const QVariantMap receipt = card->getTag("MTZhiheReceipt").toMap();
        ServerPlayer *actor = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!actor || !owner || !actor->getPile("yhjyye").contains(receipt.value("card", -1).toInt())) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player; ctx.initiator = actor;
        ctx.instanceID = receipt.value("instance").toInt(); ctx.extra_data = receipt; ctx.targets = {actor};
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.original_data = &data; ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.initiator && ctx.initiator->getPile("yhjyye").contains(ctx.extra_data.toMap().value("card", -1).toInt()); }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // A response-use has no CardFinished; retire the receipt at its own response boundary.
        if (const Card *card = resolvedCard(event, *ctx.original_data)) card->removeTag("MTZhiheReceipt");
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.extra_data.toMap().value("card", -1).toInt();
        if (target->getPile("yhjyye").contains(id))
            room->throwCard(Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE,
                target->objectName(), objectName(), ""), nullptr);
        return false;
    }
};
class MTWenqi : public TriggerSkillV2
{
public:
    MTWenqi() : TriggerSkillV2("mtwenqi")
    {
        events << Damaged << EventSkillInvoking;
        frequency = Compulsory;
    }

    LimitScope getLimitScope() const override { return Limit_Turn; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            if (isUsable(accepted)) addUsage(accepted);
            else {
                accepted.is_canceled = true;
                *ctx.original_data = QVariant::fromValue(accepted);
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damaged) return {};
        TriggerList result;
        if (!player || !room->historyScopes().value("turn_id").toLongLong()) return result;
        const DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *from = damage.from, *to = damage.to;
        if (!from || from == to || from->isDead()) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName())
                && (from == p || to == p))
                result[p] << usableTriggerInstances(this, p);
        }
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        if (!from || from->isDead()) return false;
        ctx.targets = {from};
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx, ServerPlayer *from) const override
    {
        if (!from || from->isDead()) return false;
        room->sendCompulsoryTriggerLog(p, this);

        QStringList choices;
        int num = MTJiye::getQueshaoSuitsNum(p);

        if (!p->getPile("yhjyye").isEmpty())
            choices << "get=" + QString::number(num + 1);
        choices << "draw=" + QString::number(num);

        QString choice = room->askForChoice(from, objectName(), choices.join("+"), QVariant::fromValue(p));

        if (choice.startsWith("get")) {
            QList<int> ye = p->getPile("yhjyye");
            if (!ye.isEmpty()) {
                room->fillAG(ye, from);
                int id;
                { const auto clear = qScopeGuard([&]() { room->clearAG(from); });
                  id = room->askForAG(from, ye, false, objectName(), "@mtwenqi-get"); }
                if (!p->getPile("yhjyye").contains(id) || from->isDead()) return false;

                if (from == p) {
                    LogMessage log;
                    log.type = "$KuangbiGet";
                    log.from = from;
                    log.arg = "yhjyye";
                    log.card_str = QString::number(id);
                    room->sendLog(log);
                }
                room->obtainCard(from, id);

                num = MTJiye::getQueshaoSuitsNum(p);
                if (num > 0 && from->isAlive() && !from->isNude())
                    room->askForDiscard(from, objectName(), num, num, false, true);
            }
        } else {
            num = MTJiye::getQueshaoSuitsNum(p);
            from->drawCards(num * getEffectiveAmount(ctx), objectName());
            if (from->isAlive())
                from->turnOver();
        }
        return false;
    }
};


class MTYanyi : public TriggerSkillV2
{
public:
    MTYanyi() : TriggerSkillV2("mtyanyi")
    {
        events << EventPhaseChanging;
        global = true;
        frequency = Compulsory;
    }

    // The phase is replaced after other phase-change handling, as the retired skill did.
    bool usesEventPriority() const override
    {
        return true;
    }

    int getPriority(TriggerEvent) const override
    {
        return -1;
    }

    static bool countsPhase(ServerPlayer *player, const QVariant &data)
    {
        if (!player || !player->isAlive() || !player->hasSkill("mtyanyi") || player->getMaxHp() <= 0
            || player->getPhase() == Player::NotActive) return false;
        const Player::Phase phase = data.value<PhaseChangeStruct>().to;
        return !player->isSkipped(phase) && phase != Player::NotActive && phase != Player::RoundStart;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!countsPhase(player, data)) return {};
        const qint64 current = room->historyParent(room->currentHistoryEventId(), "phase", true).value("id").toLongLong();
        if (!current) return {};
        QVariantMap filter{{"kind", "phase"}, {"turn_id", room->historyScopes().value("turn_id")},
            {"player", player->objectName()}};
        int ordinal = 1;
        for (;;) {
            const QVariantMap page = room->queryHistoryEvents(filter);
            if (!page.value("complete").toBool()) return {};
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap entry = value.toMap();
                if (entry.value("id").toLongLong() == current) continue;
                const int phase = entry.value("data").toMap().value("phase").toInt();
                if (phase == Player::RoundStart || phase == Player::NotActive) continue;
                const QVariantMap phaseData = entry.value("data").toMap();
                if (!phaseData.contains("entered")) return {};
                if (phaseData.value("entered").toBool()) ++ordinal;
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        return ordinal == player->getMaxHp() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {
        PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();

        QStringList phases;
        phases << "roundstart" << "start" << "judge" << "draw" << "play" << "discard" << "finish" << "notactive";

        LogMessage log;
        log.type = "#MTYanyiPhase";
        log.from = player;
        log.arg = objectName();
        log.arg2 = phases.at(int(change.to));
        log.arg3 = "play";
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        player->peiyin(this);

        change.to = Player::Play;
        *ctx.original_data = QVariant::fromValue(change);
        return false;
    }
};

class MTJishiVS : public ViewAsSkillV2
{
public:
    MTJishiVS() : ViewAsSkillV2("mtjishi")
    {
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getKingdom() == "wei";
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *to_select) const override
    {
        Slash slash(Card::NoSuit, 0);
        slash.setSkillName("_mtjishi");
        slash.setFlags("mtjishi_user_" + request.initiator->objectName());
        return slash.targetFilter(targets, to_select, request.initiator)
            && !request.initiator->isProhibited(to_select, &slash, targets);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return !targets.isEmpty();
    }

    TargetEffectMode targetEffectMode() const override
    {
        return WholeTargetGroup;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTJishiCard";
    }

    // The dialog declares whether health or maximum health is lost.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.userString != "hp" && request.userString != "maxhp") return nullptr;
        return ViewAsSkillV2::createCard(request);
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator) return false;
        if (request.userString == "hp")
            room->loseHp(HpLostStruct(ctx.initiator, 1, "mtjishi", ctx.initiator));
        else if (request.userString == "maxhp")
            room->loseMaxHp(ctx.initiator, 1, "mtjishi");
        else
            return false;
        return true;
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || source->isDead()) return FinishSkill;
        Room *room = source->getRoom();

        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_mtjishi");
        room->setCardFlag(slash, "mtjishi_user_" + source->objectName());
        CardUseStruct use;
        use.setOwnedCard(slash);
        use.sourceRef = ctx.sourceRef;
        use.skillExecutionID = ctx.executionID;
        slash->setTag("MTJishiReceipt", QVariantMap{{"actor", source->objectName()},
            {"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}});

        if (source->isLocked(slash)) return FinishSkill;

        QList<ServerPlayer *> tos;
        foreach (ServerPlayer *p, targets) {
            if (source->canSlash(p, slash, false))
                tos << p;
        }
        if (tos.isEmpty()) return FinishSkill;

        room->setCardFlag(slash, "SlashIgnoreArmor");
        use.from = source; use.to = tos;
        room->useCardFromSkillEffect(use, ctx, false);
        return FinishSkill;
    }
};

class MTJishi : public TriggerSkillV2
{
public:
    MTJishi() : TriggerSkillV2("mtjishi")
    {
        events << Damage; global = true; frequency = Compulsory;
        view_as_skill = new MTJishiVS;
        waked_skills = "#mtjishi";
    }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::tiansuan("mtjishi", "hp,maxhp");
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !damage.card || !damage.by_user) return true;
        const QVariantMap receipt = damage.card->getTag("MTJishiReceipt").toMap();
        ServerPlayer *actor = room->findPlayerByObjectName(receipt.value("actor").toString());
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!actor || actor != player || actor->isDead() || !owner || receipt.value("instance").toInt() <= 0) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player; ctx.initiator = actor;
        ctx.instanceID = receipt.value("instance").toInt(); ctx.extra_data = receipt; ctx.targets = {actor};
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.amount = receipt.value("amount", 1).toInt();
        ctx.original_data = &data; ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.card && damage.card->getTag("MTJishiReceipt") == ctx.extra_data;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *user) const override
    {
        room->sendCompulsoryTriggerLog(user, this);
        user->drawCards(getEffectiveAmount(ctx), objectName());
        if (user->isDead()) return false;

        QStringList choices;
        choices << "add";
        if (user->isWounded())
            choices << "recover";
        QString choice = room->askForChoice(user, objectName(), choices.join("+"));
        if (choice == "add")
            room->gainMaxHp(user, getEffectiveAmount(ctx), objectName());
        else
            room->recover(user, RecoverStruct(objectName(), user, getEffectiveAmount(ctx)));
        return false;
    }
};

class MTJishiTargetMod : public TargetModSkillV2
{
public:
    MTJishiTargetMod() : TargetModSkillV2("#mtjishi")
    {
        frequency = NotFrequent;
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if ((ctx.modType == TargetModSkill::Residue || ctx.modType == TargetModSkill::DistanceLimit)
            && ctx.primary && ctx.card && ctx.card->hasFlag("mtjishi_user_" + ctx.primary->objectName())
            && (ctx.card->getSkillName() == "mtjishi" || ctx.card->hasFlag("mtjishi_used_slash")))
            return ctx.modType == TargetModSkill::Residue ? CorrectSkillResult::unlimitedResidue()
                : CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

class MTYitaoVS : public ViewAsSkillV2
{
public:
    MTYitaoVS() : ViewAsSkillV2("mtyitao")
    {
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getKingdom() == "wu";
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *to_select) const override
    {
        Duel duel(Card::NoSuit, 0);
        duel.setSkillName("_mtyitao");
        return duel.targetFilter(targets, to_select, request.initiator)
            && !request.initiator->isProhibited(to_select, &duel, targets);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return !targets.isEmpty();
    }

    TargetEffectMode targetEffectMode() const override
    {
        return WholeTargetGroup;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTYitaoCard";
    }

    // The dialog declares whether health or maximum health is lost.
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.userString != "hp" && request.userString != "maxhp") return nullptr;
        return ViewAsSkillV2::createCard(request);
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator) return false;
        if (request.userString == "hp")
            room->loseHp(HpLostStruct(ctx.initiator, 1, "mtyitao", ctx.initiator));
        else if (request.userString == "maxhp")
            room->loseMaxHp(ctx.initiator, 1, "mtyitao");
        else
            return false;
        return true;
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || source->isDead()) return FinishSkill;
        Room *room = source->getRoom();

        Duel *duel = new Duel(Card::NoSuit, 0);
        duel->setSkillName("_mtyitao");
        CardUseStruct use;
        use.setOwnedCard(duel);
        use.sourceRef = ctx.sourceRef;
        use.skillExecutionID = ctx.executionID;
        duel->setTag("MTYitaoReceipt", QVariantMap{{"actor", source->objectName()},
            {"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}});

        if (source->isLocked(duel)) return FinishSkill;

        QList<ServerPlayer *> tos;
        foreach (ServerPlayer *p, targets) {
            if (source->canUse(duel, p, true))
                tos << p;
        }
        if (tos.isEmpty()) return FinishSkill;


        use.from = source;
        use.to = tos;
        use.no_offset_list << "_ALL_TARGETS";
        room->useCardFromSkillEffect(use, ctx, true);
        return FinishSkill;
    }
};

class MTYitao : public TriggerSkillV2
{
public:
    MTYitao() : TriggerSkillV2("mtyitao")
    {
        events << Damage; global = true; frequency = Compulsory;
        view_as_skill = new MTYitaoVS;
    }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::tiansuan("mtyitao", "hp,maxhp");
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !damage.card || !damage.by_user) return true;
        const QVariantMap receipt = damage.card->getTag("MTYitaoReceipt").toMap();
        ServerPlayer *actor = room->findPlayerByObjectName(receipt.value("actor").toString());
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!actor || actor != player || actor->isDead() || !owner || receipt.value("instance").toInt() <= 0) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player; ctx.initiator = actor;
        ctx.instanceID = receipt.value("instance").toInt(); ctx.extra_data = receipt; ctx.targets = {actor};
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.amount = receipt.value("amount", 1).toInt();
        ctx.original_data = &data; ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.card && damage.card->getTag("MTYitaoReceipt") == ctx.extra_data;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *user) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->sendCompulsoryTriggerLog(user, this);
        user->drawCards(getEffectiveAmount(ctx), objectName());
        if (user->isDead()) return false;

        int hp = damage.to->getHp(), hand = damage.to->getHandcardNum();
        hp = qMax(1, hp);
        hp = qMin(hp, 6);
        hand = qMax(1, hand);
        hand = qMin(hand, 6);

        QStringList choices;
        choices << QString("hp2=%1=%2").arg(damage.to->objectName()).arg(hp);
        choices << QString("hand=%1=%2").arg(damage.to->objectName()).arg(hand);
        QString choice = room->askForChoice(user, objectName(), choices.join("+"));

        int maxhp = user->getMaxHp();
        if (choice.startsWith("hp2")) {
            //room->setPlayerProperty(user, "maxhp", hp);
            hp = damage.to->getHp();
            hp = qMax(1, hp);
            hp = qMin(hp, 6);
            if (maxhp < hp)
                room->gainMaxHp(user, hp - maxhp, objectName());
            else if (maxhp > hp)
                room->loseMaxHp(user, maxhp - hp, objectName());
         } else {
            //room->setPlayerProperty(user, "maxhp", hand);
            hand = damage.to->getHandcardNum();
            hand = qMax(1, hand);
            hand = qMin(hand, 6);
            if (maxhp < hand)
                room->gainMaxHp(user, hand - maxhp, objectName());
            else if (maxhp > hand)
                room->loseMaxHp(user, maxhp - hand, objectName());
        }
        return false;
    }
};

class MTJuyuan : public TriggerSkillV2
{
public:
    MTJuyuan() : TriggerSkillV2("mtjuyuan")
    { events << Dying; waked_skills = "#mtjuyuan-flag,#mtjuyuan-prohibit"; }
    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *who)
    {
        QList<ServerPlayer *> result;
        for (ServerPlayer *target : room->getAllPlayers())
            if (who->inMyAttackRange(target)) result << target;
        return result;
    }
    static qint64 dyingId(Room *room)
    { return room->historyParent(room->currentHistoryEventId(), "dying", true).value("id").toLongLong(); }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *who = data.value<DyingStruct>().who;
        return player && player->isAlive() && player->hasSkill(objectName()) && who && who->isAlive()
            && who != player && !candidates(room, who).isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        const QList<ServerPlayer *> choices = candidates(room, who);
        ctx.targets = room->askForPlayersChosen(player, choices, objectName(), 0, choices.size(),
            "@mtjuyuan-draw:" + who->objectName());
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        const int dispatch = who->getTag("MTJuyuanNextReceipt").toInt() + 1;
        who->setTag("MTJuyuanNextReceipt", dispatch);
        ctx.extra_data = dispatch;
        QVariantList receipts = who->getTag("MTJuyuanReceipts").toList();
        receipts << QVariantMap{{"dispatch", dispatch}, {"dying", dyingId(room)},
            {"owner", player->objectName()}, {"instance", ctx.instanceID}, {"count", 0},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}};
        who->setTag("MTJuyuanReceipts", receipts);
        player->peiyin(this);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        if (!target || target->isDead()) return false;
        QVariantList receipts = who->getTag("MTJuyuanReceipts").toList();
        for (QVariant &value : receipts) {
            QVariantMap receipt = value.toMap();
            if (receipt.value("dispatch").toInt() != ctx.extra_data.toInt()) continue;
            QStringList blocked = receipt.value("blocked").toStringList(); blocked << target->objectName();
            receipt.insert("blocked", blocked); receipt.insert("count", receipt.value("count").toInt() + 1);
            value = receipt;
        }
        // Commit the applied restriction before draw callbacks can lose its source.
        who->setTag("MTJuyuanReceipts", receipts);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MTJuyuanFlag : public TriggerSkillV2
{
public:
    MTJuyuanFlag() : TriggerSkillV2("#mtjuyuan-flag")
    { events << AskForPeaches << QuitDying << EventPhaseStart << EventPhaseChanging; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return false;
        QVariantList restrictions = player->getTag("MTJuyuanRestrictions").toList();
        if (event == EventPhaseStart && player->getPhase() == Player::RoundStart) {
            for (QVariant &value : restrictions) {
                QVariantMap receipt = value.toMap();
                if (!receipt.value("active").toBool()) {
                    receipt.insert("active", true);
                    receipt.insert("turn", room->historyScopes().value("turn_id")); value = receipt;
                }
            }
            player->setTag("MTJuyuanRestrictions", restrictions);
            for (ServerPlayer *target : room->getAllPlayers(true)) {
                bool prohibited = false;
                for (const QVariant &value : restrictions) {
                    const QVariantMap receipt = value.toMap();
                    if (receipt.value("active").toBool() && target != player
                        && receipt.value("owner").toString() != target->objectName()) prohibited = true;
                }
                room->setPlayerMark(target, "mtjuyuanProhibitedBy_" + player->objectName(), prohibited ? 1 : 0);
            }
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            QVariantList kept;
            for (const QVariant &value : restrictions)
                if (!value.toMap().value("active").toBool()) kept << value;
            player->setTag("MTJuyuanRestrictions", kept);
            for (ServerPlayer *target : room->getAllPlayers(true))
                room->setPlayerMark(target, "mtjuyuanProhibitedBy_" + player->objectName(), 0);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != AskForPeaches && event != QuitDying) return true;
        ServerPlayer *who = data.value<DyingStruct>().who;
        if (!who || !player || who->isDead()) return true;
        const qint64 dying = MTJuyuan::dyingId(room);
        for (const QVariant &value : who->getTag("MTJuyuanReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("dying").toLongLong() != dying || receipt.value("count").toInt() <= 0) continue;
            if (event == AskForPeaches && !receipt.value("blocked").toStringList().contains(player->objectName())) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player; ctx.initiator = who;
            ctx.instanceID = receipt.value("dispatch").toInt(); ctx.extra_data = receipt; ctx.targets = {player};
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        return who && who->getTag("MTJuyuanReceipts").toList().contains(ctx.extra_data);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *who = ctx.original_data->value<DyingStruct>().who;
        QVariantList receipts = who->getTag("MTJuyuanReceipts").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return false;
        QVariantMap receipt = ctx.extra_data.toMap();
        if (event == AskForPeaches) {
            QStringList blocked = receipt.value("blocked").toStringList(); blocked.removeAll(target->objectName());
            receipt.insert("blocked", blocked); receipts[index] = receipt;
            who->setTag("MTJuyuanReceipts", receipts);
            return true;
        }
        receipts.removeAt(index); who->setTag("MTJuyuanReceipts", receipts);
        QVariantList restrictions = who->getTag("MTJuyuanRestrictions").toList();
        receipt.insert("active", false); restrictions << receipt;
        who->setTag("MTJuyuanRestrictions", restrictions);
        for (int i = 0; i < receipt.value("count").toInt(); ++i) {
            if (who->isDead() || owner->isDead()) break;
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_mtjuyuan"); slash->setFlags("SlashNoRespond");
            CardUseStruct use; use.setOwnedCard(slash); use.from = who; use.to = {owner};
            use.sourceRef = ctx.sourceRef; use.skillExecutionID = ctx.executionID;
            if (who->isLocked(slash) || !who->canSlash(owner, slash, false)) break;
            SkillContext accepted = ctx;
            accepted.activationRef = SkillInstanceRef(receipt.value("owner").toString(),
                SkillInstanceKey("mtjuyuan", receipt.value("instance").toInt()));
            room->useCardFromSkillEffect(use, accepted, false);
        }
        return false;
    }
};

class MTJuyuanProhibit : public ProhibitSkill
{
public:
    MTJuyuanProhibit() : ProhibitSkill("#mtjuyuan-prohibit") {}
    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        if (!from || !to || from == to || card->isKindOf("SkillCard") || from->getPhase() == Player::NotActive) return false;
        return to->getMark("mtjuyuanProhibitedBy_" + from->objectName()) > 0;
    }
};
class MTFupan : public TriggerSkillV2
{
public:
    MTFupan() : TriggerSkillV2("mtfupan") { events << Damaged; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *owner, QVariant &data) const override
    {
        if (!owner || owner->isDead() || !owner->hasSkill(objectName())) return {};
        ServerPlayer *from = data.value<DamageStruct>().from;
        bool eligible = owner->getKingdom() == "wei" && from && from->isAlive() && !from->isAllNude();
        if (owner->getKingdom() == "shu" && !owner->isKongcheng()) {
            QList<ServerPlayer *> candidates = room->getOtherPlayers(owner); candidates.removeOne(from);
            eligible = !candidates.isEmpty();
        }
        return eligible ? TriggerList{{owner, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.choice = owner->getKingdom() == "wei" ? "take" : "choose_give";
        ctx.targets = {ctx.choice == "take" ? ctx.original_data->value<DamageStruct>().from : owner};
        return !ctx.targets.isEmpty() && ctx.targets.first();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!owner->isAlive() || !target || target->isDead()) return false;
        if (ctx.choice == "take") {
            QVariantList ids;
            for (const QString &area : QStringList{"h", "e", "j"}) {
                if (owner->isDead() || target->isDead()) break;
                if (target->getCards(area).isEmpty()) continue;
                const int id = room->askForCardChosen(owner, target, area, objectName());
                if (id >= 0 && room->getCardOwner(id) == target) ids << id;
            }
            if (ids.isEmpty()) return false;
            ctx.extra_data = QVariantMap{{"giver", target->objectName()}, {"cards", ids}};
            ctx.choice = "obtain";
            skillEffect(event, room, owner, ctx, owner);
            return false;
        }
        if (ctx.choice == "obtain") {
            const QVariantMap transfer = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(transfer.value("giver").toString());
            QList<int> ids;
            for (const QVariant &value : transfer.value("cards").toList()) {
                const int id = value.toInt();
                const Player::Place place = room->getCardPlace(id);
                if (giver && room->getCardOwner(id) == giver && !Sanguosha->getCard(id)->hasFlag("using")
                    && (place == Player::PlaceHand || place == Player::PlaceEquip || place == Player::PlaceDelayedTrick)) ids << id;
            }
            if (ids.isEmpty()) return false;
            DummyCard cards(ids); room->obtainCard(target, &cards, objectName());
            QVariantList held;
            for (int id : ids) if (target->handCards().contains(id)) held << id;
            if (target->isDead() || held.isEmpty()) return false;
            QList<ServerPlayer *> candidates = room->getAlivePlayers(); candidates.removeOne(giver);
            if (candidates.isEmpty()) return false;
            ServerPlayer *recipient = room->askForPlayerChosen(owner, candidates, objectName(),
                "@mtfupan-give:" + (giver ? giver->objectName() : QString()));
            if (!recipient) return false;
            ctx.extra_data = held; ctx.choice = "give_wei";
            skillEffect(event, room, owner, ctx, recipient);
            return false;
        }
        if (ctx.choice == "choose_give") {
            QList<ServerPlayer *> candidates = room->getOtherPlayers(owner);
            ServerPlayer *from = ctx.original_data->value<DamageStruct>().from; candidates.removeOne(from);
            if (owner->isKongcheng() || candidates.isEmpty()) return false;
            CardsMoveStruct move = room->askForYijiStruct(owner, owner->handCards(), objectName(), false, false, false,
                owner->getHandcardNum(), candidates, CardMoveReason(), from ? "@mtfupan-give2:" + from->objectName()
                    : "@mtfupan-give3", false, false);
            ServerPlayer *recipient = qobject_cast<ServerPlayer *>(move.to);
            QList<int> ids = move.card_ids;
            if (!recipient || ids.isEmpty()) {
                recipient = candidates.at(qsanRandomBounded(candidates.size()));
                if (owner->isKongcheng()) return false;
                ids = {owner->getRandomHandCardId()};
            }
            QVariantList selected; for (int id : ids) selected << id;
            ctx.extra_data = selected; ctx.choice = "give_shu";
            skillEffect(event, room, owner, ctx, recipient);
            return false;
        }
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (owner->handCards().contains(id) && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
        }
        if (ids.isEmpty()) return false;
        const bool shu = ctx.choice == "give_shu";
        if (target != owner) room->giveCard(owner, target, ids, objectName());
        if (owner->isAlive() && shu && room->canMoveField("ej")) room->moveField(owner, objectName(), false, "ej");
        if (owner->isAlive() && target != owner) room->changeKingdom(owner, shu ? "wei" : "shu");
        return false;
    }
};
class MTFeiyan : public TriggerSkillV2
{
public:
    MTFeiyan() : TriggerSkillV2("mtfeiyan") { events << TargetSpecified << EventSkillInvoking; waked_skills = "#mtfeiyan"; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    static ServerPlayer *eligible(ServerPlayer *player, const QVariant &data)
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || player->getPhase() != Player::Play || !use.card || use.to.size() != 1
            || !(use.card->isKindOf("Slash") || use.card->isNDTrick())) return nullptr;
        ServerPlayer *target = use.to.first();
        return target != player && target->isAlive() && player->getEquips().size() <= target->getEquips().size()
            ? target : nullptr;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            if (isUsable(accepted)) addUsage(accepted);
            else {
                accepted.is_canceled = true;
                *ctx.original_data = QVariant::fromValue(accepted);
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified) return {};
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        ServerPlayer *target = eligible(player, data);
        return target && (player->getKingdom() == "wei" || (player->getKingdom() == "qun" && !target->isNude()))
            ? TriggerList{{player, usableTriggerInstances(this, player)}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ServerPlayer *target = eligible(player, *ctx.original_data);
        if (!target) return false;
        const int amount = qMax(1, target->getEquips().size() - player->getEquips().size());
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        ctx.choice = player->getKingdom(); ctx.targets = {target};
        ctx.amount = amount * getEffectiveAmount(ctx);
        return player->askForSkillInvoke(this, ctx.choice == "wei"
            ? QString("wei:%1::%2:%3").arg(target->objectName()).arg(card->objectName()).arg(amount)
            : QString("qun:%1::%2").arg(target->objectName()).arg(amount));
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || target->isDead() || owner->isDead()) return false;
        if (ctx.choice == "receive") {
            const QVariantMap transfer = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(transfer.value("giver").toString());
            QList<int> ids;
            for (const QVariant &value : transfer.value("cards").toList()) {
                const int id = value.toInt();
                const Player::Place place = room->getCardPlace(id);
                if (giver && room->getCardOwner(id) == giver && !Sanguosha->getCard(id)->hasFlag("using")
                    && (place == Player::PlaceHand || place == Player::PlaceEquip)) ids << id;
            }
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, objectName()); }
            return false;
        }
        const int amount = getEffectiveAmount(ctx);
        const QString kingdom = ctx.choice;
        const int dispatch = owner->getTag("MTFeiyanNextReceipt").toInt() + 1;
        owner->setTag("MTFeiyanNextReceipt", dispatch);
        QVariantMap receipt{{"dispatch", dispatch}, {"owner", owner->objectName()}, {"actor", owner->objectName()},
            {"target", target->objectName()}, {"instance", ctx.instanceID}, {"amount", amount},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}, {"phase", room->historyScopes().value("phase_id")}};
        if (amount > 2) {
            QVariantList pending = owner->getTag("MTFeiyanPending").toList();
            receipt.insert("next_kingdom", kingdom == "wei" ? "qun" : "wei");
            pending << receipt; owner->setTag("MTFeiyanPending", pending);
        }
        if (kingdom == "wei") {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            QVariantList repeats = card->getTag("MTFeiyanRepeats").toList(); repeats << receipt;
            card->setTag("MTFeiyanRepeats", repeats);
        } else {
            QList<int> chosen;
            for (int i = 0; i < amount && chosen.size() < target->getCardCount(); ++i) {
                const int id = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodNone, chosen);
                if (id < 0 || chosen.contains(id)) break;
                chosen << id;
            }
            QVariantList ids; for (int id : chosen) ids << id;
            ctx.extra_data = QVariantMap{{"giver", target->objectName()}, {"cards", ids}};
            ctx.choice = "receive";
            skillEffect(event, room, owner, ctx, owner);
        }
        return false;
    }
};

class MTFeiyanEffect : public TriggerSkillV2
{
public:
    MTFeiyanEffect() : TriggerSkillV2("#mtfeiyan")
    { events << EventPhaseEnd << CardFinished << DamageInflicted << EventSkillInvoking << EventSkillEffectFinished; global = true; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event == EventSkillInvoking || event == EventSkillEffectFinished || !player || player->isDead()) return true;
        const Card *card = event == CardFinished ? data.value<CardUseStruct>().card : nullptr;
        if (event == EventPhaseEnd && player->getPhase() != Player::Play) return true;
        const QVariantList receipts = card ? card->getTag("MTFeiyanRepeats").toList()
            : player->getTag(event == EventPhaseEnd ? "MTFeiyanPending" : "MTFeiyanDamage").toList();
        for (const QVariant &value : receipts) {
            const QVariantMap receipt = value.toMap();
            if (event == EventPhaseEnd && receipt.value("phase") != room->historyScopes().value("phase_id")) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ServerPlayer *recipient = card ? room->findPlayerByObjectName(receipt.value("target").toString()) : player;
            if (!owner || !recipient || recipient->isDead()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player; ctx.initiator = owner;
            ctx.instanceID = receipt.value("dispatch").toInt(); ctx.extra_data = receipt; ctx.targets = {recipient};
            ctx.amount = card ? receipt.value("amount").toInt() : 1;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.original_data = &data; ctx.current_event = event;
            ctx.choice = card ? "repeat" : event == EventPhaseEnd ? "kingdom" : "damage";
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (ctx.choice == "repeat") {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            return card && card->getTag("MTFeiyanRepeats").toList().contains(ctx.extra_data);
        }
        return ctx.invoker && ctx.invoker->getTag(ctx.choice == "kingdom" ? "MTFeiyanPending" : "MTFeiyanDamage")
            .toList().contains(ctx.extra_data);
    }
    static void retire(Room *room, const SkillContext &ctx)
    {
        // Retire at this trigger, before an effect-target cancellation can defer the receipt.
        if (ctx.choice == "repeat") {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            QVariantList pending = card->getTag("MTFeiyanRepeats").toList(); pending.removeAll(ctx.extra_data);
            card->setTag("MTFeiyanRepeats", pending);
        } else {
            const QString key = ctx.choice == "kingdom" ? "MTFeiyanPending" : "MTFeiyanDamage";
            if (!ctx.invoker) return;
            QVariantList pending = ctx.invoker->getTag(key).toList();
            const bool removed = pending.removeAll(ctx.extra_data) > 0;
            ctx.invoker->setTag(key, pending);
            if (removed && ctx.choice == "damage") room->removePlayerMark(ctx.invoker, "&mtfeiyanDamage");
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking && event != EventSkillEffectFinished) return false;
        const SkillContext finished = data.value<SkillContext>();
        if (finished.skill_name == objectName()) retire(room, finished);
        return false;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { retire(room, ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "kingdom") {
            QVariantList pending = target->getTag("MTFeiyanDamage").toList(); pending << ctx.extra_data;
            target->setTag("MTFeiyanDamage", pending); room->addPlayerMark(target, "&mtfeiyanDamage");
            room->changeKingdom(target, ctx.extra_data.toMap().value("next_kingdom").toString());
        } else if (ctx.choice == "damage") {
            target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        } else {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            // Repeat the accepted card effect, without creating a second card-use/payment transaction.
            for (int i = 0; i < getEffectiveAmount(ctx) && owner->isAlive() && target->isAlive(); ++i)
            {
                QList<ServerPlayer *> targets{target};
                card->use(room, owner, targets);
            }
        }
        return false;
    }
};
class MTJiukuang : public TriggerSkillV2
{
public:
    MTJiukuang() : TriggerSkillV2("mtjiukuang")
    {
        events << CardFinished;
        frequency = Compulsory;
        waked_skills = "#mtjiukuang";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = data.value<CardUseStruct>().card;
        return player && player->isAlive() && player->hasSkill(objectName()) && card && card->isKindOf("Analeptic")
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        room->sendCompulsoryTriggerLog(player, this);

        if (player->getLostHp() > 0)
            room->recover(player, RecoverStruct(objectName(), player, getEffectiveAmount(ctx)));
        if (player->isAlive())
            player->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MTJiukuangTMD : public TargetModSkillV2
{
public:
    MTJiukuangTMD() : TargetModSkillV2("#mtjiukuang", "Analeptic")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::Residue && ctx.primary
            && ctx.primary->getPhase() == Player::Play && ctx.primary->hasSkill("mtjiukuang"))
            return CorrectSkillResult::unlimitedResidue();
        return CorrectSkillResult::noEffect();
    }
};

class MTZongqingVS : public ViewAsSkillV2
{
public:
    MTZongqingVS() : ViewAsSkillV2("mtzongqing") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h");
    }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MTZongqingCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator) return false;
        QList<int> ids;
        QMap<int, QVariantMap> snapshots;
        for (int id : ctx.initiator->handCards()) {
            const Card *card = Sanguosha->getCard(id);
            if (card->hasFlag("using") || !ctx.initiator->canDiscard(ctx.initiator, id)) continue;
            ids << id;
            snapshots[id] = QVariantMap{{"type", card->getTypeId()}, {"suit", int(card->getSuit())}, {"damage", card->isDamageCard()}};
        }
        if (ids.isEmpty()) return false;
        const QVariantMap before = room->queryHistoryMoves(QVariantMap{{"from", ctx.initiator->objectName()}, {"limit", 1}});
        room->throwCard(ids, objectName(), ctx.initiator);
        // Bonuses describe cards actually discarded by this payment, not proposed materials.
        QVariantMap filter{{"from", ctx.initiator->objectName()}, {"after", before.value("watermark")}};
        QSet<int> committed;
        bool known = before.value("complete").toBool() && ctx.executionID > 0;
        do {
            const QVariantMap page = room->queryHistoryMoves(filter);
            known = known && page.value("complete").toBool() && page.value("attribution_complete").toBool();
            foreach (const QVariant &value, page.value("items").toList()) {
                const QVariantMap move = value.toMap().value("data").toMap();
                const int id = move.value("card_id", -1).toInt();
                if (snapshots.contains(id) && move.value("execution_id").toLongLong() == ctx.executionID
                    && move.value("from_place").toInt() == int(Player::PlaceHand)
                    && move.value("to_place").toInt() == int(Player::DiscardPile)
                    && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
                    committed << id;
            }
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after"); filter["watermark"] = page.value("watermark");
        } while (true);
        QSet<int> types, suits;
        bool damage = false;
        if (known) for (int id : committed) {
            types << snapshots[id].value("type").toInt(); suits << snapshots[id].value("suit").toInt();
            damage = damage || snapshots[id].value("damage").toBool();
        }
        ctx.extra_data = QVariantMap{{"known", known}, {"types", types.size()}, {"suits", suits.size()}, {"damage", damage}};
        return true;
    }    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker && ctx.invoker->isAlive()) { ctx.choice = "self"; skillEffect(ctx, ctx.invoker); }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || target->isDead()) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "draw") {
            target->drawCards(2 * getEffectiveAmount(ctx), objectName());
            return ContinueEffects;
        }
        const QVariantMap paid = ctx.extra_data.toMap();
        target->drawCards(getEffectiveAmount(ctx), objectName());
        if (target->isDead()) return ContinueEffects;
        if (paid.value("types").toInt() >= 2) {
            Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
            analeptic->setSkillName("_mtzongqing");
            CardUseStruct use; use.setOwnedCard(analeptic); use.from = target; use.to = {target};
            use.sourceRef = ctx.sourceRef; use.skillExecutionID = ctx.executionID;
            if (target->canUse(analeptic, target, true)) room->useCardFromSkillEffect(use, ctx, true);
        }
        if (paid.value("suits").toInt() >= 2 && target->isAlive()) {
            ServerPlayer *recipient = room->askForPlayerChosen(target, room->getAlivePlayers(), objectName(), "@mtzongqing-draw");
            if (recipient) { ctx.choice = "draw"; skillEffect(ctx, recipient); }
        }
        if (paid.value("damage").toBool() && target->isAlive())
            room->addPlayerMark(target, "&mtzongqing", getEffectiveAmount(ctx));
        return ContinueEffects;
    }
};
class MTZongqing : public TriggerSkillV2
{
public:
    MTZongqing() : TriggerSkillV2("mtzongqing")
    {
        events << EventPhaseStart; global = true;
        view_as_skill = new MTZongqingVS;
        waked_skills = "#mtzongqing";
    }

    // The distance mark lasts until the holder's next turn, even without Zongqing.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->isAlive() && player->getPhase() == Player::RoundStart && player->getMark("&mtzongqing") > 0)
            room->setPlayerMark(player, "&mtzongqing", 0);
        return true;
    }
};

class MTZongqingDis : public DistanceSkillV2
{
public:
    MTZongqingDis() : DistanceSkillV2("#mtzongqing")
    {
        frequency = NotCompulsory;
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const int mark = ctx.secondary ? ctx.secondary->getMark("&mtzongqing") : 0;
        return mark > 0 ? CorrectSkillResult::useAmount(mark) : CorrectSkillResult::noEffect();
    }
};

class MTZanzhangVS : public ViewAsSkillV2
{
public:
    MTZanzhangVS() : ViewAsSkillV2("mtzanzhang")
    {
    }

    static int excess(const Player *player)
    {
        return player->getHandcardNum() - player->getHp();
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || excess(request.initiator) == 0) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return true;
        return isPromptRequest(request, "@@mtzanzhang") && request.activationRef.isValid()
            && request.initiator->getMark(borrowedActivationMarkName(objectName())) == request.activationRef.key.instanceID;
    }

    // With more hand cards than health, give up to the excess; otherwise take one from the field.
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const int x = excess(request.initiator);
        return x > 0 && card && !card->hasFlag("using") && !request.initiator->isCardLimited(card, Card::MethodMove)
            && matchesFilter(request, card, ".|.|.|hand") && request.selectedCardIds.length() < x;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const int x = excess(request.initiator);
        if (x < 0) return request.selectedCardIds.isEmpty();
        return x > 0 && !request.selectedCardIds.isEmpty() && replaySelection(this, request);
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *to_select) const override
    {
        if (!targets.isEmpty() || !to_select) return false;
        const int x = excess(request.initiator);
        if (x > 0)
            return to_select != request.initiator;
        return x < 0 && !(to_select->getEquips().isEmpty() && to_select->getJudgingArea().isEmpty());
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.length() == 1;
    }

    bool willThrowSelectedCards() const override
    {
        return false;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTZanzhangCard";
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || source->isDead() || !ctx.use_card) return ContinueEffects;
        Room *room = source->getRoom();
        if (ctx.choice == "obtain") {
            const QVariantMap transfer = ctx.extra_data.toMap();
            const int id = transfer.value("id", -1).toInt();
            ServerPlayer *giver = room->findPlayerByObjectName(transfer.value("giver").toString());
            if (id >= 0 && giver && room->getCardOwner(id) == giver
                && (room->getCardPlace(id) == Player::PlaceEquip || room->getCardPlace(id) == Player::PlaceDelayedTrick))
                room->obtainCard(target, id, objectName());
            return ContinueEffects;
        }
        QList<int> ids;
        for (int id : ctx.use_card->getSubcards()) {
            const Card *card = Sanguosha->getCard(id);
            if (ctx.initiator->handCards().contains(id) && !card->hasFlag("using")
                && !ctx.initiator->isCardLimited(card, Card::MethodMove)) ids << id;
        }
        if (!ctx.use_card->getSubcards().isEmpty() && ids.isEmpty()) return ContinueEffects;
        if (ids.isEmpty()) {
            if (target->getEquips().isEmpty() && target->getJudgingArea().isEmpty()) return ContinueEffects;
            int id = room->askForCardChosen(source, target, "ej", "mtzanzhang");
            if (id >= 0 && room->getCardOwner(id) == target
                && (room->getCardPlace(id) == Player::PlaceEquip || room->getCardPlace(id) == Player::PlaceDelayedTrick)) {
                ctx.choice = "obtain"; ctx.extra_data = QVariantMap{{"giver", target->objectName()}, {"id", id}};
                skillEffect(ctx, source);
            }
        } else
            room->giveCard(ctx.initiator, target, ids, "mtzanzhang");
        return ContinueEffects;
    }
};

class MTZanzhang : public TriggerSkillV2
{
public:
    MTZanzhang() : TriggerSkillV2("mtzanzhang")
    {
        events << EventPhaseStart;
        view_as_skill = new MTZanzhangVS;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *target, QVariant &) const override
    {
        if (!target || !target->isAlive() || !target->hasSkill(objectName()) || target->getPhase() != Player::Finish)
            return TriggerList();
        const int x = target->getHandcardNum() - target->getHp();
        if (x == 0 || (x > 0 && target->isKongcheng())) return TriggerList();
        return TriggerList{{target, QStringList{objectName()}}};
    }

    // The Zanzhang card is the invocation; declining it never invoked the skill.
    bool effect(TriggerEvent, Room *room, ServerPlayer *target, SkillContext &ctx) const override
    {
        const int x = target->getHandcardNum() - target->getHp();
        if (x == 0) return false;
        QString pro = x > 0 ? "@mtzanzhang-give:" + QString::number(x) : "@mtzanzhang-get";
        Room::AcceptedViewAsEffectScope prompt(room, target, objectName(), ctx);
        if (prompt.isValid()) room->askForUseCard(target, "@@mtzanzhang", pro, -1, Card::MethodNone);
        return false;
    }
};

class MTHongya : public TriggerSkillV2
{
public:
    MTHongya() : TriggerSkillV2("mthongya")
    {
        events << CardsMoveOneTime << EventSkillInvoking;
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    static QList<ServerPlayer *> wuTargets(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->canDiscard(p, "he"))
                targets << p;
        }
        return targets;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            if (isUsable(accepted)) addUsage(accepted);
            else {
                accepted.is_canceled = true;
                *ctx.original_data = QVariant::fromValue(accepted);
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime) return {};
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (!room->historyScopes().value("phase_id").toLongLong()) return {};

        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == move.to) return TriggerList();

        bool flag = false;
        if (move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))) {
            if (move.reason.m_reason == CardMoveReason::S_REASON_GIVE)
                flag = true;
        } else if (move.from && move.to == player) {
            if (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip) {
                if (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
                    flag = true;
            }
        }
        if (!flag) return TriggerList();

        if (player->getKingdom() == "wu") {
            if (!player->canDiscard(player, "he") || wuTargets(room, player).isEmpty()) return TriggerList();
        } else if (player->getKingdom() != "shu") {
            return TriggerList();
        }
        return TriggerList{{player, usableTriggerInstances(this, player)}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ctx.choice = owner->getKingdom();
        ServerPlayer *target = room->askForPlayerChosen(owner, ctx.choice == "wu" ? wuTargets(room, owner)
            : room->getAlivePlayers(), objectName(), ctx.choice == "wu" ? "@mthongya-wu" : "@mthongya-shu", true, true);
        if (!target) return false;
        ctx.targets = {target};
        if (ctx.choice == "wu") {
            const int own = room->askForCardChosen(owner, owner, "he", objectName(), true, Card::MethodDiscard);
            const int other = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (own < 0 || other < 0) return false;
            ctx.extra_data = QVariantMap{{owner->objectName(), own}, {target->objectName(), other}};
            ctx.targets.prepend(owner);
        }
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        owner->peiyin(this);
        if (ctx.choice != "wu") return false;
        ctx.manual_effect = true;
        const QVariantMap selected = ctx.extra_data.toMap();
        ctx.extra_data = QVariantMap{{"selected", selected}, {"admitted", QVariantList()}};
        for (ServerPlayer *target : ctx.targets) skillEffect(event, room, owner, ctx, target);
        QList<CardsMoveStruct> moves;
        for (const QVariant &value : ctx.extra_data.toMap().value("admitted").toList()) {
            const int id = value.toInt();
            ServerPlayer *holder = room->getCardOwner(id);
            if (!holder || !ctx.targets.contains(holder) || !owner->canDiscard(holder, id)
                || Sanguosha->getCard(id)->hasFlag("using")
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) continue;
            CardMoveReason reason(holder == owner ? CardMoveReason::S_REASON_THROW : CardMoveReason::S_REASON_DISMANTLE,
                owner->objectName(), holder->objectName(), objectName(), "");
            moves << CardsMoveStruct(QList<int>{id}, nullptr, Player::DiscardPile, reason);
        }
        if (!moves.isEmpty()) room->moveCardsAtomic(moves, true);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "shu") target->drawCards(getEffectiveAmount(ctx), objectName());
        else {
            QVariantMap state = ctx.extra_data.toMap();
            const QVariantMap selected = state.value("selected").toMap();
            if (!selected.contains(target->objectName())) return false;
            QVariantList admitted = state.value("admitted").toList(); admitted << selected.value(target->objectName());
            state.insert("admitted", admitted); ctx.extra_data = state;
        }
        return false;
    }
};
MTYinglveCard::MTYinglveCard()
{
    setSkillName("mtyinglve");
    mute = true;
}

// The card recorded in mtyinglve_cardID, cloned so it can only be used (not
// recast). Returns nullptr when there is no record or the room lacks the card.
static Card *cloneMTYinglveCard(const Player *player)
{
    if (!player) return nullptr;
    int id = player->getMark("mtyinglve_cardID") - 1;
    if (id < 0) return nullptr;
    const Card *c = Sanguosha->getCard(id);
    if (!c) return nullptr;
    Card *card = Sanguosha->cloneCard(c);
    if (!card) return nullptr;
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card;
}

bool MTYinglveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *card = cloneMTYinglveCard(Self);
    return card && card->targetFilter(targets, to_select, Self);
}

bool MTYinglveCard::targetFixed() const
{
    // The server has no engine Self. Report not-fixed there so
    // Room::areCardTargetsLegal checks the use through the player-aware
    // targetFilter/targetsFeasible, which handle target-fixed cards as well.
    Card *card = cloneMTYinglveCard(Self);
    return card && card->targetFixed();
}

bool MTYinglveCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card *card = cloneMTYinglveCard(Self);
    return card && card->targetsFeasible(targets, Self);
}

class MTYinglveVS : public ViewAsSkillV2
{
public:
    MTYinglveVS() : ViewAsSkillV2("mtyinglve") {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.activationRef.key.instanceID > 0
            && request.activationRef.key.instanceID == request.initiator->getMark(borrowedActivationMarkName(objectName()))
            && request.pattern == "@@mtyinglve"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->handCards().contains(request.initiator->getMark("mtyinglve_cardID") - 1);
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "MTYinglveCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
        const Player *candidate) const override
    {
        Card *card = cloneMTYinglveCard(request.initiator);
        return card && card->targetFilter(targets, candidate, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        Card *card = cloneMTYinglveCard(request.initiator);
        return card && card->targetsFeasible(targets, request.initiator);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        // Target-fixed cards have no target-group callback in the active pipeline.
        return ctx.targets.isEmpty() ? useRevealedCard(ctx, {}) : ContinueEffects;
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    { return useRevealedCard(ctx, targets); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), objectName()); return FinishSkill; }
    EffectFlow useRevealedCard(SkillContext &ctx, const QList<ServerPlayer *> &targets) const
    {
        ServerPlayer *from = ctx.invoker;
        const int id = from->getMark("mtyinglve_cardID") - 1;
        if (!from->handCards().contains(id)) return FinishSkill;
        // The prompted use remains before the reward draw, including target-fixed cards.
        Card *card = Sanguosha->cloneCard(Sanguosha->getCard(id));
        if (!card) return FinishSkill;
        card->addSubcard(id); card->setSkillName(objectName()); card->setCanRecast(false);
        CardUseStruct use(card, from, targets); use.setOwnedCard(card);
        if (from->getRoom()->useCardFromSkillEffect(use, ctx, true) && from->isAlive())
            skillEffect(ctx, from);
        return FinishSkill;
    }
};

class MTYinglve : public TriggerSkillV2
{
public:
    MTYinglve() : TriggerSkillV2("mtyinglve")
    {
        events << EventPhaseStart;
        view_as_skill = new MTYinglveVS;
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isKongcheng())
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            && !candidates(room, player).isEmpty()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(player, candidates(room, player), objectName(), "@mtyinglve-target", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *t) const override
    {
        player->peiyin(this);

        QList<int> hands = t->handCards();
        if (!hands.isEmpty()) {
            int id = room->doGongxin(player, t, hands, objectName());
            if (id < 0) id = t->getRandomHandCardId();
            if (id < 0 || room->getCardOwner(id) != t || room->getCardPlace(id) != Player::PlaceHand) return false;
            room->showCard(t, id);

            const Card *card = Sanguosha->getCard(id);

            const int previousCard = t->getMark("mtyinglve_cardID");
            const auto restoreCard = qScopeGuard([&] { room->setPlayerMark(t, "mtyinglve_cardID", previousCard); });
            room->setPlayerMark(t, "mtyinglve_cardID", id + 1);
            // Borrow the exact provider instance only for this response window.
            const Room::AcceptedViewAsEffectScope borrowed(room, t, objectName(), ctx);
            if (!card->isAvailable(t)  || t->isLocked(card, false) ||
                    !room->askForUseCard(t, "@@mtyinglve", "@mtyinglve:" + card->objectName())) {
                QList<int> ids;
                foreach (int card_id, t->handCards()) {
                    if (card_id == id || Sanguosha->getCard(card_id)->hasFlag("using")
                        || t->isCardLimited(Sanguosha->getCard(card_id), Card::MethodRecast)) continue;
                    ids << card_id;
                }
                if (ids.isEmpty()) return false;

                DummyCard *dummy = new DummyCard(ids);
                dummy->deleteLater();

                LogMessage log;
                log.type = "$RecastCard";
                log.from = t;
                log.card_str = ListI2S(ids).join("+");
                room->sendLog(log);

                room->moveCardTo(dummy, t, NULL, Player::DiscardPile,
                          CardMoveReason(CardMoveReason::S_REASON_RECAST, t->objectName(), objectName(), ""));
                t->drawCards(ids.length(), "recast");
            }
        }
        return false;
    }
};

class MTXianding : public TriggerSkillV2
{
public:
    MTXianding() : TriggerSkillV2("mtxianding")
    {
        events << DamageCaused;

    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->isDead()) return result;
        ServerPlayer *current = room->getCurrent();
        if (!current || current->isDead() || current->getPhase() == Player::NotActive
            || room->countHistoryCards(current, "turn") < 0 || room->countHistoryCards(current, "turn") > 1) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()))
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || player->isDead()) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ctx.targets = {damage.to};
        return p->askForSkillInvoke(this, QString("damage:%1:%2:%3").arg(player->objectName())
            .arg(damage.to->objectName()).arg(damage.damage + 1));
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *p, SkillContext &ctx, ServerPlayer *) const override
    {
        p->peiyin(this);
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        damage.damage += getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class MTWangheVS : public ViewAsSkillV2
{
public:
    MTWangheVS() : ViewAsSkillV2("mtwanghe") {}
    static QString chosenName(const ActiveSkillRequest &request)
    {
        return request.initiator && request.activationRef.isValid()
            ? request.initiator->getSkillInstanceStateValue("mtwanghe", request.activationRef.key.instanceID, "card_name").toString()
            : QString();
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtwanghe!") && request.activationRef.isValid()
            && request.initiator->getMark(borrowedActivationMarkName(objectName())) == request.activationRef.key.instanceID
            && !chosenName(request).isEmpty();
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    { return cardHistoryKey(chosenName(request), objectName()); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const QString name = chosenName(request);
        Card *card = name.isEmpty() ? nullptr : Sanguosha->cloneCard(name);
        if (card) card->setSkillName(objectName());
        return card;
    }
};

class MTWanghe : public TriggerSkillV2
{
public:
    MTWanghe() : TriggerSkillV2("mtwanghe")
    { events << Damaged << CardsMoveOneTime << Death; frequency = Compulsory; view_as_skill = new MTWangheVS; }
    static QString eventName(TriggerEvent event)
    { return event == Damaged ? "damaged" : event == CardsMoveOneTime ? "cardsmove" : "death"; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || move.to == player) return {};
            int count = 0;
            for (Player::Place place : move.from_places)
                if (place == Player::PlaceHand || place == Player::PlaceEquip) ++count;
            if (count < 2) return {};
        }
        QStringList names;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (!player->getSkillInstanceStateValue(objectName(), id, "deleted").toStringList().contains(eventName(event)))
                names << SkillInstanceUtils::formatName(objectName(), id);
        return names.isEmpty() ? TriggerList() : TriggerList{{player, names}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        const QString timing = eventName(event);
        const QVariant round = room->historyScopes().value("round_id");
        QVariantMap state = player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "timings").toMap();
        QStringList deleted = player->getSkillInstanceStateValue(objectName(), ctx.instanceID, "deleted").toStringList();
        if (deleted.contains(timing)) return false;
        if (state.contains(timing) && state.value(timing) == round) {
            deleted << timing;
            player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "deleted", deleted);
            LogMessage log; log.type = "#MTWangheDelete"; log.from = player;
            log.arg = objectName(); log.arg2 = "mtwanghe:" + timing; room->sendLog(log);
            return false;
        }
        const QList<int> choices = room->getAvailableCardList(player, "basic", objectName());
        if (choices.isEmpty()) return false;
        room->fillAG(choices, player);
        int id;
        { const auto clear = qScopeGuard([&]() { room->clearAG(player); });
          id = room->askForAG(player, choices, true, objectName(), "@mtwanghe-basic"); }
        if (!choices.contains(id)) return false;
        const QString name = Sanguosha->getEngineCard(id)->objectName();
        state.insert(timing, round);
        player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "timings", state);
        Room::AcceptedViewAsEffectScope prompt(room, player, objectName(), ctx);
        if (!prompt.isValid()) return false;
        player->setSkillInstanceStateValue(objectName(), prompt.activationRef().key.instanceID, "card_name", name);
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return false;
        card->setSkillName(objectName());
        CardUseStruct use; use.setOwnedCard(card); use.from = player;
        if (!card->targetFixed()) {
            if (room->askForUseCard(player, "@@mtwanghe!", "@mtwanghe:" + name)) return false;
            const QList<ServerPlayer *> targets = room->getCardTargets(player, card);
            if (targets.isEmpty()) return false;
            use.to = {targets.at(qsanRandomBounded(targets.size()))};
        }
        room->useCardFromSkillEffect(use, ctx, true);
        return false;
    }
};
class MTChunzu : public TriggerSkillV2
{
public:
    MTChunzu() : TriggerSkillV2("mtchunzu")
    { events << BuryVictim << EventSkillInvoking; frequency = Limited; waked_skills = "#mtchunzu"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            if (isUsable(accepted)) addUsage(accepted);
            else {
                accepted.is_canceled = true;
                *ctx.original_data = QVariant::fromValue(accepted);
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != BuryVictim) return {};
        TriggerList result;
        const DeathStruct death = data.value<DeathStruct>();
        if (!death.who || death.who->getRole() != "rebel") return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != death.who) result[owner] << usableTriggerInstances(this, owner);
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; ctx.targets = {ctx.owner}; return ctx.owner && ctx.owner->askForSkillInvoke(this); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList receipts = target->getTag("MTChunzuReceipts").toList();
        receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}, {"got", QVariantList()}};
        target->setTag("MTChunzuReceipts", receipts);
        room->doSuperLightbox(target, objectName());
        if (target->isWounded()) room->recover(target, RecoverStruct(objectName(), ctx.owner, target->getLostHp()));
        return false;
    }
};

class MTChunzuRecord : public TriggerSkillV2
{
public:
    MTChunzuRecord() : TriggerSkillV2("#mtchunzu")
    { events << DrawNCards; global = true; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DrawStruct draw = data.value<DrawStruct>();
        if (!player || player->isDead() || draw.reason == "InitialHandCards" || draw.num < 1) return true;
        for (const QVariant &value : player->getTag("MTChunzuReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = player; ctx.invoker = player; ctx.initiator = player;
            ctx.instanceID = receipt.value("instance").toInt(); ctx.extra_data = receipt; ctx.targets = {player};
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getTag("MTChunzuReceipts").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const DrawStruct draw = ctx.original_data->value<DrawStruct>();
        ctx.amount = draw.num;
        return draw.num > 0;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>(); draw.num = 0;
        *ctx.original_data = QVariant::fromValue(draw);
        const QVariantMap history = room->queryCardHistory(target, "game");
        if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return false;
        QVariantMap receipt = ctx.extra_data.toMap();
        QVariantList got = receipt.value("got").toList();
        QList<int> choices;
        for (const QVariant &value : history.value("items").toList()) {
            const QVariantMap card = value.toMap().value("data").toMap().value("card").toMap();
            QList<int> ids;
            if (card.value("virtual").toBool()) {
                for (const QVariant &id : card.value("subcards").toList()) ids << id.toInt();
            } else if (card.contains("id")) ids << card.value("id").toInt();
            for (int id : ids)
                if (id >= 0 && room->getCardPlace(id) == Player::DiscardPile && !got.contains(id) && !choices.contains(id)) choices << id;
        }
        QList<int> selected;
        for (int i = 0; i < getEffectiveAmount(ctx) && !choices.isEmpty() && target->isAlive(); ++i) {
            room->fillAG(choices, target);
            int id;
            { const auto clear = qScopeGuard([&]() { room->clearAG(target); });
              id = room->askForAG(target, choices, false, "mtchunzu", "@mtchunzu"); }
            if (!choices.contains(id)) break;
            choices.removeAll(id);
            if (room->getCardPlace(id) == Player::DiscardPile) selected << id;
        }
        QList<int> obtainable;
        for (int id : selected) if (room->getCardPlace(id) == Player::DiscardPile) { obtainable << id; got << id; }
        receipt.insert("got", got);
        QVariantList receipts = target->getTag("MTChunzuReceipts").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index >= 0) receipts[index] = receipt;
        target->setTag("MTChunzuReceipts", receipts);
        if (target->isAlive() && !obtainable.isEmpty()) {
            DummyCard cards(obtainable); room->obtainCard(target, &cards, "mtchunzu");
        }
        return false;
    }
};
class MTBishi : public TriggerSkillV2
{
public:
    MTBishi() : TriggerSkillV2("mtbishi")
    {
        events << CardUsed << CardResponded;
        frequency = Compulsory;
        waked_skills = "#mtbishi";
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        CardUseStruct use;
        if (event == CardUsed) use = data.value<CardUseStruct>();
        else {
            const CardResponseStruct response = data.value<CardResponseStruct>();
            if (!response.m_isUse) return result;
            use.card = response.m_card; use.from = player;
        }
        if (!use.card || use.card->isKindOf("SkillCard")) return result;
        const qint64 current = room->historyParent(room->currentHistoryEventId(), event == CardUsed ? "use_card" : "respond_card", true).value("id").toLongLong();
        if (!current) return result;
        QList<qint64> uses;
        for (ServerPlayer *actor : room->getAllPlayers(true)) {
            const QVariantMap history = room->queryCardHistory(actor, "turn");
            if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return result;
            for (const QVariant &value : history.value("items").toList()) {
                const qint64 id = value.toMap().value("event_id").toLongLong();
                if (id && !uses.contains(id)) uses << id;
            }
        }
        std::sort(uses.begin(), uses.end());
        if (uses.size() < 2 || uses.at(1) != current) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()) && (use.from == p || use.to.contains(p)))
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *p) const override
    {
        room->sendCompulsoryTriggerLog(p, this);
        p->drawCards(getEffectiveAmount(ctx), objectName());
        if (p->getKingdom() != "qun") return false;
        QString mark = QString("&mtbishi-Self%1Clear").arg(int(Player::RoundStart));
        room->addPlayerMark(p, mark, getEffectiveAmount(ctx));
        return false;
    }
};

class MTBishiDis : public DistanceSkillV2
{
public:
    MTBishiDis() : DistanceSkillV2("#mtbishi")
    {
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        QString mark = QString("&mtbishi-Self%1Clear").arg(int(Player::RoundStart));
        const int n = ctx.secondary ? ctx.secondary->getMark(mark) : 0;
        return n > 0 ? CorrectSkillResult::useAmount(n) : CorrectSkillResult::noEffect();
    }
};

class MTChushi : public TriggerSkillV2
{
public:
    MTChushi() : TriggerSkillV2("mtchushi")
    {
        events << EventPhaseStart;
        global = true;
        waked_skills = "olkanpo,bazhen";
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->getPhase() != Player::RoundStart) return true;
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            QVariantList keep;
            QStringList expired;
            foreach (const QVariant &value, p->tag.value("MTChushiGrants").toList()) {
                const QVariantMap receipt = value.toMap();
                if (receipt.value("expires").toString() != player->objectName()) { keep << value; continue; }
                // Retire only the instance created by this accepted grant.
                if (receipt.value("instance").toInt() > 0)
                    expired << SkillInstanceUtils::formatName(receipt.value("skill").toString(), receipt.value("instance").toInt());
            }
            p->tag["MTChushiGrants"] = keep;
            foreach (const QString &skill, expired) room->detachSkillFromPlayer(p, skill);
        }
        return true;
    }

    static QList<ServerPlayer *> richer(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (p->getHandcardNum() > player->getHandcardNum()) targets << p;
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            && !richer(room, player).isEmpty() ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->askForSkillInvoke(this)) return false;
        ctx.targets = richer(room, player);
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        player->peiyin(this);
        ctx.extra_data = QVariantMap();
        const QList<ServerPlayer *> donors = ctx.targets;
        foreach (ServerPlayer *donor, donors) {
            if (player->isDead()) break;
            ctx.choice = "give";
            skillEffect(event, room, player, ctx, donor);
        }
        if (player->isDead()) return false;
        const QStringList names = ctx.extra_data.toMap().value("donors").toStringList();
        QString choice = "kanpo";
        if (player->getKingdom() != "shu" && !names.isEmpty())
            choice = room->askForChoice(player, objectName(), "kanpo+bazhen", names.join("+"));
        if (player->getKingdom() == "shu" || choice == "kanpo") {
            ctx.choice = "olkanpo";
            skillEffect(event, room, player, ctx, player);
        }
        if (player->getKingdom() == "shu" || choice == "bazhen") {
            ctx.choice = "bazhen";
            foreach (const QString &name, names)
                if (ServerPlayer *p = room->findPlayerByObjectName(name)) skillEffect(event, room, player, ctx, p);
        }
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || target->isDead() || player->isDead()) return false;
        QVariantMap state = ctx.extra_data.toMap();
        if (ctx.choice == "give") {
            const Card *card = room->askForCard(target, "..", "@mtchushi-give:" + player->objectName(),
                QVariant::fromValue(player), Card::MethodNone, player);
            if (!card) return false;
            state["giver"] = target->objectName();
            state["card"] = card->getEffectiveId();
            ctx.extra_data = state;
            ctx.choice = "receive";
            skillEffect(event, room, player, ctx, player);
        } else if (ctx.choice == "receive") {
            ServerPlayer *giver = room->findPlayerByObjectName(state.value("giver").toString());
            const int id = state.value("card", -1).toInt();
            if (!giver || giver->isDead() || id < 0 || room->getCardOwner(id) != giver
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
            QStringList donors = state.value("donors").toStringList();
            if (!donors.contains(giver->objectName())) donors << giver->objectName();
            state["donors"] = donors;
            ctx.extra_data = state;
            room->giveCard(giver, target, Sanguosha->getCard(id), objectName());
        } else if (!target->hasSkill(ctx.choice, true)) {
            acquireTimedSkill(room, target, ctx.choice, ctx, "MTChushiGrants", player->objectName());
        }
        return false;
    }
};
class MTJijing : public TriggerSkillV2
{
public:
    MTJijing() : TriggerSkillV2("mtjijing") { events << EventPhaseStart << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Round; }
    int getMaxUsageLimit(const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->hasSkill("mtbiancai") && ctx.owner->getKingdom() == "shu" ? 2 : 1; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            if (isUsable(accepted)) addUsage(accepted);
            else {
                accepted.is_canceled = true;
                *ctx.original_data = QVariant::fromValue(accepted);
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart) return {};
        TriggerList result;
        if (player && player->isAlive() && player->getPhase() == Player::Start)
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->hasSkill(objectName())) result[p] << usableTriggerInstances(this, p);
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; ctx.targets = {owner}; return owner->askForSkillInvoke(this); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        return TriggerSkillV2::effect(event, room, owner, ctx);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int dispatch = target->tag.value("MTJijingNextReceipt").toInt() + 1;
        target->tag["MTJijingNextReceipt"] = dispatch;
        QVariantList receipts = target->tag.value("MTJijingReceipts").toList();
        receipts << QVariantMap{{"dispatch", dispatch}, {"turn", room->historyScopes().value("turn_id")},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}};
        target->tag["MTJijingReceipts"] = receipts;
        owner->peiyin(this);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MTJijingRecord : public TriggerSkillV2
{
public:
    MTJijingRecord() : TriggerSkillV2("#mtjijing")
    { events << EventPhaseChanging << Pindian; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            foreach (ServerPlayer *p, room->getAllPlayers(true)) p->tag.remove("MTJijingReceipts");
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    static bool gainedThisTurn(Room *room, ServerPlayer *player, int cardId)
    {
        QVariantMap filter{{"to", player->objectName()}, {"turn_id", room->historyScopes().value("turn_id")}};
        bool found = false;
        do {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool()) return false;
            foreach (const QVariant &value, page.value("items").toList()) {
                const QVariantMap move = value.toMap().value("data").toMap();
                if (move.value("card_id", -1).toInt() == cardId
                    && move.value("to_place").toInt() == int(Player::PlaceHand)) found = true;
            }
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after");
            filter["watermark"] = page.value("watermark");
        } while (true);
        return found;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != Pindian) return true;
        const PindianStruct *pd = data.value<PindianStruct *>();
        if (!pd) return true;
        const QList<ServerPlayer *> players{pd->from, pd->to};
        foreach (ServerPlayer *p, players) {
            const Card *card = p == pd->from ? pd->from_card : pd->to_card;
            if (!p || p->isDead() || !card || room->getCardPlace(card->getEffectiveId()) != Player::PlaceTable
                || p->tag.value("MTJijingReceipts").toList().isEmpty() || !gainedThisTurn(room, p, card->getEffectiveId())) continue;
            foreach (const QVariant &value, p->tag.value("MTJijingReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                if (receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
                SkillContext ctx;
                ctx.skill_name = objectName(); ctx.owner = p; ctx.invoker = actor; ctx.initiator = p;
                ctx.instanceID = receipt.value("dispatch").toInt(); ctx.extra_data = receipt;
                ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                    SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
                ctx.targets = {p}; ctx.original_data = &data; ctx.current_event = event;
                contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->tag.value("MTJijingReceipts").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const PindianStruct *pd = ctx.original_data->value<PindianStruct *>();
        if (!pd) return false;
        const Card *card = target == pd->from ? pd->from_card : target == pd->to ? pd->to_card : nullptr;
        if (card && room->getCardPlace(card->getEffectiveId()) == Player::PlaceTable)
            room->obtainCard(target, card);
        return false;
    }
};
class MTBiancai : public TriggerSkillV2
{
public:
    MTBiancai() : TriggerSkillV2("mtbiancai")
    {
        events << EventPhaseStart << CardUsed;
        global = true;
        waked_skills = "#mtbiancai";
    }

    // A won pindian makes that type of card unanswerable by the loser, even if Biancai is lost.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed || !player || player->isDead()) return true;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard")) return true;
        int id = use.card->getTypeId();
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (player->getMark(QString("mtbiancai_win_%1_%2-Clear").arg(id).arg(p->objectName())) > 0)
                use.no_respond_list << p->objectName();
        }
        data = QVariant::fromValue(use);
        return true;
    }

    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->canPindian(p))
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play || !player->canPindian() || player->getKingdom() != "shu"
            || candidates(room, player).isEmpty()) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *t = room->askForPlayerChosen(player, candidates(room, player), objectName(), "@mtbiancai", true, true);
        if (!t) return false;
        ctx.targets = QList<ServerPlayer *>{t};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *t) const override
    {
        player->peiyin(this);
        if (!t || !player->canPindian(t)) return false;
        PindianStruct *pindian = player->PinDian(t, objectName());
        if (!pindian || !pindian->from_card || !pindian->to_card) return false;

        if (pindian->success) {
            int from_id = pindian->from_card->getTypeId(), to_id = pindian->to_card->getTypeId();
            room->setPlayerMark(player, QString("mtbiancai_win_%1_%2-Clear").arg(from_id).arg(t->objectName()), 1);
            room->setPlayerMark(player, QString("mtbiancai_win_%1_%2-Clear").arg(to_id).arg(t->objectName()), 1);
        } else {
            QString from_type = pindian->from_card->getType(), to_type = pindian->to_card->getType();
            room->setPlayerMark(player, QString("mtbiancai_notwin_%1-Clear").arg(from_type), 1);
            room->setPlayerMark(player, QString("mtbiancai_notwin_%1-Clear").arg(to_type), 1);
            const QString reason = QString("mtbiancai:%1:%2").arg(ctx.activationRef.ownerObjectName).arg(ctx.activationRef.key.instanceID);
            room->setPlayerCardLimitation(player, "use", from_type + "," + to_type, true, reason);
        }
        return false;
    }
};

class MTBiancaiLimit : public CardLimitSkill
{
public:
    MTBiancaiLimit() : CardLimitSkill("#mtbiancai")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        QStringList trs;
        foreach (QString mark, target->getMarkNames()) {
            if (!mark.startsWith("mtbiancai_notwin_") || target->getMark(mark) < 1) continue;
            QStringList marks = mark.split("_");
            if (marks.length() != 3) continue;
            QString type = marks.last().split("-").first();
            trs << type;
        }
        return trs.join(",");
    }
};

class MTChenxiao : public TriggerSkillV2
{
public:
    MTChenxiao() : TriggerSkillV2("mtchenxiao")
    { events << EventPhaseStart; waked_skills = "#mtchenxiao"; }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->getPhase() == Player::RoundStart)
            foreach (const SkillInstance &instance, player->getSkillInstances())
                if (instance.skillName == objectName()) player->removeSkillInstanceStateValue(objectName(), instance.instanceID, "failed");
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->isDead() || player->getPhase() != Player::Play) return result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getKingdom() != "jin" || !p->canPindian(player)) continue;
            foreach (int id, p->getValidSkillInstanceIds(objectName()))
                if (!p->getSkillInstanceStateValue(objectName(), id, "failed").toBool())
                    result[p] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !owner->canPindian(ctx.invoker) || !owner->askForSkillInvoke(this, ctx.invoker)) return false;
        ctx.targets = {ctx.invoker};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!owner->canPindian(target)) return false;
        owner->peiyin(this);
        if (owner->pindian(target, objectName())) {
            room->setPlayerMark(target, QString("MTChenxiao_%1-Clear").arg(owner->objectName()), 1);
            // Chenxiao resets every Jijing grant held by this character, each by exact quota identity.
            if (const Skill *jijing = Sanguosha->getSkill("mtjijing")) {
                foreach (int id, owner->getValidSkillInstanceIds("mtjijing")) {
                    SkillContext reset;
                    reset.owner = owner; reset.invoker = owner; reset.initiator = owner; reset.instanceID = id;
                    reset.skill_name = "mtjijing";
                    reset.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey("mtjijing", id));
                    reset.sourceRef = room->resolveSkillInstanceRootRef(reset.activationRef);
                    jijing->resetUsage(reset);
                }
            }
        } else owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "failed", true);
        return false;
    }
};
class MTChenxiaoProhibit : public ProhibitSkill
{
public:
    MTChenxiaoProhibit() : ProhibitSkill("#mtchenxiao")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return from->getMark(QString("MTChenxiao_%1-Clear").arg(to->objectName())) > 0 && !card->isKindOf("SkillCard");
    }
};

class MTZhuluVS : public ViewAsSkillV2
{
public:
    MTZhuluVS() : ViewAsSkillV2("mtzhulu") {}
    static Slash *handSlash(const Player *player)
    {
        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->setSkillName("mtzhulu"); slash->addSubcards(player->handCards());
        return slash;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@mtzhulu") && request.initiator && !request.initiator->isKongcheng()
            && request.activationRef.key.instanceID == request.initiator->getMark(borrowedActivationMarkName(objectName()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request)) return nullptr;
        foreach (int id, request.initiator->handCards())
            if (Sanguosha->getCard(id)->hasFlag("using")) return nullptr;
        Slash *slash = handSlash(request.initiator);
        slash->tag["MTZhuluReceipt"] = request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "receipt");
        return slash;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *candidate) const override
    {
        if (!candidate || !canActivate(request)) return false;
        const QVariantMap receipt = request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "receipt").toMap();
        if (!receipt.value("shown").toMap().contains(candidate->objectName())) return false;
        QScopedPointer<Slash> slash(handSlash(request.initiator));
        return slash->targetFilter(targets, candidate, request.initiator)
            && !request.initiator->isProhibited(candidate, slash.data(), targets);
    }
};

class MTZhulu : public TriggerSkillV2
{
public:
    MTZhulu() : TriggerSkillV2("mtzhulu")
    {
        events << CardFinished << EventPhaseChanging;
        global = true; view_as_skill = new MTZhuluVS;
        waked_skills = "#mtzhulu-slash-ndl,#mtzhulu";
    }
    static QList<ServerPlayer *> showable(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) if (!p->isKongcheng()) result << p;
        return result;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardFinished || !player || player->isDead() || player->getPhase() != Player::Play
            || showable(room, player).isEmpty()) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->isKindOf("SkillCard")) return result;
        const QVariant turn = room->historyScopes().value("turn_id");
        foreach (int id, player->getValidSkillInstanceIds(objectName())) {
            const QVariantMap state = player->getSkillInstanceStateValue(objectName(), id, "turn_state").toMap();
            if (state.value("turn") != turn || !state.value("banned").toBool())
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging) return false;
        if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        // Enumerating a retained effect does not consume its receipt.
        const QVariantList receipts = player->tag.value("MTZhuluReceipts").toList();
        if (player->isDead() || player->isKongcheng()) return true;
        foreach (const QVariant &value, receipts) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = player; ctx.invoker = player; ctx.initiator = player;
            ctx.instanceID = receipt.value("dispatch").toInt(); ctx.extra_data = receipt;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.targets = {player}; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx) : ctx.sourceRef.isValid(); }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) return true;
        ServerPlayer *target = room->askForPlayerChosen(owner, showable(room, owner), objectName(), "@mtzhulu-show", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) {
            QVariantList receipts = owner->tag.value("MTZhuluReceipts").toList();
            receipts.removeAll(ctx.extra_data);
            owner->tag["MTZhuluReceipts"] = receipts;
        }
        return TriggerSkillV2::effect(event, room, owner, ctx);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        if (event == EventPhaseChanging) {
            SkillContext accepted = ctx;
            const QVariantMap receipt = ctx.extra_data.toMap();
            accepted.activationRef = SkillInstanceRef(receipt.value("activation_owner").toString(),
                SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_instance").toInt()));
            const Room::AcceptedViewAsEffectScope prompt(room, target, objectName(), accepted);
            if (prompt.isValid()) {
                target->setSkillInstanceStateValue(objectName(), prompt.activationRef().key.instanceID, "receipt", receipt);
                room->askForUseCard(target, "@@mtzhulu", "@mtzhulu");
            }
            return false;
        }
        if (target->isKongcheng() || owner->isDead()) return false;
        const int id = room->askForCardChosen(owner, target, "h", objectName());
        if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) return false;
        const QVariant turn = room->historyScopes().value("turn_id"), phase = room->historyScopes().value("phase_id");
        QVariantMap state = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "turn_state").toMap();
        if (state.value("turn") != turn) state = QVariantMap{{"turn", turn}};
        if (state.value("phase") != phase) { state["phase"] = phase; state["suits"] = QStringList(); }
        QStringList suits = state.value("suits").toStringList();
        const QString suit = Sanguosha->getCard(id)->getSuitString();
        const bool duplicate = suits.contains(suit);
        if (!duplicate) suits << suit;
        state["suits"] = suits; state["banned"] = duplicate;
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "turn_state", state);
        QVariantList receipts = owner->tag.value("MTZhuluReceipts").toList();
        int index = -1;
        for (int i = 0; i < receipts.size(); ++i)
            if (receipts.at(i).toMap().value("activation_instance").toInt() == ctx.activationRef.key.instanceID
                && receipts.at(i).toMap().value("turn") == turn) { index = i; break; }
        QVariantMap receipt = index < 0 ? QVariantMap() : receipts.at(index).toMap();
        if (index < 0) {
            const int dispatch = owner->tag.value("MTZhuluNextReceipt").toInt() + 1;
            owner->tag["MTZhuluNextReceipt"] = dispatch;
            receipt = QVariantMap{{"turn", turn}, {"dispatch", dispatch}, {"source_owner", ctx.sourceRef.ownerObjectName},
                {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
                {"activation_instance", ctx.activationRef.key.instanceID}};
        }
        QVariantMap shown = receipt.value("shown").toMap();
        QVariantList ids = shown.value(target->objectName()).toList();
        if (!ids.contains(id)) ids << id;
        shown[target->objectName()] = ids; receipt["shown"] = shown;
        if (index < 0) receipts << receipt; else receipts[index] = receipt;
        owner->tag["MTZhuluReceipts"] = receipts;
        owner->peiyin(this); room->showCard(target, id);
        if (!duplicate) { ctx.choice = "draw"; skillEffect(event, room, owner, ctx, owner); }
        return false;
    }
};

class MTZhuluDamage : public TriggerSkillV2
{
public:
    MTZhuluDamage() : TriggerSkillV2("#mtzhulu") { events << Damage; global = true; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || damage.chain || damage.transfer || !damage.to || !damage.from || damage.from->isDead()) return true;
        const QVariantMap receipt = damage.card->tag.value("MTZhuluReceipt").toMap();
        if (receipt.isEmpty()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = damage.from; ctx.invoker = damage.from; ctx.initiator = damage.from;
        ctx.instanceID = receipt.value("dispatch").toInt(); ctx.extra_data = receipt;
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.targets = {damage.to}; ctx.original_data = &data; ctx.current_event = event;
        contexts << ctx; return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override { return ctx.sourceRef.isValid(); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (ctx.choice != "obtain") {
            ctx.choice = "obtain"; skillEffect(event, room, owner, ctx, owner); return false;
        }
        QList<int> ids;
        const QVariantList shown = ctx.extra_data.toMap().value("shown").toMap().value(damage.to->objectName()).toList();
        foreach (const QVariant &value, shown) {
            const int id = value.toInt();
            if (room->getCardOwner(id) == damage.to && room->getCardPlace(id) == Player::PlaceHand
                && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
        }
        if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, "mtzhulu"); }
        return false;
    }
};

class MTZhuluNoDistanceLimit : public TargetModSkillV2
{
public:
    MTZhuluNoDistanceLimit() : TargetModSkillV2("#mtzhulu-slash-ndl") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->getSkillName() == "mtzhulu")
            return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};
class MTZhengwang : public TriggerSkillV2
{
public:
    MTZhengwang() : TriggerSkillV2("mtzhengwang")
    { events << EventPhaseStart; waked_skills = "#mtzhengwang"; }
    static int turnDamage(Room *room, ServerPlayer *player)
    {
        QVariantMap filter{{"from", player->objectName()}, {"turn_id", room->historyScopes().value("turn_id")}};
        int total = 0;
        do {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool()) return -1;
            foreach (const QVariant &value, page.value("items").toList())
                total += value.toMap().value("data").toMap().value("amount").toInt();
            if (!page.value("has_more").toBool()) return total;
            filter["after"] = page.value("next_after"); filter["watermark"] = page.value("watermark");
        } while (true);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getRole() != "lord" || player->getPhase() != Player::NotActive || room->isCurrentExtraTurn()) return result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->hasSkill(objectName()) && p->getKingdom() == "jin") result[p] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int damage = turnDamage(room, ctx.invoker);
        if (damage < 0 || !owner->askForSkillInvoke(this,
            QString("mtzhengwang:%1:%2").arg(ctx.invoker->objectName()).arg(damage))) return false;
        ctx.extra_data = damage; ctx.targets = {owner}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        owner->peiyin(this); target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        if (target->isDead()) return false;
        const qint64 cause = room->currentHistoryEventId();
        if (!cause) return false; // No invented identity when the journal cannot link this extra turn.
        const int dispatch = target->tag.value("MTZhengwangNextReceipt").toInt() + 1;
        target->tag["MTZhengwangNextReceipt"] = dispatch;
        QVariantMap receipt{{"dispatch", dispatch}, {"cause", cause}, {"threshold", ctx.extra_data},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}};
        QVariantList receipts = target->tag.value("MTZhengwangReceipts").toList();
        receipts << receipt; target->tag["MTZhengwangReceipts"] = receipts;
        if (room->scheduleExtraTurn(target, ctx.sourceRef) <= 0) {
            receipts.removeAll(receipt); target->tag["MTZhengwangReceipts"] = receipts;
        }
        return false;
    }
};

class MTZhengwangRecord : public TriggerSkillV2
{
public:
    MTZhengwangRecord() : TriggerSkillV2("#mtzhengwang")
    { events << EventPhaseEnd << EventPhaseChanging; global = true; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    static bool currentReceipt(Room *room, const QVariantMap &receipt)
    {
        const SkillInstanceRef source(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        return room->isCurrentExtraTurn() && room->getCurrentExtraTurnSourceRef() == source
            && room->getCurrentExtraTurnCauseEventId() == receipt.value("cause").toLongLong();
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        QVariantList keep;
        foreach (const QVariant &value, player->tag.value("MTZhengwangReceipts").toList())
            if (!currentReceipt(room, value.toMap())) keep << value;
        player->tag["MTZhengwangReceipts"] = keep;
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseEnd || !player || player->isDead() || player->getPhase() != Player::Play || player->isKongcheng()) return true;
        const int damage = MTZhengwang::turnDamage(room, player);
        if (damage < 0) return true;
        foreach (const QVariant &value, player->tag.value("MTZhengwangReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (!currentReceipt(room, receipt) || damage > receipt.value("threshold").toInt()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = player; ctx.invoker = player; ctx.initiator = player;
            ctx.instanceID = receipt.value("dispatch").toInt(); ctx.extra_data = receipt;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.targets = {player}; ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->tag.value("MTZhengwangReceipts").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    { target->throwAllHandCards(); return false; }
};
class MTZhuizunVS : public ViewAsSkillV2
{
public:
    MTZhuizunVS() : ViewAsSkillV2("mtzhuizun", 2)
    {
        expand_pile = "mtzhuizunde";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getPile("mtzhuizunde").length() > 1;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ViewAsSkillV2::canSelectCard(request, card) && request.initiator
            && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.initiator->getPile("mtzhuizunde").contains(card->getEffectiveId()) && !card->hasFlag("using");
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.length() == 2 && replaySelection(this, request);
    }

    TargetMode targetMode() const override
    {
        return NoTarget;
    }

    bool willThrowSelectedCards() const override
    {
        return false;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "MTZhuizunCard";
    }

    // The two "de" leave the pile as the price.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ServerPlayer *source = ctx.initiator;
        if (!source || !cardSelectionFeasible(request)) return false;
        DummyCard dummy(request.selectedCardIds);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), QString(), "mtzhuizun", "");
        room->throwCard(&dummy, reason, NULL);
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    { return ctx.invoker ? skillEffect(ctx, ctx.invoker) : FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *source) const override
    {
        if (!source || source->isDead()) return FinishSkill;
        Room *room = source->getRoom();
        QStringList choices;
        if (!source->hasSkill("tenyearrende", true)) choices << "tenyearrende1";
        if (!source->hasSkill("olsishu", true)) choices << "olsishu1";
        if (choices.isEmpty()) return FinishSkill;
        QString choice = room->askForChoice(source, objectName(), choices.join("+"));
        choice.chop(1);
        acquireTimedSkill(room, source, choice, ctx, "MTZhuizunGrants");
        return FinishSkill;
    }
};
class MTZhuizun : public TriggerSkillV2
{
public:
    MTZhuizun() : TriggerSkillV2("mtzhuizun")
    {
        events << EventPhaseStart;
        view_as_skill = new MTZhuizunVS;
        waked_skills = "#mtzhuizun";
    }

    // The turn player's hand cards whose suits are not yet among p's "de".
    static QStringList offerable(ServerPlayer *player, ServerPlayer *p)
    {
        QList<Card::Suit> suits;
        foreach (int id, p->getPile("mtzhuizunde")) {
            Card::Suit suit = Sanguosha->getCard(id)->getSuit();
            if (!suits.contains(suit))
                suits << suit;
        }

        QStringList ids;
        foreach (int id, player->handCards()) {
            if (!suits.contains(Sanguosha->getCard(id)->getSuit()) && !Sanguosha->getCard(id)->hasFlag("using"))
                ids << QString::number(id);
        }
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::RoundStart || player->isKongcheng()) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()) && p->getKingdom() == "qun" && !offerable(player, p).isEmpty())
                result[p] << objectName();
        }
        return result;
    }

    // The turn player decides whether to offer a card to p.
    bool cost(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (player->isDead() || player->isKongcheng()) return false;
        const QStringList ids = offerable(player, p);
        if (ids.isEmpty()) return false;
        const Card *c = room->askForCard(player, ids.join(","), "@mtzhuizun:" + p->objectName(), QVariant::fromValue(p), Card::MethodNone);
        if (!c) return false;
        ctx.extra_data = QVariantMap{{"card", c->getEffectiveId()}, {"actor", player->objectName()}};
        ctx.targets = {player};
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = room->findPlayerByObjectName(ctx.extra_data.toMap().value("actor").toString());
        if (!actor || actor->isDead()) return false;
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        const int id = ctx.extra_data.toMap().value("card", -1).toInt();
        if (id < 0 || room->getCardOwner(id) != actor || room->getCardPlace(id) != Player::PlaceHand
            || !offerable(actor, owner).contains(QString::number(id))) return false;
        if (ctx.choice != "place") {
            ctx.choice = "place";
            skillEffect(event, room, owner, ctx, owner);
        } else {
            owner->peiyin(this);
            target->addToPile("mtzhuizunde", id);
            if (actor->isAlive()) { ctx.choice = "draw"; skillEffect(event, room, owner, ctx, actor); }
        }
        return false;
    }
};
class MTZhuizunEffect : public TriggerSkillV2
{
public:
    MTZhuizunEffect() : TriggerSkillV2("#mtzhuizun")
    { events << RoundEnd; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            const QVariantList grants = p->tag.take("MTZhuizunGrants").toList();
            foreach (const QVariant &value, grants) {
                const QVariantMap grant = value.toMap();
                if (grant.value("instance").toInt() > 0)
                    room->detachSkillFromPlayer(p, SkillInstanceUtils::formatName(grant.value("skill").toString(), grant.value("instance").toInt()));
            }
        }
        return true;
    }
};
class MTZhanhua : public TriggerSkillV2
{
public:
    MTZhanhua() : TriggerSkillV2("mtzhanhua")
    { events << DamageCaused << EventPhaseStart << Dying << EventSkillInvoking; global = true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent event) const override
    { return TriggerSkill::getPriority(event) - (event == DamageCaused ? 1 : 0); }
    static QString branch(const SkillContext &ctx)
    {
        if (!ctx.choice.isEmpty()) return ctx.choice;
        if (!ctx.original_data) return {};
        if (ctx.original_data->canConvert<DamageStruct>()) return "damage";
        if (ctx.original_data->canConvert<DyingStruct>()) return "dying";
        return "hand";
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        return ctx.owner && !ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID,
            "used").toStringList().contains(branch(ctx));
    }
    void addUsage(const SkillContext &ctx) const override
    {
        QStringList used = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "used").toStringList();
        const QString choice = branch(ctx);
        if (!used.contains(choice)) used << choice;
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "used", used);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        SkillContext accepted = data.value<SkillContext>();
        if (accepted.bypass_cost && accepted.owner && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName()
            && accepted.activationRef.ownerObjectName == accepted.owner->objectName()
            && accepted.activationRef.key.instanceID == accepted.instanceID) {
            if (isUsable(accepted)) addUsage(accepted);
            else { accepted.is_canceled = true; data = QVariant::fromValue(accepted); }
        }
        return true;
    }
    // Peaks are game facts, including before this instance was acquired. Old journals without a baseline are unknown.
    static QVariantMap peaks(Room *room, ServerPlayer *player)
    {
        QVariantMap filter{{"kind", "player_state"}, {"player", player->objectName()}};
        QVariantMap result{{"hp", 0}, {"maxhp", 0}, {"hand", 0}, {"damage", 0}};
        bool baseline = false;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool()) return {};
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap state = value.toMap().value("data").toMap();
                baseline = baseline || state.value("boundary") == "baseline";
                for (const QString &key : QStringList{"hp", "maxhp", "hand"}) {
                    const QString field = key == "hand" ? "hand_count" : key;
                    if (!state.contains(field + "_before") || !state.contains(field + "_after")) return {};
                    result[key] = qMax(result.value(key).toInt(), qMax(state.value(field + "_before").toInt(),
                        state.value(field + "_after").toInt()));
                }
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        if (!baseline) return {};
        QVariantMap damageFilter{{"from", player->objectName()}, {"watermark", filter.value("watermark")}};
        for (;;) {
            const QVariantMap page = room->queryActualDamage(damageFilter);
            if (!page.value("complete").toBool()) return {};
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap damage = value.toMap().value("data").toMap();
                if (!damage.contains("amount")) return {};
                result["damage"] = qMax(result.value("damage").toInt(), damage.value("amount").toInt());
            }
            if (!page.value("has_more").toBool()) break;
            damageFilter.insert("after", page.value("next_after"));
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseStart && player->getPhase() != Player::Play) return {};
        if (event == Dying && data.value<DyingStruct>().who != player) return {};
        if (event == DamageCaused && (!data.value<DamageStruct>().to || data.value<DamageStruct>().from != player)) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.choice = event == DamageCaused ? "damage" : event == Dying ? "dying" : "hand";
        if (!isUsable(ctx)) return false;
        const QVariantMap history = peaks(room, player);
        if (history.isEmpty()) return false;
        const int amount = getEffectiveAmount(ctx);
        QString prompt;
        if (ctx.choice == "damage") {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            ctx.targets = {damage.to};
            prompt = QString("damage:%1:%2").arg(damage.to->objectName())
                .arg(qMax(history.value("damage").toInt(), damage.damage) + amount);
        } else {
            ctx.targets = {player};
            if (ctx.choice == "hand") prompt = QString("handcardnum:%1").arg(history.value("hand").toInt() + amount);
            else prompt = QString("dying:%1:%2").arg(history.value("maxhp").toInt() + amount)
                .arg(qMin(history.value("maxhp").toInt(), history.value("hp").toInt()) + amount);
        }
        // Description values are a projection, never the historical authority.
        player->setSkillDescriptionSwap(objectName(), "%arg1", QString::number(history.value("damage").toInt()));
        player->setSkillDescriptionSwap(objectName(), "%arg2", QString::number(history.value("hand").toInt()));
        player->setSkillDescriptionSwap(objectName(), "%arg3", QString::number(history.value("maxhp").toInt()));
        player->setSkillDescriptionSwap(objectName(), "%arg4", QString::number(history.value("hp").toInt()));
        room->changeTranslation(player, objectName(), 1);
        const QVariant previous = player->getTag("MTZhanhuaDamageCaused");
        const auto restore = qScopeGuard([&]() {
            if (previous.isValid()) player->setTag("MTZhanhuaDamageCaused", previous);
            else player->removeTag("MTZhanhuaDamageCaused");
        });
        if (ctx.choice == "damage") player->setTag("MTZhanhuaDamageCaused", *ctx.original_data);
        return player->askForSkillInvoke(this, prompt);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap history = peaks(room, owner);
        if (history.isEmpty()) return false;
        const int amount = getEffectiveAmount(ctx);
        owner->peiyin(this);
        if (ctx.choice == "damage") {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            // This skill intentionally includes the pending damage value in its peak, independently of committed damage history.
            damage.damage = qMax(history.value("damage").toInt(), damage.damage) + amount;
            *ctx.original_data = QVariant::fromValue(damage);
        } else if (ctx.choice == "hand") {
            target->drawCards(qMax(0, history.value("hand").toInt() + amount - target->getHandcardNum()), objectName());
        } else {
            const int maximum = history.value("maxhp").toInt() + amount;
            const int hp = qMin(maximum, history.value("hp").toInt() + amount);
            room->setPlayerProperty(target, "maxhp", maximum);
            if (target->isAlive() && target->getHp() < hp)
                room->recover(target, RecoverStruct(objectName(), owner, hp - target->getHp()));
        }
        return false;
    }
};
MTHongwuCard::MTHongwuCard()
{
    setSkillName("mthongwu");
    mute = true;
}

bool MTHongwuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (subcards.isEmpty()) return false;
    const Card *c = Sanguosha->getCard(subcards.first());
    Card *card = Sanguosha->cloneCard(c);
    if (!card) return false;
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFilter(targets, to_select, Self);
}

bool MTHongwuCard::targetFixed() const
{
    if (subcards.isEmpty()) return false;
    const Card *c = Sanguosha->getCard(subcards.first());
    Card *card = Sanguosha->cloneCard(c);
    if (!card) return false;
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFixed();
}

bool MTHongwuCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (subcards.isEmpty()) return false;
    const Card *c = Sanguosha->getCard(subcards.first());
    Card *card = Sanguosha->cloneCard(c);
    if (!card) return false;
    card->addSubcard(c);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetsFeasible(targets, Self);
}

class MTHongwuVS : public ViewAsSkillV2
{
public:
    MTHongwuVS() : ViewAsSkillV2("mthongwu", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.activationRef.key.instanceID > 0
            && request.activationRef.key.instanceID == request.initiator->getMark(borrowedActivationMarkName(objectName()))
            && request.pattern == "@@mthongwu"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && card->isDamageCard() && !card->hasFlag("using")
            && request.initiator->handCards().contains(card->getEffectiveId())
            && card->isAvailable(request.initiator);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = Sanguosha->cloneCard(original);
        if (!card) return nullptr;
        card->addSubcard(original);
        card->setSkillName(objectName());
        card->setCanRecast(false);
        return card;
    }
};

class MTHongwu : public TriggerSkillV2
{
public:
    MTHongwu() : TriggerSkillV2("mthongwu")
    {
        events << EventPhaseStart << EventPhaseChanging << EventSkillEffectFinished; global = true;
        view_as_skill = new MTHongwuVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillEffectFinished) return false;
        const SkillContext finished = data.value<SkillContext>();
        if (finished.skill_name != objectName() || finished.activationRef.isValid() || !finished.owner) return false;
        const QVariantMap receipt = finished.extra_data.toMap();
        QVariantList pending = finished.owner->getTag("MTHongwuReceipts").toList();
        pending.removeAll(receipt); finished.owner->setTag("MTHongwuReceipts", pending);
        ServerPlayer *actor = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
        if (actor) room->removePlayerCardLimitationByReason(actor, receipt.value("reason").toString());
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || player->isDead() || player->getPhase() != Player::RoundStart) return result;
        const QVariant round = room->historyScopes().value("round_id");
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            foreach (int id, owner->getValidSkillInstanceIds(objectName()))
                if (owner->getSkillInstanceStateValue(objectName(), id, "failed_round") != round)
                    result[owner] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging) return false;
        if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            foreach (const QVariant &value, owner->tag.value("MTHongwuReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                if (receipt.value("actor").toString() != player->objectName()
                    || receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
                SkillContext ctx;
                ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player; ctx.initiator = player;
                ctx.is_forced = true;
                ctx.instanceID = receipt.value("dispatch").toInt(); ctx.extra_data = receipt;
                ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                    SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
                ctx.targets = {player}; ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
            }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.owner && ctx.owner->tag.value("MTHongwuReceipts").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) return true;
        if (!ctx.invoker || ctx.invoker->isDead() || !owner->askForSkillInvoke(this, ctx.invoker)) return false;
        ctx.targets = {owner, ctx.invoker}; room->sortByActionOrder(ctx.targets);
        ctx.extra_data = ctx.invoker->objectName(); return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            owner->peiyin(this);
            return TriggerSkillV2::effect(event, room, owner, ctx);
        }
        const QVariantMap receipt = ctx.extra_data.toMap();
        QVariantList receipts = owner->tag.value("MTHongwuReceipts").toList();
        receipts.removeAll(ctx.extra_data); owner->tag["MTHongwuReceipts"] = receipts;
        ServerPlayer *actor = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
        const auto cleanup = qScopeGuard([&]() {
            if (actor) room->removePlayerCardLimitationByReason(actor, receipt.value("reason").toString());
        });
        ctx.manual_effect = true;
        for (ServerPlayer *target : ctx.targets) skillEffect(event, room, owner, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == EventPhaseStart) {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            if (target->objectName() != ctx.extra_data.toString() || target->isDead()) return false;
            const QString suit = Card::Suit2String(room->askForSuit(target, objectName()));
            const int dispatch = owner->tag.value("MTHongwuNextReceipt").toInt() + 1;
            owner->tag["MTHongwuNextReceipt"] = dispatch;
            const QString reason = QString("mthongwu:%1:%2").arg(owner->objectName()).arg(dispatch);
            QVariantList receipts = owner->tag.value("MTHongwuReceipts").toList();
            receipts << QVariantMap{{"dispatch", dispatch}, {"turn", room->historyScopes().value("turn_id")},
                {"actor", target->objectName()}, {"suit", suit}, {"reason", reason},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
                {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
            owner->tag["MTHongwuReceipts"] = receipts;
            LogMessage log; log.type = "#ChooseSuit"; log.from = target; log.arg = suit;
            room->sendLog(log, target);
            room->setPlayerCardLimitation(target, "use,response,discard", QString(".|%1|.|.").arg(suit), true, reason);
            return false;
        }
        const QVariantMap receipt = ctx.extra_data.toMap();
        const QString suit = receipt.value("suit").toString();
        const QString guess = Card::Suit2String(room->askForSuit(owner, objectName()));
        LogMessage log; log.type = guess == suit ? "#MTHongwuRight" : "#MTHongwuWrong";
        log.from = owner; log.arg = guess; log.arg2 = suit; room->sendLog(log);
        if (target->isDead()) return false;
        if (guess == suit) {
            QList<int> discard;
            foreach (const Card *card, target->getCards("he"))
                if (!card->isDamageCard() && !card->hasFlag("using") && target->canDiscard(target, card->getEffectiveId()))
                    discard << card->getEffectiveId();
            if (!discard.isEmpty()) { DummyCard cards(discard); room->throwCard(&cards, objectName(), target); }
        } else {
            const int originalId = receipt.value("activation_instance").toInt();
            if (owner->hasSkillInstance(objectName(), originalId))
                owner->setSkillInstanceStateValue(objectName(), originalId, "failed_round", room->historyScopes().value("round_id"));
            SkillContext accepted = ctx;
            accepted.activationRef = SkillInstanceRef(receipt.value("activation_owner").toString(),
                SkillInstanceKey(receipt.value("activation_skill").toString(), originalId));
            const Room::AcceptedViewAsEffectScope prompt(room, target, objectName(), accepted);
            if (prompt.isValid())
                while (target->isAlive() && !target->isKongcheng())
                    if (!room->askForUseCard(target, "@@mthongwu", "@mthongwu")) break;
        }
        return false;
    }
};

MaotuPackage::MaotuPackage()
    : Package("maotu")
{
    General *mt_wenhui = new General(this, "mt_wenhui", "wei", 3);
    mt_wenhui->addSkill(new MTLiaoshi);

    mt_wenhui->addSkill(new MTTongyi);

    General *mt_xiahouba = new General(this, "mt_xiahouba", "wei", 4);
    mt_xiahouba->addSkill(new MTXianzheng);
    mt_xiahouba->addSkill(new MTNianchou);
    mt_xiahouba->addSkill(new MTNianchouTargetMod);

    General *mt_zhugeshang = new General(this, "mt_zhugeshang", "shu", 4);
    mt_zhugeshang->addSkill(new MTJieli);
    mt_zhugeshang->addSkill(new MTJieliTargetMod);
    mt_zhugeshang->addSkill(new MTFuyi);
    mt_zhugeshang->addSkill(new MTFuyiDamage);
    mt_zhugeshang->addSkill(new MTFuyiTurn);

    General *mt_luoxian = new General(this, "mt_luoxian", "shu", 4);
    mt_luoxian->addSkill(new MTZhongyi);

    General *mt_zhaoshuang = new General(this, "mt_zhaoshuang", "wu", 3);
    mt_zhaoshuang->addSkill(new MTWeiqie);
    mt_zhaoshuang->addSkill(new MTGuanda);

    General *mt_weizhao = new General(this, "mt_weizhao", "wu", 3);
    mt_weizhao->addSkill(new MTZhilie);
    mt_weizhao->addSkill(new MTChuanjiu);

    General *mt_liubei = new General(this, "mt_liubei", "qun", 3);
    mt_liubei->addSkill(new MTDianpei);
    mt_liubei->addSkill(new MTRenyi);
    mt_liubei->addSkill(new MTRenyiTargetMod);

    General *mt_zhurong = new General(this, "mt_zhurong", "qun", 4, false);
    mt_zhurong->addSkill(new MTFeiren);
    mt_zhurong->addSkill(new MTFeirenTargetMod);
    mt_zhurong->addSkill(new MTFuzhan);

    General *mt_simayan = new General(this, "mt_simayan$", "jin", 4);
    mt_simayan->addSkill(new MTRenyu);
    mt_simayan->addSkill(new MTFengshang);
    mt_simayan->addSkill(new MTFengshangKeep);
    mt_simayan->addSkill(new MTJiawei);

    General *mt_wangjun = new General(this, "mt_wangjun", "jin", 4);
    mt_wangjun->addSkill(new MTGuzhao);
    mt_wangjun->addSkill(new MTGuzhaoTargetMod);

    General *mt_shenzhouyu = new General(this, "mt_shenzhouyu", "god", 4);
    mt_shenzhouyu->addSkill(new MTGuqu);
    mt_shenzhouyu->addSkill(new MTLunhuan);
    mt_shenzhouyu->addSkill("yingzi");

    General *mt_shencaopi = new General(this, "mt_shencaopi", "god", 3);
    mt_shencaopi->addSkill(new MTJiye);
    mt_shencaopi->addSkill(new MTZhihe);
    mt_shencaopi->addSkill(new MTWenqi);

    General *mt_zhugedan = new General(this, "mt_zhugedan", "wei+wu", 4);
    mt_zhugedan->addSkill(new MTYanyi);
    mt_zhugedan->addSkill(new MTJishi);
    mt_zhugedan->addSkill(new MTJishiTargetMod);
    mt_zhugedan->addSkill(new MTYitao);

    General *mt_mengda = new General(this, "mt_mengda", "wei+shu", 4);
    mt_mengda->addSkill(new MTJuyuan);
    mt_mengda->addSkill(new MTJuyuanFlag);
    mt_mengda->addSkill(new MTJuyuanProhibit);
    mt_mengda->addSkill(new MTFupan);

    General *mt_zhangyan = new General(this, "mt_zhangyan", "wei+qun", 4);
    mt_zhangyan->addSkill(new MTFeiyan);
    mt_zhangyan->addSkill(new MTFeiyanEffect);

    General *mt_liuling = new General(this, "mt_liuling", "wei+jin", 3);
    mt_liuling->addSkill(new MTJiukuang);
    mt_liuling->addSkill(new MTJiukuangTMD);
    mt_liuling->addSkill(new MTZongqing);
    mt_liuling->addSkill(new MTZongqingDis);

    General *mt_zhangwen = new General(this, "mt_zhangwen", "wu+shu", 3);
    mt_zhangwen->addSkill(new MTZanzhang);
    mt_zhangwen->addSkill(new MTHongya);

    General *mt_sunfuren = new General(this, "mt_sunfuren", "wu+qun", 3, false);
    mt_sunfuren->addSkill(new MTYinglve);
    mt_sunfuren->addSkill(new MTXianding);

    General *mt_luji = new General(this, "mt_luji", "wu+jin", 3);
    mt_luji->addSkill(new MTWanghe);
    mt_luji->addSkill(new MTChunzu);
    mt_luji->addSkill(new MTChunzuRecord);

    General *mt_zhugejun = new General(this, "mt_zhugejun", "shu+qun", 3);
    mt_zhugejun->addSkill(new MTBishi);
    mt_zhugejun->addSkill(new MTBishiDis);
    mt_zhugejun->addSkill(new MTChushi);

    General *mt_limi = new General(this, "mt_limi", "shu+jin", 3);
    mt_limi->addSkill(new MTJijing);
    mt_limi->addSkill(new MTJijingRecord);
    mt_limi->addSkill(new MTBiancai);
    mt_limi->addSkill(new MTBiancaiLimit);
    mt_limi->addSkill(new MTChenxiao);
    mt_limi->addSkill(new MTChenxiaoProhibit);

    General *mt_liuyuan = new General(this, "mt_liuyuan", "qun+jin", 4);
    mt_liuyuan->addSkill(new MTZhulu);
    mt_liuyuan->addSkill(new MTZhuluDamage);
    mt_liuyuan->addSkill(new MTZhuluNoDistanceLimit);
    mt_liuyuan->addSkill(new MTZhengwang);
    mt_liuyuan->addSkill(new MTZhengwangRecord);
    mt_liuyuan->addSkill(new MTZhuizun);
    mt_liuyuan->addSkill(new MTZhuizunEffect);

    General *mt_shendiaochan = new General(this, "mt_shendiaochan", "god", 3, false);
    mt_shendiaochan->addSkill(new MTZhanhua);
    mt_shendiaochan->addSkill(new MTHongwu);

    addMetaObject<MTYinglveCard>();
    addMetaObject<MTHongwuCard>();
}
ADD_PACKAGE(Maotu)
