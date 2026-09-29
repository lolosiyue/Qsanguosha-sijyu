#include "ol-strengthen.h"
#include "skill-instance-utils.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "qt-collection-utils.h"
#include "maneuvering.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
//#include "json.h"
#include "wind.h"
#include "yjcm.h"
#include "yjcm2012.h"
#include "yjcm2013.h"
#include "yjcm2014.h"
//#include "mountain.h"
//#include "ai.h"
#include "exppattern.h"
#if !defined(QSAN_ENGINE_BUILD)
#include "clientstruct.h"
#endif
#include "server-info.h"
#include "room-state.h"
#include <QScopeGuard>

namespace {
// Nested liege responses must not replace the enclosing card-use reason.
auto olRequestScope()
{
    RoomState *state = Sanguosha->currentRoomState();
    const auto reason = state->getCurrentCardUseReason();
    const QString pattern = state->getCurrentCardUsePattern();
    return qScopeGuard([=] {
        state->setCurrentCardUseReason(reason);
        state->setCurrentCardUsePattern(pattern);
    });
}
}

// Ordinary conversions leave material payment and target dispatch to the card pipeline.
// PhaseChanging already belongs to the next phase; expire only the recorded, closed predecessor.
static bool finishedOLPlayPhase(Room *room, const QVariant &id, const ServerPlayer *actor)
{
    if (!actor || id.toLongLong() <= 0) return false;
    const QVariantMap phase = room->historyEvent(id.toLongLong());
    const QVariantMap value = phase.value("data").toMap();
    return phase.value("status").toString() == "finished" && phase.value("kind").toString() == "phase"
        && value.value("phase").toInt() == Player::Play && value.value("player").toString() == actor->objectName()
        && phase.value("turn_id") == room->historyScopes().value("turn_id");
}

// Build exact candidates before interception: waiving payment must not revive a spent quota.
static TriggerList usableOLCandidates(const Skill *skill, Room *room, TriggerEvent event,
    QVariant &data, const TriggerList &candidates)
{
    TriggerList result;
    for (auto it = candidates.constBegin(); it != candidates.constEnd(); ++it) {
        ServerPlayer *owner = it.key();
        if (!owner) continue;
        foreach (int id, owner->getValidSkillInstanceIds(skill->objectName())) {
            SkillContext ctx;
            ctx.owner = ctx.invoker = ctx.initiator = owner;
            ctx.skill_name = skill->objectName();
            ctx.instanceID = id;
            ctx.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(skill->objectName(), id));
            ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
            ctx.original_data = &data;
            ctx.current_event = event;
            if (!ctx.sourceRef.isValid() || !skill->isUsable(ctx)) continue;
            foreach (const QString &candidate, it.value()) {
                const int target = candidate.indexOf("->");
                result[owner] << SkillInstanceUtils::formatName(skill->objectName(), id)
                    + (target < 0 ? QString() : candidate.mid(target));
            }
        }
    }
    return result;
}

class OLCardConversion : public ViewAsSkillV2
{
public:
    OLCardConversion(const QString &name, const QString &cardName, const QString &material, bool allowResponse = false)
        : ViewAsSkillV2(name, 1), cardName(cardName), material(material), allowResponse(allowResponse)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        Card *card = Sanguosha->cloneCard(cardName);
        if (!card) return false;
        card->setSkillName(objectName());
        const bool play = request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
        const bool response = request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
        const bool allowed = play ? card->isAvailable(request.initiator)
            : allowResponse && response && !request.pattern.startsWith('@') && !request.pattern.startsWith('.')
                && Sanguosha->matchPattern(request.pattern, request.initiator, card);
        delete card;
        return allowed;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && candidate->getEffectiveId() >= 0 && !candidate->hasFlag("using")
            && (request.initiator->hasCard(candidate)
                || request.initiator->getHandPile().contains(candidate->getEffectiveId()))
            && request.selectedCardIds.isEmpty()
            && ExpPattern(material).match(request.initiator, candidate);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *materialCard = Sanguosha->getCard(request.selectedCardIds.first());
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        if (!canSelectCard(selection, materialCard)) return nullptr;
        Card *card = Sanguosha->cloneCard(cardName, materialCard->getSuit(), materialCard->getNumber());
        if (card) {
            card->addSubcard(materialCard);
            card->setSkillName(objectName());
        }
        return card;
    }

    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        // Revalidate the selected material after cost hooks; the ordinary card pipeline moves it.
        const Card *card = createCard(request);
        const bool valid = card != nullptr;
        delete card;
        return valid;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        Card *card = Sanguosha->cloneCard(cardName);
        const QString key = card ? card->getClassName() : QString();
        delete card;
        return key;
    }

private:
    QString cardName;
    QString material;
    bool allowResponse;
};

class OLHujia : public Hujia
{
public:
	OLHujia() : Hujia("olhujia")
	{
	}
};

class OLHujiaDraw : public TriggerSkillV2
{
public:
    OLHujiaDraw() : TriggerSkillV2("#olhujia$") { events << CardUsed << CardResponded << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        // The Wei responder makes the choice; quota and provenance belong to the selected lord's helper instance.
        ctx.initiator = ctx.invoker;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (active.bypass_cost && active.activationRef == ctx.activationRef) addUsage(active);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if ((event != CardUsed && event != CardResponded) || !player || !player->isAlive()
            || player->getKingdom() != "wei" || player->hasFlag("CurrentPlayer")) return result;
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        if (!card || !card->isKindOf("Jink")) return result;
        foreach (ServerPlayer *lord, room->getOtherPlayers(player))
            if (lord->isAlive() && lord->hasLordSkill("olhujia")) result[lord] << objectName() + "->" + lord->objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.invoker || !ctx.invoker->isAlive()) return false;
        if (!room->askForPlayerChosen(ctx.invoker, QList<ServerPlayer *>() << ctx.owner, objectName(), "@olhujia-draw", true)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = ctx.invoker;
        log.to << target;
        log.arg = ctx.owner->isWeidi() ? "weidi" : "olhujia";
        room->sendLog(log);
        if (ctx.owner->isWeidi()) room->broadcastSkillInvoke("weidi");
        else room->broadcastSkillInvoke("hujia", 1 + qsanRandomBounded(2));
        room->notifySkillInvoked(ctx.owner, log.arg);
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) target->drawCards(amount, "olhujia");
        return false;
    }
};

OLJijiangCard::OLJijiangCard() : JijiangCard("oljijiang")
{
	mute = true;
}

class OLJijiangVS : public ViewAsSkillV2
{
public:
    OLJijiangVS() : ViewAsSkillV2("oljijiang$") {}

    static bool hasShuGenerals(const Player *player)
    {
        foreach (const Player *p, player->getAliveSiblings())
            if (p->getKingdom() == "shu") return true;
        return false;
    }
    bool isEnabledAtPlay(const Player *player) const
    {
        return player && hasShuGenerals(player) && Slash::IsAvailable(player);
    }
    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return player && hasShuGenerals(player)
            && (pattern.contains("slash") || pattern.contains("Slash") || pattern == "@jijiang")
            && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "spent", false).toBool()) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return isEnabledAtPlay(request.initiator);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && hasShuGenerals(request.initiator)
            && (request.pattern.contains("slash", Qt::CaseInsensitive) || request.pattern == "@jijiang");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        return slash;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        const auto requestScope = olRequestScope();
        ServerPlayer *liubei = ctx.invoker;
        if (!liubei || !liubei->isAlive()) return false;
        if (!liubei->isLord() && liubei->hasSkill("weidi"))
            room->broadcastSkillInvoke("weidi");
        else {
            int r = 1 + qsanRandomBounded(2);
            if (!liubei->hasInnateSkill("jijiang") && liubei->getMark("ruoyu") > 0)
                r += 2;
            else if (liubei->isJieGeneral())
                r = qsanRandomBounded(2) + 5;
            room->broadcastSkillInvoke("jijiang", r);
        }
        room->notifySkillInvoked(liubei, objectName());
        QList<ServerPlayer *> marked, targets = ctx.targets;
        for (ServerPlayer *target : targets) {
            if (!target->hasFlag("OLJijiangTarget")) {
                target->setFlags("OLJijiangTarget");
                marked << target;
            }
        }
        const auto restoreHints = qScopeGuard([marked] {
            for (ServerPlayer *target : marked) target->setFlags("-OLJijiangTarget");
        });
        for (ServerPlayer *liege : room->getLieges("shu", liubei)) {
            const Card *provided = room->askForCard(liege, "slash", "@oljijiang-slash:" + liubei->objectName(),
                QVariant::fromValue(liubei), Card::MethodResponse, liubei, false, "", true);
            if (!provided) continue;
            Card *slash = Sanguosha->cloneCard(provided->objectName(), provided->getSuit(), provided->getNumber());
            if (!slash) return false;
            slash->addSubcard(provided);
            slash->setSkillName(objectName());
            slash->setFlags("YUANBEN");
            slash->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
            slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
            slash->deleteLater();
            QList<ServerPlayer *> legal;
            foreach (ServerPlayer *target, targets)
                if (liubei->canSlash(target, slash, false)) legal << target;
            if (legal.isEmpty()) continue;
            targets = legal;
            ctx.targets = legal;
            ctx.updated_targets = legal;
            ctx.updated_card = slash;
            if (ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
                CardUseStruct use = ctx.original_data->value<CardUseStruct>();
                use.m_isOwnerUse = false;
                use.to = legal;
                *ctx.original_data = QVariant::fromValue(use);
            }
            if (ctx.initiator)
                ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "spent", true);
            return true;
        }
        if (ctx.initiator)
            ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "spent", true);
        return false;
    }
};

class OLJijiang : public TriggerSkillV2
{
public:
    OLJijiang() : TriggerSkillV2("oljijiang$")
    {
        events << CardAsked << CardUsed << CardResponded << EventPhaseChanging;
        view_as_skill = new OLJijiangVS;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override
    {
        return ctx.current_event == CardUsed || ctx.current_event == CardResponded ? ctx.invoker : ctx.owner;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging && player == ctx.owner
            && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive)
            ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "spent");
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardUsed && event != CardResponded) return false;
        if (!player || !player->isAlive() || player->hasFlag("CurrentPlayer") || player->getKingdom() != "shu") return true;
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        if (!card || !card->isKindOf("Slash")) return true;
        foreach (ServerPlayer *lord, room->getOtherPlayers(player)) {
            if (!lord->isAlive() || !lord->hasLordSkill(this) || lord->getMark("oljijiang-Clear") > 0) continue;
            foreach (int id, lord->getValidSkillInstanceIds(objectName())) {
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = lord;
                ctx.invoker = ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(lord->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                if (!ctx.sourceRef.isValid()) continue;
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
                if (!amountOk) ctx.amount = getBaseAmount();
                ctx.original_data = &data;
                ctx.current_event = event;
                contexts << ctx;
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *liubei, QVariant &data) const override
    {
        return event == CardAsked && liubei && liubei->hasLordSkill(this) && canAsk(room, liubei, data)
            ? TriggerList{{liubei, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *liubei, SkillContext &ctx) const override
    {
        if (event == CardUsed || event == CardResponded)
            return ctx.owner && ctx.invoker && ctx.owner->isAlive() && ctx.invoker->isAlive()
                && ctx.owner->hasLordSkill(this) && ctx.owner->getMark("oljijiang-Clear") <= 0
                && !ctx.invoker->hasFlag("CurrentPlayer") && ctx.invoker->getKingdom() == "shu";
        return canAsk(room, liubei, *ctx.original_data)
            && (liubei->hasFlag("qinwangjijiang") || room->askForSkillInvoke(liubei, objectName(), *ctx.original_data));
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *liubei, SkillContext &ctx) const override
    {
        if (event == CardUsed || event == CardResponded) {
            ServerPlayer *drawer = ctx.owner;
            if (!drawer || !ctx.invoker) return false;
            room->doAnimate(1, ctx.invoker->objectName(), drawer->objectName());
            room->addPlayerMark(drawer, "oljijiang-Clear");
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = ctx.invoker;
            log.to << drawer;
            log.arg = drawer->isWeidi() ? "weidi" : objectName();
            room->sendLog(log);
            if (drawer->isWeidi())
                room->broadcastSkillInvoke("weidi");
            else {
                int r = 1 + qsanRandomBounded(2);
                if (!drawer->hasInnateSkill("jijiang") && drawer->getMark("ruoyu") > 0)
                    r += 2;
                else if (drawer->isJieGeneral())
                    r += 4;
                room->broadcastSkillInvoke("jijiang", r);
            }
            room->notifySkillInvoked(drawer, objectName());
            const int amount = getEffectiveAmount(ctx);
            if (amount > 0) drawer->drawCards(amount, objectName());
            return false;
        }
        const auto requestScope = olRequestScope();
        if (!liubei->isLord() && liubei->hasSkill("weidi"))
            room->broadcastSkillInvoke("weidi");
        else {
            int r = 1 + qsanRandomBounded(2);
            if (!liubei->hasInnateSkill("jijiang") && liubei->getMark("ruoyu") > 0)
                r += 2;
            else if (liubei->isJieGeneral())
                r += 4;
            room->broadcastSkillInvoke("jijiang", r);
        }
        foreach (ServerPlayer *liege, room->getLieges("shu", liubei)) {
            const Card *slash = room->askForCard(liege, "slash", "@oljijiang-slash:" + liubei->objectName(),
                QVariant::fromValue(liubei), Card::MethodResponse, liubei, false, "", true);
            if (!slash) continue;
            room->setCardFlag(slash, "YUANBEN");
            room->provide(slash);
            return true;
        }
        return false;
    }

private:
    static bool canAsk(Room *room, ServerPlayer *liubei, const QVariant &data)
    {
        const QStringList patterns = data.toStringList();
        return patterns.size() >= 3 && patterns.first() == "slash" && patterns.at(2) == "response"
            && !patterns.at(1).contains("jijiang-slash") && !room->getLieges("shu", liubei).isEmpty();
    }
};

class OLPaoxiao : public TriggerSkillV2
{
public:
    OLPaoxiao() : TriggerSkillV2("olpaoxiao")
    {
        frequency = Compulsory;
        events << DamageCaused << CardOffset << EventPhaseChanging;
        global = true;
    }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    static void project(Room *room, ServerPlayer *player)
    {
        int count = 0;
        foreach (const QVariant &entry, player->getTag("olpaoxiao_effects").toList())
            if (entry.toMap().value("turn") == room->historyScopes().value("turn_id")) count += entry.toMap().value("amount").toInt();
        room->setPlayerMark(player, "&olpaoxiao_missed-Clear", count);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QVariant turn = room->historyScopes().value("turn_id");
            foreach (ServerPlayer *player, room->getAllPlayers(true)) {
                QVariantList receipts = player->getTag("olpaoxiao_effects").toList();
                for (int i = receipts.length() - 1; i >= 0; --i)
                    if (receipts.at(i).toMap().value("turn") == turn) receipts.removeAt(i);
                player->setTag("olpaoxiao_effects", receipts);
                project(room, player);
            }
        }
        return true;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        if (!receipt.contains("receipt")) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive() || !ref(receipt, "activation").isValid()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("olpaoxiao_effects").toList())
            if (entry.toMap().value("receipt") == receipt.value("receipt") && ref(entry.toMap(), "source") == ctx.sourceRef
                && ref(entry.toMap(), "activation") == ref(receipt, "activation")
                && entry.toMap().value("turn") == room->historyScopes().value("turn_id")) return true;
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DamageCaused) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || damage.from != player || !damage.to || !damage.to->isAlive()
            || !damage.card || !damage.card->isKindOf("Slash")) return true;
        foreach (const QVariant &entry, player->getTag("olpaoxiao_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("turn") != room->historyScopes().value("turn_id") || receipt.value("amount").toInt() <= 0) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = ref(receipt, "source");
            if (!ctx.sourceRef.isValid() || !ref(receipt, "activation").isValid()) continue;
            // This damage bonus was already granted; it does not reactivate a removed skill instance.
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.extra_data = receipt;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardOffset || !player || !player->isAlive() || !player->hasSkill(this)) return result;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.from == player && effect.card && effect.card->isKindOf("Slash")) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets << (event == DamageCaused ? ctx.original_data->value<DamageStruct>().to : ctx.invoker);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == CardOffset) {
            const int sequence = room->getTag("olpaoxiao_sequence").toInt() + 1;
            room->setTag("olpaoxiao_sequence", sequence);
            QVariantMap receipt{{"receipt", sequence}, {"amount", amount}, {"turn", room->historyScopes().value("turn_id")},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            QVariantList receipts = target->getTag("olpaoxiao_effects").toList();
            receipts << receipt;
            target->setTag("olpaoxiao_effects", receipts);
            project(room, target);
        } else {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to != target) return false;
            QVariantList receipts = ctx.owner->getTag("olpaoxiao_effects").toList();
            for (int i = receipts.length() - 1; i >= 0; --i)
                if (receipts.at(i).toMap().value("receipt") == ctx.extra_data.toMap().value("receipt")) receipts.removeAt(i);
            ctx.owner->setTag("olpaoxiao_effects", receipts);
            project(room, ctx.owner);
            LogMessage log;
            log.type = "#OLpaoxiaoDamage";
            log.from = ctx.invoker;
            log.to << target;
            log.arg = QString::number(damage.damage);
            damage.damage += amount;
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(ctx.invoker, objectName());
            ctx.original_data->setValue(damage);
        }
        return false;
    }
};

class OLPaoxiaoMod : public TargetModSkillV2
{
public:
    OLPaoxiaoMod() : TargetModSkillV2("#olpaoxiaomod") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // The related instance, rather than an owner-wide name lookup, grants residue.
        return ctx.modType == Residue ? CorrectSkillResult::unlimitedResidue()
            : CorrectSkillResult::noEffect();
    }
};
class OLTishen : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    OLTishen() : TriggerSkillV2("oltishen") { events << EventPhaseStart << EventSkillInvoking; frequency = Limited; limit_mark = "@oltishenMark"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        // Waiving payment never waives the quota, including an intercepted/skipped effect.
        if (active.activationRef.ownerObjectName == ctx.activationRef.ownerObjectName
            && active.activationRef.key.skillName == ctx.activationRef.key.skillName
            && active.activationRef.key.instanceID == ctx.activationRef.key.instanceID) {
            if (active.bypass_cost) addUsage(active);
            if (ctx.owner->getMark(limit_mark) > 0) room->removePlayerMark(ctx.owner, limit_mark);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != EventPhaseStart) return result;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Start && player->isWounded())
            result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.invoker->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        if (ctx.owner->getMark("@oltishenMark") > 0) room->removePlayerMark(ctx.owner, "@oltishenMark");
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(ctx.invoker, objectName());
        const int count = target->getLostHp() * getEffectiveAmount(ctx);
        if (count > 0) {
            room->recover(target, RecoverStruct(ctx.invoker, nullptr, count, objectName()));
            target->drawCards(count, objectName());
        }
        return false;
    }
};
class OLLongdan : public ViewAsSkillV2
{
public:
    OLLongdan() : ViewAsSkillV2("ollongdan", 1) { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return player->isWounded() || Slash::IsAvailable(player) || Analeptic::IsAvailable(player);
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
        if (request.pattern.startsWith('.') || request.pattern.startsWith('@')) return false;
        for (const QString &name : {QString("slash"), QString("jink"), QString("peach"), QString("analeptic")}) {
            Card *card = Sanguosha->cloneCard(name);
            const bool matches = card && Sanguosha->matchPattern(request.pattern, player, card)
                && (name != "peach" || player->getMark("Global_PreventPeach") == 0);
            delete card;
            if (matches) return true;
        }
        return false;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!candidate || !request.initiator || !request.selectedCardIds.isEmpty()
            || candidate->getEffectiveId() < 0 || candidate->hasFlag("using")
            || (!request.initiator->hasCard(candidate)
                && !request.initiator->getHandPile().contains(candidate->getEffectiveId()))) return false;
        Card *converted = convert(candidate);
        if (!converted) return false;
        const bool matches = request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? converted->isAvailable(request.initiator)
            : Sanguosha->matchPattern(request.pattern, request.initiator, converted)
                && (!converted->isKindOf("Peach") || request.initiator->getMark("Global_PreventPeach") == 0);
        delete converted;
        return matches;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        return canSelectCard(selection, material) ? convert(material) : nullptr;
    }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        // Revalidate the selected material after cost hooks; the ordinary card pipeline moves it.
        const Card *card = createCard(request);
        const bool valid = card != nullptr;
        delete card;
        return valid;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        const Card *card = createCard(request);
        const QString key = card ? card->getClassName() : QString();
        delete card;
        return key;
    }
private:
    Card *convert(const Card *material) const
    {
        QString name;
        if (material->isKindOf("Slash")) name = "jink";
        else if (material->isKindOf("Jink")) name = "slash";
        else if (material->isKindOf("Peach")) name = "analeptic";
        else if (material->isKindOf("Analeptic")) name = "peach";
        if (name.isEmpty()) return nullptr;
        Card *card = Sanguosha->cloneCard(name, material->getSuit(), material->getNumber());
        if (card) {
            // Material and suit/number survive conversion into the ordinary pipeline.
            card->addSubcard(material);
            card->setSkillName(objectName());
        }
        return card;
    }
};
class OLYajiao : public TriggerSkillV2
{
public:
    OLYajiao() : TriggerSkillV2("olyajiao") { events << CardUsed << CardResponded; frequency = Frequent; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this) || room->getCurrent() == player) return result;
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        const bool hand = event == CardUsed ? data.value<CardUseStruct>().m_isHandcard : data.value<CardResponseStruct>().m_isHandcard;
        if (card && card->getTypeId() != Card::TypeSkill && hand) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        const Card *card = event == CardUsed ? ctx.original_data->value<CardUseStruct>().card
            : ctx.original_data->value<CardResponseStruct>().m_card;
        if (!card) return false;
        QVariantMap details;
        details.insert("type", int(card->getTypeId()));
        ctx.extra_data = details;
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap details = ctx.extra_data.toMap();
        const QString stage = details.value("stage").toString();
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (stage == "give") {
            const int id = details.value("id", -1).toInt();
            if (id >= 0 && room->getCardPlace(id) == Player::DrawPile) {
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), target->objectName(), objectName(), "");
                room->obtainCard(target, Sanguosha->getCard(id), reason, true);
            }
            return false;
        }
        if (stage == "discard") {
            if (!target->inMyAttackRange(ctx.invoker) || !ctx.invoker->canDiscard(target, "hej")) return false;
            const int id = room->askForCardChosen(ctx.invoker, target, "hej", objectName(), false, Card::MethodDiscard);
            if (id >= 0 && room->getCardOwner(id) == target && ctx.invoker->canDiscard(target, id))
                room->throwCard(Sanguosha->getCard(id), room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : target, ctx.invoker);
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        // Amount counts inspections; each resolved branch concerns that inspection's one physical card.
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.invoker->isAlive(); ++i) {
            const QList<int> ids = room->getNCards(1, false);
            if (ids.isEmpty()) break;
            const int id = ids.first();
            try {
                CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                    CardMoveReason(CardMoveReason::S_REASON_TURNOVER, ctx.invoker->objectName(), objectName(), ""));
                room->moveCardsAtomic(move, true);
            } catch (...) {
                if (room->getCardPlace(id) == Player::PlaceTable
                    || (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id))) room->returnToTopDrawPile(ids);
                throw;
            }
            if (room->getCardPlace(id) != Player::PlaceTable) continue;
            room->returnToTopDrawPile(ids);
            if (!ctx.invoker->isAlive()) break;
            const bool match = int(Sanguosha->getCard(id)->getTypeId()) == details.value("type").toInt();
            QList<ServerPlayer *> candidates;
            foreach (ServerPlayer *other, room->getAlivePlayers())
                if (match || (other->inMyAttackRange(ctx.invoker) && ctx.invoker->canDiscard(other, "hej"))) candidates << other;
            if (candidates.isEmpty()) continue;
            ServerPlayer *recipient = nullptr;
            if (match) room->fillAG(ids, ctx.invoker);
            try {
                recipient = room->askForPlayerChosen(ctx.invoker, candidates, match ? objectName() : "olyajiao_discard",
                    match ? "@olyajiao-give" : "@olyajiao-discard", true, true);
            } catch (...) { if (match) room->clearAG(ctx.invoker); throw; }
            if (match) room->clearAG(ctx.invoker);
            if (!recipient) continue;
            SkillContext branch = ctx;
            QVariantMap state;
            state.insert("stage", match ? "give" : "discard");
            state.insert("id", id);
            branch.extra_data = state;
            branch.targets = QList<ServerPlayer *>() << recipient;
            skillEffect(event, room, owner, branch, recipient);
        }
        return false;
    }
};

class OLLeiji : public TriggerSkillV2
{
public:
    OLLeiji() : TriggerSkillV2("olleiji") { events << CardResponded << CardUsed << FinishJudge; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return result;
        if (event == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (!judge || !judge->card || judge->who != player) return result;
            if (Sanguosha->translate(judge->reason) == "暴虐") return result;
            if (judge->card->getSuit() == Card::Club || judge->card->getSuit() == Card::Spade) result[player] << objectName();
        } else {
            const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
            if (card && (card->isKindOf("Jink") || card->isKindOf("Lightning"))) result[player] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantMap details;
        if (event == FinishJudge) {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (!judge || !judge->card) return false;
            details.insert("finished", true);
            details.insert("club", judge->card->getSuit() == Card::Club);
        } else if (!ctx.invoker->askForSkillInvoke(this)) return false;
        ctx.extra_data = details;
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap details = ctx.extra_data.toMap();
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (details.value("damage").toBool()) {
            const int count = details.value("club").toBool() ? amount : 2 * amount;
            room->damage(DamageStruct(objectName(), ctx.invoker, target, count, DamageStruct::Thunder));
            return false;
        }
        if (!details.value("finished").toBool()) {
            room->broadcastSkillInvoke(objectName());
            JudgeStruct judge;
            judge.pattern = ".|black";
            judge.good = true;
            judge.reason = objectName();
            judge.who = target;
            room->judge(judge);
            return false;
        }
        if (details.value("club").toBool()) room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        if (!ctx.invoker->isAlive()) return false;
        const int count = details.value("club").toBool() ? amount : 2 * amount;
        ServerPlayer *victim = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(),
            "@olleiji-invoke:" + QString::number(count), false, true);
        if (victim) {
            SkillContext damage = ctx;
            QVariantMap branch = details;
            branch.insert("damage", true);
            damage.extra_data = branch;
            damage.targets = QList<ServerPlayer *>() << victim;
            skillEffect(event, room, owner, damage, victim);
        }
        return false;
    }
};

class OLGuidao : public TriggerSkillV2
{
public:
    OLGuidao() : TriggerSkillV2("olguidao") { events << AskForRetrial; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (player && player->isAlive() && player->hasSkill(this) && !player->isNude()
            && judge && judge->who && judge->card) result[player] << objectName();
        return result;
    }
    bool materialValid(Room *room, const SkillContext &ctx) const
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        const int id = ctx.extra_data.toMap().value("id", -1).toInt();
        if (!judge || !judge->card || id < 0 || room->getCardOwner(id) != ctx.invoker
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        const Card *card = Sanguosha->getCard(id);
        return !card->hasFlag("using") && !ctx.invoker->isCardLimited(card, Card::MethodResponse) && card->isBlack();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge || !judge->who || !judge->card) return false;
        const QString color = "black";
        const QString prompt = QStringList({"@olguidao-card", judge->who->objectName(), objectName(), judge->reason,
            QString::number(judge->card->getEffectiveId())}).join(":");
        // Retrial owns the atomic card movement; selection must not consume the response material yet.
        const Card *card = room->askForCard(ctx.invoker, ".|" + color, prompt, *ctx.original_data,
            Card::MethodNone, judge->who, true, objectName());
        if (!card || (card->isVirtualCard() && card->subcardsLength() != 1)) return false;
        QVariantMap details;
        details.insert("id", card->getEffectiveId());
        ctx.extra_data = details;
        ctx.targets = QList<ServerPlayer *>() << judge->who;
        return materialValid(room, ctx);
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override { return materialValid(room, ctx); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.extra_data.toMap().value("draw").toBool()) {
            if (getEffectiveAmount(ctx) > 0) target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (getEffectiveAmount(ctx) <= 0 || !judge || judge->who != target || !materialValid(room, ctx)) return false;
        const Card *card = Sanguosha->getCard(ctx.extra_data.toMap().value("id").toInt());
        room->broadcastSkillInvoke(objectName());
        room->retrial(card, ctx.invoker, judge, objectName(), true);
        if (card->getSuit() == Card::Spade && card->getNumber() >= 2 && card->getNumber() <= 9 && ctx.invoker->isAlive()) {
            SkillContext draw = ctx;
            QVariantMap details;
            details.insert("draw", true);
            draw.extra_data = details;
            draw.targets = QList<ServerPlayer *>() << ctx.invoker;
            skillEffect(event, room, owner, draw, ctx.invoker);
        }
        return false;
    }
};

// A phase affordance is a child of one exact lord instance. A second phase lease never retires the first.
class OLLordAttachment : public TriggerSkillV2
{
public:
    OLLordAttachment(const QString &name, const QString &helper) : TriggerSkillV2(name), helper(helper)
    {
        events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill << EventLoseSkill << EventPhaseChanging;
        global = true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const QString key = objectName() + "_attached";
        const QVariant phase = room->historyScopes().value("phase_id");
        QVariantList leases = room->getTag(key).toList();
        const bool expire = (event == EventPhaseEnd && player && player->getPhase() == Player::Play)
            || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play);
        QList<SkillInstanceRef> retired;
        for (int i = leases.length() - 1; i >= 0; --i) {
            const QVariantMap lease = leases.at(i).toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(lease.value("owner").toString(), true);
            const SkillInstanceRef child(lease.value("owner").toString(), SkillInstanceKey(helper, lease.value("id").toInt()));
            if (!holder || !holder->hasSkillInstance(helper, child.key.instanceID) || (expire && (event == EventPhaseEnd ? lease.value("phase") == phase
                : finishedOLPlayPhase(room, lease.value("phase"), player)))) {
                retired << child;
                leases.removeAt(i);
            }
        }
        room->setTag(key, leases);
        foreach (const SkillInstanceRef &child, retired) {
            bool retained = false;
            foreach (const QVariant &entry, leases)
                if (entry.toMap().value("owner").toString() == child.ownerObjectName && entry.toMap().value("id").toInt() == child.key.instanceID) retained = true;
            if (!retained) room->detachAttachedSkill(child);
        }
        if (expire || phase.toLongLong() <= 0 || (event != EventPhaseStart && event != EventAcquireSkill)) return true;
        foreach (ServerPlayer *holder, room->getAlivePlayers()) {
            if (holder->getPhase() != Player::Play || (event == EventPhaseStart ? holder != player
                : room->historyEvent(phase.toLongLong()).value("data").toMap().value("player").toString() != holder->objectName())) continue;
            foreach (ServerPlayer *lord, room->getOtherPlayers(holder)) {
                if (!lord->hasLordSkill(this, true)) continue;
                foreach (int id, lord->getValidSkillInstanceIds(objectName())) {
                    const SkillInstanceRef parent(lord->objectName(), SkillInstanceKey(objectName(), id));
                    const SkillInstanceRef child = room->attachSkillToPlayer(holder, helper, parent);
                    if (!child.isValid()) continue;
                    const QVariantMap lease{{"owner", holder->objectName()}, {"id", child.key.instanceID}, {"phase", phase}};
                    leases = room->getTag(key).toList();
                    if (!leases.contains(lease)) leases << lease;
                    room->setTag(key, leases);
                }
            }
        }
        return true;
    }
private:
    QString helper;
};

static SkillInstanceRef olAttachedParent(const ActiveSkillRequest &request)
{
    if (!request.initiator || !request.activationRef.isValid() || request.activationRef.ownerObjectName != request.initiator->objectName()) return SkillInstanceRef();
    const SkillInstance *instance = request.initiator->findSkillInstance(request.activationRef.key.skillName, request.activationRef.key.instanceID);
    return instance ? instance->parentRef : SkillInstanceRef();
}

OLHuangtianCard::OLHuangtianCard()
{
    setSkillName("olhuangtian_attach");
    will_throw = false;
    handling_method = Card::MethodNone;
    mute = true;
}

class OLHuangtianViewAsSkill : public ViewAsSkillV2
{
public:
    OLHuangtianViewAsSkill() : ViewAsSkillV2("olhuangtian_attach", 1) { attached_lord_skill = true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLHuangtianCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getKingdom() == "qun"
            && olAttachedParent(request).key.skillName == "olhuangtian";
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using")
            && request.initiator->hasCard(card)
            && (card->isKindOf("Jink") || (card->getSuit() == Card::Spade && request.initiator->handCards().contains(card->getEffectiveId())));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        const SkillInstanceRef parent = olAttachedParent(request);
        return parent.isValid() && selected.isEmpty() && target && target->isAlive() && target != request.initiator
            && target->objectName() == parent.ownerObjectName && target->hasLordSkill("olhuangtian")
            && target->getValidSkillInstanceIds(parent.key.skillName).contains(parent.key.instanceID);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1 && canSelectTarget(request, QList<const Player *>(), selected.first());
    }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override { return cardSelectionFeasible(request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || !ctx.use_card || ctx.use_card->getSubcards().length() != 1) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        Room *room = target->getRoom();
        if (room->getCardOwner(id) != ctx.invoker || !ctx.invoker->hasCard(Sanguosha->getCard(id))
            || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        if (target->isWeidi()) room->broadcastSkillInvoke("weidi", -1, target);
        else room->broadcastSkillInvoke("huangtian", qsanRandomBounded(2) + 5, target);
        room->giveCard(ctx.invoker, target, Sanguosha->getCard(id), "olhuangtian", true);
        return ContinueEffects;
    }
};

class OLHuangtian : public OLLordAttachment
{
public:
    OLHuangtian() : OLLordAttachment("olhuangtian$", "olhuangtian_attach") { }
};

OLGuhuoCard::OLGuhuoCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

static void showGuhuoBox(Room *room, const QString &phase, ServerPlayer *yuji,
	const QString &declared, int realId)
{
	JsonArray args;
	args << QStringLiteral("guhuo_box") << phase
		 << (yuji ? yuji->objectName() : QString()) << declared << realId;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

bool OLGuhuoCard::olguhuo(ServerPlayer *yuji) const
{
	Room *room = yuji->getRoom();
	room->setTag("OLGuhuoType", user_string);
	showGuhuoBox(room, "declare", yuji, user_string, -1);

	QList<ServerPlayer *> questioned,aps = room->getOtherPlayers(yuji);
	foreach (ServerPlayer *player, aps) {
		QString choice = "noquestion+question";
		if (player->hasSkill("chanyuan")) {
			room->sendCompulsoryTriggerLog(player, "chanyuan", true, true);
			choice = "noquestion";
		}
		choice = room->askForChoice(player, "olguhuo", choice, QVariant::fromValue(yuji));
		if (choice == "question"){
			room->setEmotion(player, "question");
			questioned << player;
		}else
			room->setEmotion(player, "no-question");
		LogMessage log;
		log.type = "#GuhuoQuery";
		log.from = player;
		log.arg = choice;
		room->sendLog(log);
	}

	LogMessage log;
	log.type = "$GuhuoResult";
	log.from = yuji;
	log.card_str = QString::number(subcards.first());
	room->sendLog(log);

	showGuhuoBox(room, "reveal", yuji, user_string, subcards.first());
	room->getThread()->delay(2000);
	showGuhuoBox(room, "clear", yuji, QString(), -1);

	QList<CardsMoveStruct> moves;
	bool success = false;
	if (questioned.isEmpty()) {
		success = true;
		CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "olguhuo");
		CardsMoveStruct move(subcards, yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
		moves.append(move);
		room->moveCardsAtomic(moves, true);
	} else {
		const Card *card = Sanguosha->getCard(subcards.first());
		if (user_string == "peach+analeptic")
			success = card->objectName() == yuji->getTag("OLGuhuoSaveSelf").toString();
		else if (user_string == "slash")
			success = card->objectName().contains("slash");
		else if (user_string == "normal_slash")
			success = card->objectName() == "slash";
		else
			success = card->match(user_string);
		if (success) {
			CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "olguhuo");
			CardsMoveStruct move(subcards, yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
			moves.append(move);
			room->moveCardsAtomic(moves, true);
		} else {
			room->moveCardTo(this, yuji, nullptr, Player::DiscardPile,
				CardMoveReason(CardMoveReason::S_REASON_PUT, yuji->objectName(), "", "olguhuo"), true);
		}
	}
	foreach (ServerPlayer *player, aps) {
		room->setEmotion(player, ".");
		if(questioned.contains(player)){
			if(success){
				if (!player->canDiscard(player, "he")||!room->askForDiscard(player, "olguhuo", 1, 1, true, true, "olguhuo-discard"))
					room->loseHp(HpLostStruct(player, 1, "olguhuo", yuji));
				if (player->isAlive())
					room->acquireSkill(player, "chanyuan");
			}else{
				player->drawCards(1, "olguhuo");
			}
		}
	}
	yuji->removeTag("OLGuhuoSaveSelf");
	yuji->removeTag("OLGuhuoSlash");
	room->addPlayerMark(yuji, "olguhuo-Clear");
	return success;
}

bool OLGuhuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}

	const Card *_card = Self->getTag("olguhuo").value<const Card *>();
	if (_card == nullptr)
		return false;

	card = Sanguosha->cloneCard(_card);
	card->setCanRecast(false);
	card->deleteLater();
	return card->targetFilter(targets, to_select, Self);
}

bool OLGuhuoCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetFixed();
	}

	const Card *_card = Self ? Self->getTag("olguhuo").value<const Card *>() : nullptr;
	if (_card == nullptr)
		return false;

	card = Sanguosha->cloneCard(_card);
	card->setCanRecast(false);
	card->deleteLater();
	return card->targetFixed();
}

bool OLGuhuoCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}

	const Card *_card = Self->getTag("olguhuo").value<const Card *>();
	if (_card == nullptr)
		return false;

	card = Sanguosha->cloneCard(_card);
	card->setCanRecast(false);
	card->deleteLater();
	return card->targetsFeasible(targets, Self);
}

const Card *OLGuhuoCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer *yuji = card_use.from;
	Room *room = yuji->getRoom();

	QString to_guhuo = user_string;
	if ((user_string.contains("slash") || (user_string.contains("Slash")))
		&& Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		QStringList guhuo_list;
		static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
		foreach (const Slash *slash, slashs) {
			QString name = slash->objectName();
			if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
			guhuo_list << name;
		}

		if (guhuo_list.isEmpty())
			guhuo_list << "slash";
		to_guhuo = room->askForChoice(yuji, "olguhuo_slash", guhuo_list.join("+"));
		yuji->setTag("OLGuhuoSlash", QVariant(to_guhuo));
	}
	room->broadcastSkillInvoke("olguhuo");

	LogMessage log;
	log.type = card_use.to.isEmpty() ? "#GuhuoNoTarget" : "#Guhuo";
	log.from = yuji;
	log.to = card_use.to;
	log.arg = to_guhuo;
	log.arg2 = "olguhuo";

	room->sendLog(log);

	if (olguhuo(card_use.from)) {
		Card *card = Sanguosha->getCard(subcards.first());
		Card *use_card;
		if (to_guhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_guhuo = card->objectName();
		} else if (to_guhuo == "normal_slash")
			to_guhuo = "slash";
		if (to_guhuo.startsWith(card->objectName()))
			use_card = card;
		else{
			use_card = Sanguosha->cloneCard(to_guhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("olguhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
		}

		foreach (ServerPlayer *to, card_use.to) {
			const Skill *skill = room->isProhibited(card_use.from, to, use_card);
			if (skill) {
				log.from = to;
				log.type = "#SkillAvoid";
				if (skill->isVisible()) {
					log.arg = skill->objectName();
					log.arg2 = use_card->objectName();
					room->sendLog(log);

					room->broadcastSkillInvoke(skill->objectName());
					room->notifySkillInvoked(to, skill->objectName());
				} else {
					skill = Sanguosha->getMainSkill(skill->objectName());
					if (skill && skill->isVisible()) {
						log.arg = skill->objectName();
						if (to->hasSkill(skill)) {
							log.arg2 = objectName();
							room->sendLog(log);

							room->broadcastSkillInvoke(skill->objectName());
							room->notifySkillInvoked(to, skill->objectName());
						} else if (yuji->hasSkill(skill)) {
							log.type = "#SkillAvoidFrom";
							log.from = yuji;
							log.to.clear();
							log.to << to;
							log.arg2 = objectName();
							room->sendLog(log);

							room->broadcastSkillInvoke(skill->objectName());
							room->notifySkillInvoked(yuji, skill->objectName());
						}
					}
				}
				card_use.to.removeOne(to);
			}
		}
		return use_card;
	}
	return nullptr;
}

const Card *OLGuhuoCard::validateInResponse(ServerPlayer *yuji) const
{
	Room *room = yuji->getRoom();
	room->broadcastSkillInvoke("olguhuo");

	QString to_guhuo;
	if (user_string == "peach+analeptic") {
		QStringList guhuo_list;
		static QList<const Peach *> peachs = Sanguosha->findChildren<const Peach *>();
		foreach (const Peach *peach, peachs) {
			QString name = peach->objectName();
			if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(peach->getPackage())) continue;
			guhuo_list << name;
			break;
		}
		static QList<const Analeptic *> anas = Sanguosha->findChildren<const Analeptic *>();
		foreach (const Analeptic *ana, anas) {
			QString name = ana->objectName();
			if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(ana->getPackage())) continue;
			guhuo_list << name;
			break;
		}

		if (guhuo_list.isEmpty())
			guhuo_list << "peach";
		to_guhuo = room->askForChoice(yuji, "olguhuo_saveself", guhuo_list.join("+"));
		yuji->setTag("OLGuhuoSaveSelf", QVariant(to_guhuo));
	} else if (user_string.contains("slash") || user_string.contains("Slash")) {
		QStringList guhuo_list;
		static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
		foreach (const Slash *slash, slashs) {
			QString name = slash->objectName();
			if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
			guhuo_list << name;
		}

		if (guhuo_list.isEmpty())
			guhuo_list << "slash";
		to_guhuo = room->askForChoice(yuji, "olguhuo_slash", guhuo_list.join("+"));
		yuji->setTag("OLGuhuoSlash", QVariant(to_guhuo));
	} else
		to_guhuo = user_string;

	LogMessage log;
	log.type = "#GuhuoNoTarget";
	log.from = yuji;
	log.arg = to_guhuo;
	log.arg2 = "olguhuo";
	room->sendLog(log);

	if (olguhuo(yuji)) {
		Card *card = Sanguosha->getCard(subcards.first());
		if (to_guhuo == "slash" && card->isKindOf("Slash"))
			to_guhuo = card->objectName();
		else if (to_guhuo == "normal_slash")
			to_guhuo = "slash";
		
		if (to_guhuo.startsWith(card->objectName())||card->objectName().startsWith(to_guhuo))
			return card;
		else{
			Card *use_card = Sanguosha->cloneCard(to_guhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("olguhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
			return use_card;
		}
	}
	return nullptr;
}

class OLGuhuo : public OneCardViewAsSkill
{
public:
	OLGuhuo() : OneCardViewAsSkill("olguhuo")
	{
		response_or_use = true;
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::guhuo(objectName(), true, true, true, false, false, false);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		bool current = false;
		QList<const Player *> players = player->getAliveSiblings();
		players.append(player);
		foreach (const Player *p, players) {
			if (p->getPhase() != Player::NotActive) {
				current = true;
				break;
			}
		}
		if (!current) return false;

		if (player->isKongcheng() || player->getMark("olguhuo-Clear") > 0
			|| pattern.startsWith(".") || pattern.startsWith("@"))
			return false;
		if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
		for (int i = 0; i < pattern.length(); i++) {
			QChar ch = pattern[i];
			if (ch.isUpper() || ch.isDigit()) return false; // This is an extremely dirty hack!! For we need to prevent patterns like 'BasicCard'
		}
		return true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		bool current = false;
		foreach (const Player *p, player->getAliveSiblings(true)) {
			if (p->getPhase() != Player::NotActive) {
				current = true;
				break;
			}
		}
		if (!current) return false;
		return !player->isKongcheng() && player->getMark("olguhuo-Clear") <= 0;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
			|| Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
			OLGuhuoCard *card = new OLGuhuoCard;
			card->setUserString(Sanguosha->currentRoomState()->getCurrentCardUsePattern());
			card->addSubcard(originalCard);
			return card;
		}

		const Card *c = Self->getTag("olguhuo").value<const Card *>();
		if (c) {
			OLGuhuoCard *card = new OLGuhuoCard;
			card->setUserString(c->objectName());
			card->addSubcard(originalCard);
			return card;
		}
		return nullptr;
	}

	bool isEnabledAtNullification(const ServerPlayer *player) const
	{
		ServerPlayer *current = player->getRoom()->getCurrent();
		if (!current || current->isDead() || current->getPhase() == Player::NotActive) return false;
		return (!player->isKongcheng() || !player->getHandPile().isEmpty()) && player->getMark("olguhuo-Clear") <= 0;
	}
};

class OLShebian : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLShebian() : TriggerSkillV2("olshebian") { events << TurnedOver; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && room->canMoveField("e")) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.invoker->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.invoker->isAlive() && room->canMoveField("e"); ++i)
            room->moveField(ctx.invoker, objectName(), false, "e");
        return false;
    }
};
OLQimouCard::OLQimouCard()
{
    setSkillName("olqimou");
	target_fixed = true;
}


class OLQimou : public ViewAsSkillV2
{
public:
    OLQimou() : ViewAsSkillV2("olqimou") { frequency = Limited; limit_mark = "@olqimouMark"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLQimouCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getHp() > 0;
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        QStringList choices;
        for (int i = 1; i <= ctx.invoker->getHp(); ++i) choices << QString::number(i);
        if (choices.isEmpty()) return false;
        const int number = room->askForChoice(ctx.invoker, objectName(), choices.join("+")).toInt();
        if (number < 1 || number > ctx.invoker->getHp()) return false;
        ctx.extra_data = number;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        const int number = ctx.extra_data.toInt();
        if (!ctx.invoker->isAlive() || number < 1 || number > ctx.invoker->getHp()) return false;
        // Chosen HP is the payment; the resulting draw and turn modifiers are interceptable effects.
        room->loseHp(HpLostStruct(ctx.invoker, number, objectName(), ctx.invoker));
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.owner->getRoom();
        if (ctx.owner->getMark(limit_mark) > 0) room->removePlayerMark(ctx.owner, limit_mark);
        room->doSuperLightbox(ctx.invoker, objectName());
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = ctx.extra_data.toInt() * getEffectiveAmount(ctx);
        if (amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        target->drawCards(amount, objectName());
        if (target->isAlive()) {
            room->addDistance(target, -amount);
            room->addSlashCishu(target, amount);
        }
        return ContinueEffects;
    }
};
OLTianxiangCard::OLTianxiangCard() : TenyearTianxiangCard("oltianxiang")
{
    setSkillName("oltianxiang");
		handling_method = Card::MethodDiscard;
}

class OLTianxiang : public TriggerSkillV2
{
public:
    OLTianxiang() : TriggerSkillV2("oltianxiang") { events << DamageInflicted; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static bool material(Room *room, ServerPlayer *owner, int id)
    {
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        return card && card->getSuit() == Card::Heart && !card->hasFlag("using") && room->getCardOwner(id) == owner
            && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip) && owner->canDiscard(owner, id);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this) || data.value<DamageStruct>().to != player
            || room->getOtherPlayers(player).isEmpty()) return result;
        foreach (const Card *card, player->getCards("he"))
            if (material(room, player, card->getEffectiveId())) { result[player] << objectName(); break; }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *selected = room->askForCard(ctx.owner, ".|heart", "@oltianxiang", *ctx.original_data, Card::MethodNone);
        if (!selected || !material(room, ctx.owner, selected->getEffectiveId())) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@oltianxiang");
        if (!target) return false;
        ctx.extra_data = QVariantMap{{"card", selected->getEffectiveId()}, {"recipient", target->objectName()}};
        ctx.targets << ctx.owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toMap().value("card", -1).toInt();
        if (!material(room, ctx.owner, id)) return false;
        room->throwCard(Sanguosha->getCard(id), ctx.owner, nullptr);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        QVariantMap state = ctx.extra_data.toMap();
        if (state.value("resolution").toBool()) {
            if (room->askForChoice(ctx.owner, objectName(), "damage+losehp") == "damage") {
                room->damage(DamageStruct(objectName(), nullptr, target, amount));
                if (target->isAlive()) target->drawCards(qMin(5, target->getLostHp()) * amount, objectName());
            } else {
                room->loseHp(HpLostStruct(target, amount, objectName(), ctx.owner));
                const int id = state.value("card", -1).toInt();
                if (target->isAlive() && id >= 0 && room->getCardPlace(id) == Player::DiscardPile && !Sanguosha->getCard(id)->hasFlag("using"))
                    room->obtainCard(target, id, true);
            }
            return false;
        }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target) return false;
        // Prevention belongs to the original victim; the chosen recipient gets a separate effect hook.
        target->damageRevises(*ctx.original_data, -damage.damage);
        ServerPlayer *recipient = room->findPlayerByObjectName(state.value("recipient").toString());
        if (recipient && recipient->isAlive() && ctx.owner->isAlive()) {
            SkillContext resolution = ctx;
            state.insert("resolution", true);
            resolution.extra_data = state;
            skillEffect(event, room, ctx.owner, resolution, recipient);
        }
        return true;
    }
    int getEffectIndex(const ServerPlayer *player, const Card *) const override
    {
        int index = qsanRandomBounded(2) + 1;
        if (!player->hasInnateSkill(this) && player->hasSkill("olluoyan")) index += 2;
        return index;
    }
};

class OLHongyan : public FilterSkill
{
public:
	OLHongyan() : FilterSkill("olhongyan")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->getSuit() == Card::Spade;
	}

	const Card *viewAs(const Card *original) const
	{
		Card *new_card = Sanguosha->cloneCard(original->objectName(),Card::Heart,original->getNumber());
		new_card->setSkillName("olhongyan");
		return new_card;
	}

	int getEffectIndex(const ServerPlayer *, const Card *) const
	{
		return -2;
	}
};

class OLHongyanKeep : public MaxCardsSkillV2
{
public:
    OLHongyanKeep() : MaxCardsSkillV2("#olhongyan-keep") {}
    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        foreach (const Card *card, ctx.holder->getEquips()) {
            if (card->getSuit() == Card::Heart)
                return CorrectSkillResult::useAmount(ctx.holder->getMaxHp() * ctx.currentAmount);
        }
        return CorrectSkillResult::noEffect();
    }
};
class OLPiaoling : public TriggerSkillV2
{
public:
    OLPiaoling() : TriggerSkillV2("olpiaoling") { events << EventPhaseChanging; frequency = Frequent; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && data.value<PhaseChangeStruct>().to == Player::NotActive)
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this)) return false;
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.extra_data.isValid()) {
            const int id = ctx.extra_data.toInt();
            if (id < 0 || room->getCardPlace(id) != Player::DiscardPile) return false;
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), target->objectName(), objectName(), "");
            room->obtainCard(target, Sanguosha->getCard(id), reason, true);
            // The self-discard is the consequence of choosing self, not an invocation payment.
            if (target == ctx.invoker && target->isAlive() && target->canDiscard(target, "he"))
                room->askForDiscard(target, objectName(), 1, 1, false, true);
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.invoker->isAlive(); ++i) {
            JudgeStruct judge;
            judge.pattern = ".|heart";
            judge.who = target;
            judge.reason = objectName();
            room->judge(judge);
            if (!judge.card || judge.isBad() || !ctx.invoker->isAlive()) continue;
            const int id = judge.card->getEffectiveId();
            if (id < 0 || room->getCardPlace(id) != Player::DiscardPile) continue;
            ServerPlayer *recipient = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(),
                "@olpiaoling-invoke:" + judge.card->objectName(), true);
            if (recipient) {
                SkillContext give = ctx;
                give.extra_data = id;
                give.targets = QList<ServerPlayer *>() << recipient;
                skillEffect(event, room, owner, give, recipient);
            } else if (room->getCardPlace(id) == Player::DiscardPile) {
                LogMessage log;
                log.type = "$PutCard2";
                log.from = ctx.invoker;
                log.card_str = judge.card->toString();
                room->sendLog(log);
                // Judgement has already finished; the card now comes from the discard pile.
                CardMoveReason reason(CardMoveReason::S_REASON_PUT, ctx.invoker->objectName(), objectName(), "");
                CardsMoveStruct move(id, nullptr, nullptr, Player::DiscardPile, Player::DrawPile, reason);
                room->moveCardsAtomic(move, true);
            }
        }
        return false;
    }
};

class OLLuanjiVS : public ViewAsSkillV2
{
public:
    OLLuanjiVS() : ViewAsSkillV2("olluanji", 2) { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        ArcheryAttack card(Card::NoSuit, 0);
        card.setSkillName(objectName());
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && card.isAvailable(request.initiator);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !candidate || candidate->hasFlag("using") || candidate->getEffectiveId() < 0
            || request.selectedCardIds.length() >= 2 || request.selectedCardIds.contains(candidate->getEffectiveId())
            || (!request.initiator->handCards().contains(candidate->getEffectiveId())
                && !request.initiator->getHandPile().contains(candidate->getEffectiveId()))) return false;
        return request.selectedCardIds.isEmpty()
            || candidate->getSuit() == Sanguosha->getCard(request.selectedCardIds.first())->getSuit();
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 2) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        foreach (int id, request.selectedCardIds) {
            if (!canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        ArcheryAttack *card = new ArcheryAttack(Card::SuitToBeDecided, 0);
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request);
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "ArcheryAttack"; }
};

class OLLuanji : public TriggerSkillV2
{
public:
    OLLuanji() : TriggerSkillV2("olluanji") { events << CardUsed; view_as_skill = new OLLuanjiVS; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (player && player->isAlive() && player->hasSkill(this) && use.from == player
            && use.card && use.card->isKindOf("ArcheryAttack") && use.to.length() >= 2) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, use.to, objectName(), "@olluanji-remove", true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.to.contains(target) || getEffectiveAmount(ctx) <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        LogMessage log;
        log.type = "#QiaoshuiRemove";
        log.from = ctx.invoker;
        log.to << target;
        log.card_str = use.card->toString();
        log.arg = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(ctx.invoker, objectName());
        // Only mutate the accepted card use after the recipient effect hook.
        use.to.removeOne(target);
        ctx.original_data->setValue(use);
        return false;
    }
};
class OLXueyi : public TriggerSkillV2
{
public:
    OLXueyi(const QString &name) : TriggerSkillV2(name + "$"), xueyi(name) { events << GameStart << EventPhaseStart; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasLordSkill(this)) return result;
        if (event == GameStart) {
            foreach (ServerPlayer *other, room->getAlivePlayers())
                if (other->getKingdom() == "qun") { result[player] << objectName(); break; }
        } else if (player->getMark("&olyi") > 0 && player->getPhase() == (xueyi == "olxueyi" ? Player::RoundStart : Player::Play))
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == GameStart) {
            int count = 0;
            foreach (ServerPlayer *other, room->getAlivePlayers()) if (other->getKingdom() == "qun") ++count;
            ctx.extra_data = count * (xueyi == "olxueyi" ? 1 : 2);
        } else if (ctx.invoker->getMark("&olyi") <= 0 || !ctx.invoker->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == GameStart) return true;
        if (ctx.invoker->getMark("&olyi") <= 0) return false;
        // Yi is a public, spendable game resource; it is not a hidden activation counter.
        room->removePlayerMark(ctx.invoker, "&olyi");
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == GameStart) {
            room->sendCompulsoryTriggerLog(ctx.invoker, ctx.invoker->isWeidi() ? "weidi" : objectName(), true, true);
            room->addPlayerMark(target, "&olyi", ctx.extra_data.toInt() * amount);
        } else {
            room->broadcastSkillInvoke(objectName());
            target->drawCards(amount, objectName());
        }
        return false;
    }
private:
    QString xueyi;
};

class OLXueyiKeep : public MaxCardsSkillV2
{
public:
    OLXueyiKeep(const QString &name) : MaxCardsSkillV2("#" + name + "-keep$"), xueyi(name)
    {
        setBaseAmount(name == "olxueyi" ? 2 : 1);
    }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder || !ctx.holder->hasLordSkill(xueyi)) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.holder->getMark("&olyi") * ctx.currentAmount);
    }
private:
    QString xueyi;
};

class OLHuojiVs : public OLCardConversion
{
public:
    OLHuojiVs() : OLCardConversion("olhuoji", "fire_attack", ".|red") {}
};
class OLHuoji : public TriggerSkillV2
{
public:
    OLHuoji() : TriggerSkillV2("olhuoji") { events << CardEffected; view_as_skill = new OLHuojiVs; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 0; }
    bool usesEventPriority() const override { return true; }
    int getEffectIndex(const ServerPlayer *player, const Card *) const override
    {
        int index = qsanRandomBounded(2) + 1;
        if (player->getGeneralName().contains("pangtong") || player->getGeneral2Name().contains("pangtong")) index += 2;
        return index;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (player && player->isAlive() && effect.to == player && effect.from && effect.from->isAlive()
            && effect.from->hasSkill(this) && effect.card && effect.card->isKindOf("FireAttack"))
            result[effect.from] << objectName() + "->" + player->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.preferredTarget) return false;
        ctx.targets << ctx.preferredTarget;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        if (amount <= 0 || !effect.card || effect.from != ctx.invoker || effect.to != target) return false;
        // Preserve the ordinary trick's nullification/offset hooks before replacing only its card effect.
        if (effect.nullified) {
            LogMessage log;
            log.type = "#CardNullified";
            log.from = target;
            log.card_str = effect.card->toString();
            room->sendLog(log);
            return true;
        }
        target->setFlags("Global_NonSkillNullify");
        if (!effect.offset_card) effect.offset_card = room->isCanceled(effect);
        ctx.original_data->setValue(effect);
        if (effect.offset_card && !room->getThread()->trigger(CardOffset, room, effect.from, *ctx.original_data)) return true;
        room->getThread()->trigger(CardOnEffect, room, target, *ctx.original_data);
        effect = ctx.original_data->value<CardEffectStruct>();
        if (!effect.card || effect.to != target || effect.from != ctx.invoker) return true;
        if (!target->isAlive() || !ctx.invoker->isAlive() || target->isKongcheng()) return true;
        const Card *shown = target->getRandomHandCard();
        if (!shown) return true;
        const QString color = shown->getColorString();
        room->showCard(target, shown->getEffectiveId());
        if (!target->isAlive() || !ctx.invoker->isAlive()) return true;
        const Card *payment = room->askForCard(ctx.invoker, ".|" + color + "|.|hand",
            "olhuoji0:" + target->objectName() + "::" + color, *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        const QList<int> ids = payment ? payment->getSubcards() : QList<int>();
        const int id = payment && !payment->isVirtualCard() ? payment->getEffectiveId() : (ids.length() == 1 ? ids.first() : -1);
        const Card *material = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (material && !material->hasFlag("using") && room->getCardOwner(id) == ctx.invoker
            && room->getCardPlace(id) == Player::PlaceHand && material->getColorString() == color
            && (ctx.bypass_cost || ctx.invoker->canDiscard(ctx.invoker, id))) {
            if (!ctx.bypass_cost) room->throwCard(material, objectName(), ctx.invoker);
            if (target->isAlive() && ctx.invoker->isAlive()) room->damage(DamageStruct(effect.card, ctx.invoker, target, amount, DamageStruct::Fire));
        } else ctx.invoker->setFlags("FireAttackFailed_" + target->objectName());
        return true;
    }
};

class OLKanpoVs : public OLCardConversion
{
public:
    OLKanpoVs() : OLCardConversion("olkanpo", "nullification", ".|black", true) {}
};
class OLKanpo : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLKanpo() : TriggerSkillV2("olkanpo") { events << CardUsed; view_as_skill = new OLKanpoVs; }
    int getEffectIndex(const ServerPlayer *player, const Card *) const override
    {
        int index = qsanRandomBounded(2) + 1;
        if (player->getGeneralName().contains("pangtong") || player->getGeneral2Name().contains("pangtong")) index += 2;
        return index;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (player && player->isAlive() && player->hasSkill(this) && use.from == player
            && use.card && use.card->isKindOf("Nullification")) result[player] << objectName();
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const int voice = use.card->getSkillName().contains("olkanpo") ? 0 : getEffectIndex(ctx.invoker, use.card);
        room->sendCompulsoryTriggerLog(ctx.invoker, this, voice);
        if (!use.no_respond_list.contains("_ALL_TARGETS")) use.no_respond_list << "_ALL_TARGETS";
        ctx.original_data->setValue(use);
        return false;
    }
};
class OLCangzhuo : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLCangzhuo() : TriggerSkillV2("olcangzhuo") { events << EventPhaseProceeding; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this) || player->getPhase() != Player::Discard) return result;
        // Unknown history is not evidence of zero uses; never substitute the old listener mark.
        if (room->historyScopes().value("turn_id").toString() == "0"
            || room->countHistoryCards(player, "turn", "TrickCard") != 0) return result;
        foreach (const Card *card, player->getCards("h")) {
            if (card->isKindOf("TrickCard")) { result[player] << objectName(); break; }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> cards;
        foreach (const Card *card, target->getCards("h")) if (card->isKindOf("TrickCard")) cards << card->getEffectiveId();
        if (!cards.isEmpty()) {
            room->sendCompulsoryTriggerLog(ctx.invoker, objectName(), true, true);
            room->ignoreCards(target, cards);
        }
        return false;
    }
};
class OLLianhuan : public OLCardConversion
{
public:
    OLLianhuan() : OLCardConversion("ollianhuan", "iron_chain", ".|club") {}
};
class OLLianhuanMod : public TargetModSkillV2
{
public:
    OLLianhuanMod() : TargetModSkillV2("#ollianhuanmod", "IronChain") { frequency = NotFrequent; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == ExtraTarget ? CorrectSkillResult::useAmount(ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};
class OLNiepan : public TriggerSkillV2
{
public:
    OLNiepan() : TriggerSkillV2("olniepan")
    {
        events << AskForPeaches << EventSkillInvoking;
        frequency = Limited;
        limit_mark = "@olniepanMark";
        setBaseAmount(3);
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (active.activationRef == ctx.activationRef) {
            // The display token is a projection; the accepted instance owns the game quota.
            if (active.bypass_cost) addUsage(active);
            room->setPlayerMark(ctx.owner, limit_mark, 0);
        }
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == AskForPeaches && player && player->isAlive() && player->hasSkill(this)
            && data.value<DyingStruct>().who == player) result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        ctx.invoker->peiyin(this);
        room->doSuperLightbox(ctx.invoker, objectName());
        target->throwAllHandCardsAndEquips();
        if (!target->isAlive()) return false;
        if (target->isChained()) room->setPlayerChained(target);
        if (!target->faceUp()) target->turnOver();
        if (!target->isAlive()) return false;
        target->drawCards(amount, objectName());
        const int recover = qMin(amount, target->getMaxHp()) - target->getHp();
        if (target->isAlive() && recover > 0) room->recover(target, RecoverStruct(objectName(), ctx.invoker, recover));
        if (!target->isAlive()) return false;
        QStringList skills;
        for (const QString &skill : {QStringLiteral("bazhen"), QStringLiteral("olhuoji"), QStringLiteral("olkanpo")})
            if (!target->hasSkill(skill, true)) skills << skill;
        // This is a permanent acquired skill, not a helper tied to the spent limited source.
        if (!skills.isEmpty()) room->acquireSkillFromEffect(target, room->askForChoice(target, objectName(), skills.join("+")), ctx);
        return false;
    }
};

class OLJianchu : public TriggerSkillV2
{
public:
    OLJianchu() : TriggerSkillV2("oljianchu") { events << TargetSpecified; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(this) || use.from != player
            || !use.card || !use.card->isKindOf("Slash")) return result;
        foreach (ServerPlayer *target, use.to)
            if (target->isAlive() && player->canDiscard(target, "he")) result[player] << objectName() + "->" + target->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.preferredTarget;
        if (!target || !target->isAlive() || !ctx.invoker->canDiscard(target, "he")
            || !ctx.invoker->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        ctx.targets = QList<ServerPlayer *>() << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.invoker->isAlive()
            && ctx.invoker->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            if (id < 0 || room->getCardOwner(id) != target || !ctx.invoker->canDiscard(target, id)) break;
            const bool basic = Sanguosha->getCard(id)->isKindOf("BasicCard");
            room->throwCard(id, objectName(), target, ctx.invoker);
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (basic) {
                if (use.card && room->CardInTable(use.card) && target->isAlive()) target->obtainCard(use.card, true);
            } else {
                LogMessage log;
                log.type = "#NoJink";
                log.from = target;
                room->sendLog(log);
                if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
                ctx.original_data->setValue(use);
                room->addSlashCishu(ctx.invoker, 1);
            }
        }
        return false;
    }
};
class OLHanzhan : public TriggerSkillV2
{
public:
    OLHanzhan(const QString &name = "olhanzhan") : TriggerSkillV2(name) { events << AskforPindianCard; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const PindianStruct *pindian = data.value<PindianStruct *>();
        if (!pindian || !pindian->from || !pindian->to || !pindian->from->isAlive() || !pindian->to->isAlive()) return result;
        if (pindian->from->hasSkill(this) && !pindian->to_card && !pindian->to->isKongcheng())
            result[pindian->from] << objectName() + "->" + pindian->to->objectName();
        if (pindian->to->hasSkill(this) && !pindian->from_card && !pindian->from->isKongcheng())
            result[pindian->to] << objectName() + "->" + pindian->from->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        ServerPlayer *target = ctx.preferredTarget;
        if (!pindian || !target || !target->isAlive() || target->isKongcheng()
            || (target != pindian->from && target != pindian->to)
            || (target == pindian->from && pindian->from_card) || (target == pindian->to && pindian->to_card)
            || !ctx.invoker->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        ctx.targets = QList<ServerPlayer *>() << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        if (!pindian || target->isKongcheng() || getEffectiveAmount(ctx) <= 0) return false;
        const Card **slot = target == pindian->from ? &pindian->from_card : target == pindian->to ? &pindian->to_card : nullptr;
        if (!slot || *slot) return false;
        room->broadcastSkillInvoke(objectName());
        // The live pindian payload owns this choice; no player tag retains the pointer.
        *slot = target->getRandomHandCard();
        return false;
    }
};
SecondOLHanzhanCard::SecondOLHanzhanCard()
{
    setSkillName("secondolhanzhan");
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}



class SecondOLHanzhanVS : public ViewAsSkillV2
{
public:
    SecondOLHanzhanVS() : ViewAsSkillV2("secondolhanzhan", 1) { expand_pile = "#secondolhanzhan"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern == "@@secondolhanzhan";
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && getExpandPileCardIds(request.initiator).contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool pay(Room *room, SkillContext &, const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) && room->getCardPlace(request.selectedCardIds.first()) == Player::PlaceTable;
    }
};

class SecondOLHanzhan : public OLHanzhan
{
public:
    SecondOLHanzhan() : OLHanzhan("secondolhanzhan")
    {
        events << Pindian;
        view_as_skill = new SecondOLHanzhanVS;
    }
    int getPriority(TriggerEvent event) const override
    {
        return event == AskforPindianCard ? 3 : TriggerSkillV2::getPriority(event);
    }
    static QList<int> availableSlash(Room *room, const PindianStruct *pindian)
    {
        QList<int> ids;
        if (!pindian || !pindian->from_card || !pindian->to_card) return ids;
        const bool fromSlash = pindian->from_card->isKindOf("Slash"), toSlash = pindian->to_card->isKindOf("Slash");
        if (fromSlash && (!toSlash || pindian->from_number >= pindian->to_number) && room->CardInTable(pindian->from_card))
            ids << pindian->from_card->getEffectiveId();
        if (toSlash && (!fromSlash || pindian->to_number >= pindian->from_number) && room->CardInTable(pindian->to_card)
            && !ids.contains(pindian->to_card->getEffectiveId())) ids << pindian->to_card->getEffectiveId();
        return ids;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == AskforPindianCard) return OLHanzhan::triggerable(event, room, player, data);
        TriggerList result;
        const PindianStruct *pindian = data.value<PindianStruct *>();
        if (availableSlash(room, pindian).isEmpty()) return result;
        const QList<ServerPlayer *> owners = QList<ServerPlayer *>() << pindian->from << pindian->to;
        foreach (ServerPlayer *owner, owners)
            if (owner && owner->isAlive() && owner->hasSkill(this)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == AskforPindianCard) return OLHanzhan::cost(event, room, player, ctx);
        const QList<int> ids = availableSlash(room, ctx.original_data->value<PindianStruct *>());
        if (ids.isEmpty()) return false;
        // This is selection of an already-public pindian card, not a second skill activation.
        room->fillAG(ids, ctx.invoker);
        int id = -1;
        try { id = room->askForAG(ctx.invoker, ids, true, objectName(), "@secondolhanzhan"); }
        catch (...) { room->clearAG(ctx.invoker); throw; }
        room->clearAG(ctx.invoker);
        if (!ids.contains(id)) return false;
        ctx.extra_data = id;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == AskforPindianCard) return OLHanzhan::effectTarget(event, room, player, ctx, target);
        const int id = ctx.extra_data.toInt();
        if (getEffectiveAmount(ctx) <= 0 || !availableSlash(room, ctx.original_data->value<PindianStruct *>()).contains(id)) return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.invoker, objectName());
        room->obtainCard(target, id, true);
        return false;
    }
};
OLWulieCard::OLWulieCard() { setSkillName("olwulie"); mute = true; }

class OLWulie : public TriggerSkillV2
{
public:
    OLWulie() : TriggerSkillV2("olwulie")
    {
        events << EventPhaseStart << DamageInflicted << EventSkillInvoking;
        frequency = Limited;
        limit_mark = "@olwulieMark";
        global = true;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("olwulie_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt").toInt() == ctx.instanceID && receipt.value("remaining").toInt() > 0
                && ref(receipt, "source") == ctx.sourceRef && ref(receipt, "activation") == ref(ctx.extra_data.toMap(), "activation")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx) && (!ctx.activationRef.isValid() || isUsable(ctx));
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.skill_name == objectName() && active.activationRef.isValid()) {
                if (active.bypass_cost) addUsage(active);
                if (active.owner) room->removePlayerMark(active.owner, "@olwulieMark");
            }
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DamageInflicted) return false;
        if (!player || !player->isAlive() || data.value<DamageStruct>().to != player || data.value<DamageStruct>().damage <= 0) return true;
        foreach (const QVariant &entry, player->getTag("olwulie_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("remaining").toInt() <= 0 || !ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.sourceRef = ref(receipt, "source");
            ctx.extra_data = receipt;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.is_forced = true;
            contexts << ctx;
            break; // One pending ward prevents this entire damage, regardless of the number of grants.
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish
            && player->getHp() > 0 && !room->getOtherPlayers(player).isEmpty()) result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageInflicted) { ctx.targets << ctx.invoker; return true; }
        if (!isUsable(ctx)) return false;
        ctx.targets = room->askForPlayersChosen(ctx.invoker, room->getOtherPlayers(ctx.invoker), objectName(), 0, qMax(0, ctx.invoker->getHp()), "@olwulie", true);
        return !ctx.targets.isEmpty();
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageInflicted) return true;
        if (!isUsable(ctx) || ctx.targets.isEmpty() || ctx.targets.length() > ctx.invoker->getHp()) return false;
        addUsage(ctx);
        room->loseHp(HpLostStruct(ctx.invoker, ctx.targets.length(), objectName(), ctx.invoker));
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == DamageInflicted) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to != target || damage.damage <= 0) return false;
            QVariantList receipts = ctx.owner->getTag("olwulie_effects").toList();
            bool consumed = false;
            for (int i = 0; i < receipts.length(); ++i) {
                QVariantMap receipt = receipts.at(i).toMap();
                if (receipt.value("receipt").toInt() != ctx.instanceID) continue;
                const int left = receipt.value("remaining").toInt() - 1;
                if (left > 0) { receipt.insert("remaining", left); receipts[i] = receipt; }
                else receipts.removeAt(i);
                consumed = true;
                break;
            }
            if (!consumed) return false;
            ctx.owner->setTag("olwulie_effects", receipts);
            room->removePlayerMark(ctx.owner, "&ollie");
            LogMessage log;
            log.type = "#OLwuliePrevent";
            log.from = target;
            log.arg = objectName();
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);
            return true;
        }
        const int sequence = room->getTag("olwulie_sequence").toInt() + 1;
        room->setTag("olwulie_sequence", sequence);
        const QVariantMap receipt{{"receipt", sequence}, {"remaining", amount},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        QVariantList receipts = target->getTag("olwulie_effects").toList();
        receipts << receipt;
        target->setTag("olwulie_effects", receipts);
        room->addPlayerMark(target, "&ollie", amount);
        return false;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) { room->broadcastSkillInvoke(objectName()); room->doSuperLightbox(ctx.invoker, objectName()); }
        return TriggerSkillV2::effect(event, room, player, ctx);
    }
};

OLFangquanCard::OLFangquanCard() { setSkillName("olfangquan"); mute = true; }

class OLFangquan : public TriggerSkillV2
{
public:
    OLFangquan() : TriggerSkillV2("olfangquan") { events << EventPhaseChanging << EventPhaseStart; global = true; }
    int getPriority(TriggerEvent event) const override { return event == EventPhaseStart ? 1 : TriggerSkillV2::getPriority(event); }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    static void replace(ServerPlayer *owner, int sequence, const QVariantMap &replacement = QVariantMap())
    {
        QVariantList receipts = owner->getTag("olfangquan_effects").toList();
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("receipt").toInt() == sequence) receipts.removeAt(i);
        if (!replacement.isEmpty()) receipts << replacement;
        owner->setTag("olfangquan_effects", receipts);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        if (!receipt.contains("receipt")) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ref(receipt, "activation").isValid()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("olfangquan_effects").toList())
            if (entry.toMap().value("receipt") == receipt.value("receipt") && ref(entry.toMap(), "source") == ctx.sourceRef
                && entry.toMap().value("turn") == room->historyScopes().value("turn_id")) return true;
        return false;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::NotActive) {
            QVariantList receipts = player->getTag("olfangquan_effects").toList();
            for (int i = receipts.length() - 1; i >= 0; --i) {
                const QVariantMap receipt = receipts.at(i).toMap();
                if (receipt.value("turn") == room->historyScopes().value("turn_id") && !receipt.contains("recipient")) receipts.removeAt(i);
            }
            player->setTag("olfangquan_effects", receipts);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart) return false;
        if (!player || (player->getPhase() != Player::Discard && player->getPhase() != Player::NotActive)) return true;
        foreach (const QVariant &entry, player->getTag("olfangquan_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
            if (player->getPhase() == Player::Discard ? (!player->isAlive() || receipt.value("offered").toBool()) : !receipt.contains("recipient")) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = ref(receipt, "source");
            if (!ctx.sourceRef.isValid() || !ref(receipt, "activation").isValid()) continue;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.extra_data = receipt;
            ctx.is_forced = player->getPhase() == Player::NotActive;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseChanging && player && player->isAlive() && player->hasSkill(this)
            && data.value<PhaseChangeStruct>().to == Player::Play && !player->isSkipped(Player::Play)
            && room->historyScopes().value("turn_id").toLongLong() > 0) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) {
            if (ctx.owner->isSkipped(Player::Play) || !ctx.owner->askForSkillInvoke(this)) return false;
            ctx.targets << ctx.owner;
            return true;
        }
        QVariantMap receipt = ctx.extra_data.toMap();
        if (ctx.owner->getPhase() == Player::NotActive) {
            ServerPlayer *recipient = room->findPlayerByObjectName(receipt.value("recipient").toString());
            if (!recipient || !recipient->isAlive()) { replace(ctx.owner, receipt.value("receipt").toInt()); return false; }
            ctx.targets << recipient;
            return true;
        }
        // This offer occurs at the first discard phase, even if the optional hand payment is declined.
        receipt.insert("offered", true);
        replace(ctx.owner, receipt.value("receipt").toInt(), receipt);
        ctx.extra_data = receipt;
        if (!ctx.owner->canDiscard(ctx.owner, "h") || room->getOtherPlayers(ctx.owner).isEmpty()) return false;
        const Card *card = room->askForCard(ctx.owner, ".|.|.|hand", "@olfangquan-give", *ctx.original_data, Card::MethodNone);
        if (!card || card->getEffectiveId() < 0 || !ctx.owner->getHandcards().contains(card) || card->hasFlag("using")
            || !ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) return false;
        ServerPlayer *recipient = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@olfangquan-give");
        if (!recipient) return false;
        receipt.insert("card", card->getEffectiveId());
        ctx.extra_data = receipt;
        ctx.targets << recipient;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) { ctx.owner->skip(Player::Play, true); return true; }
        if (ctx.owner->getPhase() == Player::NotActive) return true;
        const int id = ctx.extra_data.toMap().value("card", -1).toInt();
        if (id < 0 || room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
            || Sanguosha->getCard(id)->hasFlag("using") || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == EventPhaseChanging) {
            const int sequence = room->getTag("olfangquan_sequence").toInt() + 1;
            room->setTag("olfangquan_sequence", sequence);
            replace(target, sequence, QVariantMap{{"receipt", sequence}, {"turn", room->historyScopes().value("turn_id")}, {"amount", amount},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}});
            return false;
        }
        QVariantMap receipt = ctx.extra_data.toMap();
        if (ctx.owner->getPhase() == Player::Discard) {
            receipt.insert("recipient", target->objectName());
            receipt.insert("amount", amount);
            replace(ctx.owner, receipt.value("receipt").toInt(), receipt);
            LogMessage log;
            log.type = "#Fangquan";
            log.from = ctx.owner;
            log.to << target;
            room->sendLog(log);
        } else {
            replace(ctx.owner, receipt.value("receipt").toInt());
            for (int i = 0; i < amount && target->isAlive(); ++i)
                room->executeExtraTurn(target, QList<Player::Phase>(), objectName(), ctx.sourceRef);
        }
        return false;
    }
};

class OLRuoyu : public TriggerSkillV2
{
public:
    OLRuoyu() : TriggerSkillV2("olruoyu$")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "oljijiang,olsishu";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (active.activationRef == ctx.activationRef) {
            // The display token is a projection; the accepted instance owns the game quota.
            if (active.bypass_cost) addUsage(active);
            room->setPlayerMark(ctx.owner, objectName(), 1);
        }
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasLordSkill(this) && (player->isLowestHpPlayer() || player->canWake(objectName())))
            result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.invoker->isWeidi()) room->broadcastSkillInvoke("weidi");
        else room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.invoker, objectName());
        room->doSuperLightbox(ctx.invoker, objectName());
        if (room->changeMaxHpForAwakenSkill(target, amount, objectName())) {
            const int recover = qMin(3 * amount, target->getMaxHp()) - target->getHp();
            if (recover > 0) room->recover(target, RecoverStruct(objectName(), ctx.invoker, recover));
            // Both wake rewards are permanent acquired grants.
            if (target->isAlive()) {
                room->acquireSkillFromEffect(target, "oljijiang", ctx);
                if (target->isAlive()) room->acquireSkillFromEffect(target, "olsishu", ctx);
            }
        }
        return false;
    }
};

class OLSishu : public TriggerSkillV2
{
public:
    OLSishu() : TriggerSkillV2("olsishu") { events << StartJudge << EventPhaseStart; global = true; }
    static SkillInstanceRef receiptRef(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap pending = ctx.extra_data.toMap();
        if (!pending.contains("receipt")) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("olsishu_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt") == pending.value("receipt") && receiptRef(receipt, "source") == ctx.sourceRef
                && receiptRef(receipt, "activation") == receiptRef(pending, "activation")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != StartJudge) return false;
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!player || !player->isAlive() || !judge || judge->who != player || judge->reason != "indulgence") return true;
        foreach (const QVariant &entry, player->getTag("olsishu_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = receiptRef(receipt, "source");
            if (!ctx.sourceRef.isValid() || !receiptRef(receipt, "activation").isValid()) continue;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.extra_data = receipt;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Play
            && player->hasSkill(this)) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.invoker;
        if (event == EventPhaseStart) target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(),
            objectName(), "@olsishu-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == StartJudge) {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (!judge || judge->who != target || judge->reason != "indulgence") return false;
            LogMessage log;
            log.type = "#OLsishuEffect";
            log.from = target;
            log.arg = objectName();
            room->sendLog(log);
            judge->good = false;
            return false;
        }
        ctx.invoker->peiyin(this);
        QVariantList receipts = target->getTag("olsishu_effects").toList();
        foreach (const QVariant &entry, receipts)
            if (receiptRef(entry.toMap(), "source") == ctx.sourceRef && receiptRef(entry.toMap(), "activation") == ctx.activationRef)
                return false;
        const int sequence = room->getTag("olsishu_receipt_sequence").toInt() + 1;
        room->setTag("olsishu_receipt_sequence", sequence);
        // The permanent indulgence modifier has real provenance and outlives its source grant.
        receipts << QVariantMap{{"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"receipt", sequence}, {"amount", amount}};
        target->setTag("olsishu_effects", receipts);
        room->setPlayerMark(target, "&olsishu", 1);
        return false;
    }
};

OLZhibaCard::OLZhibaCard() { setSkillName("olzhiba"); mute = true; }
OLZhibaPindianCard::OLZhibaPindianCard() { setSkillName("olzhiba_pindian"); mute = true; }

class OLZhibaAction : public ViewAsSkillV2
{
public:
    OLZhibaAction(const QString &name, bool attached) : ViewAsSkillV2(name, 0), attached(attached)
    {
        attached_lord_skill = attached;
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return attached ? "OLZhibaPindianCard" : "OLZhibaCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canPindian()
            && (attached ? request.initiator->getKingdom() == "wu" && olAttachedParent(request).key.skillName == "olzhiba"
                : request.initiator->hasLordSkill("olzhiba"));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive() || target == request.initiator || !selected.isEmpty()
            || !request.initiator->canPindian(target)) return false;
        if (!attached) return target->getKingdom() == "wu";
        const SkillInstanceRef parent = olAttachedParent(request);
        return parent.isValid() && target->objectName() == parent.ownerObjectName && target->hasLordSkill("olzhiba")
            && target->getValidSkillInstanceIds(parent.key.skillName).contains(parent.key.instanceID);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1 && canSelectTarget(request, QList<const Player *>(), selected.first());
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.extra_data.toMap().contains("cards")) {
            QList<int> ids;
            foreach (const QVariant &entry, ctx.extra_data.toMap().value("cards").toList()) {
                const int id = entry.toInt();
                if (!ids.contains(id) && room->getCardPlace(id) == Player::DiscardPile && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
            }
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, true); }
            return ContinueEffects;
        }
        ServerPlayer *lord = attached ? target : ctx.invoker;
        if (lord->isWeidi()) room->broadcastSkillInvoke("weidi", -1, lord);
        else room->broadcastSkillInvoke("olzhiba", -1, lord);
        if (attached && room->askForChoice(target, objectName(), "accept+reject") == "reject") {
            LogMessage log;
            log.type = "#ZhibaReject";
            log.from = target;
            log.to << ctx.invoker;
            log.arg = objectName();
            room->sendLog(log);
            return ContinueEffects;
        }
        for (int i = 0; i < amount && ctx.invoker->isAlive() && target->isAlive() && ctx.invoker->canPindian(target); ++i) {
            const PindianStruct *pindian = ctx.invoker->PinDian(target, objectName());
            if (!pindian) break;
            const bool win = attached ? pindian->from_number <= pindian->to_number : pindian->from_number >= pindian->to_number;
            if (!win || !lord->isAlive()) continue;
            QList<int> ids;
            if (pindian->from_card) ids << pindian->from_card->getEffectiveId();
            if (pindian->to_card && !ids.contains(pindian->to_card->getEffectiveId())) ids << pindian->to_card->getEffectiveId();
            for (int j = ids.length() - 1; j >= 0; --j) if (room->getCardPlace(ids.at(j)) != Player::DiscardPile) ids.removeAt(j);
            if (ids.isEmpty() || room->askForChoice(lord, "olzhiba_pindian_obtain", "obtainPindianCards+reject") != "obtainPindianCards") continue;
            // Pindian has committed: obtaining its cards retains the accepted lord instance even if it retired in a callback.
            SkillContext obtain = ctx;
            obtain.extra_data = QVariantMap{{"cards", ListI2V(ids)}};
            skillEffect(obtain, lord);
        }
        return ContinueEffects;
    }
private:
    bool attached;
};

class OLZhibaVS : public OLZhibaAction
{
public:
    OLZhibaVS() : OLZhibaAction("olzhiba$", false) { }
};

class OLZhibaPindian : public OLZhibaAction
{
public:
    OLZhibaPindian() : OLZhibaAction("olzhiba_pindian", true) { }
};

class OLZhiba : public OLLordAttachment
{
public:
    OLZhiba() : OLLordAttachment("olzhiba$", "olzhiba_pindian") { view_as_skill = new OLZhibaVS; }
};

class OLQiaomeng : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLQiaomeng() : TriggerSkillV2("olqiaomeng") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (player && player->isAlive() && player->hasSkill(this) && damage.to && damage.to->isAlive()
            && !damage.to->hasFlag("Global_DebutFlag") && damage.card && damage.card->isKindOf("Slash")
            && player->canDiscard(damage.to, "hej")) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!target || !room->askForSkillInvoke(ctx.invoker, objectName(), QVariant::fromValue(target))) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.invoker->isAlive() && ctx.invoker->canDiscard(target, "hej"); ++i) {
            int id = room->askForCardChosen(ctx.invoker, target, "hej", objectName(), false, Card::MethodDiscard);
            const Card *card = Sanguosha->getCard(id);
            CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, ctx.invoker->objectName(), target->objectName(), objectName(), "");
            room->throwCard(card, reason, target, ctx.invoker);
            if (card->isKindOf("Horse") && ctx.invoker->isAlive() && room->getCardPlace(id) == Player::DiscardPile)
                room->obtainCard(ctx.invoker, card);
        }
        return false;
    }
};
class OLYicong : public DistanceSkillV2
{
public:
    OLYicong() : DistanceSkillV2("olyicong") { setHolderSelector(CorrectSkill_Participants); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        int correction = 0;
        if (ctx.holder == ctx.primary) correction -= ctx.currentAmount;
        if (ctx.holder == ctx.secondary && ctx.holder->getHp() <= 2) correction += ctx.currentAmount;
        return CorrectSkillResult::useAmount(correction);
    }
};
class OLYicongEffect : public TriggerSkillV2
{
public:
    OLYicongEffect() : TriggerSkillV2("#olyicong-effect") { events << HpChanged; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill("olyicong")) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariant &data = *ctx.original_data;
        const int hp = ctx.owner->getHp();
        int index = 0;
        if (data.canConvert<RecoverStruct>()) {
            if (hp > 2 && hp - data.value<RecoverStruct>().recover <= 2) index = 1;
        } else {
            const int reduction = data.canConvert<DamageStruct>() ? data.value<DamageStruct>().damage : data.toInt();
            if (hp <= 2 && hp + reduction > 2) index = 2;
        }
        if (!index) return false;
        if (ctx.owner->getGeneralName().contains("sp_") || ctx.owner->getGeneral2Name().contains("sp_")) index += 2;
        ctx.extra_data = index;
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The distance itself is supplied by the independently queried V2 correction.
        room->broadcastSkillInvoke("olyicong", ctx.extra_data.toInt());
        return false;
    }
};
class OLHuashen : public TriggerSkillV2
{
public:
	OLHuashen() : TriggerSkillV2("olhuashen")
	{
		events << GameStart;
		frequency = Compulsory;
		m_baseAmount = 3;
	}

	SkillDialogInfo getDialogInfo() const override
	{
		SkillDialogInfo info = SkillDialogInfo::named("huashen", objectName());
		info.parameters.insert("viewOnly", true);
		return info;
	}

	// Shared with standard Xinsheng. Identities stay on the owner; the public animation is only a count.
	static void publishPool(ServerPlayer *zuoci, const QStringList &huashens)
	{
		Room *room = zuoci->getRoom();
		const QString pile = huashens.join("+");
		zuoci->setProperty("Huashens", pile);
		zuoci->setProperty("huashen_general", pile);
		room->notifyProperty(zuoci, zuoci, "Huashens");
		room->notifyProperty(zuoci, zuoci, "huashen_general");
		room->setPlayerMark(zuoci, "@huashen", huashens.length());
	}

	static QStringList currentPool(const ServerPlayer *zuoci)
	{
		QStringList huashens = zuoci->property("Huashens").toString().split("+");
		if (zuoci->property("Huashens").toString().isEmpty()) huashens.clear();
		return huashens;
	}

	static void AcquireGenerals(ServerPlayer *zuoci, int n, QStringList remove_list)
	{
		Room *room = zuoci->getRoom();
		QStringList list = GetAvailableGenerals(zuoci, remove_list);
		if (list.isEmpty()) return;
		qsanShuffle(list);
		n = qMin(n, list.length());
		QStringList huashens = currentPool(zuoci);

		QStringList acquired = list.mid(0, n);
		foreach (QString name, acquired) {
			huashens << name;
			const General *general = Sanguosha->getGeneral(name);
			if (general) {
				foreach (const TriggerSkill *skill, general->getTriggerSkills())
					room->getThread()->addTriggerSkill(skill);
			}
		}
		publishPool(zuoci, huashens);

		QStringList hidden;
		for (int i = 0; i < n; i++) hidden << "unknown";
		room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), hidden.join(":"), room->getOtherPlayers(zuoci));
		room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), acquired.join(":"), QList<ServerPlayer *>() << zuoci);

		LogMessage log;
		log.type = "#GetHuashen";
		log.from = zuoci;
		log.arg = QString::number(n);
		log.arg2 = QString::number(huashens.length());
		room->sendLog(log);

		log.type = "#GetHuashenDetail";
		log.arg = acquired.join("\\, \\");
		room->sendLog(log, zuoci);
	}

	static QStringList GetAvailableGenerals(ServerPlayer *zuoci, QStringList remove_list)
	{
		QStringList all = Sanguosha->getLimitedGeneralNames();
		Room *room = zuoci->getRoom();
		if (room->getMode() == "06_XMode") {
			foreach(ServerPlayer *p, room->getAlivePlayers())
				all << p->getTag("XModeBackup").toStringList();
		} else if (room->getMode() == "02_1v1") {
			foreach(ServerPlayer *p, room->getAlivePlayers())
				all << p->getTag("1v1Arrange").toStringList();
		}
		QSet<QString> huashen_set, room_set;
		foreach(QString huashen, zuoci->property("Huashens").toString().split("+")){
			if(huashen.isEmpty()) continue;
			huashen_set << huashen;
		}
		foreach (ServerPlayer *player, room->getAlivePlayers()) {
			QString name = player->getGeneralName();/*
			if (Sanguosha->isGeneralHidden(name)) {
				QString fname = Sanguosha->findConvertFrom(name);
				if (!fname.isEmpty()) name = fname;
			}*/
			room_set << name;

			if (!player->getGeneral2()) continue;

			name = player->getGeneral2Name();/*
			if (Sanguosha->isGeneralHidden(name)) {
				QString fname = Sanguosha->findConvertFrom(name);
				if (!fname.isEmpty()) name = fname;
			}*/
			room_set << name;
		}

		static QSet<QString> banned;
		if (banned.isEmpty()) {
			banned << "zuoci" << "guzhielai" << "dengshizai" << "yt_caochong" << "jiangboyue" << "ol_zuoci";
		}
		QSet<QString> remove_set = qsanToSet(remove_list);
		return (qsanToSet(all) - banned - huashen_set - room_set - remove_set).values();
	}

	static void SelectSkill(ServerPlayer *zuoci, SkillInstanceRef source, const SkillContext &accepted)
	{
		Room *room = zuoci->getRoom();
		if (source.ownerObjectName != zuoci->objectName() || source.key.skillName != "olhuashen"
			|| !zuoci->hasSkillInstance("olhuashen", source.key.instanceID)) return;

		QStringList huashens = currentPool(zuoci);
		if (huashens.isEmpty()) return;
		const QString previousSkill = zuoci->getSkillInstanceStateValue("olhuashen", source.key.instanceID, "chosen_skill").toString();
		const int previousId = zuoci->getSkillInstanceStateValue("olhuashen", source.key.instanceID, "chosen_id").toInt();
		const auto selectionUnchanged = [&] {
			return zuoci->hasSkillInstance("olhuashen", source.key.instanceID)
				&& zuoci->getSkillInstanceStateValue("olhuashen", source.key.instanceID, "chosen_id").toInt() == previousId
				&& zuoci->getSkillInstanceStateValue("olhuashen", source.key.instanceID, "chosen_skill").toString() == previousSkill;
		};

		QString skill_name;
		QStringList skill_names;
		const General *general = nullptr;
		AI* ai = zuoci->getAI();
		if (ai) {
			QHash<QString, const General *> hash;
			foreach (QString general_name, huashens) {
				const General *general = Sanguosha->getGeneral(general_name);
				if (!general) continue;
				foreach (const Skill *skill, general->getVisibleSkillList()) {
					if (skill->isLordSkill()
						|| skill->isLimitedSkill()
						|| skill->getFrequency() == Skill::Wake)
						continue;
					hash[skill->objectName()] = general;
					skill_names << skill->objectName();
				}
			}
			if (skill_names.isEmpty()) return;
			skill_name = ai->askForChoice("olhuashen", skill_names.join("+"), QVariant());
			general = hash.value(skill_name);
		} else {
			QString general_name = room->askForGeneral(zuoci, huashens);
			if (!selectionUnchanged() || !huashens.contains(general_name)) return;
			general = Sanguosha->getGeneral(general_name);
			if (!general) return;

			foreach (const Skill *skill, general->getVisibleSkillList()) {
				if (skill->isLordSkill()
					|| skill->isLimitedSkill()
					|| skill->getFrequency() == Skill::Wake)
					continue;
				skill_names << skill->objectName();
			}
			if (skill_names.isEmpty()) return;
			skill_name = room->askForChoice(zuoci, "olhuashen", skill_names.join("+"));
		}

		if (!general || !skill_names.contains(skill_name)
			|| !zuoci->hasSkillInstance("olhuashen", source.key.instanceID)
			|| !currentPool(zuoci).contains(general->objectName())
			|| zuoci->getSkillInstanceStateValue("olhuashen", source.key.instanceID, "chosen_id").toInt() != previousId
			|| zuoci->getSkillInstanceStateValue("olhuashen", source.key.instanceID, "chosen_skill").toString() != previousSkill) return;

		QString kingdom = general->getKingdom();
		if (general->getKingdoms().contains("+")) {
			kingdom = room->askForKingdom(zuoci, general->objectName() + "_ChooseKingdom");
			if (!selectionUnchanged()) return;
			room->setPlayerProperty(zuoci, "kingdom", kingdom);
			if (!selectionUnchanged()) return;
		} else if (zuoci->getKingdom() != kingdom) {
			if (kingdom == "god") {
				kingdom = room->askForKingdom(zuoci);
				if (!selectionUnchanged()) return;
			}
			room->setPlayerProperty(zuoci, "kingdom", kingdom);
			if (!selectionUnchanged()) return;
		}

		if (!selectionUnchanged()) return;
		if (zuoci->getGender() != general->getGender()) zuoci->setGender(general->getGender());
		if (!selectionUnchanged()) return;
		JsonArray arg;
		arg << QSanProtocol::S_GAME_EVENT_HUASHEN << zuoci->objectName() << general->objectName() << skill_name;
		room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

		if (!selectionUnchanged()) return;
		if (!previousSkill.isEmpty() && previousId > 0)
			room->detachSkillFromPlayer(zuoci, SkillInstanceUtils::formatName(previousSkill, previousId), false, true);
		if (!selectionUnchanged()) return;
		int committedGrant = 0;
		bool selectionPublished = false;
		const auto releaseUnpublished = qScopeGuard([&] {
			if (selectionPublished || committedGrant <= 0) return;
			QVariantList grants = zuoci->getTag("OLHuashenGrants").toList();
			grants.removeOne(QVariantMap{{"root", source.key.instanceID}, {"skill", skill_name}, {"id", committedGrant}});
			zuoci->setTag("OLHuashenGrants", grants);
			try {
				if (zuoci->hasSkillInstance(skill_name, committedGrant))
					room->detachSkillFromPlayer(zuoci, SkillInstanceUtils::formatName(skill_name, committedGrant), false, true);
			} catch (...) {}
		});
		const int acquired = room->acquireSkillFromEffect(zuoci, skill_name, accepted, [&](int committedId) {
			committedGrant = committedId;
			QVariantList grants = zuoci->getTag("OLHuashenGrants").toList();
			grants << QVariantMap{{"root", source.key.instanceID}, {"skill", skill_name}, {"id", committedId}};
			zuoci->setTag("OLHuashenGrants", grants);
		});
		if (acquired <= 0) return;
		if (!selectionUnchanged()) {
			QVariantList grants = zuoci->getTag("OLHuashenGrants").toList();
			grants.removeOne(QVariantMap{{"root", source.key.instanceID}, {"skill", skill_name}, {"id", acquired}});
			zuoci->setTag("OLHuashenGrants", grants);
			room->detachSkillFromPlayer(zuoci, SkillInstanceUtils::formatName(skill_name, acquired), false, true);
			return;
		}
		zuoci->setSkillInstanceStateValue("olhuashen", source.key.instanceID, "chosen_skill", skill_name);
		zuoci->setSkillInstanceStateValue("olhuashen", source.key.instanceID, "chosen_id", acquired);
		QVariantList grants = zuoci->getTag("OLHuashenGrants").toList();
		for (int i = grants.size() - 1; i >= 0; --i)
			if (grants.at(i).toMap().value("root").toInt() == source.key.instanceID) grants.removeAt(i);
		grants << QVariantMap{{"root", source.key.instanceID}, {"skill", skill_name}, {"id", acquired}};
		zuoci->setTag("OLHuashenGrants", grants);
		zuoci->setTag("OLHuashenSkill", skill_name);
		selectionPublished = true;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName())
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
	{ ctx.targets = {owner}; return true; }
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *zuoci) const override
	{
		room->broadcastSkillInvoke(objectName(), zuoci->isMale(), -1);
		room->sendCompulsoryTriggerLog(zuoci, objectName());
		AcquireGenerals(zuoci, getEffectiveAmount(ctx), QStringList());
		SelectSkill(zuoci, ctx.activationRef, ctx);
		return false;
	}
};

class OLHuashenSelect : public TriggerSkillV2
{
public:
	OLHuashenSelect() : TriggerSkillV2("#olhuashen-select") { events << EventPhaseStart; }
	bool usesEventPriority() const override { return true; }
	int getPriority(TriggerEvent) const override { return 4; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && player->hasSkill("olhuashen")
			&& (player->getPhase() == Player::RoundStart || player->getPhase() == Player::NotActive)
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{ ctx.targets = {player}; return player->askForSkillInvoke("olhuashen"); }
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *zuoci) const override
	{
		QStringList choices;
		choices << "change" << "exchangeone";
		if (OLHuashen::currentPool(zuoci).length() > 1) choices << "exchangetwo";
		const QString choice = room->askForChoice(zuoci, "olhuashen", choices.join("+"));
		if (choice == "change") {
			const SkillInstance *helper = zuoci->findSkillInstance(objectName(), ctx.activationRef.key.instanceID);
			if (helper) OLHuashen::SelectSkill(zuoci, helper->parentRef, ctx);
			return false;
		}
		const int n = choice == "exchangetwo" ? 2 : 1;
		QStringList remaining = OLHuashen::currentPool(zuoci);
		QStringList remove_list;
		for (int i = 0; i < n; i++) {
			remaining.removeAll(QString());
			if (remaining.isEmpty()) break;
			const QString general_name = room->askForGeneral(zuoci, remaining);
			if (!remaining.contains(general_name)) break;
			remove_list << general_name;
			remaining.removeOne(general_name);
		}
		if (remove_list.isEmpty()) return false;
		QStringList live = OLHuashen::currentPool(zuoci);
		foreach (const QString &name, remove_list) live.removeOne(name);
		OLHuashen::publishPool(zuoci, live);

		LogMessage log;
		log.type = "#RemoveHuashenDetail";
		log.from = zuoci;
		log.arg = QString::number(remove_list.length());
		log.arg2 = remove_list.join("\\, \\");
		room->sendLog(log);

		OLHuashen::AcquireGenerals(zuoci, remove_list.length(), remove_list);
		return false;
	}
};

class OLHuashenClear : public TriggerSkillV2
{
public:
	OLHuashenClear() : TriggerSkillV2("#olhuashen-clear")
	{ events << EventLoseSkill; global = true; }

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const SkillChangeStruct change = data.value<SkillChangeStruct>();
		if (!player || change.skillName != "olhuashen") return true;
		QVariantList grants = player->getTag("OLHuashenGrants").toList();
		QStringList expired;
		for (int i = grants.size() - 1; i >= 0; --i) {
			const QVariantMap grant = grants.at(i).toMap();
			if (grant.value("root").toInt() != change.instanceID) continue;
			expired << SkillInstanceUtils::formatName(grant.value("skill").toString(), grant.value("id").toInt());
			grants.removeAt(i);
		}
		player->setTag("OLHuashenGrants", grants);
		for (const QString &skill : expired) room->detachSkillFromPlayer(player, skill, false, true);
		const QString shown = player->getTag("OLHuashenSkill").toString();
		if (!shown.isEmpty()) {
			bool stillChosen = false;
			foreach (const QVariant &entry, grants)
				if (entry.toMap().value("skill").toString() == shown) stillChosen = true;
			if (!stillChosen) player->removeTag("OLHuashenSkill");
		}
		if (player->hasSkill("olhuashen", true)) return true;
		if (player->getKingdom() != player->getGeneral()->getKingdom() && player->getGeneral()->getKingdom() != "god")
			room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());
		if (player->getGender() != player->getGeneral()->getGender())
			player->setGender(player->getGeneral()->getGender());
		player->removeTag("OLHuashenSkill");
		OLHuashen::publishPool(player, QStringList());
		return true;
	}
};

class OLJiuchiVS : public OLCardConversion
{
public:
    OLJiuchiVS() : OLCardConversion("oljiuchi", "analeptic", ".|spade|.|hand", true) {}
};
class OLJiuchi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLJiuchi() : TriggerSkillV2("oljiuchi") { events << Damage; view_as_skill = new OLJiuchiVS; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (player && player->isAlive() && player->hasSkill(this) && damage.from == player
            && damage.card && damage.card->isKindOf("Slash") && damage.card->hasFlag("drank"))
            result[player] << objectName();
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.invoker->hasSkill("benghuai")) {
            LogMessage log;
            log.type = "#BenghuaiNullification";
            log.from = ctx.invoker;
            log.arg = objectName();
            log.arg2 = "benghuai";
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(ctx.invoker, objectName());
        }
        // Public applied invalidity is intentionally retained until the turn ends.
        room->addPlayerMark(ctx.invoker, "benghuai_nullification-Clear");
        return false;
    }
};
class OLJiuchiTargetMod : public TargetModSkillV2
{
public:
    OLJiuchiTargetMod() : TargetModSkillV2("#oljiuchi-target", "Analeptic") { frequency = NotFrequent; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == Residue ? CorrectSkillResult::unlimitedResidue()
            : CorrectSkillResult::noEffect();
    }
};
class OLBaonue : public TriggerSkillV2
{
public:
    OLBaonue() : TriggerSkillV2("olbaonue$") { events << Damage; frequency = Frequent; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || player->getKingdom() != "qun" || damage.from != player || damage.damage <= 0) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
            if (!owner->isAlive() || !owner->hasLordSkill(this)) continue;
            for (int i = 0; i < damage.damage; ++i) result[owner] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            if (ctx.invoker->isWeidi()) {
                room->broadcastSkillInvoke("weidi");
                room->notifySkillInvoked(ctx.invoker, "weidi");
            } else {
                room->broadcastSkillInvoke(objectName());
                room->notifySkillInvoked(ctx.invoker, objectName());
            }
            JudgeStruct judge;
            judge.pattern = ".|spade";
            judge.reason = objectName();
            judge.good = true;
            judge.who = target;
            room->judge(judge);
            if (judge.isGood() && target->isAlive()) {
                room->recover(target, RecoverStruct(objectName(), ctx.invoker));
                // Judgement callbacks may move the physical card before the recovery completes.
                if (target->isAlive() && judge.card && room->getCardPlace(judge.card->getEffectiveId()) == Player::DiscardPile)
                    room->obtainCard(target, judge.card);
            }
        }
        return false;
    }
};

class OLJiuyuan : public TriggerSkillV2
{
public:
    OLJiuyuan() : TriggerSkillV2("oljiuyuan$") { events << PreHpRecover; }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        // The recovering Wu player decides whether to benefit this exact lord instance.
        ctx.initiator = ctx.invoker;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getKingdom() != "wu" || !player->hasFlag("CurrentPlayer")) return result;
        foreach (ServerPlayer *lord, room->getOtherPlayers(player))
            if (lord->hasLordSkill(this) && lord->isAlive() && lord->isWounded() && player->getHp() >= lord->getHp())
                result[lord] << objectName() + "->" + lord->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive() || !ctx.owner->isWounded() || ctx.invoker->getHp() < ctx.owner->getHp()
            || !ctx.invoker->askForSkillInvoke(this, ctx.owner)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.extra_data.toString() == "draw") { target->drawCards(amount, objectName()); return false; }
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = ctx.invoker;
        log.to << target;
        log.arg = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(ctx.owner->isWeidi() ? "weidi" : "jiuyuan");
        room->notifySkillInvoked(ctx.owner, ctx.owner->isWeidi() ? "weidi" : objectName());
        room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        if (ctx.invoker->isAlive()) {
            SkillContext draw = ctx;
            draw.extra_data = QStringLiteral("draw");
            skillEffect(event, room, owner, draw, ctx.invoker);
        }
        return true;
    }
};

class OLBotu : public TriggerSkillV2
{
public:
    OLBotu() : TriggerSkillV2("olbotu") { events << EventPhaseStart; frequency = Frequent; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static bool collectSuits(Room *room, const Player *player, QStringList &suits)
    {
        const QString turn = room->historyScopes().value("turn_id").toString();
        if (turn.isEmpty() || turn == "0") return false;
        const QVariantMap history = room->queryCardHistory(player, "turn", QString(), false, true);
        if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return false;
        foreach (const QVariant &item, history.value("items").toList()) {
            const QVariantMap card = item.toMap();
            if (!card.contains("type") || !card.contains("suit")) return false;
            if (card.value("type").toInt() == Card::TypeSkill) continue;
            QString suit;
            switch (card.value("suit").toInt()) {
            case Card::Spade: suit = "spade_char"; break;
            case Card::Club: suit = "club_char"; break;
            case Card::Heart: suit = "heart_char"; break;
            case Card::Diamond: suit = "diamond_char"; break;
            default: continue; // NoSuit is not one of the four printed suits.
            }
            if (!suits.contains(suit)) suits << suit;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        QStringList suits;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::NotActive
            && collectSuits(room, player, suits) && suits.length() == 4) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) room->executeExtraTurn(target, QList<Player::Phase>(), ctx.sourceRef.key.skillName, ctx.sourceRef);
        return false;
    }
};

class OLBotuMark : public TriggerSkillV2
{
public:
    OLBotuMark() : TriggerSkillV2("#olbotu-mark") { events << CardFinished << EventPhaseStart; global = true; }
    int getPriority(TriggerEvent) const override { return 1; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player) return true;
        const bool clear = event == EventPhaseStart && player->getPhase() == Player::NotActive;
        if (!clear && (event != CardFinished || player->getPhase() != Player::Play || !player->hasSkill("olbotu", true))) return true;
        QStringList suits;
        if (!clear && !OLBotu::collectSuits(room, player, suits)) return true;
        // The mark/property are a UI projection only; the rule always queries the Room journal.
        foreach (const QString &mark, player->getMarkNames())
            if (mark.startsWith("&olbotu")) room->setPlayerMark(player, mark, 0);
        room->safeSetPlayerProperty(player, "olbotu_suit", suits);
        if (!suits.isEmpty()) {
            suits.prepend("&olbotu");
            room->setPlayerMark(player, suits.join("+"), 1);
        }
        return true;
    }
};
class OLTuntian : public TriggerSkillV2
{
public:
    OLTuntian() : TriggerSkillV2("oltuntian") { events << CardsMoveOneTime << FinishJudge; frequency = Frequent; global = true; }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    static void consume(ServerPlayer *player, int receipt)
    {
        QVariantList receipts = player->getTag("oltuntian_judges").toList();
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("receipt").toInt() == receipt) receipts.removeAt(i);
        player->setTag("oltuntian_judges", receipts);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("oltuntian_judges").toList())
            if (entry.toMap() == ctx.extra_data.toMap() && ref(entry.toMap(), "source") == ctx.sourceRef) return true;
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardsMoveOneTime || !player || !player->isAlive() || !player->hasSkill(this)) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player) return result;
        bool eligible = !player->hasFlag("CurrentPlayer")
            && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
            && !(move.to == player && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip));
        if (player->hasFlag("CurrentPlayer") && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
            foreach (int id, move.card_ids) if (Sanguosha->getCard(id)->isKindOf("Slash")) { eligible = true; break; }
        if (eligible) result[player] << objectName();
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != FinishJudge) return false;
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!judge || judge->reason != objectName() || judge->who != player || !player || !player->isAlive()
            || !judge->card || !judge->isGood() || room->getCardPlace(judge->card->getEffectiveId()) != Player::PlaceJudge) return true;
        const qint64 parent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
        if (parent <= 0) return true;
        foreach (const QVariant &entry, player->getTag("oltuntian_judges").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("parent").toLongLong() != parent || !ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = ref(receipt, "source");
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.extra_data = receipt;
            ctx.is_forced = true;
            contexts << ctx;
        }
        return true;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardsMoveOneTime && (room->currentHistoryEventId() <= 0 || !ctx.invoker->askForSkillInvoke(this, *ctx.original_data))) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == FinishJudge) {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (judge && judge->who == target && judge->card && judge->isGood()
                && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge) {
                consume(ctx.owner, ctx.extra_data.toMap().value("receipt").toInt());
                target->addToPile("field", judge->card->getEffectiveId());
            }
            return false;
        }
        for (int i = 0; i < amount && target->isAlive(); ++i) {
            const qint64 parent = room->currentHistoryEventId();
            if (parent <= 0) break;
            const int sequence = room->getTag("oltuntian_sequence").toInt() + 1;
            room->setTag("oltuntian_sequence", sequence);
            QVariantMap receipt{{"receipt", sequence}, {"parent", parent},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            QVariantList receipts = target->getTag("oltuntian_judges").toList();
            receipts << receipt;
            target->setTag("oltuntian_judges", receipts);
            room->broadcastSkillInvoke(objectName());
            JudgeStruct judge;
            judge.pattern = ".|heart";
            judge.good = false;
            judge.reason = objectName();
            judge.who = target;
            // The finish listener consumes only the matching accepted judge, even if the source retires during retrial.
            try { room->judge(judge); }
            catch (...) { consume(target, sequence); throw; }
            consume(target, sequence);
        }
        return false;
    }
};

class OLTuntianDistance : public DistanceSkillV2
{
public:
    OLTuntianDistance() : DistanceSkillV2("#oltuntian-dist") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return CorrectSkillResult::useAmount(-ctx.holder->getPile("field").length() * ctx.currentAmount);
    }
};
class OLZaoxian : public TriggerSkillV2
{
public:
    OLZaoxian() : TriggerSkillV2("olzaoxian")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "jixi";
    }
    static SkillInstanceRef receiptRef(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (active.activationRef == ctx.activationRef) {
            if (active.bypass_cost) addUsage(active);
            room->setPlayerMark(ctx.owner, objectName(), 1);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(this) && (player->getPile("field").length() >= 3 || player->canWake(objectName())))
            result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        ctx.invoker->peiyin(this);
        room->doSuperLightbox(ctx.invoker, objectName());
        if (room->changeMaxHpForAwakenSkill(target, -amount, objectName())) room->acquireSkillFromEffect(target, "jixi", ctx);
        const int sequence = room->getTag("olzaoxian_receipt_sequence").toInt() + 1;
        room->setTag("olzaoxian_receipt_sequence", sequence);
        // The granted extra turn remains owed after this awakening grant is retired.
        QVariantMap receipt{{"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"receipt", sequence}, {"amount", amount}};
        QVariantList receipts = target->getTag("olzaoxian_effects").toList();
        receipts << receipt;
        target->setTag("olzaoxian_effects", receipts);
        return false;
    }
};

class OLZaoxianExtraTurn : public TriggerSkillV2
{
public:
    OLZaoxianExtraTurn() : TriggerSkillV2("#olzaoxian-extra-turn")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
        global = true;
    }
    int getPriority(TriggerEvent) const override { return 1; }
    bool usesEventPriority() const override { return true; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const QVariantMap pending = ctx.extra_data.toMap();
        foreach (const QVariant &entry, ctx.owner->getTag("olzaoxian_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt") == pending.value("receipt")
                && OLZaoxian::receiptRef(receipt, "source") == ctx.sourceRef
                && OLZaoxian::receiptRef(receipt, "activation") == OLZaoxian::receiptRef(pending, "activation")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override { return isSourceAvailable(room, ctx); }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::NotActive) return true;
        foreach (const QVariant &entry, player->getTag("olzaoxian_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("amount").toInt() <= 0) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = OLZaoxian::receiptRef(receipt, "source");
            // Retained continuation: original activation stays in the receipt, without a new reveal gate.
            if (!ctx.sourceRef.isValid() || !OLZaoxian::receiptRef(receipt, "activation").isValid()) continue;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.extra_data = receipt;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        QVariantList receipts = ctx.owner->getTag("olzaoxian_effects").toList();
        const QVariant id = ctx.extra_data.toMap().value("receipt");
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("receipt") == id) receipts.removeAt(i);
        ctx.owner->setTag("olzaoxian_effects", receipts);
        for (int i = 0; i < amount && target->isAlive(); ++i) room->executeExtraTurn(target, QList<Player::Phase>(), ctx.sourceRef.key.skillName, ctx.sourceRef);
        return false;
    }
};

OLChangbiaoCard::OLChangbiaoCard()
{
    setSkillName("olchangbiao");
    will_throw = false;
    handling_method = Card::MethodUse;
}

class OLChangbiaoVS : public ViewAsSkillV2
{
public:
    OLChangbiaoVS() : ViewAsSkillV2("olchangbiao", 999) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && Slash::IsAvailable(request.initiator);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->isVirtualCard() || card->hasFlag("using")
            || request.selectedCardIds.contains(card->getEffectiveId())) return false;
        const int id = card->getEffectiveId();
        if (!request.initiator->handCards().contains(id) && !request.initiator->getHandPile().contains(id)) return false;
        Slash slash(Card::SuitToBeDecided, 0);
        slash.addSubcards(request.selectedCardIds);
        slash.addSubcard(id);
        slash.setSkillName(objectName());
        return !request.initiator->isCardLimited(&slash, Card::MethodUse, true);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest selected = request;
        selected.selectedCardIds.clear();
        foreach (int id, request.selectedCardIds) {
            if (!canSelectCard(selected, Sanguosha->getCard(id))) return false;
            selected.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::SuitToBeDecided, 0);
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        return slash;
    }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        return canActivate(request) && cardSelectionFeasible(request);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || !ctx.initiator || !ctx.sourceRef.isValid() || !ctx.activationRef.isValid()) return FinishSkill;
        // Only an accepted effect, including waived payment, installs the delayed benefit metadata.
        ctx.use_card->setTag("olchangbiao_accepted", QVariantMap{{"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
            {"activation_id", ctx.activationRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}});
        return ContinueEffects;
    }
};

class OLChangbiao : public TriggerSkillV2
{
public:
    OLChangbiao() : TriggerSkillV2("olchangbiao")
    {
        events << CardUsed << EventPhaseEnd << EventPhaseChanging;
        view_as_skill = new OLChangbiaoVS;
        global = true;
        frequency = Compulsory;
    }
    static SkillInstanceRef receiptRef(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!player || use.from != player || !use.card || !use.card->isKindOf("Slash")) return true;
            QVariantMap receipt = use.card->getTag("olchangbiao_accepted").toMap();
            if (receipt.isEmpty() || !(receiptRef(receipt, "source") == use.sourceRef)
                || !(receiptRef(receipt, "activation") == use.activationRef)) return true;
            const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            const QVariant phase = room->historyScopes().value("phase_id");
            if (useId <= 0 || phase.toLongLong() <= 0) return true;
            QVariantList receipts = player->getTag("olchangbiao_effects").toList();
            foreach (const QVariant &entry, receipts)
                if (entry.toMap().value("use_id").toLongLong() == useId) return true;
            const int sequence = room->getTag("olchangbiao_receipt_sequence").toInt() + 1;
            room->setTag("olchangbiao_receipt_sequence", sequence);
            receipt.insert("receipt", sequence);
            receipt.insert("use_id", QString::number(useId));
            receipt.insert("phase", phase);
            receipt.insert("materials", use.card->subcardsLength());
            receipts << receipt;
            player->setTag("olchangbiao_effects", receipts);
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play) {
            foreach (ServerPlayer *holder, room->getAllPlayers(true)) {
                QVariantList receipts = holder->getTag("olchangbiao_effects").toList();
                for (int i = receipts.length() - 1; i >= 0; --i)
                    if (finishedOLPlayPhase(room, receipts.at(i).toMap().value("phase"), player)) receipts.removeAt(i);
                holder->setTag("olchangbiao_effects", receipts);
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const QVariantMap pending = ctx.extra_data.toMap();
        foreach (const QVariant &entry, ctx.owner->getTag("olchangbiao_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt") == pending.value("receipt") && receiptRef(receipt, "source") == ctx.sourceRef
                && receiptRef(receipt, "activation") == receiptRef(pending, "activation")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override { return isSourceAvailable(room, ctx); }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseEnd || !player || player->getPhase() != Player::Play) return true;
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return true;
        foreach (ServerPlayer *holder, room->getAlivePlayers()) {
            foreach (const QVariant &entry, holder->getTag("olchangbiao_effects").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("phase") != phase) continue;
                const QVariantMap damage = room->queryCardUseDamage(receipt.value("use_id").toLongLong());
                if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()) continue;
                const int amount = receipt.value("amount").toInt() * receipt.value("materials").toInt() * damage.value("items").toList().length();
                if (amount <= 0) continue;
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = ctx.invoker = ctx.initiator = holder;
                ctx.sourceRef = receiptRef(receipt, "source");
                if (!ctx.sourceRef.isValid() || !receiptRef(receipt, "activation").isValid()) continue;
                ctx.instanceID = receipt.value("receipt").toInt();
                ctx.current_event = event;
                ctx.original_data = &data;
                ctx.extra_data = receipt;
                ctx.setModifiedAmount(amount);
                contexts << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) target->drawCards(amount, objectName());
        return false;
    }
};

OLTiaoxinCard::OLTiaoxinCard()
{
    setSkillName("oltiaoxin");
}

class OLTiaoxin : public ViewAsSkillV2
{
public:
    OLTiaoxin() : ViewAsSkillV2("oltiaoxin") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.activationRef.isValid()) return 1;
        const QVariant bonus = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "extra_turn");
        return 1 + (bonus.toLongLong() > 0 && bonus == ctx.owner->getRoom()->historyScopes().value("turn_id") ? 1 : 0);
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLTiaoxinCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return request.initiator && target && target->isAlive() && selected.isEmpty() && target->inMyAttackRange(request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
    static int dealtDamage(Room *room, qint64 useId, ServerPlayer *victim)
    {
        if (useId <= 0) return -1;
        const QVariantMap damage = room->queryCardUseDamage(useId);
        if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()) return -1;
        foreach (const QVariant &fact, damage.value("items").toList())
            if (fact.toMap().value("data").toMap().value("to").toString() == victim->objectName()) return 1;
        return 0;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const int amount = getEffectiveAmount(ctx);
        const QVariant turn = room->historyScopes().value("turn_id");
        if (amount <= 0 || !ctx.invoker || !ctx.invoker->isAlive()) return ContinueEffects;
        CardUseStruct slash;
        if (target->canSlash(ctx.invoker, nullptr, false))
            slash = room->askForUseSlashToStruct(target, ctx.invoker, "@oltiaoxin-slash:" + ctx.invoker->objectName(), true, false, false,
                ctx.invoker, ctx.use_card, "oltiaoxin_slash");
        if (!ctx.invoker->isAlive() || !target->isAlive()) return ContinueEffects;
        // An absent answer is known; an accepted answer needs exact-use evidence before declaring it harmless.
        if (slash.card && dealtDamage(room, slash.targetModReveal.useHistoryEventId, ctx.invoker) != 0) return ContinueEffects;
        if (!ctx.invoker->canDiscard(target, "he")) return ContinueEffects;
        QList<int> ids;
        for (int i = 0; i < amount; ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard, ids);
            if (id < 0 || ids.contains(id) || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || !ctx.invoker->canDiscard(target, id)) break;
            ids << id;
        }
        if (ids.isEmpty()) return ContinueEffects;
        QList<int> valid;
        foreach (int id, ids)
            if (room->getCardOwner(id) == target && !Sanguosha->getCard(id)->hasFlag("using")
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && ctx.invoker->canDiscard(target, id)) valid << id;
        if (valid.isEmpty()) return ContinueEffects;
        room->throwCard(valid, objectName(), target, ctx.invoker);
        // The extra allowance belongs only to this live activation instance, not its reacquired replacement.
        const SkillInstanceRef &ref = ctx.activationRef;
        if (ctx.owner && ctx.owner->hasSkillInstance(ref.key.skillName, ref.key.instanceID))
            ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_turn", turn);
        return ContinueEffects;
    }
};

class OLZhiji : public TriggerSkillV2
{
public:
    OLZhiji() : TriggerSkillV2("olzhiji")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "tenyearguanxing";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (active.activationRef.ownerObjectName == ctx.activationRef.ownerObjectName
            && active.activationRef.key.skillName == ctx.activationRef.key.skillName
            && active.activationRef.key.instanceID == ctx.activationRef.key.instanceID) {
            if (active.bypass_cost) addUsage(active);
            room->setPlayerMark(ctx.owner, objectName(), 1);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->hasSkill(this)
            && (player->getPhase() == Player::Start || player->getPhase() == Player::Finish)
            && (player->isKongcheng() || player->canWake(objectName()))) result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ctx.extra_data = ctx.invoker->isWounded() && room->askForChoice(ctx.invoker, objectName(), "recover+draw") == "recover";
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.owner->isKongcheng()) {
            LogMessage log;
            log.type = "#ZhijiWake";
            log.from = ctx.invoker;
            log.arg = objectName();
            room->sendLog(log);
        }
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.invoker, objectName());
        room->doSuperLightbox(ctx.invoker, objectName());
        if (ctx.extra_data.toBool()) room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        else target->drawCards(2 * amount, objectName());
        // Awakening grants a permanent acquired skill, which survives removal of this awakening instance.
        if (target->isAlive() && room->changeMaxHpForAwakenSkill(target, -amount, objectName()))
            room->acquireSkillFromEffect(target, "tenyearguanxing", ctx);
        return false;
    }
};

OLZaiqiCard::OLZaiqiCard() { setSkillName("olzaiqi"); }

class OLZaiqi : public TriggerSkillV2
{
public:
    OLZaiqi() : TriggerSkillV2("olzaiqi") { events << EventPhaseEnd; }
    static int redCards(Room *room)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return -1;
        QVariantMap filter{{"turn_id", turn}};
        int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool()) return -1;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap();
                if (move.value("to_place").toInt() != Player::DiscardPile) continue;
                const QVariantMap card = move.value("card").toMap();
                if (!card.contains("suit")) return -1;
                const int suit = card.value("suit").toInt();
                if (suit == Card::Heart || suit == Card::Diamond) ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            filter.insert("after", page.value("next_after"));
        }
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Discard && redCards(room) > 0)
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = qMin(redCards(room), int(room->getAlivePlayers().length()));
        if (count <= 0) return false;
        ctx.targets = room->askForPlayersChosen(ctx.owner, room->getAlivePlayers(), objectName(), 0, count, "@mobilezaiqi:" + QString::number(count), true);
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.extra_data.toMap().contains("recoverer")) {
            ServerPlayer *recoverer = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recoverer").toString(), true);
            room->recover(target, RecoverStruct(objectName(), recoverer, amount));
            return false;
        }
        QStringList choices{"draw"};
        if (ctx.owner->isAlive() && ctx.owner->isWounded()) choices << "recover=" + ctx.owner->objectName();
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant::fromValue(ctx.owner)) == "draw")
            target->drawCards(amount, objectName());
        else if (ctx.owner->isAlive()) {
            // The chooser pays no cost; the actual healed player receives its own target hook.
            SkillContext recovery = ctx;
            recovery.extra_data = QVariantMap{{"recoverer", target->objectName()}};
            skillEffect(event, room, ctx.owner, recovery, ctx.owner);
        }
        return false;
    }
};

class OLDuanliang : public OLCardConversion
{
public:
    OLDuanliang() : OLCardConversion("olduanliang", "supply_shortage", "BasicCard,EquipCard|black") {}
};
class OLDuanliangTargetMod : public TargetModSkillV2
{
public:
    OLDuanliangTargetMod() : TargetModSkillV2("#olduanliang-target", "SupplyShortage") { setBaseAmount(999); }
    static bool undamaged(Room *room, const Player *player)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return false;
        const QVariantMap facts = room->queryActualDamage({{"turn_id", turn}, {"from", player->objectName()}, {"limit", 1}});
        return facts.value("complete").toBool() && facts.value("attribution_complete").toBool() && facts.value("items").toList().isEmpty();
    }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != DistanceLimit || !ctx.holder) return CorrectSkillResult::noEffect();
        const ServerPlayer *server = qobject_cast<const ServerPlayer *>(ctx.holder);
        const bool eligible = server ? undamaged(server->getRoom(), ctx.holder) : ctx.holder->getMark("olduanliang_no_damage") > 0;
        return eligible ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class OLDuanliangHistory : public TriggerSkillV2
{
public:
    OLDuanliangHistory() : TriggerSkillV2("#olduanliang-history") { events << TurnStart << Damage << EventAcquireSkill; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // One public factual projection supports client targeting; server authority remains the current-turn journal.
        const QList<ServerPlayer *> players = event == TurnStart ? room->getAllPlayers(true)
            : QList<ServerPlayer *>() << (event == Damage ? data.value<DamageStruct>().from : player);
        foreach (ServerPlayer *holder, players)
            if (holder) room->setPlayerMark(holder, "olduanliang_no_damage", OLDuanliangTargetMod::undamaged(room, holder) ? 1 : 0);
        return true;
    }
};

class OLJiezi : public TriggerSkillV2
{
public:
    OLJiezi() : TriggerSkillV2("oljiezi") { events << EventPhaseSkipped << EventPhaseEnd; global = true; }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const QVariantMap receipt = ctx.owner->getTag("oljiezi_effect").toMap();
        return !receipt.isEmpty() && receipt == ctx.extra_data.toMap() && ref(receipt, "source") == ctx.sourceRef;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseEnd) return false;
        if (!player || !player->isAlive() || player->getPhase() != Player::Draw) return true;
        const QVariantMap receipt = player->getTag("oljiezi_effect").toMap();
        if (receipt.value("receipt").toInt() <= 0 || !ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = ref(receipt, "source");
        ctx.instanceID = receipt.value("receipt").toInt();
        ctx.current_event = event;
        ctx.original_data = &data;
        ctx.extra_data = receipt;
        ctx.is_forced = true;
        ctx.setModifiedAmount(receipt.value("amount").toInt());
        contexts << ctx;
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseSkipped || !player || player->getPhase() != Player::Draw) return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) if (owner->hasSkill(this)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd) { ctx.targets << ctx.invoker; return true; }
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(), "@oljiezi-target", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (event == EventPhaseEnd) {
            // Consume the applied phase grant before inserting it, so the inserted Draw phase cannot recurse forever.
            ctx.owner->removeTag("oljiezi_effect");
            room->setPlayerMark(ctx.owner, "&olxhzi", 0);
            if (amount > 0) {
                LogMessage log;
                log.type = "#ZhenguEffect";
                log.from = target;
                log.arg = objectName();
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName());
                for (int i = 0; i < amount; ++i) target->insertPhase(Player::Draw);
            }
            return false;
        }
        if (amount <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        bool least = target->getTag("oljiezi_effect").toMap().isEmpty();
        foreach (ServerPlayer *other, room->getOtherPlayers(target))
            if (other->getHandcardNum() < target->getHandcardNum()) least = false;
        if (!least) { target->drawCards(amount, objectName()); return false; }
        const int sequence = room->getTag("oljiezi_sequence").toInt() + 1;
        room->setTag("oljiezi_sequence", sequence);
        target->setTag("oljiezi_effect", QVariantMap{{"receipt", sequence}, {"amount", amount},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}});
        room->setPlayerMark(target, "&olxhzi", amount);
        return false;
    }
};

OLQiaobianCard::OLQiaobianCard() : QiaobianCard()
{
	mute = true;
	m_skillName = "olqiaobian";
}

static void moveOLQiaobianFieldCard(Room *room, ServerPlayer *zhanghe, ServerPlayer *from)
{
    if (!from->hasEquip() && from->getJudgingArea().isEmpty()) return;
    const int cardId = room->askForCardChosen(zhanghe, from, "ej", "olqiaobian");
    if (cardId < 0 || room->getCardOwner(cardId) != from) return;
    const Card *card = Sanguosha->getCard(cardId);
    const Player::Place place = room->getCardPlace(cardId);
    int equipIndex = -1;
    if (place == Player::PlaceEquip) {
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        if (!equip) return;
        equipIndex = int(equip->location());
    }
    QList<ServerPlayer *> tos;
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (equipIndex > -1) {
            if (p->getEquip(equipIndex) == nullptr) tos << p;
        } else if (!zhanghe->isProhibited(p, card) && !p->containsTrick(card->objectName())) {
            tos << p;
        }
    }
    if (tos.isEmpty()) return;
    const QVariant previous = room->getTag("OLQiaobianTarget");
    const auto restore = qScopeGuard([&] {
        if (previous.isValid()) room->setTag("OLQiaobianTarget", previous);
        else room->removeTag("OLQiaobianTarget");
    });
    room->setTag("OLQiaobianTarget", QVariant::fromValue(from));
    ServerPlayer *to = room->askForPlayerChosen(zhanghe, tos, "olqiaobian", "@qiaobian-to:::" + card->objectName());
    if (!to) return;
    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, from->objectName(), to->objectName());
    room->moveCardTo(card, from, to, place,
        CardMoveReason(CardMoveReason::S_REASON_TRANSFER, zhanghe->objectName(), "olqiaobian", ""), true);
}

class OLQiaobianVS : public ViewAsSkillV2
{
public:
    OLQiaobianVS() : ViewAsSkillV2("olqiaobian") {}
    int pendingPhase(const ActiveSkillRequest &request) const
    {
        return request.initiator && request.activationRef.isValid()
            ? request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "pending_phase", -1).toInt() : -1;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.pattern == "@@olqiaobian" && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && (pendingPhase(request) == Player::Draw || pendingPhase(request) == Player::Play);
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        const Card *card = ViewAsSkillV2::createCard(request);
        if (card) card->setTag("OLQiaobianPhase", pendingPhase(request));
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLQiaobianCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *candidate) const override
    {
        if (!candidate || !candidate->isAlive() || targets.contains(candidate)) return false;
        if (pendingPhase(request) == Player::Draw)
            return targets.size() < 2 && candidate != request.initiator && !candidate->isKongcheng();
        return pendingPhase(request) == Player::Play && targets.isEmpty()
            && (!candidate->getJudgingArea().isEmpty() || !candidate->getEquips().isEmpty());
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        if (targets.isEmpty()) return false;
        QList<const Player *> prefix;
        for (const Player *target : targets) {
            if (!canSelectTarget(request, prefix, target)) return false;
            prefix << target;
        }
        return true;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ctx.choice = QString::number(pendingPhase(request));
        return canActivate(request);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !actor->isAlive() || !target) return ContinueEffects;
        Room *room = actor->getRoom();
        const int phase = ctx.use_card ? ctx.use_card->getTag("OLQiaobianPhase", ctx.choice.toInt()).toInt() : ctx.choice.toInt();
        if (phase == Player::Draw) {
            const int count = getEffectiveAmount(ctx);
            for (int i = 0; i < count && actor->isAlive() && target->isAlive() && !target->isKongcheng(); ++i) {
                const int id = room->askForCardChosen(actor, target, "h", objectName());
                if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) continue;
                room->obtainCard(actor, Sanguosha->getCard(id),
                    CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, actor->objectName()), false);
            }
        } else if (phase == Player::Play) {
            moveOLQiaobianFieldCard(room, actor, target);
        }
        return ContinueEffects;
    }
};

class OLQiaobian : public TriggerSkillV2
{
public:
    OLQiaobian() : TriggerSkillV2("olqiaobian") { events << EventPhaseChanging; view_as_skill = new OLQiaobianVS; }
    static int phaseIndex(Player::Phase phase)
    {
        switch (phase) {
        case Player::Judge: return 1;
        case Player::Draw: return 2;
        case Player::Play: return 3;
        case Player::Discard: return 4;
        default: return 0;
        }
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Player::Phase phase = data.value<PhaseChangeStruct>().to;
        if (!player || !player->isAlive() || !player->hasSkill(this) || phaseIndex(phase) <= 0 || player->isSkipped(phase)) return {};
        if (!player->canDiscard(player, "he") && player->getMark("&olzhbian") <= 0) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const Player::Phase phase = ctx.original_data->value<PhaseChangeStruct>().to;
        const int index = phaseIndex(phase);
        if (index <= 0 || !owner || owner->isSkipped(phase)) return false;
        const QStringList phases{"judge", "draw", "play", "discard"};
        const QString phaseName = phases.at(index - 1);
        const int previous = owner->getMark("olqiaobianPhase");
        const auto restore = qScopeGuard([&] { room->setPlayerMark(owner, "olqiaobianPhase", previous); });
        room->setPlayerMark(owner, "olqiaobianPhase", int(phase));
        QStringList choices;
        if (owner->canDiscard(owner, "he")) choices << "card=" + phaseName;
        if (owner->getMark("&olzhbian") > 0) choices << "mark=" + phaseName;
        if (choices.isEmpty()) return false;
        choices << "cancel";
        const QString choice = room->askForChoice(owner, objectName(), choices.join("+"), *ctx.original_data);
        if (choice == "cancel") return false;
        if (choice.startsWith("mark")) {
            if (owner->getMark("&olzhbian") <= 0) return false;
            ctx.extra_data = QVariantMap{{"pay", "mark"}, {"phase", int(phase)}, {"index", index}};
        } else {
            const Card *selection = room->askForExchange(owner, objectName(), 1, 1, true,
                QString("#olqiaobian-%1").arg(index), false);
            if (!selection || selection->getSubcards().size() != 1) return false;
            const int id = selection->getSubcards().first();
            if (!owner->canDiscard(owner, id)) return false;
            ctx.extra_data = QVariantMap{{"pay", "card"}, {"id", id}, {"phase", int(phase)}, {"index", index}};
        }
        ctx.targets << owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const QVariantMap payment = ctx.extra_data.toMap();
        if (payment.value("pay").toString() == "mark") {
            if (!owner || owner->getMark("&olzhbian") <= 0) return false;
            owner->loseMark("&olzhbian");
            return true;
        }
        const int id = payment.value("id", -1).toInt();
        if (!owner || !owner->canDiscard(owner, id)) return false;
        room->throwCard(id, objectName(), owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != owner || !owner->isAlive()) return false;
        const QVariantMap payment = ctx.extra_data.toMap();
        const Player::Phase phase = Player::Phase(payment.value("phase").toInt());
        const int index = payment.value("index").toInt();
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = owner;
        log.arg = objectName();
        room->sendLog(log);
        owner->peiyin(this);
        room->notifySkillInvoked(owner, objectName());
        if (!owner->isSkipped(phase) && (index == 2 || index == 3)) {
            Room::AcceptedViewAsEffectScope prompt(room, owner, objectName(), ctx);
            if (prompt.isValid()) {
                const int previousPhase = owner->getMark("olqiaobianPhase");
                const auto restore = qScopeGuard([&] { room->setPlayerMark(owner, "olqiaobianPhase", previousPhase); });
                room->setPlayerMark(owner, "olqiaobianPhase", int(phase));
                owner->setSkillInstanceStateValue(objectName(), prompt.activationRef().key.instanceID, "pending_phase", int(phase));
                room->askForUseCard(owner, "@@olqiaobian", QString("@olqiaobian-%1").arg(index), index, Card::MethodNone);
            }
        }
        owner->skip(phase, true);
        return false;
    }
};

class OLQiaobianGameStart : public TriggerSkillV2
{
public:
    OLQiaobianGameStart() : TriggerSkillV2("#olqiaobian") { events << GameStart; frequency = Compulsory; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->hasSkill("olqiaobian")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.targets = {owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (!target || amount <= 0) return false;
        room->sendCompulsoryTriggerLog(target, "olqiaobian", true, true);
        target->gainMark("&olzhbian", amount);
        return false;
    }
};

class OLQiaobianMark : public TriggerSkillV2
{
public:
    OLQiaobianMark() : TriggerSkillV2("#olqiaobian-mark") { events << EventPhaseStart; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::Finish) return true;
        QStringList records = player->property("SkillDescriptionRecord_olqiaobian").toStringList();
        const QString hand = QString::number(player->getHandcardNum());
        if (records.contains(hand)) return true;
        const bool empty = records.isEmpty();
        records << hand;
        room->safeSetPlayerProperty(player, "SkillDescriptionRecord_olqiaobian", records);
        player->setSkillDescriptionSwap("olqiaobian", "%arg11", records.join(","));
        room->changeTranslation(player, "olqiaobian", 1);
        if (!empty && player->hasSkill("olqiaobian")) {
            room->sendCompulsoryTriggerLog(player, "olqiaobian");
            player->gainMark("&olzhbian");
        }
        return true;
    }
};

class OLBeige : public TriggerSkillV2
{
public:
    OLBeige() : TriggerSkillV2("olbeige") { events << Damaged; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.to || !damage.to->isAlive() || !damage.card || !damage.card->isKindOf("Slash")) return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(this) && !owner->isNude()) result[owner] << objectName() + "->" + damage.to->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.preferredTarget || !ctx.preferredTarget->isAlive()
            || !ctx.invoker->askForSkillInvoke(this, ctx.preferredTarget)) return false;
        ctx.targets = QList<ServerPlayer *>() << ctx.preferredTarget;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap state = ctx.extra_data.toMap();
        const QString stage = state.value("stage").toString();
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (stage == "return") {
            foreach (int id, ListV2I(state.value("ids").toList()))
                if (target->isAlive() && room->getCardPlace(id) == Player::DiscardPile) room->obtainCard(target, id);
            return false;
        }
        if (stage == "outcome") {
            const QString suit = state.value("suit").toString();
            if (suit == "heart") room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
            else if (suit == "diamond") target->drawCards(2 * amount, objectName());
            else if (suit == "club") room->askForDiscard(target, objectName(), 2 * amount, 2 * amount, false, true);
            else if (suit == "spade") target->turnOver();
            return false;
        }
        ctx.invoker->peiyin(this);
        JudgeStruct judge;
        judge.good = true;
        judge.play_animation = false;
        judge.who = target;
        judge.reason = objectName();
        room->judge(judge);
        if (!judge.card || !ctx.invoker->isAlive()) return false;
        const QString suit = judge.card->getSuitString();
        const QString number = judge.card->getNumberString();
        // The optional payment follows the revealed judgement by rule; cancelling does not undo it.
        const QString prompt = "@olbeige-discard:" + target->objectName() + ":" + number
            + ":<img src='image/system/cardsuit/" + suit + ".png' height=17/>";
        const Card *selected = room->askForCard(ctx.invoker, "..", prompt, *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!selected || (selected->isVirtualCard() && selected->subcardsLength() != 1)) return false;
        const int id = selected->getEffectiveId();
        if (id < 0 || room->getCardOwner(id) != ctx.invoker || Sanguosha->getCard(id)->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || (!ctx.bypass_cost && !ctx.invoker->canDiscard(ctx.invoker, id))) return false;
        const Card *material = Sanguosha->getCard(id);
        QList<int> returns;
        if (material->getNumberString() == number) returns << id;
        if (material->getSuitString() == suit && !returns.contains(judge.card->getEffectiveId())) returns << judge.card->getEffectiveId();
        if (!ctx.bypass_cost) room->throwCard(id, objectName(), ctx.invoker);
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *recipient = suit == "heart" || suit == "diamond" ? target : damage.from;
        if (recipient && recipient->isAlive()) {
            SkillContext outcome = ctx;
            QVariantMap details;
            details.insert("stage", "outcome");
            details.insert("suit", suit);
            outcome.extra_data = details;
            outcome.targets = QList<ServerPlayer *>() << recipient;
            skillEffect(event, room, owner, outcome, recipient);
        }
        if (ctx.invoker->isAlive() && !returns.isEmpty()) {
            SkillContext retrieve = ctx;
            QVariantMap details;
            details.insert("stage", "return");
            details.insert("ids", ListI2V(returns));
            retrieve.extra_data = details;
            retrieve.targets = QList<ServerPlayer *>() << ctx.invoker;
            skillEffect(event, room, owner, retrieve, ctx.invoker);
        }
        return false;
    }
};

class OLJieming : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLJieming() : TriggerSkillV2("oljieming") { events << Damaged; waked_skills = "#oljieming"; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && data.value<DamageStruct>().damage > 0)
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(), "@oljieming-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const int count = ctx.original_data->value<DamageStruct>().damage;
        for (int i = 0; i < count && ctx.invoker->isAlive(); ++i) {
            const QList<ServerPlayer *> targets = i == 0 ? ctx.targets : QList<ServerPlayer *>();
            if (i == 0) {
                foreach (ServerPlayer *target, targets) skillEffect(event, room, player, ctx, target);
            } else {
                ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(), "@oljieming-invoke", true, true);
                if (!target) break;
                skillEffect(event, room, player, ctx, target);
            }
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->peiyin(this);
        const int upper = qMin(5, target->getMaxHp());
        target->drawCards(upper * getEffectiveAmount(ctx), objectName());
        const int excess = target->getHandcardNum() - upper;
        if (target->isAlive() && target->canDiscard(target, "h") && excess > 0)
            room->askForDiscard(target, objectName(), excess, excess);
        return false;
    }
};
class OLJiemingDeath : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLJiemingDeath() : TriggerSkillV2("#oljieming") { events << Death; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (player && data.value<DeathStruct>().who == player && player->hasSkill(this)) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), "oljieming", "@oljieming-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->peiyin("oljieming");
        const int upper = qMin(5, target->getMaxHp());
        target->drawCards(upper * getEffectiveAmount(ctx), "oljieming");
        const int excess = target->getHandcardNum() - upper;
        if (target->isAlive() && target->canDiscard(target, "h") && excess > 0)
            room->askForDiscard(target, "oljieming", excess, excess);
        return false;
    }
};
OLQiangxiCard::OLQiangxiCard() { setSkillName("olqiangxi"); }

class OLQiangxi : public ViewAsSkillV2
{
public:
    OLQiangxi() : ViewAsSkillV2("olqiangxi", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLQiangxiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && card->getEffectiveId() >= 0
            && card->isKindOf("Weapon") && !card->hasFlag("using") && request.initiator->hasCard(card)
            && !request.initiator->isJilei(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return true;
        if (request.selectedCardIds.length() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive() || target == request.initiator || !selected.isEmpty()
            || !request.activationRef.isValid()) return false;
        const auto &key = request.activationRef.key;
        const QVariantMap scopes = request.initiator->getSkillInstanceStateValue(key.skillName, key.instanceID, "turn_targets").toMap();
        const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator);
        QString turn;
        if (server) turn = server->getRoom()->historyScopes().value("turn_id").toString();
        else {
            // The mirrored active-scope stack supplies client legality without fabricating Room history.
            qint64 latest = 0;
            for (auto it = scopes.constBegin(); it != scopes.constEnd(); ++it)
                if (it.key().toLongLong() > latest) { latest = it.key().toLongLong(); turn = it.key(); }
        }
        return !scopes.value(turn).toStringList().contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        return targets.length() == 1 && canSelectTarget(request, QList<const Player *>(), targets.first());
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        QList<const Player *> targets;
        foreach (ServerPlayer *target, ctx.targets) targets << target;
        if (!cardSelectionFeasible(request) || !targetsFeasible(request, targets)) return false;
        if (request.selectedCardIds.isEmpty()) {
            // Source-less self damage is the alternative payment, before the target damage effect.
            room->damage(DamageStruct(objectName(), nullptr, ctx.invoker));
            return true;
        }
        if (!ctx.invoker->canDiscard(ctx.invoker, request.selectedCardIds.first())) return false;
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) target->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        return ContinueEffects;
    }
};

class OLQiangxiRecord : public TriggerSkillV2
{
public:
    OLQiangxiRecord() : TriggerSkillV2("#olqiangxi-record")
    {
        events << EventSkillInvoking << TurnStart << EventPhaseChanging << EventAcquireSkill;
        global = true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QString turn = room->historyScopes().value("turn_id").toString();
        if (turn.isEmpty() || turn == "0") return;
        if (event == EventSkillInvoking) {
            const SkillContext active = ctx.original_data->value<SkillContext>();
            const SkillInstanceRef &ref = active.activationRef;
            if (!ref.isValid() || ref.key.skillName != "olqiangxi") return;
            ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
            if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
            QVariantMap scopes = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "turn_targets").toMap();
            QStringList targets = scopes.value(turn).toStringList();
            foreach (ServerPlayer *target, active.targets)
                if (target && !targets.contains(target->objectName())) targets << target->objectName();
            scopes.insert(turn, targets);
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "turn_targets", scopes);
            return;
        }
        const bool ending = event == EventPhaseChanging && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive;
        if (event == EventPhaseChanging && !ending) return;
        foreach (ServerPlayer *holder, room->getAllPlayers(true)) {
            foreach (int id, holder->getSkillInstanceIds("olqiangxi")) {
                QVariantMap scopes = holder->getSkillInstanceStateValue("olqiangxi", id, "turn_targets").toMap();
                // Remove only the ending scope; a suspended outer turn retains its exact target allowance.
                if (ending) scopes.remove(turn);
                else if (!scopes.contains(turn)) scopes.insert(turn, QStringList());
                holder->setSkillInstanceStateValue("olqiangxi", id, "turn_targets", scopes);
            }
        }
    }
};

class OLNinge : public TriggerSkillV2
{
public:
    OLNinge() : TriggerSkillV2("olninge") { events << Damaged; frequency = Compulsory; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static bool isSecondDamage(Room *room, ServerPlayer *victim)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        const qint64 current = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (!victim || turn.toLongLong() <= 0 || current <= 0) return false;
        QVariantMap filter{{"turn_id", turn}, {"to", victim->objectName()}};
        int count = 0;
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                // Anchor to the actual second damage event, not the order of nested Damaged callbacks.
                if (++count == 2) return entry.toMap().value("event_id").toLongLong() == current;
            }
            if (!page.value("has_more").toBool()) return false;
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !isSecondDamage(room, player)) return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(this) && (owner == player || owner == damage.from)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.extra_data.toString() == "discard") {
            QList<int> ids;
            for (int i = 0; i < amount && ctx.invoker->canDiscard(target, "ej"); ++i) {
                const int id = room->askForCardChosen(ctx.invoker, target, "ej", objectName(), false, Card::MethodDiscard, ids);
                if (id < 0 || ids.contains(id) || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                    || (room->getCardPlace(id) != Player::PlaceEquip && room->getCardPlace(id) != Player::PlaceDelayedTrick)
                    || !ctx.invoker->canDiscard(target, id)) break;
                ids << id;
            }
            QList<int> valid;
            foreach (int id, ids)
                if (room->getCardOwner(id) == target && !Sanguosha->getCard(id)->hasFlag("using")
                    && (room->getCardPlace(id) == Player::PlaceEquip || room->getCardPlace(id) == Player::PlaceDelayedTrick)
                    && ctx.invoker->canDiscard(target, id)) valid << id;
            if (!valid.isEmpty()) room->throwCard(valid, objectName(), target, ctx.invoker);
            return false;
        }
        ctx.invoker->peiyin(this);
        target->drawCards(amount, objectName());
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
        if (victim && victim->isAlive() && ctx.invoker->isAlive() && ctx.invoker->canDiscard(victim, "ej")) {
            ctx.extra_data = "discard";
            skillEffect(event, room, ctx.owner, ctx, victim);
            ctx.extra_data.clear();
        }
        return false;
    }
};

OLJianmieCard::OLJianmieCard()
{
    setSkillName("oljianmie");
}

bool OLJianmieCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty() && to!=Self;
}



class OLJianmie : public ViewAsSkillV2
{
public:
    OLJianmie() : ViewAsSkillV2("oljianmie") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLJianmieCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        return candidate && request.initiator && selected.isEmpty() && candidate != request.initiator;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.length() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        CardEffectStruct effect;
        effect.from = ctx.invoker;
        effect.to = target;
        effect.card = ctx.use_card;
        const QVariant data = QVariant::fromValue(effect);
        const QString first = room->askForChoice(ctx.invoker, objectName(), "red+black", data);
        const QString second = room->askForChoice(target, objectName(), "red+black", data);
        const Card *cards = room->askForDiscard(ctx.invoker, objectName(), 999, 999, false, false, "", ".|" + first);
        const int own = cards ? cards->subcardsLength() : 0;
        cards = room->askForDiscard(target, objectName(), 999, 999, false, false, "", ".|" + second);
        const int other = cards ? cards->subcardsLength() : 0;
        if (own == other || !ctx.invoker->isAlive() || !target->isAlive()) return ContinueEffects;
        ServerPlayer *from = own > other ? ctx.invoker : target;
        ServerPlayer *to = own > other ? target : ctx.invoker;
        Card *duel = Sanguosha->cloneCard("duel");
        if (!duel) return ContinueEffects;
        duel->setSkillName("_oljianmie");
        if (from->canUse(duel, to)) {
            CardUseStruct use(duel, from, to);
            use.setOwnedCard(duel);
            room->useCardFromSkillEffect(use, ctx);
        } else delete duel;
        return ContinueEffects;
    }
};
OLMiejiCard::OLMiejiCard()
{
    setSkillName("olmieji");
    will_throw = false;
    handling_method = Card::MethodNone;
}

class OLMieji : public ViewAsSkillV2
{
public:
    OLMieji() : ViewAsSkillV2("olmieji", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLMiejiCard"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && card->getEffectiveId() >= 0
            && !card->hasFlag("using") && card->isKindOf("TrickCard") && request.initiator->getHandcards().contains(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        return request.initiator && target && target->isAlive() && target != request.initiator && targets.isEmpty() && !target->isKongcheng();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        return targets.length() == 1 && canSelectTarget(request, QList<const Player *>(), targets.first());
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        const int id = request.selectedCardIds.first();
        if (room->getCardOwner(id) != ctx.invoker || room->getCardPlace(id) != Player::PlaceHand) return false;
        // Placing the actual trick on top is payment; the target's discard is the effect.
        room->moveCardTo(Sanguosha->getCard(id), ctx.invoker, nullptr, Player::DrawPile,
            CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.invoker->objectName(), objectName(), QString()), true);
        return true;
    }
    static QList<int> discardable(Room *room, ServerPlayer *target)
    {
        QList<int> ids;
        foreach (const Card *card, target->getCards("he")) {
            const int id = card->getEffectiveId();
            if (id >= 0 && room->getCardOwner(id) == target && !card->hasFlag("using") && target->canDiscard(target, id)) ids << id;
        }
        return ids;
    }
    static int choose(Room *room, ServerPlayer *target, const QList<int> &ids)
    {
        if (ids.isEmpty()) return -1;
        if (ids.length() == 1) return ids.first();
        room->fillAG(ids, target);
        int id = -1;
        try { id = room->askForAG(target, ids, false, "olmieji"); }
        catch (...) { room->clearAG(target); throw; }
        room->clearAG(target);
        return ids.contains(id) ? id : ids.first();
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            const QList<int> available = discardable(room, target);
            if (available.isEmpty()) break;
            QList<int> tricks, other;
            foreach (int id, available) {
                if (Sanguosha->getCard(id)->isKindOf("TrickCard")) tricks << id;
                else other << id;
            }
            // Selecting a trick settles the one-card option; otherwise choose two available non-tricks.
            QList<int> first = tricks;
            if (other.length() >= 2 || available.length() == 1) first << other;
            const int id = choose(room, target, first);
            QList<int> ids;
            if (id >= 0) ids << id;
            if (other.contains(id) && other.length() > 1) {
                other.removeOne(id);
                const int second = choose(room, target, other);
                if (second >= 0) ids << second;
            }
            const QList<int> current = discardable(room, target);
            QList<int> valid;
            foreach (int chosen, ids) if (current.contains(chosen) && !valid.contains(chosen)) valid << chosen;
            if (!valid.isEmpty()) room->throwCard(valid, objectName(), target);
        }
        return ContinueEffects;
    }
};

class OLLihuo : public TriggerSkillV2
{
public:
    OLLihuo() : TriggerSkillV2("ollihuo") { events << CardFinished << ChangeSlash; global = true; }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    static bool convertible(const CardUseStruct &use)
    {
        if (!use.from || !use.card || use.card->objectName() != "slash") return false;
        FireSlash preview(use.card->getSuit(), use.card->getNumber());
        preview.addSubcard(use.card);
        preview.setSkillName("ollihuo");
        foreach (ServerPlayer *target, use.to) if (!use.from->canSlash(target, &preview, false)) return false;
        return true;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == ChangeSlash && player && player->isAlive() && player->hasSkill(this)
            && convertible(data.value<CardUseStruct>())) result[player] << objectName();
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !player || !player->isAlive() || use.from != player || use.targetModReveal.useHistoryEventId <= 0) return true;
        const QVariantMap receipt = use.card->getTag("ollihuo_effect").toMap();
        if (receipt.value("actor").toString() != player->objectName() || !ref(receipt, "source").isValid()
            || !ref(receipt, "activation").isValid() || receipt.value("amount").toInt() <= 0) return true;
        const QVariantMap damage = room->queryCardUseDamage(use.targetModReveal.useHistoryEventId);
        if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()
            || damage.value("items").toList().isEmpty()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = ref(receipt, "source");
        ctx.instanceID = receipt.value("receipt").toInt();
        ctx.current_event = event;
        ctx.original_data = &data;
        ctx.extra_data = receipt;
        ctx.setModifiedAmount(receipt.value("amount").toInt());
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.original_data || !ctx.owner || !ctx.owner->isAlive()) return false;
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        const QVariantMap receipt = card ? card->getTag("ollihuo_effect").toMap() : QVariantMap();
        return receipt == ctx.extra_data.toMap() && receipt.value("receipt").toInt() > 0
            && ref(receipt, "source") == ctx.sourceRef && ref(receipt, "activation").isValid();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == ChangeSlash && (!convertible(ctx.original_data->value<CardUseStruct>())
            || !ctx.invoker->askForSkillInvoke(this, *ctx.original_data, false))) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == ChangeSlash) {
            if (use.from != target || !convertible(use)) return false;
            FireSlash *slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
            slash->addSubcard(use.card);
            slash->setSkillName(objectName());
            slash->deleteLater();
            use.sourceRef = ctx.sourceRef;
            use.activationRef = ctx.activationRef;
            use.changeCard(slash);
            const int sequence = room->getTag("ollihuo_sequence").toInt() + 1;
            room->setTag("ollihuo_sequence", sequence);
            // The card conversion and its post-use obligation are one accepted effect.
            use.card->setTag("ollihuo_effect", QVariantMap{{"receipt", sequence}, {"amount", amount}, {"actor", target->objectName()},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}});
            ctx.original_data->setValue(use);
            return false;
        }
        if (use.card) use.card->removeTag("ollihuo_effect");
        room->sendCompulsoryTriggerLog(ctx.invoker, this);
        // This discard-or-HP-loss is the applied obligation, not a second skill activation's cost.
        const Card *selected = room->askForExchange(target, objectName(), amount, amount, true, "ollihuo0:", true);
        QList<int> ids = selected ? selected->getSubcards() : QList<int>();
        QSet<int> unique;
        bool valid = ids.length() == amount;
        foreach (int id, ids) {
            if (unique.contains(id) || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || !target->canDiscard(target, id)) valid = false;
            unique.insert(id);
        }
        if (valid) room->throwCard(ids, objectName(), target);
        else if (target->isAlive()) room->loseHp(HpLostStruct(target, amount, objectName(), ctx.invoker));
        return false;
    }
};

OLChunlaoCard::OLChunlaoCard()
{
    setSkillName("olchunlao");
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

class OLChunlaoViewAsSkill : public ViewAsSkillV2
{
public:
    OLChunlaoViewAsSkill() : ViewAsSkillV2("olchunlao", 1) { expand_pile = "wine"; }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLChunlaoCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern.contains("peach") && !request.initiator->getPile("wine").isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using")
            && request.initiator->getPile("wine").contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || !room->getCurrentDyingPlayer()) return false;
        room->throwCard(request.selectedCardIds, objectName(), ctx.invoker);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *dying = ctx.invoker->getRoom()->getCurrentDyingPlayer();
        if (dying && dying->isAlive()) skillEffect(ctx, dying);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && target->getHp() <= 0; ++i) {
            Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
            analeptic->setSkillName("_olchunlao");
            analeptic->deleteLater();
            // This rescue is an ordinary Analeptic effect under the already paid wine activation.
            room->useCardFromSkillEffect(CardUseStruct(analeptic, target, target, false), ctx, true);
        }
        return ContinueEffects;
    }
};

class OLChunlao : public TriggerSkillV2
{
public:
    OLChunlao() : TriggerSkillV2("olchunlao")
    {
        events << CardsMoveOneTime << HpLost;
        view_as_skill = new OLChunlaoViewAsSkill;
        frequency = Compulsory;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static QList<int> discarded(Room *room, ServerPlayer *owner, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        if (!move.from || !move.from->isAlive() || move.reason.m_playerId.isEmpty()
            || (move.from != owner && !owner->isAdjacentTo(move.from))
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return ids;
        foreach (int id, move.card_ids)
            if (room->getCardPlace(id) == Player::DiscardPile && Sanguosha->getCard(id)->isKindOf("Slash") && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
        return ids;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == HpLost) {
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->hasSkill(this) && owner->getPile("wine").length() >= 2) result[owner] << objectName();
        } else if (player && player->isAlive() && player->hasSkill(this) && !discarded(room, player, data.value<CardsMoveOneTimeStruct>()).isEmpty())
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<int> ids;
        if (event == CardsMoveOneTime) ids = discarded(room, ctx.owner, ctx.original_data->value<CardsMoveOneTimeStruct>());
        else {
            QList<int> remaining = ctx.owner->getPile("wine");
            if (remaining.length() < 2) return false;
            room->fillAG(remaining, ctx.owner);
            try {
                for (int i = 0; i < 2; ++i) {
                    const int id = room->askForAG(ctx.owner, remaining, i == 0, objectName(), "olchunlao0:");
                    if (!remaining.contains(id)) break;
                    ids << id;
                    remaining.removeOne(id);
                    room->takeAG(ctx.owner, id, false);
                }
            } catch (...) { room->clearAG(ctx.owner); throw; }
            room->clearAG(ctx.owner);
            if (ids.length() != 2) return false;
        }
        if (ids.isEmpty()) return false;
        ctx.extra_data = ListI2V(ids);
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        QList<int> ids;
        foreach (const QVariant &entry, ctx.extra_data.toList()) {
            const int id = entry.toInt();
            if (Sanguosha->getCard(id)->hasFlag("using")) continue;
            if (event == HpLost ? target->getPile("wine").contains(id) : room->getCardPlace(id) == Player::DiscardPile) ids << id;
        }
        if (ids.isEmpty() || (event == HpLost && ids.length() != 2)) return false;
        if (event == HpLost) { DummyCard cards(ids); room->obtainCard(target, &cards, false); }
        else target->addToPile("wine", ids);
        return false;
    }
};

class OLRenxin : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLRenxin() : TriggerSkillV2("olrenxin") { events << Dying; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        ServerPlayer *target = data.value<DyingStruct>().who;
        if (!target || target->getHp() > 0) return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            if (owner != target && owner->hasSkill(this) && owner->canDiscard(owner, "he")) result[owner] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DyingStruct>().who;
        if (!target || target == ctx.invoker || target->getHp() > 0) return false;
        const Card *card = room->askForCard(ctx.invoker, "EquipCard", "olrenxin0:" + target->objectName(),
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card || card->getEffectiveId() < 0 || card->getSubcards().length() > 1 || card->hasFlag("using")
            || !ctx.invoker->hasCard(Sanguosha->getCard(card->getEffectiveId()))
            || !ctx.invoker->canDiscard(ctx.invoker, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets << target;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != ctx.invoker || !ctx.invoker->canDiscard(ctx.invoker, id)
            || !Sanguosha->getCard(id)->isKindOf("EquipCard") || Sanguosha->getCard(id)->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        room->throwCard(id, objectName(), ctx.invoker);
        ctx.invoker->turnOver();
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->peiyin(this);
        const int amount = getEffectiveAmount(ctx) - target->getHp();
        if (amount > 0) room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        return false;
    }
};
class OLChengxiang : public TriggerSkillV2
{
public:
    OLChengxiang() : TriggerSkillV2("olchengxiang") { events << Damaged; m_baseAmount = 4; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this))
            for (int i = 0; i < data.value<DamageStruct>().damage; ++i) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = getEffectiveAmount(ctx);
        if (count <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        QList<int> cards;
        auto unclaimed = [&]() {
            QList<int> result;
            foreach (int id, cards)
                if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) result << id;
            return result;
        };
        try {
            for (int i = 0; i < count; ++i) cards << room->getNCards(1);
            room->fillAG(cards, target);
            QList<int> enabled = cards, chosen;
            int sum = 0;
            while (target->isAlive() && !enabled.isEmpty()) {
                const QList<int> candidates = enabled;
                foreach (int id, candidates) {
                    if (sum + Sanguosha->getCard(id)->getNumber() > 13) {
                        enabled.removeOne(id);
                        room->takeAG(nullptr, id, false);
                    }
                }
                if (enabled.isEmpty()) break;
                const int id = room->askForAG(target, enabled, !chosen.isEmpty(), objectName());
                if (!enabled.removeOne(id)) break;
                chosen << id;
                sum += Sanguosha->getCard(id)->getNumber();
                room->takeAG(target, id, false);
            }
            room->clearAG(target);
            if (target->isAlive() && !chosen.isEmpty()) {
                const QList<int> available = unclaimed();
                for (int i = chosen.length() - 1; i >= 0; --i) if (!available.contains(chosen.at(i))) chosen.removeAt(i);
                DummyCard selected(chosen);
                if (!chosen.isEmpty()) room->obtainCard(target, &selected);
            }
            const QList<int> remainder = unclaimed();
            if (!remainder.isEmpty()) {
                DummyCard discard(remainder);
                CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, target->objectName(), objectName(), "");
                room->throwCard(&discard, reason, nullptr);
            }
        } catch (...) {
            // Preserve only still-unclaimed deck cards when a nested resolution aborts inspection.
            room->clearAG(target);
            room->returnToTopDrawPile(unclaimed());
            throw;
        }
        return false;
    }
};
OLGanluCard::OLGanluCard()
{
    setSkillName("olganlu");
	will_throw = false;
}



bool OLGanluCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	foreach(const Player *to, targets){
		if(to->hasEquip())
			return targets.length() == 2;
	}
	return false;
}

bool OLGanluCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.length()<2;
}



class OLGanlu : public ViewAsSkillV2
{
public:
    OLGanlu() : ViewAsSkillV2("olganlu", 999) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLGanluCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && card->getEffectiveId() >= 0 && !card->hasFlag("using")
            && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.initiator->hasCard(card) && !request.initiator->isJilei(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        ActiveSkillRequest rebuilt = request;
        rebuilt.selectedCardIds.clear();
        foreach (int id, request.selectedCardIds) {
            if (!canSelectCard(rebuilt, Sanguosha->getCard(id))) return false;
            rebuilt.selectedCardIds << id;
        }
        return true;
    }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *candidate) const override
    {
        return candidate && selected.length() < 2 && !selected.contains(candidate);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (!request.initiator || selected.length() != 2 || (!selected.first()->hasEquip() && !selected.last()->hasEquip())) return false;
        const int difference = qAbs(selected.first()->getEquips().length() - selected.last()->getEquips().length());
        const int required = difference <= request.initiator->getLostHp() ? 0 : difference;
        return request.selectedCardIds.length() == required;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        QList<const Player *> targets;
        foreach (ServerPlayer *target, ctx.targets) targets << target;
        if (!ctx.initiator || !cardSelectionFeasible(request) || !targetsFeasible(request, targets)) return false;
        foreach (int id, request.selectedCardIds) if (!ctx.initiator->canDiscard(ctx.initiator, id)) return false;
        // Exactly X cards are paid before any equipment is exchanged.
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (targets.length() != 2) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        LogMessage log;
        log.type = "#GanluSwap";
        log.from = ctx.invoker;
        log.to = targets;
        room->sendLog(log);
        room->swapEquips(targets.first(), targets.last(), objectName());
        return ContinueEffects;
    }
};
class OLBuyi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLBuyi() : TriggerSkillV2("olbuyi") { events << Dying; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        ServerPlayer *target = data.value<DyingStruct>().who;
        if (!target || target->isNude() || target->getHp() >= 1) return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) if (owner->hasSkill(this)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DyingStruct>().who;
        if (!target || target->isNude() || target->getHp() >= 1 || !ctx.invoker->askForSkillInvoke(this, target)) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->isNude()) return false;
        const int id = room->askForCardChosen(ctx.invoker, target, "he", "buyi");
        room->showCard(target, id);
        const Card *card = Sanguosha->getCard(id);
        if (card->getTypeId() != Card::TypeBasic && !target->isJilei(card)) {
            room->throwCard(card, objectName(), target);
            room->broadcastSkillInvoke(objectName());
            const int amount = getEffectiveAmount(ctx);
            if (amount > 0) room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        }
        return false;
    }
};
class OLQieting : public TriggerSkillV2
{
public:
    OLQieting() : TriggerSkillV2("olqieting") { events << EventPhaseChanging; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static bool history(Room *room, ServerPlayer *actor, bool *bonus)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (!actor || turn.toLongLong() <= 0) return false;
        const QVariantMap damage = room->queryActualDamage({{"turn_id", turn}, {"from", actor->objectName()}, {"limit", 1}});
        if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()
            || !damage.value("items").toList().isEmpty()) return false;
        *bonus = true;
        QVariantMap filter{{"turn_id", turn}, {"from", actor->objectName()}, {"kind", "use_card"}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap data = entry.toMap().value("data").toMap();
                const QVariantMap card = data.value("card").toMap();
                if (!card.contains("type") || !data.contains("targets")) return false;
                if (card.value("type").toInt() == Card::TypeSkill) continue;
                foreach (const QVariant &target, data.value("targets").toList())
                    if (target.toString() != actor->objectName()) *bonus = false;
            }
            if (!page.value("has_more").toBool()) return true;
            filter.insert("after", page.value("next_after"));
        }
    }
    static QStringList choices(ServerPlayer *actor, ServerPlayer *recipient)
    {
        QStringList result;
        if (actor && actor->isAlive() && recipient && recipient->isAlive())
            for (int slot = 0; slot < S_EQUIP_AREA_LENGTH; ++slot)
                if (actor->getEquip(slot) && !recipient->getEquip(slot) && recipient->hasEquipArea(slot)) result << QString::number(slot);
        result << "draw";
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        bool bonus = false;
        if (!player || !player->isAlive() || data.value<PhaseChangeStruct>().to != Player::NotActive || !history(room, player, &bonus)) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->isAlive() && owner->hasSkill(this)) result[owner] << objectName() + "->" + player->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *actor = ctx.preferredTarget;
        bool bonus = false;
        if (!actor || !actor->isAlive() || !history(room, actor, &bonus) || !ctx.invoker->askForSkillInvoke(this, actor)) return false;
        const QStringList available = choices(actor, ctx.invoker);
        const QString choice = room->askForChoice(ctx.invoker, objectName(), available.join("+"), QVariant::fromValue(actor));
        if (!available.contains(choice)) return false;
        QVariantMap details;
        details.insert("actor", actor->objectName());
        details.insert("bonus", bonus);
        details.insert("choice", choice);
        ctx.extra_data = details;
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        const QVariantMap details = ctx.extra_data.toMap();
        ServerPlayer *actor = room->findPlayerByObjectName(details.value("actor").toString());
        if (!actor || !actor->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        QString choice = details.value("choice").toString();
        const bool firstDraw = choice == "draw";
        for (int step = 0; step < 2 && target->isAlive() && actor->isAlive(); ++step) {
            if (choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
            else {
                bool ok = false;
                const int slot = choice.toInt(&ok);
                if (ok && slot >= 0 && slot < S_EQUIP_AREA_LENGTH && !target->getEquip(slot) && target->hasEquipArea(slot)) {
                    const Card *equip = actor->getEquip(slot);
                    if (equip && !equip->hasFlag("using")) room->moveCardTo(equip, target, Player::PlaceEquip);
                }
            }
            if (step > 0 || !details.value("bonus").toBool() || !target->isAlive() || !ctx.invoker->isAlive() || !actor->isAlive()) break;
            // The second choice is the other category; rebuild available equipment after nested moves.
            QStringList available = firstDraw ? choices(actor, target) : QStringList{"draw"};
            if (firstDraw) available.removeAll("draw");
            available << "cancel";
            choice = room->askForChoice(ctx.invoker, objectName(), available.join("+"), QVariant::fromValue(actor));
            if (!available.contains(choice) || choice == "cancel") break;
        }
        return false;
    }
};

OLZhijianCard::OLZhijianCard()
{
    setSkillName("olzhijian");
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool OLZhijianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (!targets.isEmpty() || to_select == Self) return false;
	const Card *card = Sanguosha->getCard(getEffectiveId());
	return !Self->isProhibited(to_select, card);
}



class OLZhijian : public ViewAsSkillV2
{
public:
    OLZhijian() : ViewAsSkillV2("olzhijian", 1) {}
    QString historyKey(const ActiveSkillRequest &) const override { return "OLZhijianCard"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && request.selectedCardIds.isEmpty()
            && !candidate->hasFlag("using") && candidate->isKindOf("EquipCard")
            && request.initiator->hasCard(candidate);
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        if (!candidate || !request.initiator || !selected.isEmpty() || candidate == request.initiator || request.selectedCardIds.length() != 1) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        return equip && candidate->getEquipArea(equip->location()) > 0 && !request.initiator->isProhibited(candidate, card);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.length() == 1; }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const int id = ctx.use_card->getSubcards().value(0, -1);
        if (id < 0 || room->getCardOwner(id) != ctx.initiator
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
        const Card *card = Sanguosha->getCard(id);
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        if (!equip || target->getEquipArea(equip->location()) <= 0) return ContinueEffects;
        QList<CardsMoveStruct> moves;
        const int slot = equip->location();
        if (target->getEquips(slot).length() >= target->getEquipArea(slot)) {
            const Card *old = target->getEquip(slot);
            if (!old) return ContinueEffects;
            moves << CardsMoveStruct(old->getEffectiveId(), nullptr, Player::DiscardPile,
                CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, target->objectName(), objectName(), ""));
        }
        moves << CardsMoveStruct(id, target, Player::PlaceEquip,
            CardMoveReason(CardMoveReason::S_REASON_USE, target->objectName(), objectName(), ""));
        room->moveCardsAtomic(moves, true);
        if (ctx.invoker->isAlive()) ctx.invoker->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};
class OLGuzheng : public TriggerSkillV2
{
public:
    OLGuzheng() : TriggerSkillV2("olguzheng") { events << CardsMoveOneTime << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    QList<int> discarded(Room *room, const CardsMoveOneTimeStruct &move) const
    {
        QList<int> ids;
        if (!move.from || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
            return ids;
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const Player::Place place = move.from_places.value(i);
            if ((place == Player::PlaceHand || place == Player::PlaceEquip)
                && room->getCardPlace(move.card_ids.at(i)) == Player::DiscardPile) ids << move.card_ids.at(i);
        }
        return ids;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        // Waiving payment never waives the quota, including an intercepted/skipped effect.
        if (active.bypass_cost && active.activationRef.ownerObjectName == ctx.activationRef.ownerObjectName
            && active.activationRef.key.skillName == ctx.activationRef.key.skillName
            && active.activationRef.key.instanceID == ctx.activationRef.key.instanceID)
            addUsage(active);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardsMoveOneTime) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.from || !move.from->isAlive() || discarded(room, move).length() < 2) return result;
        // CardsMoveOneTime is dispatched once per player by the movement service.
        if (player && player->isAlive() && player != move.from && player->hasSkill(this)) result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = discarded(room, move);
        ServerPlayer *target = move.from ? room->findPlayerByObjectName(move.from->objectName()) : nullptr;
        if (!target || !target->isAlive() || ids.length() < 2 || !ctx.invoker->askForSkillInvoke(this, target)) return false;
        ctx.extra_data = ListI2V(ids);
        ctx.targets << target;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> ids;
        foreach (int id, ListV2I(ctx.extra_data.toList())) if (room->getCardPlace(id) == Player::DiscardPile) ids << id;
        if (ids.isEmpty() || getEffectiveAmount(ctx) <= 0) return false;
        ctx.invoker->peiyin(this);
        room->fillAG(ids, ctx.invoker);
        QList<int> returned;
        for (int i = 0; i < getEffectiveAmount(ctx) && !ids.isEmpty(); ++i) {
            const int id = room->askForAG(ctx.invoker, ids, false, objectName());
            if (!ids.removeOne(id)) break;
            returned << id;
            room->takeAG(ctx.invoker, id, false, QList<ServerPlayer *>() << ctx.invoker);
        }
        room->clearAG(ctx.invoker);
        if (!returned.isEmpty()) {
            DummyCard give(returned);
            room->obtainCard(target, &give);
        }
        if (!ctx.invoker->isAlive() || !ctx.invoker->askForSkillInvoke(this, "olguzheng0", false)) return false;
        // Returning the chosen cards can trigger nested movement; recheck every remainder.
        QList<int> remaining;
        foreach (int id, ids) if (room->getCardPlace(id) == Player::DiscardPile) remaining << id;
        if (!remaining.isEmpty()) {
            DummyCard get(remaining);
            room->obtainCard(ctx.invoker, &get);
        }
        return false;
    }
};
class OLJiushiVs : public ViewAsSkillV2
{
public:
    OLJiushiVs() : ViewAsSkillV2("oljiushi") { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.initiator->faceUp()) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? Analeptic::IsAvailable(request.initiator)
            : request.pattern.contains("analeptic");
    }
    const Card *createCard(const ActiveSkillRequest &) const override
    {
        Analeptic *card = new Analeptic(Card::NoSuit, 0);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Analeptic"; }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        return ctx.initiator && ctx.initiator->isAlive() && ctx.initiator->faceUp();
    }
};

class OLJiushi : public TriggerSkillV2
{
public:
    OLJiushi() : TriggerSkillV2("oljiushi")
    {
        events << PreCardUsed << DamageDone << DamageComplete << CardsMoveOneTime << EventPhaseChanging;
        view_as_skill = new OLJiushiVs;
        global = true;
    }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent triggerEvent) const override
    {
        return triggerEvent == PreCardUsed ? 5 : TriggerSkill::getPriority(triggerEvent);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            for (ServerPlayer *holder : room->getAllPlayers(true))
                for (int id : holder->getSkillInstanceIds(objectName()))
                    holder->removeSkillInstanceStateValue(objectName(), id, "damage_faces");
            return true;
        }
        if (event != DamageDone || !player || data.value<DamageStruct>().to != player) return true;
        const qint64 damageId = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (damageId <= 0) return true;
        for (int id : player->getSkillInstanceIds(objectName())) {
            QVariantMap faces = player->getSkillInstanceStateValue(objectName(), id, "damage_faces").toMap();
            faces.insert(QString::number(damageId), !player->faceUp());
            player->setSkillInstanceStateValue(objectName(), id, "damage_faces", faces);
        }
        return true;
    }
    SkillContext oneContext(Room *room, ServerPlayer *player, QVariant &data, TriggerEvent event, int id, const QString &choice) const
    {
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.instanceID = id;
        ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
        ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
        bool amountOk = false;
        ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
        if (!amountOk) ctx.amount = getBaseAmount();
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.choice = choice;
        ctx.targets = {player};
        return ctx;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event == DamageComplete) {
            if (!player || data.value<DamageStruct>().to != player) return true;
            const qint64 damageId = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
            if (damageId <= 0) return true;
            for (int id : player->getValidSkillInstanceIds(objectName())) {
                const QVariantMap faces = player->getSkillInstanceStateValue(objectName(), id, "damage_faces").toMap();
                if (!faces.value(QString::number(damageId)).toBool() || !player->isAlive() || player->faceUp()) continue;
                SkillContext ctx = oneContext(room, player, data, event, id, "damage");
                if (ctx.sourceRef.isValid()) contexts << ctx;
            }
            return true;
        }
        if ((event != PreCardUsed && event != CardsMoveOneTime) || !player || !player->isAlive()) return false;
        const QList<int> ids = player->getValidSkillInstanceIds(objectName());
        if (ids.isEmpty()) return true;
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && (use.card->getSkillNames().contains(objectName()) || (!player->faceUp() && use.card->hasTip("luoying")))) {
                SkillContext ctx = oneContext(room, player, data, event, ids.first(), QString());
                if (ctx.sourceRef.isValid()) contexts << ctx;
            }
            return true;
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::PlaceHand && move.to == player && move.reason.m_skillName == "luoying" && !move.card_ids.isEmpty()) {
            SkillContext ctx = oneContext(room, player, data, event, ids.first(), QString());
            if (ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool cost(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == DamageComplete) {
            if (!ctx.owner || ctx.owner->faceUp() || !ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data)) return false;
            ctx.targets = {ctx.owner};
            return true;
        }
        if (!player) return false;
        ctx.targets = {player};
        if (event == CardsMoveOneTime) {
            if (player->faceUp() || player->hasFlag("CurrentPlayer")) return true;
            const int projected = player->getMark("luoyingNum-SelfClear") + ctx.original_data->value<CardsMoveOneTimeStruct>().card_ids.length();
            if (projected < player->getMaxHp()) return true;
            ctx.choice = player->askForSkillInvoke(this, *ctx.original_data) ? "flip" : QString();
            return true;
        }
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == DamageComplete) {
            if (!target->faceUp() && getEffectiveAmount(ctx) > 0) {
                room->broadcastSkillInvoke(objectName(), 3);
                target->turnOver();
            }
            return false;
        }
        if (event == PreCardUsed) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (use.card && use.card->getSkillNames().contains(objectName())) target->turnOver();
            if (!target->faceUp() && use.card && use.card->hasTip("luoying")) {
                use.no_respond_list << "_ALL_TARGETS";
                ctx.original_data->setValue(use);
            }
            return false;
        }
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.to != target || move.reason.m_skillName != "luoying") return false;
        foreach (int id, move.card_ids) {
            target->addMark("luoyingNum-SelfClear");
            if (target->hasCard(id)) room->setCardTip(id, "luoying");
        }
        if (ctx.choice == "flip" && !target->faceUp() && !target->hasFlag("CurrentPlayer")
            && target->getMark("luoyingNum-SelfClear") >= target->getMaxHp() && getEffectiveAmount(ctx) > 0) {
            target->setMark("luoyingNum-SelfClear", 0);
            room->broadcastSkillInvoke(objectName(), 3);
            target->turnOver();
        }
        return false;
    }
};

class OLJizhi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLJizhi() : TriggerSkillV2("oljizhi") { events << CardUsed; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const Card *card = data.value<CardUseStruct>().card;
        if (player && player->isAlive() && player->hasSkill(this) && card && card->isKindOf("TrickCard") && !card->isVirtualCard())
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->peiyin("jizhi");
        const QList<int> cards = target->drawCardsList(getEffectiveAmount(ctx), objectName());
        foreach (int id, cards) {
            if (target->isAlive() && Sanguosha->getCard(id)->isKindOf("BasicCard") && target->hasCard(id)
                && room->askForCard(target, QString::number(id), "oljizhi0:")) room->addMaxCards(target, 1);
        }
        return false;
    }
};
class OLQicai : public TargetModSkillV2
{
public:
    OLQicai() : TargetModSkillV2("olqicai", ".") { setBaseAmount(999); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == DistanceLimit && ctx.card && ctx.card->isKindOf("TrickCard")
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

// Luoying cards belong to the precise Jiushi grant, not a global Qicai definition.
class OLJiushiTargetMod : public TargetModSkillV2
{
public:
    OLJiushiTargetMod() : TargetModSkillV2("#oljiushi-target", ".") { setBaseAmount(999); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == DistanceLimit && ctx.card && ctx.card->hasTip("luoying") && !ctx.holder->faceUp()
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};
class OLQicaiLimit : public CardLimitSkill
{
public:
	OLQicaiLimit() : CardLimitSkill("#olqicai-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "discard";//设置为限制弃置
	}

	QString limitPattern(const Player *target, const Card *card) const
	{
		if(card->isKindOf("Armor")||card->isKindOf("Treasure")){
			foreach (const Player *p, target->getAliveSiblings()) {//获取其他角色
				if (p->getEquipsId().contains(card->getId())//这张牌在他的装备区
					&&p->hasSkill("olqicai"))//且这个角色拥有奇才
					return card->toString();//则这张牌不能被target弃置
			}
		}
		return "";
	}
};

class OL2HuojiVs : public OLCardConversion
{
public:
    OL2HuojiVs() : OLCardConversion("ol2huoji", "fire_attack", ".|red") {}
};
class OL2Huoji : public TriggerSkillV2
{
public:
    OL2Huoji() : TriggerSkillV2("ol2huoji") { events << CardEffected; view_as_skill = new OL2HuojiVs; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 0; }
    bool usesEventPriority() const override { return true; }
    int getEffectIndex(const ServerPlayer *player, const Card *) const override
    {
        int index = qsanRandomBounded(2) + 1;
        if (player->getGeneralName().contains("pangtong") || player->getGeneral2Name().contains("pangtong")) index += 2;
        return index;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (player && player->isAlive() && effect.to == player && effect.from && effect.from->isAlive()
            && effect.from->hasSkill(this) && effect.card && effect.card->isKindOf("FireAttack"))
            result[effect.from] << objectName() + "->" + player->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.preferredTarget) return false;
        ctx.targets << ctx.preferredTarget;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        if (amount <= 0 || !effect.card || effect.from != ctx.invoker || effect.to != target) return false;
        // Preserve the ordinary trick's nullification/offset hooks before replacing only its card effect.
        if (effect.nullified) {
            LogMessage log;
            log.type = "#CardNullified";
            log.from = target;
            log.card_str = effect.card->toString();
            room->sendLog(log);
            return true;
        }
        target->setFlags("Global_NonSkillNullify");
        if (!effect.offset_card) effect.offset_card = room->isCanceled(effect);
        ctx.original_data->setValue(effect);
        if (effect.offset_card && !room->getThread()->trigger(CardOffset, room, effect.from, *ctx.original_data)) return true;
        room->getThread()->trigger(CardOnEffect, room, target, *ctx.original_data);
        effect = ctx.original_data->value<CardEffectStruct>();
        if (!effect.card || effect.to != target || effect.from != ctx.invoker) return true;
        if (!target->isAlive() || !ctx.invoker->isAlive() || target->isKongcheng()) return true;
        const Card *shown = target->getRandomHandCard();
        if (!shown) return true;
        const QString color = shown->getColorString();
        room->showCard(target, shown->getEffectiveId());
        if (!target->isAlive() || !ctx.invoker->isAlive()) return true;
        const Card *payment = room->askForCard(ctx.invoker, ".|" + color + "|.|hand",
            "ol2huoji0:" + target->objectName() + "::" + color, *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        const QList<int> ids = payment ? payment->getSubcards() : QList<int>();
        const int id = payment && !payment->isVirtualCard() ? payment->getEffectiveId() : (ids.length() == 1 ? ids.first() : -1);
        const Card *material = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (material && !material->hasFlag("using") && room->getCardOwner(id) == ctx.invoker
            && room->getCardPlace(id) == Player::PlaceHand && material->getColorString() == color
            && (ctx.bypass_cost || ctx.invoker->canDiscard(ctx.invoker, id))) {
            if (!ctx.bypass_cost) room->throwCard(material, objectName(), ctx.invoker);
            if (target->isAlive() && ctx.invoker->isAlive()) room->damage(DamageStruct(effect.card, ctx.invoker, target, amount, DamageStruct::Normal));
        }
        return true;
    }
};

class OL2KanpoVs : public OLCardConversion
{
public:
    OL2KanpoVs() : OLCardConversion("ol2kanpo", "nullification", ".|black", true) {}
};
class OL2Kanpo : public TriggerSkillV2
{
public:
    OL2Kanpo() : TriggerSkillV2("ol2kanpo")
    {
        events << CardUsed << TrickCardCanceling << CardFinished;
        view_as_skill = new OL2KanpoVs;
        frequency = Compulsory;
        global = true;
    }
    int getEffectIndex(const ServerPlayer *player, const Card *) const override
    {
        int index = qsanRandomBounded(2) + 1;
        if (player->getGeneralName().contains("pangtong") || player->getGeneral2Name().contains("pangtong")) index += 2;
        return index;
    }
    static SkillInstanceRef receiptRef(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap pending = ctx.extra_data.toMap();
        if (!pending.contains("receipt")) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        if (!effect.card) return false;
        foreach (const QVariant &entry, effect.card->getTag("ol2kanpo_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt == pending && receiptRef(receipt, "source") == ctx.sourceRef) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card) const_cast<Card *>(use.card)->removeTag("ol2kanpo_effects");
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != TrickCardCanceling) return false;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!player || !player->isAlive() || !effect.card) return true;
        foreach (const QVariant &entry, effect.card->getTag("ol2kanpo_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            SkillContext ctx;
            ctx.skill_name = objectName();
            // This event's player is the candidate nullification responder, not the original skill holder.
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = receiptRef(receipt, "source");
            if (!ctx.sourceRef.isValid() || !receiptRef(receipt, "activation").isValid()) continue;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.extra_data = receipt;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardUsed || !player || !player->isAlive() || !player->hasSkill(this)) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from == player && use.card && use.card->isKindOf("Nullification")) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || target != ctx.invoker) return false;
        if (event == TrickCardCanceling) return true;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.from != target) return false;
        const int sequence = room->getTag("ol2kanpo_receipt_sequence").toInt() + 1;
        room->setTag("ol2kanpo_receipt_sequence", sequence);
        QVariantList receipts = use.card->getTag("ol2kanpo_effects").toList();
        // Protection belongs to this physical/virtual use and survives subsequent source removal.
        receipts << QVariantMap{{"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"receipt", sequence}, {"amount", amount}};
        const_cast<Card *>(use.card)->setTag("ol2kanpo_effects", receipts);
        return false;
    }
};

class OLJiang : public TriggerSkillV2
{
public:
    OLJiang() : TriggerSkillV2("oljiang") { events << TargetSpecified << TargetConfirmed << CardsMoveOneTime; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static bool matches(const Card *card)
    {
        return card && (card->isKindOf("Duel") || (card->isKindOf("Slash") && card->isRed()));
    }
    static int firstDiscard(Room *room, const CardsMoveOneTimeStruct &move)
    {
        if (move.to_place != Player::DiscardPile) return -1;
        const QVariant turn = room->historyScopes().value("turn_id");
        const qint64 event = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
        if (turn.toLongLong() <= 0 || event <= 0) return -1;
        QVariantMap filter{{"turn_id", turn}};
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool()) return -1;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), movement = fact.value("data").toMap();
                if (movement.value("to_place").toInt() != int(Player::DiscardPile)) continue;
                const QVariantMap card = movement.value("card").toMap();
                const QStringList classes = card.value("classes").toStringList();
                if (!classes.contains("Duel") && !(classes.contains("Slash") && card.value("red").toBool())) continue;
                const int id = movement.value("card_id", -1).toInt();
                // Count the global first eligible discard, including events before this grant was acquired.
                return fact.value("event_id").toLongLong() == event && move.card_ids.contains(id)
                    && room->getCardPlace(id) == Player::DiscardPile ? id : -1;
            }
            if (!page.value("has_more").toBool()) return -1;
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == CardsMoveOneTime) {
            if (!player || !player->isAlive() || !player->hasSkill(this)
                || firstDiscard(room, data.value<CardsMoveOneTimeStruct>()) < 0) return result;
            result[player] << objectName();
        } else {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (player && player->isAlive() && player->hasSkill(this) && matches(use.card)
                && ((event == TargetSpecified && use.from == player) || (event == TargetConfirmed && use.to.contains(player))))
                result[player] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardsMoveOneTime) {
            const int id = firstDiscard(room, ctx.original_data->value<CardsMoveOneTimeStruct>());
            if (id < 0) return false;
            ctx.extra_data = id;
        }
        if (!ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != CardsMoveOneTime) return true;
        const int id = ctx.extra_data.toInt();
        if (room->getCardPlace(id) != Player::DiscardPile) return false;
        room->loseHp(HpLostStruct(ctx.invoker, 1, objectName(), ctx.invoker));
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        ctx.invoker->peiyin(this);
        if (event == CardsMoveOneTime) {
            const int id = ctx.extra_data.toInt();
            if (room->getCardPlace(id) == Player::DiscardPile) room->obtainCard(target, id, true);
        } else target->drawCards(amount, objectName());
        return false;
    }
};

class OLHunzi : public TriggerSkillV2
{
public:
    OLHunzi() : TriggerSkillV2("olhunzi")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "yingzi,yinghun";
    }
    static SkillInstanceRef receiptRef(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (active.activationRef == ctx.activationRef) {
            if (active.bypass_cost) addUsage(active);
            room->setPlayerMark(ctx.owner, objectName(), 1);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(this) && (player->getHp() == 1 || player->canWake(objectName()))) result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        ctx.invoker->peiyin(this);
        room->doSuperLightbox(ctx.invoker, objectName());
        if (room->changeMaxHpForAwakenSkill(target, -amount, objectName())) {
            room->acquireSkillFromEffect(target, "yingzi", ctx);
            if (target->isAlive()) room->acquireSkillFromEffect(target, "yinghun", ctx);
        }
        const int sequence = room->getTag("olhunzi_receipt_sequence").toInt() + 1;
        room->setTag("olhunzi_receipt_sequence", sequence);
        // The finish-phase benefit is already granted and survives removal of the awakening source.
        QVariantMap receipt{{"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"receipt", sequence}, {"amount", amount}, {"turn", room->historyScopes().value("turn_id")}};
        QVariantList receipts = target->getTag("olhunzi_effects").toList();
        receipts << receipt;
        target->setTag("olhunzi_effects", receipts);
        return false;
    }
};

class OLHunziFinish : public TriggerSkillV2
{
public:
    OLHunziFinish() : TriggerSkillV2("#olhunzi-finish")
    {
        events << EventPhaseStart << EventPhaseChanging;
        frequency = Compulsory;
        global = true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const QVariantMap pending = ctx.extra_data.toMap();
        foreach (const QVariant &entry, ctx.owner->getTag("olhunzi_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt") == pending.value("receipt")
                && receipt.value("turn") == room->historyScopes().value("turn_id")
                && OLHunzi::receiptRef(receipt, "source") == ctx.sourceRef
                && OLHunzi::receiptRef(receipt, "activation") == OLHunzi::receiptRef(pending, "activation")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override { return isSourceAvailable(room, ctx); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        const QVariant turn = room->historyScopes().value("turn_id");
        foreach (ServerPlayer *player, room->getAllPlayers(true)) {
            QVariantList receipts = player->getTag("olhunzi_effects").toList();
            for (int i = receipts.length() - 1; i >= 0; --i)
                if (receipts.at(i).toMap().value("turn") == turn) receipts.removeAt(i);
            player->setTag("olhunzi_effects", receipts);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::Finish) return true;
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return true;
        foreach (ServerPlayer *recipient, room->getAlivePlayers()) {
            foreach (const QVariant &entry, recipient->getTag("olhunzi_effects").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("turn") != turn || receipt.value("amount").toInt() <= 0) continue;
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = ctx.invoker = ctx.initiator = recipient;
                ctx.sourceRef = OLHunzi::receiptRef(receipt, "source");
                if (!ctx.sourceRef.isValid() || !OLHunzi::receiptRef(receipt, "activation").isValid()) continue;
                ctx.instanceID = receipt.value("receipt").toInt();
                ctx.original_data = &data;
                ctx.current_event = event;
                ctx.extra_data = receipt;
                ctx.setModifiedAmount(receipt.value("amount").toInt());
                contexts << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantMap receipt = ctx.extra_data.toMap();
        receipt.insert("recover", ctx.invoker->isWounded() && room->askForChoice(ctx.invoker, "olhunzi", "draw+recover") == "recover");
        ctx.extra_data = receipt;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QVariantMap pending = ctx.extra_data.toMap();
        // Keep the granted turn benefit for any additional finish phase; turn cleanup retires it.
        if (pending.value("recover").toBool()) room->recover(target, RecoverStruct("olhunzi", ctx.invoker, amount));
        else target->drawCards(2 * amount, "olhunzi");
        return false;
    }
};

class OLEnyuan : public TriggerSkillV2
{
public:
    OLEnyuan() : TriggerSkillV2("olenyuan") { events << CardsMoveOneTime << Damaged; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return result;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.from && move.from->isAlive() && move.from != move.to
                && move.card_ids.length() >= 2 && move.reason.m_reason != CardMoveReason::S_REASON_PREVIEWGIVE
                && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip))
                result[player] << objectName();
        } else {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from != player && damage.from->isAlive())
                for (int i = 0; i < damage.damage; ++i) result[player] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = nullptr;
        if (event == CardsMoveOneTime) {
            const Player *giver = ctx.original_data->value<CardsMoveOneTimeStruct>().from;
            if (giver) target = room->findPlayerByObjectName(giver->objectName());
        } else target = ctx.original_data->value<DamageStruct>().from;
        if (!target || !target->isAlive() || !ctx.invoker->isAlive()
            || !ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->peiyin(this, event == CardsMoveOneTime ? 1 : 2);
        const int amount = getEffectiveAmount(ctx);
        if (event == CardsMoveOneTime) target->drawCards(amount, objectName());
        else for (int i = 0; i < amount && target->isAlive() && ctx.invoker->isAlive(); ++i) {
            const Card *card = target->isKongcheng() ? nullptr : room->askForCard(target, ".|red|.|hand",
                "olenyuan0:" + ctx.invoker->objectName(), *ctx.original_data, Card::MethodNone);
            // The recipient's give-or-lose choice belongs to resolution, not the skill payment.
            const int id = card ? card->getEffectiveId() : -1;
            const Card *material = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (material && card->getSubcards().length() <= 1 && room->getCardOwner(id) == target
                && room->getCardPlace(id) == Player::PlaceHand && material->isRed() && !material->hasFlag("using"))
                room->giveCard(target, ctx.invoker, material, objectName());
            else room->loseHp(HpLostStruct(target, 1, objectName(), ctx.invoker));
        }
        return false;
    }
};
OLXuanhuoCard::OLXuanhuoCard()
{
    setSkillName("olxuanhuo");
    will_throw = false;
    handling_method = Card::MethodNone;
}

class OLXuanhuo : public TriggerSkillV2
{
public:
    OLXuanhuo() : TriggerSkillV2("olxuanhuo") { events << EventPhaseEnd; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Draw
            && player->getHandcardNum() >= 2 && !room->getOtherPlayers(player).isEmpty()) result[player] << objectName();
        return result;
    }
    static QList<int> materials(Room *room, ServerPlayer *owner, const QVariantList &entries)
    {
        QList<int> ids;
        foreach (const QVariant &entry, entries) {
            const int id = entry.toInt();
            if (ids.contains(id) || room->getCardOwner(id) != owner || room->getCardPlace(id) != Player::PlaceHand
                || Sanguosha->getCard(id)->hasFlag("using")) return QList<int>();
            ids << id;
        }
        return ids.length() == 2 ? ids : QList<int>();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *cards = room->askForExchange(ctx.owner, objectName(), 2, 2, false, "olxuanhuo0", true);
        if (!cards) return false;
        const QVariantList selected = ListI2V(cards->getSubcards());
        if (materials(room, ctx.owner, selected).isEmpty()) return false;
        const QList<ServerPlayer *> candidates = room->getOtherPlayers(ctx.owner);
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "olxuanhuo0");
        if (!target) return false;
        ctx.extra_data = QVariantMap{{"cards", selected}};
        ctx.targets << target;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return !materials(room, ctx.owner, ctx.extra_data.toMap().value("cards").toList()).isEmpty();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QVariantMap state = ctx.extra_data.toMap();
        if (state.contains("seize")) {
            ServerPlayer *victim = room->findPlayerByObjectName(state.value("seize").toString());
            if (!victim || !victim->isAlive()) return false;
            room->doGongxin(target, victim, victim->handCards(), objectName());
            QList<int> ids;
            for (int i = 0; i < 2 * amount && !victim->isNude(); ++i) {
                const int id = room->askForCardChosen(target, victim, "he", objectName(), true, Card::MethodNone, ids);
                if (id < 0 || ids.contains(id) || room->getCardOwner(id) != victim || Sanguosha->getCard(id)->hasFlag("using")
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) break;
                ids << id;
            }
            for (int i = ids.length() - 1; i >= 0; --i) {
                const int id = ids.at(i);
                if (room->getCardOwner(id) != victim || Sanguosha->getCard(id)->hasFlag("using")
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) ids.removeAt(i);
            }
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); }
            return false;
        }
        if (state.contains("attacker")) {
            ServerPlayer *attacker = room->findPlayerByObjectName(state.value("attacker").toString());
            if (!attacker || !attacker->isAlive() || !attacker->canSlash(target, false)) return false;
            if (!room->askForUseSlashTo(attacker, target, "olxuanhuo2:" + target->objectName() + ":" + ctx.owner->objectName(), false)
                && ctx.owner->isAlive()) {
                SkillContext seize = ctx;
                seize.extra_data = QVariantMap{{"seize", attacker->objectName()}};
                skillEffect(event, room, ctx.owner, seize, ctx.owner);
            }
            return false;
        }
        const QList<int> ids = materials(room, ctx.owner, state.value("cards").toList());
        if (ids.isEmpty()) return false;
        DummyCard gift(ids);
        room->giveCard(ctx.owner, target, &gift, objectName());
        if (!ctx.owner->isAlive() || !target->isAlive()) return false;
        QList<ServerPlayer *> candidates;
        foreach (ServerPlayer *other, room->getOtherPlayers(target)) if (target->canSlash(other, false)) candidates << other;
        if (candidates.isEmpty()) return false;
        ServerPlayer *victim = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "olxuanhuo1:" + target->objectName());
        if (victim) {
            SkillContext attack = ctx;
            attack.extra_data = QVariantMap{{"attacker", target->objectName()}};
            skillEffect(event, room, ctx.owner, attack, victim);
        }
        return false;
    }
};

class OLDangxian : public TriggerSkillV2
{
public:
    OLDangxian() : TriggerSkillV2("oldangxian")
    {
        events << EventPhaseStart << EventPhaseEnd << DamageDone;
        frequency = Compulsory;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        return ctx.owner && ctx.owner->isAlive();
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != DamageDone) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && damage.from->getMark("oldangxiandraw-PlayClear") > 0)
            damage.from->addMark("oldangxianDamage-PlayClear");
        return true;
    }
    static SkillContext bare(ServerPlayer *player, QVariant &data, TriggerEvent event, const QString &choice)
    {
        SkillContext ctx;
        ctx.skill_name = "oldangxian";
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.choice = choice;
        ctx.is_forced = true;
        ctx.targets = {player};
        return ctx;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive()) return event == EventPhaseStart || event == EventPhaseEnd;
        if (event == EventPhaseEnd) {
            if (player->getPhase() == Player::Play && player->getMark("oldangxiandraw-PlayClear") > 0
                && player->getMark("oldangxianDamage-PlayClear") < 1)
                contexts << bare(player, data, event, "punish");
            return true;
        }
        if (event != EventPhaseStart) return false;
        if (player->getPhase() == Player::Play && player->hasFlag("oldangxian")) {
            contexts << bare(player, data, event, "draw");
            return true;
        }
        if (player->getPhase() != Player::RoundStart || !player->hasSkill(this)) return true;
        const QList<int> ids = player->getValidSkillInstanceIds(objectName());
        if (ids.isEmpty()) return true;
        SkillContext ctx = bare(player, data, event, "extra");
        ctx.instanceID = ids.first();
        ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), ids.first()));
        ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
        if (!ctx.sourceRef.isValid()) return true;
        bool amountOk = false;
        ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
        if (!amountOk) ctx.amount = getBaseAmount();
        contexts << ctx;
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->isAlive();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (ctx.choice == "punish") {
            if (target->getMark("oldangxiandraw-PlayClear") <= 0 || target->getMark("oldangxianDamage-PlayClear") > 0) return false;
            room->sendCompulsoryTriggerLog(target, objectName());
            const int amount = qMax(1, getEffectiveAmount(ctx));
            room->damage(DamageStruct(objectName(), target, target, amount));
            return false;
        }
        if (ctx.choice == "extra") {
            if (getEffectiveAmount(ctx) <= 0) return false;
            room->sendCompulsoryTriggerLog(target, this);
            target->setFlags("oldangxian");
            target->insertPhase(Player::Play);
            target->setPhase(Player::RoundStart);
            room->broadcastProperty(target, "phase");
            return false;
        }
        if (!target->hasFlag("oldangxian")) return false;
        target->setFlags("-oldangxian");
        if (!target->askForSkillInvoke(this, "draw", false)) return false;
        target->addMark("oldangxiandraw-PlayClear");
        QList<int> ids = room->getDiscardPile();
        ids << room->getDrawPile();
        qsanShuffle(ids);
        foreach (int id, ids) {
            if (Sanguosha->getCard(id)->isKindOf("Slash")) {
                room->obtainCard(target, id, true);
                break;
            }
        }
        return false;
    }
};

class OLFuli : public TriggerSkillV2
{
public:
    OLFuli() : TriggerSkillV2("olfuli")
    {
        events << AskForPeaches << EventSkillInvoking;
        frequency = Limited;
        limit_mark = "@olfuli";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        // Waiving payment never waives the quota, including an intercepted/skipped effect.
        if (active.activationRef.ownerObjectName == ctx.activationRef.ownerObjectName
            && active.activationRef.key.skillName == ctx.activationRef.key.skillName
            && active.activationRef.key.instanceID == ctx.activationRef.key.instanceID) {
            if (active.bypass_cost) addUsage(active);
            if (ctx.owner->getMark(limit_mark) > 0) room->removePlayerMark(ctx.owner, limit_mark);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != AskForPeaches) return result;
        if (player && player->isAlive() && player->hasSkill(this) && data.value<DyingStruct>().who == player)
            result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    static qint64 damageTotal(Room *room, ServerPlayer *player)
    {
        QVariantMap filter;
        filter.insert("from", player->objectName());
        filter.insert("limit", 64);
        qint64 total = 0;
        while (true) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return -1;
            foreach (const QVariant &item, page.value("items").toList()) {
                const QVariantMap data = item.toMap().value("data").toMap();
                if (!data.contains("amount")) return -1;
                total += data.value("amount").toLongLong();
            }
            if (!page.value("has_more").toBool()) return total;
            // Keep the same immutable history view across pages; absence is never guessed.
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const qint64 damage = damageTotal(room, ctx.owner);
        if (damage < 0 || !ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.extra_data = QVariant::fromValue<qlonglong>(damage);
        ctx.targets << ctx.owner;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(ctx.invoker, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QSet<QString> kingdoms;
        foreach (ServerPlayer *player, room->getAlivePlayers()) kingdoms << player->getKingdom();
        const int goal = kingdoms.size() * getEffectiveAmount(ctx);
        if (goal <= 0) return false;
        const int recover = qMin(goal, target->getMaxHp()) - target->getHp();
        if (recover > 0) room->recover(target, RecoverStruct(objectName(), ctx.invoker, recover));
        if (target->isAlive() && target->getHandcardNum() < goal) target->drawCards(goal - target->getHandcardNum(), objectName());
        if (target->isAlive() && kingdoms.size() > ctx.extra_data.toLongLong()) target->turnOver();
        return false;
    }
};
class OLJiangchi : public TriggerSkillV2
{
public:
    OLJiangchi() : TriggerSkillV2("oljiangchi") { events << EventPhaseEnd; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Draw
            && player->getCardCount() > 0) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        const Card *card = room->askForCard(ctx.invoker, "..", "oljiangchi0", *ctx.original_data, Card::MethodNone);
        if (card && (card->getEffectiveId() < 0 || card->getSubcards().length() > 1 || card->hasFlag("using")
            || !ctx.invoker->hasCard(Sanguosha->getCard(card->getEffectiveId()))
            || ctx.invoker->isCardLimited(card, Card::MethodRecast))) return false;
        ctx.extra_data = card ? card->getEffectiveId() : -1;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (id < 0) return true;
        const Card *card = Sanguosha->getCard(id);
        if (room->getCardOwner(id) != ctx.invoker || card->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || ctx.invoker->isCardLimited(card, Card::MethodRecast)) return false;
        LogMessage log;
        log.type = "$RecastCard";
        log.from = ctx.invoker;
        log.card_str = card->toString();
        room->sendLog(log);
        ctx.invoker->broadcastSkillInvoke("@recast");
        room->moveCardTo(card, ctx.invoker, nullptr, Player::DiscardPile,
            CardMoveReason(CardMoveReason::S_REASON_RECAST, ctx.invoker->objectName(), objectName(), ""));
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        ctx.invoker->peiyin(this);
        const bool recast = ctx.extra_data.toInt() >= 0;
        target->drawCards(amount, recast ? "recast" : objectName());
        if (!target->isAlive()) return false;
        // These turn modifiers are applied receipts and persist if the skill is later lost.
        if (recast) room->addSlashJuli(target, 999);
        else room->setPlayerCardLimitation(target, "ignore", "Slash", true);
        room->addSlashCishu(target, recast ? amount : -amount);
        return false;
    }
};
OLZongxuanCard::OLZongxuanCard()
{
    setSkillName("olzongxuan");
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

class OLZongxuan : public TriggerSkillV2
{
public:
    OLZongxuan() : TriggerSkillV2("olzongxuan") { events << CardsMoveOneTime; }
    static bool firstDiscard(Room *room, const Player *from)
    {
        if (!from || room->historyScopes().value("turn_id").toLongLong() <= 0) return false;
        const qint64 current = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
        if (current <= 0) return false;
        QVariantMap filter{{"turn_id", room->historyScopes().value("turn_id")}, {"from", from->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool()) return false;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
                if (move.value("to_place").toInt() == Player::DiscardPile
                    && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
                    return fact.value("event_id").toLongLong() == current;
            }
            if (!page.value("has_more").toBool()) return false;
            filter.insert("after", page.value("next_after"));
        }
    }
    static QList<int> available(Room *room, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        if (move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return ids;
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const int id = move.card_ids.at(i);
            if ((move.from_places.value(i) == Player::PlaceHand || move.from_places.value(i) == Player::PlaceEquip)
                && room->getCardPlace(id) == Player::DiscardPile && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
        }
        return ids;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (available(room, move).isEmpty()) return result;
        if (move.from == player || (move.from == player->getNextAlive(player->aliveCount() - 1) && firstDiscard(room, move.from)))
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<int> remaining = available(room, ctx.original_data->value<CardsMoveOneTimeStruct>()), selected;
        if (remaining.isEmpty()) return false;
        room->fillAG(remaining, ctx.owner);
        try {
            while (!remaining.isEmpty()) {
                const int id = room->askForAG(ctx.owner, remaining, true, objectName(), "olzongxuan0");
                if (!remaining.contains(id)) break;
                selected << id;
                remaining.removeOne(id);
                room->takeAG(ctx.owner, id, false);
            }
        } catch (...) { room->clearAG(ctx.owner); throw; }
        room->clearAG(ctx.owner);
        if (selected.isEmpty()) return false;
        ctx.extra_data = ListI2V(selected);
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        QList<int> ids;
        foreach (const QVariant &entry, ctx.extra_data.toList()) {
            const int id = entry.toInt();
            if (!ids.contains(id) && room->getCardPlace(id) == Player::DiscardPile && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
        }
        if (ids.isEmpty()) return false;
        // Physical cards move once in the player's chosen order; the first-discard fact is never a usage counter.
        DummyCard cards(ids);
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, target->objectName(), objectName(), QString());
        room->moveCardTo(&cards, nullptr, Player::DrawPile, reason, false, true);
        return false;
    }
};

class OLZhiyan : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLZhiyan() : TriggerSkillV2("olzhiyan") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            if (owner->hasSkill(this) && (player == owner || player == owner->getNextAlive(owner->aliveCount() - 1)))
                result[owner] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(), "olzhiyan0", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        const QList<int> cards = target->drawCardsList(getEffectiveAmount(ctx), objectName());
        foreach (int id, cards) {
            if (!target->isAlive()) break;
            room->showCard(target, id);
            const Card *card = Sanguosha->getCard(id);
            if (card->isKindOf("EquipCard")) {
                if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand && card->isAvailable(target)) {
                    room->useCardFromSkillEffect(CardUseStruct(card, target), ctx);
                    room->recover(target, RecoverStruct(objectName(), ctx.invoker));
                }
            } else if (target->getHp() != ctx.invoker->getHp()) room->loseHp(target, 1, true, ctx.invoker, objectName());
        }
        return false;
    }
};
class OLJieZishou : public TriggerSkillV2
{
public:
    OLJieZishou() : TriggerSkillV2("oljiezishou") { events << DrawNCards << EventPhaseStart << EventPhaseChanging; global = true; }
    static SkillInstanceRef receiptRef(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    static bool dealtOtherDamage(Room *room, ServerPlayer *actor, const QVariant &turn)
    {
        if (turn.toLongLong() <= 0) return false;
        QVariantMap filter{{"turn_id", turn}, {"from", actor->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap damage = entry.toMap().value("data").toMap();
                if (!damage.value("to").toString().isEmpty() && damage.value("to").toString() != actor->objectName()) return true;
            }
            if (!page.value("has_more").toBool()) return false;
            filter.insert("after", page.value("next_after"));
        }
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const int receiptId = ctx.extra_data.toMap().value("receipt").toInt();
        if (receiptId <= 0) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("oljiezishou_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt").toInt() == receiptId && receiptRef(receipt, "source") == ctx.sourceRef
                && receiptRef(receipt, "activation") == receiptRef(ctx.extra_data.toMap(), "activation")
                && receipt.value("turn") == room->historyScopes().value("turn_id")) return true;
        }
        return false;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QVariant turn = room->historyScopes().value("turn_id");
            foreach (ServerPlayer *player, room->getAllPlayers(true)) {
                QVariantList receipts = player->getTag("oljiezishou_effects").toList();
                for (int i = receipts.length() - 1; i >= 0; --i)
                    if (receipts.at(i).toMap().value("turn") == turn) receipts.removeAt(i);
                player->setTag("oljiezishou_effects", receipts);
            }
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart) return false;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return true;
        const QVariant turn = room->historyScopes().value("turn_id");
        if (!dealtOtherDamage(room, player, turn)) return true;
        foreach (const QVariant &entry, player->getTag("oljiezishou_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("turn") != turn || receipt.value("amount").toInt() <= 0) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = receiptRef(receipt, "source");
            // A retained obligation continues its original source; it does not reactivate/reveal a retired grant.
            if (!ctx.sourceRef.isValid() || !receiptRef(receipt, "activation").isValid()) continue;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.extra_data = receipt;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == DrawNCards && player && player->isAlive() && player->hasSkill(this)
            && data.value<DrawStruct>().reason == "draw_phase") result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DrawNCards && !ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QSet<QString> kingdoms;
        foreach (ServerPlayer *player, room->getAlivePlayers()) kingdoms.insert(player->getKingdom());
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == DrawNCards) {
            ctx.invoker->peiyin(this);
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            if (draw.who != target) return false;
            draw.num += kingdoms.size() * amount;
            ctx.original_data->setValue(draw);
            // The applied turn obligation retains real provenance independently of the grant's lifetime.
            const int sequence = room->getTag("oljiezishou_receipt_sequence").toInt() + 1;
            room->setTag("oljiezishou_receipt_sequence", sequence);
            QVariantMap receipt{{"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
                {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
                {"receipt", sequence}, {"amount", amount}, {"turn", room->historyScopes().value("turn_id")}};
            QVariantList receipts = target->getTag("oljiezishou_effects").toList();
            bool replaced = false;
            for (int i = 0; i < receipts.length(); ++i) {
                const QVariantMap previous = receipts.at(i).toMap();
                if (previous.value("turn") == receipt.value("turn")
                    && receiptRef(previous, "activation") == ctx.activationRef && receiptRef(previous, "source") == ctx.sourceRef) {
                    receipt.insert("receipt", previous.value("receipt"));
                    receipts[i] = receipt;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) receipts << receipt;
            target->setTag("oljiezishou_effects", receipts);
        } else {
            // The obligation applies at each finish phase of this turn, just as the original turn flag did.
            room->askForDiscard(target, objectName(), kingdoms.size() * amount, kingdoms.size() * amount, false, true);
        }
        return false;
    }
};

class OLZongshi : public TriggerSkillV2
{
public:
    OLZongshi() : TriggerSkillV2("olzongshi") { events << DamageCaused << EventSkillInvoking; frequency = Compulsory; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const auto &ref = ctx.activationRef;
        if (!damage.from || !ref.isValid() || !ctx.owner) return false;
        const QString kingdom = ctx.extra_data.toMap().value("kingdom", damage.from->getKingdom()).toString();
        return !ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "kingdoms").toStringList().contains(kingdom);
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const auto &ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        QStringList kingdoms = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "kingdoms").toStringList();
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const QString kingdom = ctx.extra_data.toMap().value("kingdom", damage.from ? damage.from->getKingdom() : QString()).toString();
        if (!kingdom.isEmpty() && !kingdoms.contains(kingdom)) kingdoms << kingdom;
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "kingdoms", kingdoms);
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        // Waiving payment never waives the quota, including an intercepted/skipped effect.
        if (active.bypass_cost && active.activationRef.ownerObjectName == ctx.activationRef.ownerObjectName
            && active.activationRef.key.skillName == ctx.activationRef.key.skillName
            && active.activationRef.key.instanceID == ctx.activationRef.key.instanceID)
            addUsage(active);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != DamageCaused) return result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && damage.from->isAlive() && damage.to && damage.to->isAlive()
            && damage.from != damage.to && damage.to->hasSkill(this)) result[damage.to] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        QVariantMap state;
        state.insert("kingdom", damage.from->getKingdom());
        ctx.extra_data = state;
        ctx.targets << damage.to;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.invoker, this);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.extra_data.toMap().value("draw").toBool()) {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target) return false;
        target->damageRevises(*ctx.original_data, -damage.damage);
        if (damage.from && damage.from->isAlive()) {
            // Prevention and the attacker's draw have distinct recipients and effect hooks.
            SkillContext draw = ctx;
            QVariantMap state = draw.extra_data.toMap();
            state.insert("draw", true);
            draw.extra_data = state;
            skillEffect(event, room, player, draw, damage.from);
        }
        return true;
    }
};

class OLZongshiMax : public MaxCardsSkillV2
{
public:
    OLZongshiMax() : MaxCardsSkillV2("#olzongshi-max") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        QSet<QString> kingdoms;
        foreach (const Player *player, ctx.holder->getAliveSiblings(true)) kingdoms << player->getKingdom();
        return CorrectSkillResult::useAmount(kingdoms.size() * ctx.currentAmount);
    }
};
class OLZhuikong : public TriggerSkillV2
{
public:
    OLZhuikong() : TriggerSkillV2("olzhuikong") { events << EventPhaseStart; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::RoundStart) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->isWounded() && owner->hasSkill(this) && owner->canPindian(player))
                result[owner] << objectName() + "->" + player->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.preferredTarget || !ctx.invoker->canPindian(ctx.preferredTarget)
            || !ctx.invoker->askForSkillInvoke(this, ctx.preferredTarget)) return false;
        ctx.targets << ctx.preferredTarget;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.extra_data.toMap().contains("obtain")) {
            const int id = ctx.extra_data.toMap().value("obtain").toInt();
            if (id >= 0 && !room->getCardOwner(id)) room->obtainCard(target, id, true);
            return false;
        }
        if (!ctx.invoker->isAlive() || !ctx.invoker->canPindian(target)) return false;
        ctx.invoker->peiyin(this);
        PindianStruct *pindian = ctx.invoker->PinDian(target, objectName());
        if (!pindian) return false;
        if (pindian->success) {
            // Public applied restriction shared with the existing prohibit rule; it is not an activation quota.
            room->setPlayerFlag(target, "mobilezhuikong");
            return false;
        }
        const int id = pindian->to_card ? pindian->to_card->getEffectiveId() : -1;
        if (ctx.invoker->isAlive() && id >= 0 && !room->getCardOwner(id)) {
            ctx.extra_data = QVariantMap{{"obtain", id}};
            skillEffect(event, room, ctx.owner, ctx, ctx.invoker);
            ctx.extra_data.clear();
        }
        if (!target->isAlive() || !ctx.invoker->isAlive()) return false;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_" + objectName());
        CardUseStruct use;
        use.setOwnedCard(slash);
        use.from = target;
        use.to << ctx.invoker;
        if (target->canSlash(ctx.invoker, slash, false)) room->useCardFromSkillEffect(use, ctx);
        return false;
    }
};

class OLQiuyuan : public TriggerSkillV2
{
public:
    OLQiuyuan() : TriggerSkillV2("olqiuyuan") { events << TargetConfirming; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(this) || !use.card || !use.to.contains(player)
            || !(use.card->isKindOf("Slash") || (use.card->isKindOf("TrickCard") && use.card->isDamageCard()))) return result;
        foreach (ServerPlayer *other, room->getAlivePlayers())
            if (other != use.from && other != player && !use.to.contains(other)) { result[player] << objectName(); break; }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> choices;
        foreach (ServerPlayer *other, room->getAlivePlayers())
            if (other != use.from && other != ctx.owner && !use.to.contains(other)) choices << other;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, choices, objectName(), "olqiuyuan0:" + use.card->objectName(), true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.extra_data.toMap().value("give").toBool()) {
            const QVariantMap gift = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(gift.value("giver").toString());
            const int id = gift.value("card", -1).toInt();
            if (giver && id >= 0 && room->getCardOwner(id) == giver && !Sanguosha->getCard(id)->hasFlag("using")
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                room->giveCard(giver, target, Sanguosha->getCard(id), objectName());
            return false;
        }
        ctx.invoker->peiyin(this);
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.owner->isAlive(); ++i) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.card || target == use.from || target == ctx.owner || use.to.contains(target)) break;
            const Card *offered = room->askForCard(target, use.card->getType() + "+^%" + use.card->objectName(),
                "olqiuyuan1:" + use.card->objectName(), *ctx.original_data, Card::MethodNone);
            const QList<int> ids = offered ? (offered->isVirtualCard() ? offered->getSubcards() : QList<int>() << offered->getEffectiveId()) : QList<int>();
            const int id = ids.length() == 1 ? ids.first() : -1;
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            const bool valid = card && room->getCardOwner(id) == target && !card->hasFlag("using")
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && card->getTypeId() == use.card->getTypeId() && card->objectName() != use.card->objectName();
            if (valid) {
                SkillContext gift = ctx;
                gift.extra_data = QVariantMap{{"give", true}, {"giver", target->objectName()}, {"card", id}};
                skillEffect(event, room, ctx.owner, gift, ctx.owner);
            } else {
                // Re-read after the other player's response: nested effects may have edited the use.
                use = ctx.original_data->value<CardUseStruct>();
                if (target->isAlive() && !use.to.contains(target)) {
                    use.to << target;
                    room->sortByActionOrder(use.to);
                    *ctx.original_data = QVariant::fromValue(use);
                }
                break;
            }
        }
        return false;
    }
};

class OLJieJingce : public TriggerSkillV2
{
public:
    OLJieJingce() : TriggerSkillV2("oljiejingce") { events << CardUsed << EventPhaseEnd; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static bool firstSuit(Room *room, ServerPlayer *player, const CardUseStruct &use)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        const qint64 current = use.targetModReveal.useHistoryEventId;
        if (!use.card || use.card->isKindOf("SkillCard") || current <= 0 || turn.toLongLong() <= 0) return false;
        QVariantMap filter{{"kind", "use_card"}, {"player", player->objectName()}, {"turn_id", turn}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool()) return false;
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), card = fact.value("data").toMap().value("card").toMap();
                if (!card.contains("type") || !card.contains("suit")) return false;
                if (card.value("type").toInt() > 0 && card.value("suit").toInt() == use.card->getSuit())
                    return fact.value("event_id").toLongLong() == current;
            }
            if (!page.value("has_more").toBool()) return false;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    static int types(Room *room, ServerPlayer *player)
    {
        if (room->historyScopes().value("turn_id").toLongLong() <= 0) return -1;
        const QVariantMap history = room->queryCardHistory(player, "turn");
        if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return -1;
        QSet<int> values;
        foreach (const QVariant &item, history.value("items").toList()) {
            const QVariantMap card = item.toMap();
            if (!card.contains("type")) return -1;
            if (card.value("type").toInt() > 0) values.insert(card.value("type").toInt());
        }
        return values.size();
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return result;
        if (event == CardUsed) {
            if (room->getCurrent() == player && firstSuit(room, player, data.value<CardUseStruct>())) result[player] << objectName();
        } else if (player->getPhase() == Player::Play && types(room, player) > 0) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd) {
            const int count = types(room, ctx.owner);
            if (count <= 0 || !ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
            ctx.extra_data = count;
        }
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == CardUsed) {
            // Historical suit eligibility is independent of when this exact grant was acquired.
            room->sendCompulsoryTriggerLog(ctx.invoker, this);
            room->addMaxCards(target, amount);
        } else {
            ctx.invoker->peiyin(this);
            target->drawCards(ctx.extra_data.toInt() * amount, objectName());
        }
        return false;
    }
};

OLQingjianCard::OLQingjianCard()
{
    setSkillName("olqingjian");
    will_throw = false;
    handling_method = Card::MethodNone;
}

class OLQingjian : public TriggerSkillV2
{
public:
    OLQingjian() : TriggerSkillV2("olqingjian") { events << CardsMoveOneTime << EventPhaseChanging; global = true; }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    static QList<int> available(Room *room, ServerPlayer *owner, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        if (move.to != owner || move.to_place != Player::PlaceHand) return ids;
        foreach (int id, move.card_ids)
            if (room->getCardOwner(id) == owner && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
        return ids;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        if (!receipt.contains("receipt")) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive() || !ref(receipt, "activation").isValid()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("olqingjian_effects").toList())
            if (entry.toMap() == receipt && ref(receipt, "source") == ctx.sourceRef) return true;
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging) return false;
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            foreach (const QVariant &entry, owner->getTag("olqingjian_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.isEmpty() || receipt.value("turn") != room->historyScopes().value("turn_id") || owner->getPile("olqingjian").isEmpty()) continue;
            SkillContext ctx;
            ctx.owner = ctx.invoker = ctx.initiator = owner;
            ctx.skill_name = objectName();
            ctx.sourceRef = ref(receipt, "source");
            if (!ctx.sourceRef.isValid() || !ref(receipt, "activation").isValid()) continue;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.extra_data = receipt;
            ctx.is_forced = true;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == CardsMoveOneTime && player && player->isAlive() && player->hasSkill(this)
            && player->getPhase() != Player::Draw && !room->getTag("FirstRound").toBool()
            && room->historyScopes().value("turn_id").toLongLong() > 0 && !available(room, player, data.value<CardsMoveOneTimeStruct>()).isEmpty())
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) { ctx.targets << ctx.owner; return true; }
        const QList<int> ids = available(room, ctx.owner, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (ids.isEmpty()) return false;
        const Card *selected = room->askForExchange(ctx.owner, objectName(), ids.length(), 1, false, "@olqingjian", true, ListI2S(ids).join(","));
        if (!selected || selected->getSubcards().isEmpty()) return false;
        QList<int> chosen;
        foreach (int id, selected->getSubcards()) {
            if (!ids.contains(id) || chosen.contains(id) || room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
            chosen << id;
        }
        ctx.extra_data = QVariantMap{{"cards", ListI2V(chosen)}};
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QVariantMap state = ctx.extra_data.toMap();
        if (state.value("give").toBool()) {
            QList<int> ids;
            foreach (const QVariant &entry, state.value("cards").toList()) {
                const int id = entry.toInt();
                if (ctx.owner->getPile("olqingjian").contains(id) && !Sanguosha->getCard(id)->hasFlag("using") && !ids.contains(id)) ids << id;
            }
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); }
            return false;
        }
        if (event == CardsMoveOneTime) {
            QList<int> ids;
            foreach (const QVariant &entry, state.value("cards").toList()) {
                const int id = entry.toInt();
                if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
            }
            if (ids.isEmpty()) return false;
            const int sequence = room->getTag("olqingjian_sequence").toInt() + 1;
            room->setTag("olqingjian_sequence", sequence);
            // Every deposited set retains the source that created its own distribution obligation.
            QVariantList receipts = target->getTag("olqingjian_effects").toList();
            receipts << QVariantMap{{"receipt", sequence}, {"cards", ListI2V(ids)}, {"turn", room->historyScopes().value("turn_id")}, {"amount", amount},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            target->setTag("olqingjian_effects", receipts);
            target->addToPile("olqingjian", ids);
            return false;
        }
        QList<int> owned;
        foreach (const QVariant &entry, state.value("cards").toList()) {
            const int id = entry.toInt();
            if (target->getPile("olqingjian").contains(id) && !owned.contains(id)) owned << id;
        }
        QVariantList receipts = target->getTag("olqingjian_effects").toList();
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("receipt") == state.value("receipt")) receipts.removeAt(i);
        target->setTag("olqingjian_effects", receipts);
        // A cancelled or unfinished distribution puts the cards still on the pile back on this receipt.
        bool finished = false;
        const auto restore = qScopeGuard([&] {
            if (finished) return;
            QList<int> still;
            foreach (int id, owned)
                if (target->getPile("olqingjian").contains(id)) still << id;
            if (still.isEmpty()) return;
            QVariantMap restored = state;
            restored.insert("cards", ListI2V(still));
            QVariantList pending = target->getTag("olqingjian_effects").toList();
            pending << restored;
            target->setTag("olqingjian_effects", pending);
        });
        QList<int> remaining = owned;
        while (target->isAlive() && !remaining.isEmpty()) {
            QList<ServerPlayer *> recipients = room->getOtherPlayers(target);
            if (recipients.isEmpty()) break;
            ServerPlayer *recipient = room->askForPlayerChosen(target, recipients, objectName(), "@olqingjian-distribute");
            if (!recipient) recipient = recipients.first();
            QList<int> selected;
            room->fillAG(remaining, target);
            try {
                do {
                    int id = room->askForAG(target, remaining, !selected.isEmpty(), objectName());
                    if (!remaining.contains(id)) { if (!selected.isEmpty()) break; id = remaining.first(); }
                    selected << id;
                    remaining.removeOne(id);
                    room->takeAG(target, id, false);
                } while (!remaining.isEmpty());
            } catch (...) { room->clearAG(target); throw; }
            room->clearAG(target);
            SkillContext give = ctx;
            give.extra_data = QVariantMap{{"give", true}, {"cards", ListI2V(selected)}};
            skillEffect(event, room, ctx.owner, give, recipient);
        }
        QList<int> still;
        foreach (int id, owned)
            if (target->getPile("olqingjian").contains(id)) still << id;
        finished = still.isEmpty();
        return false;
    }
};

class OLFuhunVS : public ViewAsSkillV2
{
public:
    OLFuhunVS() : ViewAsSkillV2("olfuhun", 2) { response_or_use = true; response_pattern = "slash"; }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return Slash::IsAvailable(request.initiator);
        return request.pattern == "slash";
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->isVirtualCard() || card->hasFlag("using")
            || request.selectedCardIds.length() >= 2 || request.selectedCardIds.contains(card->getEffectiveId())) return false;
        return request.initiator->hasCard(card) || request.initiator->getHandPile().contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 2) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        // Rebuild ownership/zone/uniqueness from the authoritative selected physical IDs at acceptance.
        foreach (int id, request.selectedCardIds) {
            if (!canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::SuitToBeDecided, 0);
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        return slash;
    }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        return canActivate(request) && cardSelectionFeasible(request);
    }
};

class OLFuhun : public TriggerSkillV2
{
public:
    OLFuhun() : TriggerSkillV2("olfuhun")
    {
        events << TargetSpecified << Damage << PostCardEffected << CardFinished << EventPhaseChanging;
        view_as_skill = new OLFuhunVS;
        waked_skills = "wusheng,paoxiao";
        frequency = Compulsory;
        global = true;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QVariant turn = room->historyScopes().value("turn_id");
            foreach (ServerPlayer *holder, room->getAllPlayers(true)) {
                QVariantList keep, expired;
                foreach (const QVariant &entry, holder->getTag("olfuhun_grants").toList()) {
                    if (entry.toMap().value("turn") == turn) expired << entry;
                    else keep << entry;
                }
                holder->setTag("olfuhun_grants", keep);
                // Detach only this effect's exact grants; innate or other-source copies remain intact.
                foreach (const QVariant &entry, expired)
                    foreach (const QVariant &grant, entry.toMap().value("grants").toList())
                        room->detachSkillFromPlayer(holder, grant.toString(), false, true);
            }
        }
        if (event == PostCardEffected || event == CardFinished) {
            const qint64 use = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            if (use <= 0) return true;
            foreach (ServerPlayer *holder, room->getAllPlayers(true)) {
                if (event == PostCardEffected && holder != player) continue;
                QVariantList keep, expired;
                foreach (const QVariant &entry, holder->getTag("olfuhun_limits").toList()) {
                    if (entry.toMap().value("use").toLongLong() == use) expired << entry;
                    else keep << entry;
                }
                holder->setTag("olfuhun_limits", keep);
                foreach (const QVariant &entry, expired) room->removePlayerCardLimitationByReason(holder, entry.toMap().value("reason").toString());
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return result;
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && use.card->isKindOf("Slash") && use.card->isVirtualCard()
                && use.card->getEffectiveId() >= 0
                && room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong() > 0)
                result[player] << objectName();
        } else if (event == Damage) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from == player && damage.card && damage.card->isKindOf("Slash") && player->getPhase() == Player::Play
                && !player->hasSkills("wusheng+paoxiao", true)) result[player] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == TargetSpecified) ctx.targets = ctx.original_data->value<CardUseStruct>().to;
        else ctx.targets << ctx.invoker;
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (event == TargetSpecified) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            if (!use.card || useId <= 0 || !use.to.contains(target)) return false;
            const int serial = room->getTag("olfuhun_limit_sequence").toInt() + 1;
            room->setTag("olfuhun_limit_sequence", serial);
            const QString reason = "olfuhun:" + QString::number(serial);
            QVariantList receipts = target->getTag("olfuhun_limits").toList();
            receipts << QVariantMap{{"use", QString::number(useId)}, {"reason", reason},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
                {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            target->setTag("olfuhun_limits", receipts);
            room->setPlayerCardLimitation(target, "use", ".|^" + use.card->getColorString() + "|.|hand", false, reason);
            return false;
        }
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return false;
        QVariantList entries = target->getTag("olfuhun_grants").toList();
        foreach (const QVariant &entry, entries) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("turn") == turn && receipt.value("activation_owner").toString() == ctx.activationRef.ownerObjectName
                && receipt.value("activation_skill").toString() == ctx.activationRef.key.skillName
                && receipt.value("activation_id").toInt() == ctx.activationRef.key.instanceID) return false;
        }
        const int serial = room->getTag("olfuhun_grant_sequence").toInt() + 1;
        room->setTag("olfuhun_grant_sequence", serial);
        entries << QVariantMap{{"serial", serial}, {"turn", turn}, {"grants", QVariantList()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("olfuhun_grants", entries);
        for (const QString &skill : {QStringLiteral("wusheng"), QStringLiteral("paoxiao")}) {
            if (target->hasSkill(skill, true)) continue;
            const int id = room->acquireSkillFromEffect(target, skill, ctx);
            if (id <= 0) continue;
            const QString exact = SkillInstanceUtils::formatName(skill, id);
            entries = target->getTag("olfuhun_grants").toList();
            bool retained = false;
            for (int i = 0; i < entries.length(); ++i) {
                QVariantMap receipt = entries.at(i).toMap();
                if (receipt.value("serial").toInt() != serial) continue;
                QVariantList grants = receipt.value("grants").toList();
                grants << exact;
                receipt.insert("grants", grants);
                entries[i] = receipt;
                retained = true;
                break;
            }
            // Grant callbacks can end the turn before acquire returns; immediately retire such a late result.
            if (!retained) { room->detachSkillFromPlayer(target, exact, false, true); return false; }
            target->setTag("olfuhun_grants", entries);
            if (!target->isAlive()) return false;
        }
        return false;
    }
};

class OLZenhui : public TriggerSkillV2
{
public:
    OLZenhui() : TriggerSkillV2("olzenhui") { events << TargetSpecifying << EventSkillInvoking << EventPhaseChanging; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QStringList usedOptions(const SkillContext &ctx) const
    {
        const auto &ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return QStringList();
        const QString phase = ctx.extra_data.toMap().value("phase", ctx.owner->getRoom()->historyScopes().value("phase_id")).toString();
        return ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_options").toMap().value(phase).toStringList();
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.activationRef.isValid() || ctx.owner->getRoom()->historyScopes().value("phase_id").toLongLong() <= 0) return false;
        const QString branch = ctx.extra_data.toMap().value("branch").toString();
        const QStringList used = usedOptions(ctx);
        return branch.isEmpty() ? used.size() < 2 : !used.contains(branch);
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const auto &ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid() || !ctx.owner->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        const QVariantMap accepted = ctx.extra_data.toMap();
        const QString phase = accepted.value("phase").toString(), branch = accepted.value("branch").toString();
        if (phase.isEmpty() || phase == "0" || (branch != "give" && branch != "target")) return;
        QVariantMap states = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_options").toMap();
        QStringList used = states.value(phase).toStringList();
        if (!used.contains(branch)) used << branch;
        states.insert(phase, used);
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_options", states);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = ctx.original_data->value<SkillContext>();
            if (accepted.activationRef == ctx.activationRef && accepted.skill_name == objectName() && accepted.bypass_cost) addUsage(accepted);
        } else if (event == EventPhaseChanging && player == ctx.owner && ctx.original_data->value<PhaseChangeStruct>().from == Player::Play) {
            const auto &ref = ctx.activationRef;
            QVariantMap states = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_options").toMap();
            foreach (const QString &phase, states.keys()) if (finishedOLPlayPhase(room, phase, player)) states.remove(phase);
            ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_options", states);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecifying || !player || !player->isAlive() || !player->hasSkill(this) || player->getPhase() != Player::Play) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from == player && use.card && (use.card->isKindOf("Slash") || (use.card->isNDTrick() && !use.card->isKindOf("Collateral")))
            && !room->getCardTargets(player, use.card, use.to).isEmpty()) result[player] << objectName();
        return usableOLCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const QStringList used = usedOptions(ctx);
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> candidates = room->getCardTargets(ctx.invoker, use.card, use.to);
        for (int i = candidates.length() - 1; i >= 0; --i)
            if (candidates.at(i) == ctx.invoker || (used.contains("target") && candidates.at(i)->isNude())) candidates.removeAt(i);
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "olzenhui0", true, true);
        if (!target) return false;
        const Card *gift = nullptr;
        if (!used.contains("give")) gift = room->askForExchange(target, objectName(), 1, 1, true,
            "olzenhui1:" + ctx.invoker->objectName(), !used.contains("target"));
        const QList<int> ids = gift ? gift->getSubcards() : QList<int>();
        const bool give = ids.length() == 1;
        if (!give && used.contains("target")) return false;
        ctx.extra_data = QVariantMap{{"branch", give ? "give" : "target"}, {"card", give ? ids.first() : -1},
            {"payer", target->objectName()}, {"phase", room->historyScopes().value("phase_id")}};
        ctx.targets << target;
        return isUsable(ctx);
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const QVariantMap state = ctx.extra_data.toMap();
        ServerPlayer *payer = room->findPlayerByObjectName(state.value("payer").toString());
        const int id = state.value("card", -1).toInt();
        if (state.value("branch").toString() == "give" && (!payer || id < 0 || room->getCardOwner(id) != payer
            || Sanguosha->getCard(id)->hasFlag("using") || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip))) return false;
        addUsage(ctx);
        if (state.value("branch").toString() == "give") room->giveCard(payer, ctx.invoker, Sanguosha->getCard(id), objectName(), false);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !target->isAlive()) return false;
        ctx.invoker->peiyin(this);
        if (ctx.extra_data.toMap().value("branch").toString() == "give") use.from = target;
        else if (!use.to.contains(target)) { use.to << target; room->sortByActionOrder(use.to); }
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class OLJiaojin : public TriggerSkillV2
{
public:
    OLJiaojin() : TriggerSkillV2("oljiaojin") { events << DamageInflicted; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && player->canDiscard(player, "he")
            && data.value<DamageStruct>().damage > 0) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *card = room->askForCard(ctx.invoker, "EquipCard", "oljiaojin0", *ctx.original_data,
            Card::MethodNone, nullptr, false, objectName());
        if (!card || card->getEffectiveId() < 0 || card->getSubcards().length() > 1 || card->hasFlag("using")
            || !ctx.invoker->hasCard(Sanguosha->getCard(card->getEffectiveId()))
            || !ctx.invoker->canDiscard(ctx.invoker, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets << ctx.original_data->value<DamageStruct>().to;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != ctx.invoker || !ctx.invoker->canDiscard(ctx.invoker, id)
            || !Sanguosha->getCard(id)->isKindOf("EquipCard") || Sanguosha->getCard(id)->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        room->throwCard(id, objectName(), ctx.invoker);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target || getEffectiveAmount(ctx) <= 0) return false;
        // Prevention edits the live damage event; card recovery remains part of its target effect.
        target->damageRevises(*ctx.original_data, -damage.damage);
        if (damage.card && !room->getCardOwner(damage.card->getEffectiveId())) target->obtainCard(damage.card);
        return true;
    }
};
class OLJieQianxi : public TriggerSkillV2
{
public:
    OLJieQianxi() : TriggerSkillV2("oljieqianxi")
    {
        events << EventPhaseStart << ConfirmDamage << EventPhaseChanging;
        global = true;
    }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    static void project(Room *room, ServerPlayer *player)
    {
        QStringList colors;
        foreach (const QVariant &entry, player->getTag("oljieqianxi_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("turn") == room->historyScopes().value("turn_id")) colors << receipt.value("color").toString();
        }
        for (const QString &color : QStringList{"red", "black", "colorless"})
            room->setPlayerMark(player, "&oljieqianxi+" + color + "-Clear", colors.contains(color) ? 1 : 0);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QVariant turn = room->historyScopes().value("turn_id");
            foreach (ServerPlayer *owner, room->getAllPlayers(true)) {
                QVariantList receipts = owner->getTag("oljieqianxi_effects").toList();
                for (int i = receipts.length() - 1; i >= 0; --i)
                    if (receipts.at(i).toMap().value("turn") == turn) receipts.removeAt(i);
                owner->setTag("oljieqianxi_effects", receipts);
                project(room, owner);
            }
        }
        return true;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        if (!receipt.contains("receipt")) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive() || !ref(receipt, "activation").isValid()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("oljieqianxi_effects").toList())
            if (entry.toMap().value("receipt") == receipt.value("receipt") && ref(entry.toMap(), "source") == ctx.sourceRef
                && entry.toMap().value("turn") == room->historyScopes().value("turn_id")) return true;
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != ConfirmDamage) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from || !damage.from->isAlive() || !damage.to || !damage.to->isAlive()
            || !damage.card || damage.card->isVirtualCard()) return true;
        foreach (const QVariant &entry, damage.from->getTag("oljieqianxi_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("turn") != room->historyScopes().value("turn_id")
                || receipt.value("card").toInt() != damage.card->getEffectiveId()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = damage.from;
            ctx.sourceRef = ref(receipt, "source");
            if (!ctx.sourceRef.isValid() || !ref(receipt, "activation").isValid()) continue;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.extra_data = receipt;
            ctx.is_forced = true;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->hasSkill(this)
            && player->getPhase() == Player::Play && !player->isNude()
            && room->historyScopes().value("turn_id").toLongLong() > 0) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage) {
            ctx.targets << ctx.original_data->value<DamageStruct>().to;
            return true;
        }
        const Card *card = room->askForCard(ctx.owner, "..", objectName(), *ctx.original_data, Card::MethodNone);
        if (!card || card->getEffectiveId() < 0 || card->isVirtualCard()
            || !ctx.owner->hasCard(card) || card->hasFlag("using")) return false;
        ctx.extra_data = QVariantMap{{"card", card->getEffectiveId()}};
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == ConfirmDamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to == target) { damage.damage += amount; ctx.original_data->setValue(damage); }
            return false;
        }
        const int id = ctx.extra_data.toMap().value("card", -1).toInt();
        const Card *card = Sanguosha->getCard(id);
        if (!card || room->getCardOwner(id) != target || card->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        const int sequence = room->getTag("oljieqianxi_sequence").toInt() + 1;
        room->setTag("oljieqianxi_sequence", sequence);
        QVariantMap receipt{{"receipt", sequence}, {"turn", room->historyScopes().value("turn_id")},
            {"card", id}, {"color", card->getColorString()}, {"amount", amount},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        // The shown physical card keeps its accepted bonus for the whole turn, including later damage events.
        QVariantList receipts = target->getTag("oljieqianxi_effects").toList();
        receipts << receipt;
        target->setTag("oljieqianxi_effects", receipts);
        room->showCard(target, id);
        project(room, target);
        room->setCardTip(id, "oljieqianxi-Clear");
        return false;
    }
};

class OLJieQianxiLimit : public CardLimitSkill
{
public:
    OLJieQianxiLimit() : CardLimitSkill("#OLJieQianxiLimit") { }
    QString limitList(const Player *) const override { return "use,response"; }
    QString limitPattern(const Player *target, const Card *card) const override
    {
        if (!card) return QString();
        foreach (const Player *owner, target->getAliveSiblings()) {
            bool active = false;
            if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(owner)) {
                foreach (const QVariant &entry, server->getTag("oljieqianxi_effects").toList()) {
                    const QVariantMap receipt = entry.toMap();
                    if (receipt.value("turn") == server->getRoom()->historyScopes().value("turn_id")
                        && receipt.value("color").toString() == card->getColorString()) { active = true; break; }
                }
            } else active = owner->getMark("&oljieqianxi+" + card->getColorString() + "-Clear") > 0;
            // Keep the native hand-zone matcher, including virtual cards and isHandcard response checks.
            if (active && owner->distanceTo(target) == 1) return ".|" + card->getColorString() + "|.|hand";
        }
        return QString();
    }
};

OLJiaozhaoCard::OLJiaozhaoCard()
{
	setSkillName("oljiaozhao");
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

class OLJiaozhaoVS : public ViewAsSkillV2
{
public:
	OLJiaozhaoVS() : ViewAsSkillV2("oljiaozhao", 1) { response_or_use = true; }
	LimitScope getLimitScope() const override { return Limit_Custom; }
	TargetMode targetMode() const override { return NoTarget; }
	bool willThrowSelectedCards() const override { return false; }
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.activationRef.isValid()) return false;
		// Active-skill availability is probed before createCard(); the exact reveal/continuation card is checked at payment.
		if (!ctx.use_card) return true;
		if (ctx.use_card->getTag("OLJiaozhaoContinuation").toBool())
			return !ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "continuation_spent").toBool();
		return !isRevealCard(ctx.use_card) || !turnUsed(ctx.owner, ctx.activationRef);
	}
	static bool commitAccepted(const OLJiaozhaoVS &skill, SkillContext &ctx)
	{
		if (!ctx.owner || !ctx.activationRef.isValid() || !ctx.use_card) return false;
		QVariantMap receipt = ctx.interceptor_data.value("oljiaozhao");
		if (receipt.value("committed").toBool()) return true;
		const bool reveal = isRevealCard(ctx.use_card);
		const bool continuation = ctx.use_card->getTag("OLJiaozhaoContinuation").toBool();
		if (!reveal && !continuation) return true;
		if (!skill.checkCustomUsage(ctx)) return false;
		skill.addUsage(ctx);
		receipt.insert("committed", true);
		ctx.interceptor_data.insert("oljiaozhao", receipt);
		return true;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		if (!ctx.use_card || !ctx.owner || !ctx.activationRef.isValid()) return;
		if (ctx.use_card->getTag("OLJiaozhaoContinuation").toBool()) {
			ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "continuation_spent", true);
			return;
		}
		if (!isRevealCard(ctx.use_card)) return;
		const QString turn = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
		if (turn.isEmpty() || turn == "0") return;
		QVariantMap usage = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "turn_usage").toMap();
		usage.insert(turn, true);
		ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "turn_usage", usage);
		ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "current_turn_used", true);
	}
	QString historyKey(const ActiveSkillRequest &request) const override
	{
		const int material = request.selectedCardIds.value(0, -1);
		const int pending = request.initiator && request.activationRef.isValid()
			? request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "material_id", -1).toInt() : -1;
		if (material >= 0 && material == pending) {
			Card *converted = Sanguosha->cloneCard(declaration(request.initiator, request.activationRef));
			if (converted) { const QString key = converted->getClassName(); delete converted; return key; }
		}
		return "OLJiaozhaoCard";
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || !player->isAlive() || !request.activationRef.isValid()
			|| !player->findSkillInstance(objectName(), request.activationRef.key.instanceID)) return false;
		if (request.pattern == "@@oljiaozhao")
			return player->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "continuation", false).toBool()
				&& !player->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "continuation_spent", false).toBool()
				&& !declaration(player, request.activationRef).isEmpty();
		const int material = pendingMaterial(player, request.activationRef);
		if (material >= 0 && materialInHand(player, material))
			return usableConversion(player, request, material);
		return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !turnUsed(player, request.activationRef)
			&& !player->isKongcheng();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		if (request.pattern == "@@oljiaozhao") return false;
		if (!canActivate(request) || !request.initiator || !card || card->hasFlag("using")
			|| !request.initiator->getHandcards().contains(card)) return false;
		const int pending = pendingMaterial(request.initiator, request.activationRef);
		if (pending >= 0 && materialInHand(request.initiator, pending))
			return card->getEffectiveId() == pending && usableConversion(request.initiator, request, pending);
		return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !turnUsed(request.initiator, request.activationRef);
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.pattern == "@@oljiaozhao") return request.selectedCardIds.isEmpty() && canActivate(request);
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest empty = request; empty.selectedCardIds.clear();
		return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		if (request.pattern == "@@oljiaozhao") {
			Card *converted = Sanguosha->cloneCard(declaration(request.initiator, request.activationRef));
			if (converted) { converted->setSkillName(objectName()); converted->setTag("OLJiaozhaoContinuation", true); }
			return converted;
		}
		const int id = request.selectedCardIds.first();
		if (id == pendingMaterial(request.initiator, request.activationRef)) {
			Card *converted = Sanguosha->cloneCard(declaration(request.initiator, request.activationRef));
			if (converted) { converted->setSkillName(objectName()); converted->addSubcard(id); }
			return converted;
		}
		ActiveSkillCard *card = new ActiveSkillCard;
		card->setActiveSkill(this); card->setSkillName(objectName()); card->addSubcard(id); card->setTag("OLJiaozhaoReveal", true);
		return card;
	}
	bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request) || !ctx.initiator || !ctx.use_card) return false;
		return commitAccepted(*this, ctx);
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		if (!ctx.initiator || !qobject_cast<const ActiveSkillCard *>(ctx.use_card)) return ContinueEffects;
		skillEffect(ctx, ctx.initiator);
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!target || !target->isAlive() || !ctx.use_card) return ContinueEffects;
		Room *room = target->getRoom();
		const int material = ctx.use_card->getSubcards().value(0, -1);
		if (material < 0 || room->getCardOwner(material) != target || room->getCardPlace(material) != Player::PlaceHand) return FinishSkill;
		const int level = qBound(0, target->property("oljiaozhaoUp").toInt(), 2);
		if (level < 2) room->showCard(target, material);
		QList<int> ids = room->getAvailableCardList(target, "basic,trick", objectName(), ctx.use_card);
		if (level < 1)
			for (int i = ids.length() - 1; i >= 0; --i)
				if (target->getMark("oljiaozhaoBan" + Sanguosha->getEngineCard(ids.at(i))->objectName()) > 0) ids.removeAt(i);
		if (ids.isEmpty()) return FinishSkill;
		if (level < 2) room->setCardTip(material, "oljiaozhao-Clear"); // Tip is UI/AI projection; legality comes from instance state below.
		room->fillAG(ids, target);
		int id = -1;
		try { id = room->askForAG(target, ids, true, objectName(), "oljiaozhao0"); }
		catch (...) { room->clearAG(target); throw; }
		room->clearAG(target);
		if (!ids.contains(id)) id = ids.first();
		const QString name = Sanguosha->getEngineCard(id)->objectName();
		LogMessage log; log.type = "#ShouxiChoice"; log.from = target; log.arg = name;
		room->setPlayerProperty(target, "oljiaozhaoCn", name); // Legacy UI/AI projection only.
		room->addPlayerMark(target, "oljiaozhaoBan" + name);
		if (level < 2) {
			target->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "declaration", name);
			target->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "material_id", material);
			target->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "material_turn_id", room->historyScopes().value("turn_id"));
			room->sendLog(log);
		} else {
			Room::AcceptedViewAsEffectScope continuation(room, target, objectName(), ctx);
			if (continuation.isValid()) {
				const SkillInstanceRef continuationRef = continuation.activationRef();
				target->setSkillInstanceStateValue(continuationRef.key.skillName, continuationRef.key.instanceID, "declaration", name);
				target->setSkillInstanceStateValue(continuationRef.key.skillName, continuationRef.key.instanceID, "continuation", true);
				target->setSkillInstanceStateValue(continuationRef.key.skillName, continuationRef.key.instanceID, "continuation_spent", false);
				room->askForUseCard(target, "@@oljiaozhao", "oljiaozhao1:" + name);
			}
		}
		return FinishSkill;
	}
private:
	static bool isRevealCard(const Card *card)
	{ return card && (card->getTag("OLJiaozhaoReveal").toBool() || card->getClassName() == "OLJiaozhaoCard"); }
	static QString declaration(const Player *player, const SkillInstanceRef &ref)
	{ return player && ref.isValid() ? player->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "declaration").toString() : QString(); }
	static int pendingMaterial(const Player *player, const SkillInstanceRef &ref)
	{ return player && ref.isValid() ? player->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "material_id", -1).toInt() : -1; }
	static bool materialInHand(const Player *player, int id)
	{ return player && id >= 0 && player->getHandcards().contains(Sanguosha->getCard(id)) && !Sanguosha->getCard(id)->hasFlag("using"); }
	static bool turnUsed(const Player *player, const SkillInstanceRef &ref)
	{
		if (!player || !ref.isValid()) return true;
		if (const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(player)) {
			const QString turn = server->getRoom()->historyScopes().value("turn_id").toString();
			return turn.isEmpty() || turn == "0" || player->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "turn_usage").toMap().value(turn).toBool();
		}
		return player->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "current_turn_used").toBool();
	}
	static bool usableConversion(const Player *player, const ActiveSkillRequest &request, int id)
	{
		const QString name = declaration(player, request.activationRef);
		if (name.isEmpty()) return false;
		Card *converted = Sanguosha->cloneCard(name);
		if (!converted) return false;
		converted->setSkillName("oljiaozhao"); converted->addSubcard(id);
		bool valid = converted->isAvailable(player);
		if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) {
			valid = false;
			for (const QString &part : request.pattern.split("+"))
				if (name.contains(part)) { valid = true; break; }
		}
		delete converted;
		return valid;
	}
};

class OLJiaozhao : public TriggerSkillV2
{
public:
	OLJiaozhao() : TriggerSkillV2("oljiaozhao")
	{
		view_as_skill = new OLJiaozhaoVS;
		events << TurnStart << EventPhaseStart << EventPhaseEnd << EventPhaseChanging << TurnBroken << TurnedOver << EventSkillInvoking;
		global = true;
		waked_skills = "#OLJiaozhaoProhibit";
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking && data.canConvert<SkillContext>()) {
			SkillContext accepted = data.value<SkillContext>();
			if (accepted.activationRef.key.skillName == objectName()) {
				const auto *active = dynamic_cast<const OLJiaozhaoVS *>(view_as_skill);
				if (active && !OLJiaozhaoVS::commitAccepted(*active, accepted)) accepted.is_canceled = true;
				data = QVariant::fromValue(accepted);
			}
			return false;
		}
		if (!room || !player) return false;
		qint64 visibleTurn = room->historyScopes().value("turn_id").toLongLong();
		const bool endingTurn = event == TurnBroken
			|| (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
			|| (event == EventPhaseStart && player->getPhase() == Player::NotActive);
		const bool skippedExtraTurn = event == TurnedOver && room->isCurrentExtraTurn()
			&& room->historyScopes().value("phase_id").toLongLong() <= 0 && visibleTurn > 0
			&& player == room->getCurrent() && player->faceUp();
		if (endingTurn || skippedExtraTurn) {
			for (ServerPlayer *holder : room->getAllPlayers(true))
				for (int id : holder->getSkillInstanceIds(objectName())) {
					const QVariantMap state = holder->getSkillInstanceState(objectName(), id);
					if (state.value("material_turn_id").toLongLong() != visibleTurn) continue;
					holder->setSkillInstanceStateValue(objectName(), id, "declaration", QVariant());
					holder->setSkillInstanceStateValue(objectName(), id, "material_id", -1);
					holder->setSkillInstanceStateValue(objectName(), id, "continuation", false);
					holder->setSkillInstanceStateValue(objectName(), id, "continuation_spent", false);
					holder->setSkillInstanceStateValue(objectName(), id, "material_turn_id", QVariant());
				}
			if ((endingTurn && event != TurnBroken) || skippedExtraTurn)
				visibleTurn = room->historyParent(visibleTurn, "turn", false).value("id").toLongLong();
		}
		for (ServerPlayer *holder : room->getAllPlayers(true))
			for (int id : holder->getSkillInstanceIds(objectName()))
				holder->setSkillInstanceStateValue(objectName(), id, "current_turn_used",
					holder->getSkillInstanceStateValue(objectName(), id, "turn_usage").toMap().value(QString::number(visibleTurn)).toBool());
		return false;
	}
};

class OLJiaozhaoProhibit : public ProhibitSkill
{
public:
	OLJiaozhaoProhibit() : ProhibitSkill("#OLJiaozhaoProhibit")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		if (card->getSkillName()=="oljiaozhao" && to == from)
			return from->hasSkill("oljiaozhao");
		return false;
	}
};

class OLDanxin : public TriggerSkillV2
{
public:
	OLDanxin() : TriggerSkillV2("oldanxin")
	{
		events << Damaged;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == Damaged && player && player->isAlive() && player->hasSkill(this)
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data)) return false;
		ctx.targets << ctx.owner;
		return true;
	}

	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!target || !target->isAlive()) return false;
		target->peiyin(objectName());
		const int amount = getEffectiveAmount(ctx);
		if (amount > 0) target->drawCards(amount, objectName());
		// OL's permanent level remains player-wide; each Danxin instance still has its own trigger source.
		const int level = target->property("oljiaozhaoUp").toInt() + 1;
		room->setPlayerProperty(target, "oljiaozhaoUp", level);
		if (level < 3) room->changeTranslation(target, "oljiaozhao", level);
		return false;
	}
};

class OLJieZhenjun : public TriggerSkillV2
{
public:
    OLJieZhenjun() : TriggerSkillV2("oljiezhenjun")
    {
        events << EventPhaseStart << EventPhaseChanging;
        global = true;
    }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("oljiezhenjun_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt").toInt() == ctx.instanceID && ref(receipt, "source") == ctx.sourceRef
                && ref(receipt, "activation") == ref(ctx.extra_data.toMap(), "activation")
                && receipt.value("turn") == room->historyScopes().value("turn_id")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        foreach (ServerPlayer *player, room->getAllPlayers(true)) {
            QVariantList receipts = player->getTag("oljiezhenjun_effects").toList();
            for (int i = receipts.length() - 1; i >= 0; --i)
                if (receipts.at(i).toMap().value("turn") == room->historyScopes().value("turn_id")) receipts.removeAt(i);
            player->setTag("oljiezhenjun_effects", receipts);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::Finish) return false;
        foreach (ServerPlayer *recipient, room->getAlivePlayers()) {
            foreach (const QVariant &entry, recipient->getTag("oljiezhenjun_effects").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("turn") != room->historyScopes().value("turn_id") || !ref(receipt, "source").isValid()
                    || !ref(receipt, "activation").isValid()) continue;
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = ctx.invoker = ctx.initiator = recipient;
                ctx.sourceRef = ref(receipt, "source");
                ctx.instanceID = receipt.value("receipt").toInt();
                ctx.extra_data = receipt;
                ctx.original_data = &data;
                ctx.current_event = event;
                ctx.is_forced = true;
                ctx.setModifiedAmount(receipt.value("amount").toInt());
                contexts << ctx;
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(this) || player->getPhase() != Player::Start) return result;
        foreach (ServerPlayer *other, room->getAlivePlayers())
            if (player->canDiscard(other, "he")) { result[player] << objectName(); break; }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.activationRef.isValid()) { ctx.targets << ctx.invoker; return true; }
        QList<ServerPlayer *> choices;
        foreach (ServerPlayer *other, room->getAlivePlayers()) if (ctx.invoker->canDiscard(other, "he")) choices << other;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, choices, objectName() + "$-1", "oljiezhenjun0", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (!ctx.activationRef.isValid()) { target->drawCards(amount, objectName()); return false; }
        const QVariantMap state = ctx.extra_data.toMap();
        if (state.value("reward").toBool()) {
            const QVariant turn = state.value("turn");
            if (turn.toLongLong() <= 0) return false;
            const int sequence = room->getTag("oljiezhenjun_sequence").toInt() + 1;
            room->setTag("oljiezhenjun_sequence", sequence);
            const QVariantMap receipt{{"receipt", sequence}, {"turn", turn}, {"amount", amount},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            QVariantList receipts = target->getTag("oljiezhenjun_effects").toList();
            receipts << receipt;
            target->setTag("oljiezhenjun_effects", receipts);
            room->addPlayerMark(target, "&oljiezhenjun-Clear", amount);
            return false;
        }
        if (state.value("settle").toBool()) {
            if (target->canDiscard(target, "he") && room->askForChoice(target, objectName(), QString("1=%1+2=%1").arg(amount)).startsWith("1="))
                room->askForDiscard(target, objectName(), amount, amount, false, true);
            else {
                ServerPlayer *recipient = room->findPlayerByObjectName(state.value("recipient").toString());
                if (!recipient || !recipient->isAlive()) return false;
                SkillContext reward = ctx;
                QVariantMap next = state;
                next.insert("reward", true);
                reward.extra_data = next;
                skillEffect(event, room, ctx.owner, reward, recipient);
            }
            return false;
        }
        const QVariant turn = room->historyScopes().value("turn_id");
        const qint64 parent = room->currentHistoryEventId();
        const int count = qMax(1, target->getHandcardNum() - target->getHp()) * amount;
        QList<int> ids;
        for (int i = 0; i < count && target->isAlive(); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), true, Card::MethodDiscard, ids);
            if (id < 0 || ids.contains(id) || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || !ctx.invoker->canDiscard(target, id)) break;
            ids << id;
        }
        if (ids.isEmpty() || parent <= 0) return false;
        room->throwCard(ids, objectName(), target, ctx.invoker);
        QVariantMap filter{{"from", target->objectName()}, {"turn_id", turn}, {"limit", 100}};
        QSet<int> nonEquip;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool()) return false;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), value = fact.value("data").toMap();
                const int id = value.value("card_id", -1).toInt();
                if (ids.contains(id) && value.value("to_place").toInt() == Player::DiscardPile
                    && room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() == parent) {
                    const QVariantMap card = value.value("card").toMap();
                    if (!card.contains("type")) return false;
                    if (card.value("type").toInt() != Card::TypeEquip) nonEquip.insert(id);
                }
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        if (nonEquip.isEmpty() || !ctx.owner->isAlive()) return false;
        SkillContext settle = ctx;
        settle.extra_data = QVariantMap{{"settle", true}, {"recipient", target->objectName()}, {"turn", turn}};
        settle.setModifiedAmount(nonEquip.size());
        skillEffect(event, room, ctx.owner, settle, ctx.owner);
        return false;
    }
};

class OLJieYizhong : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLJieYizhong() : TriggerSkillV2("oljieyizhong") { events << TargetSpecified; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !use.card || !use.card->isKindOf("Slash") || !use.card->isBlack()) return result;
        foreach (ServerPlayer *target, use.to) {
            if (player->hasSkill(this) && target->getHandcardNum() <= player->getHandcardNum() && !result.contains(player))
                result[player] << objectName();
            if (target->isAlive() && target->hasSkill(this) && target->getHp() <= player->getHp() && !result.contains(target))
                result[target] << objectName();
        }
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        room->sendCompulsoryTriggerLog(ctx.invoker, this);
        if (ctx.owner == use.from) {
            if (!use.no_respond_list.contains("_ALL_TARGETS")) use.no_respond_list << "_ALL_TARGETS";
        } else if (!use.nullified_list.contains("_ALL_TARGETS")) use.nullified_list << "_ALL_TARGETS";
        ctx.original_data->setValue(use);
        return false;
    }
};
class OLJieJieyong : public OLCardConversion
{
public:
    OLJieJieyong() : OLCardConversion("oljiejieyong", "slash", ".|red", true) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.initiator->getHandcardNum() == 1 && OLCardConversion::canActivate(request);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        // Only the sole actual hand card qualifies; an expanded pile or equipment is not the last hand card.
        return OLCardConversion::canSelectCard(request, candidate) && request.initiator->getHandcardNum() == 1
            && request.initiator->getHandcards().contains(candidate);
    }
};

class OLJieQiangshi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    OLJieQiangshi() : TriggerSkillV2("oljieiqiangshi") { events << EventPhaseStart << CardFinished << EventPhaseChanging; }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging && player == ctx.owner
            && ctx.original_data->value<PhaseChangeStruct>().from == Player::Play) {
            ctx.owner->removeSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "card_type");
            ctx.owner->removeSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "phase");
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return result;
        if ((event == EventPhaseStart && player->getPhase() == Player::Play)
            || (event == CardFinished && data.value<CardUseStruct>().card && data.value<CardUseStruct>().card->getTypeId() > 0))
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const auto &ref = ctx.activationRef;
        if (event == CardFinished) {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            if (ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "phase") != room->historyScopes().value("phase_id")
                || ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "card_type").toString() != card->getType()) return false;
            if (!ctx.invoker->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
            ctx.targets << ctx.invoker;
            return true;
        }
        QList<ServerPlayer *> candidates;
        foreach (ServerPlayer *target, room->getAlivePlayers()) if (!target->isKongcheng()) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, candidates, objectName() + "$-1", "oljiezhenjun0", true, true);
        if (!target) return false;
        const int id = room->doGongxin(ctx.invoker, target, target->handCards(), objectName());
        if (id < 0) return false;
        ctx.extra_data = id;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == CardFinished) target->drawCards(getEffectiveAmount(ctx), objectName());
        else {
            const int id = ctx.extra_data.toInt();
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) return false;
            room->showCard(target, id);
            const QString type = Sanguosha->getCard(id)->getType();
            const auto &ref = ctx.activationRef;
            ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "card_type", type);
            ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "phase", room->historyScopes().value("phase_id"));
            // This public icon mirrors the instance choice; it never gates another instance.
            room->setPlayerMark(ctx.owner, "&oljieiqiangshi+:+" + type + "-PlayClear", 1);
        }
        return false;
    }
};
class OLJieXiantu : public TriggerSkillV2
{
public:
    OLJieXiantu() : TriggerSkillV2("oljiexiantu")
    {
        events << EventPhaseStart << EventPhaseEnd << EventPhaseChanging;
        global = true;
    }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    static int amountOf(Room *room, QVariantMap filter, qint64 parent = 0)
    {
        int count = 0;
        for (;;) {
            const QVariantMap page = parent ? room->queryHistoryMoves(filter) : room->queryActualDamage(filter);
            if (!page.value("complete").toBool()) return -1;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap();
                if (parent) {
                    if (room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() == parent
                        && fact.value("data").toMap().value("to_place").toInt() == Player::PlaceHand) ++count;
                } else count += fact.value("data").toMap().value("amount").toInt();
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        return count;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        foreach (const QVariant &entry, room->getTag("oljiexiantu_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt").toInt() == ctx.instanceID && ref(receipt, "source") == ctx.sourceRef
                && ref(receipt, "activation") == ref(ctx.extra_data.toMap(), "activation")) return ctx.owner && ctx.owner->isAlive();
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *phaseActor, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().from != Player::Play) return true;
        QVariantList receipts = room->getTag("oljiexiantu_effects").toList();
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (finishedOLPlayPhase(room, receipts.at(i).toMap().value("phase"), phaseActor)) receipts.removeAt(i);
        room->setTag("oljiexiantu_effects", receipts);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseEnd) return false;
        if (!player || player->getPhase() != Player::Play) return true;
        const QVariant phase = room->historyScopes().value("phase_id");
        foreach (const QVariant &entry, room->getTag("oljiexiantu_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("phase") != phase || receipt.value("actor").toString() != player->objectName()) continue;
            ServerPlayer *donor = room->findPlayerByObjectName(receipt.value("donor").toString());
            if (!donor || !donor->isAlive() || !ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()) continue;
            const int given = amountOf(room, {{"from", donor->objectName()}, {"to", player->objectName()}, {"phase_id", phase}, {"limit", 100}}, receipt.value("parent").toLongLong());
            const int damage = amountOf(room, {{"from", player->objectName()}, {"phase_id", phase}, {"limit", 100}});
            if (given <= 0 || damage < 0 || damage >= given) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = donor;
            ctx.sourceRef = ref(receipt, "source");
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.extra_data = receipt;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.is_forced = true;
            contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play
            || room->historyScopes().value("phase_id").toLongLong() <= 0) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player)) if (owner->hasSkill(this)) result[owner] << objectName() + "->" + player->objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd) { ctx.targets << ctx.invoker; return true; }
        if (!ctx.preferredTarget || !ctx.preferredTarget->isAlive() || !ctx.invoker->askForSkillInvoke(objectName() + "$-1", ctx.preferredTarget)) return false;
        const int count = room->askForChoice(ctx.invoker, objectName(), "1+2").toInt();
        if (count < 1 || count > 2) return false;
        ctx.extra_data = QVariantMap{{"count", count}, {"actor", ctx.preferredTarget->objectName()}, {"phase", room->historyScopes().value("phase_id")}};
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QVariantMap state = ctx.extra_data.toMap();
        if (event == EventPhaseEnd) {
            QVariantList receipts = room->getTag("oljiexiantu_effects").toList();
            for (int i = receipts.length() - 1; i >= 0; --i)
                if (receipts.at(i).toMap().value("receipt").toInt() == ctx.instanceID) receipts.removeAt(i);
            room->setTag("oljiexiantu_effects", receipts);
            room->loseHp(HpLostStruct(target, amount, objectName(), ctx.invoker));
            return false;
        }
        if (state.value("give").toBool()) {
            ServerPlayer *donor = room->findPlayerByObjectName(state.value("donor").toString());
            if (!donor || !donor->isAlive()) return false;
            QList<int> ids;
            foreach (const QVariant &entry, state.value("cards").toList()) {
                const int id = entry.toInt();
                if (id < 0 || ids.contains(id) || room->getCardOwner(id) != donor || Sanguosha->getCard(id)->hasFlag("using")
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
                ids << id;
            }
            if (ids.isEmpty() || room->currentHistoryEventId() <= 0) return false;
            const int sequence = room->getTag("oljiexiantu_sequence").toInt() + 1;
            room->setTag("oljiexiantu_sequence", sequence);
            const QVariantMap receipt{{"receipt", sequence}, {"parent", room->currentHistoryEventId()}, {"phase", state.value("phase")},
                {"donor", donor->objectName()}, {"actor", target->objectName()},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            QVariantList receipts = room->getTag("oljiexiantu_effects").toList();
            receipts << receipt;
            room->setTag("oljiexiantu_effects", receipts);
            DummyCard gift(ids);
            room->giveCard(donor, target, &gift, objectName());
            room->setPlayerMark(donor, "&oljiexiantu+#" + target->objectName() + "-PlayClear", ids.length());
            return false;
        }
        const int count = state.value("count").toInt() * amount;
        target->drawCards(count, objectName());
        ServerPlayer *actor = room->findPlayerByObjectName(state.value("actor").toString());
        if (!target->isAlive() || !actor || !actor->isAlive() || target->isNude()) return false;
        const Card *chosen = room->askForExchange(target, objectName(), count, count, true, "oljiexiantu0:" + actor->objectName());
        if (!chosen) return false;
        QVariantList cards;
        foreach (int id, chosen->getSubcards()) cards << id;
        SkillContext gift = ctx;
        QVariantMap give = state;
        give.insert("give", true);
        give.insert("donor", target->objectName());
        give.insert("cards", cards);
        gift.extra_data = give;
        skillEffect(event, room, ctx.owner, gift, actor);
        return false;
    }
};











OLStStandardPackage::OLStStandardPackage()
	: Package("ol_st_standard")
{

	General *ol_caocao = new General(this, "ol_caocao$", "wei", 4);
	ol_caocao->addSkill("tenyearjianxiong");
	ol_caocao->addSkill(new OLHujia);
	ol_caocao->addSkill(new OLHujiaDraw);
	related_skills.insert("olhujia", "#olhujia");

	General *ol_xiahd = new General(this, "ol_xiahoudun", "wei");
	ol_xiahd->addSkill("ganglie");
	ol_xiahd->addSkill(new OLQingjian);
	addMetaObject<OLQingjianCard>();

	General *ol_liubei = new General(this, "ol_liubei$", "shu", 4);
	ol_liubei->addSkill("tenyearrende");
	ol_liubei->addSkill(new OLJijiang);

	General *ol_zhangfei = new General(this, "ol_zhangfei", "shu", 4);
	ol_zhangfei->addSkill(new OLPaoxiao);
	ol_zhangfei->addSkill(new OLPaoxiaoMod);
	ol_zhangfei->addSkill(new OLTishen);
	related_skills.insert("olpaoxiao", "#olpaoxiaomod");

	General *ol_zhaoyun = new General(this, "ol_zhaoyun", "shu", 4);
	ol_zhaoyun->addSkill(new OLLongdan);
	ol_zhaoyun->addSkill(new OLYajiao);

	General *oljie_huangyueying = new General(this, "oljie_huangyueying", "shu", 3, false);
	oljie_huangyueying->addSkill(new OLJizhi);
	oljie_huangyueying->addSkill(new OLQicai);
	oljie_huangyueying->addSkill(new OLQicaiLimit);
	related_skills.insert("olqicai", "#olqicai-limit");

	General *ol_sunquan = new General(this, "ol_sunquan$", "wu", 4);
	ol_sunquan->addSkill("tenyearzhiheng");
	ol_sunquan->addSkill(new OLJiuyuan);

	General *ol_lvmeng = new General(this, "ol_lvmeng", "wu", 4);
	ol_lvmeng->addSkill("keji");
	ol_lvmeng->addSkill("qinxue");
	ol_lvmeng->addSkill(new OLBotu);
	ol_lvmeng->addSkill(new OLBotuMark);
	related_skills.insert("olbotu", "#olbotu-mark");

	General *ol_gongsunzan = new General(this, "ol_gongsunzan", "qun", 4);
	ol_gongsunzan->addSkill(new OLQiaomeng);
	ol_gongsunzan->addSkill(new OLYicong);
	ol_gongsunzan->addSkill(new OLYicongEffect);
	related_skills.insert("olyicong", "#olyicong-effect");


	addMetaObject<OLJijiangCard>();
	addMetaObject<OLHuangtianCard>();
	addMetaObject<OLGuhuoCard>();
	addMetaObject<OLQimouCard>();
	addMetaObject<OLTianxiangCard>();
	addMetaObject<OLWulieCard>();
	addMetaObject<OLFangquanCard>();
	addMetaObject<OLZhibaCard>();
	addMetaObject<OLZhibaPindianCard>();
	addMetaObject<SecondOLHanzhanCard>();
	addMetaObject<OLChangbiaoCard>();
	addMetaObject<OLTiaoxinCard>();
	addMetaObject<OLZaiqiCard>();
	addMetaObject<OLQiaobianCard>();
	addMetaObject<OLQiangxiCard>();

	skills << new OLSishu;
}
ADD_PACKAGE(OLStStandard)

OLStWindPackage::OLStWindPackage()
	: Package("ol_st_wind")
{
	General *ol_weiyan = new General(this, "ol_weiyan", "shu", 4);
	ol_weiyan->addSkill("tenyearkuanggu");
	ol_weiyan->addSkill(new OLQimou);

	General *ol_xiahouyuan = new General(this, "ol_xiahouyuan", "wei", 4);
	ol_xiahouyuan->addSkill("tenyearshensu");
	ol_xiahouyuan->addSkill(new OLShebian);

	General *ol_xiaoqiao = new General(this, "ol_xiaoqiao", "wu", 3, false);
	ol_xiaoqiao->addSkill(new OLTianxiang);
	ol_xiaoqiao->addSkill(new OLHongyan);
	ol_xiaoqiao->addSkill(new OLHongyanKeep);
	ol_xiaoqiao->addSkill(new OLPiaoling);
	related_skills.insert("olhongyan", "#olhongyan-keep");

	General *ol_zhangjiao = new General(this, "ol_zhangjiao$", "qun", 3);
	ol_zhangjiao->addSkill(new OLLeiji);
	ol_zhangjiao->addSkill(new OLGuidao);
	ol_zhangjiao->addSkill("huangtian");

	General *second_ol_zhangjiao = new General(this, "second_ol_zhangjiao$", "qun", 3);
	second_ol_zhangjiao->addSkill("olleiji");
	second_ol_zhangjiao->addSkill("olguidao");
	second_ol_zhangjiao->addSkill(new OLHuangtian);
    skills << new OLHuangtianViewAsSkill;
    related_skills.insert("olhuangtian", "olhuangtian_attach");

	General *ol_yuji = new General(this, "ol_yuji", "qun", 3);
	ol_yuji->addSkill(new OLGuhuo);

	MigrateToOLStWind(this);
}
ADD_PACKAGE(OLStWind)

OLStThicketPackage::OLStThicketPackage()
	: Package("ol_st_thicket")
{
	General *ol_zhurong = new General(this, "ol_zhurong", "shu", 4, false);
	ol_zhurong->addSkill("juxiang");
	ol_zhurong->addSkill("lieren");
	ol_zhurong->addSkill(new OLChangbiao);
	ol_zhurong->addSkill(new SlashNoDistanceLimitSkill("olchangbiao"));
	related_skills.insert("olchangbiao", "#olchangbiao-slash-ndl");

	General *ol_menghuo = new General(this, "ol_menghuo", "shu", 4);
	ol_menghuo->addSkill("huoshou");
	ol_menghuo->addSkill(new OLZaiqi);

	General *ol_xuhuang = new General(this, "ol_xuhuang", "wei", 4);
	ol_xuhuang->addSkill(new OLDuanliang);
	ol_xuhuang->addSkill(new OLDuanliangTargetMod);
    ol_xuhuang->addSkill(new OLDuanliangHistory);
    related_skills.insertMulti("olduanliang", "#olduanliang-target");
    related_skills.insertMulti("olduanliang", "#olduanliang-history");
	ol_xuhuang->addSkill(new OLJiezi);

	General *ol_sunjian = new General(this, "ol_sunjian", "wu", 4);
	ol_sunjian->addSkill("yinghun");
	ol_sunjian->addSkill(new OLWulie);

	General *second_ol_sunjian = new General(this, "second_ol_sunjian", "wu", 5, true, false, false, 4);
	second_ol_sunjian->addSkill("yinghun");
	second_ol_sunjian->addSkill("olwulie");

	General *ol_dongzhuo = new General(this, "ol_dongzhuo$", "qun", 8);
	ol_dongzhuo->addSkill(new OLJiuchi);
	ol_dongzhuo->addSkill(new OLJiuchiTargetMod);
	ol_dongzhuo->addSkill("roulin");
	ol_dongzhuo->addSkill("benghuai");
	ol_dongzhuo->addSkill(new OLBaonue);
	related_skills.insert("oljiuchi", "#oljiuchi-target");





}
ADD_PACKAGE(OLStThicket)

OLStFirePackage::OLStFirePackage()
	: Package("ol_st_fire")
{
	General *ol_wolong = new General(this, "ol_wolong", "shu", 3);
	ol_wolong->addSkill("bazhen");
	ol_wolong->addSkill(new OLHuoji);
	ol_wolong->addSkill(new OLKanpo);
	ol_wolong->addSkill(new OLCangzhuo);/*

	General *ol2_wolong = new General(this, "ol2_wolong", "shu", 3);
	ol2_wolong->addSkill("bazhen");
	ol2_wolong->addSkill(new OL2Huoji);
	ol2_wolong->addSkill(new OL2Kanpo);
	ol2_wolong->addSkill("olcangzhuo");*/

	General *ol_pangtong = new General(this, "ol_pangtong", "shu", 3);
	ol_pangtong->addSkill(new OLLianhuan);
	ol_pangtong->addSkill(new OLLianhuanMod);
	ol_pangtong->addSkill(new OLNiepan);
	related_skills.insert("ollianhuan", "#ollianhuanmod");

	General *ol_xunyu = new General(this, "ol_xunyu", "wei", 3);
	ol_xunyu->addSkill("quhu");
	ol_xunyu->addSkill(new OLJieming);
	ol_xunyu->addSkill(new OLJiemingDeath);
    related_skills.insert("oljieming", "#oljieming");

	General *ol_dianwei = new General(this, "ol_dianwei", "wei", 4);
	ol_dianwei->addSkill(new OLQiangxi);
    ol_dianwei->addSkill(new OLQiangxiRecord);
    related_skills.insertMulti("olqiangxi", "#olqiangxi-record");
	ol_dianwei->addSkill(new OLNinge);

	General *ol_taishici = new General(this, "ol_taishici", "wu", 4);
	ol_taishici->addSkill("tianyi");
	ol_taishici->addSkill(new OLHanzhan);

	General *second_ol_taishici = new General(this, "second_ol_taishici", "wu", 4);
	second_ol_taishici->addSkill("tianyi");
	second_ol_taishici->addSkill(new SecondOLHanzhan);

	General *ol_pangde = new General(this, "ol_pangde", "qun", 4);
	ol_pangde->addSkill(new OLJianchu);
	ol_pangde->addSkill("mashu");

	General *ol_yuanshao = new General(this, "ol_yuanshao$", "qun", 4);
	ol_yuanshao->addSkill(new OLLuanji);
	ol_yuanshao->addSkill(new OLXueyi("olxueyi"));
	ol_yuanshao->addSkill(new OLXueyiKeep("olxueyi"));
	related_skills.insert("olxueyi", "#olxueyi-keep");

	General *second_ol_yuanshao = new General(this, "second_ol_yuanshao$", "qun", 4);
	second_ol_yuanshao->addSkill("olluanji");
	second_ol_yuanshao->addSkill(new OLXueyi("secondolxueyi"));
	second_ol_yuanshao->addSkill(new OLXueyiKeep("secondolxueyi"));
	related_skills.insert("secondolxueyi", "#secondolxueyi-keep");




}
ADD_PACKAGE(OLStFire)

OLStMountainPackage::OLStMountainPackage()
	: Package("ol_st_mountain")
{
	General *ol_jiangwei = new General(this, "ol_jiangwei", "shu", 4);
	ol_jiangwei->addSkill(new OLTiaoxin);
	ol_jiangwei->addSkill(new OLZhiji);

	General *ol_zhanghe = new General(this, "ol_zhanghe", "wei", 4);
	ol_zhanghe->addSkill(new OLQiaobian);
	ol_zhanghe->addSkill(new OLQiaobianGameStart);
	ol_zhanghe->addSkill(new OLQiaobianMark);
	related_skills.insert("olqiaobian", "#olqiaobian");
	related_skills.insert("olqiaobian", "#olqiaobian-mark");

	General *ol_caiwenji = new General(this, "ol_jie_caiwenji", "qun", 3, false);
	ol_caiwenji->addSkill(new OLBeige);
	ol_caiwenji->addSkill("duanchang");

	General *ol_zuoci = new General(this, "ol_zuoci", "qun", 3);
	ol_zuoci->addSkill(new OLHuashen);
	ol_zuoci->addSkill(new OLHuashenSelect);
	ol_zuoci->addSkill(new OLHuashenClear);
	ol_zuoci->addSkill("xinsheng");
	related_skills.insert("olhuashen", "#olhuashen-select");
	related_skills.insert("olhuashen", "#olhuashen-clear");

	General *ol_liushan = new General(this, "ol_liushan$", "shu", 3);
	ol_liushan->addSkill("xiangle");
	ol_liushan->addSkill(new OLFangquan);
	ol_liushan->addSkill(new OLRuoyu);
	ol_liushan->addRelateSkill("oljijiang");
	ol_liushan->addRelateSkill("olsishu");

	General *ol_sunce = new General(this, "ol_sunce$", "wu", 4);
	ol_sunce->addSkill(new OLJiang);
	ol_sunce->addSkill(new OLHunzi);
	ol_sunce->addSkill(new OLHunziFinish);
	related_skills.insert("olhunzi", "#olhunzi-finish");
	ol_sunce->addSkill(new OLZhiba);
    skills << new OLZhibaPindian;
    related_skills.insert("olzhiba", "olzhiba_pindian");

	General *ol_dengai = new General(this, "ol_dengai", "wei", 4);
	ol_dengai->addSkill(new OLTuntian);
	ol_dengai->addSkill(new OLTuntianDistance);
	ol_dengai->addSkill(new OLZaoxian);
	ol_dengai->addSkill(new OLZaoxianExtraTurn);
	ol_dengai->addRelateSkill("jixi");
	related_skills.insert("oltuntian", "#oltuntian-dist");
	related_skills.insert("olzaoxian", "#olzaoxian-extra-turn");

	General *oljie_erzhang = new General(this, "oljie_erzhang", "wu", 3);
	oljie_erzhang->addSkill(new OLZhijian);
	oljie_erzhang->addSkill(new OLGuzheng);
	addMetaObject<OLZhijianCard>();




}
ADD_PACKAGE(OLStMountain)

OLStYJ2011Package::OLStYJ2011Package()
	: Package("ol_st_yj2011")
{
	General *oljie_caozhi = new General(this, "oljie_caozhi", "wei", 3);
	oljie_caozhi->addSkill("luoying");
	oljie_caozhi->addSkill(new OLJiushi);
    oljie_caozhi->addSkill(new OLJiushiTargetMod);
    related_skills.insert("oljiushi", "#oljiushi-target");

	General *oljie_fazheng = new General(this, "oljie_fazheng", "shu", 3);
	oljie_fazheng->addSkill(new OLXuanhuo);
	oljie_fazheng->addSkill(new OLEnyuan);
	addMetaObject<OLXuanhuoCard>();

	General *oljie_zhangchunhua = new General(this, "oljie_zhangchunhua", "wei", 3,false);
	oljie_zhangchunhua->addSkill("jueqing");
	oljie_zhangchunhua->addSkill("nosshangshi");
	oljie_zhangchunhua->addSkill(new OLJianmie);
	addMetaObject<OLJianmieCard>();

	General *oljie_wuguotai = new General(this, "oljie_wuguotai", "wu", 3,false);
	oljie_wuguotai->addSkill(new OLGanlu);
	oljie_wuguotai->addSkill(new OLBuyi);
	addMetaObject<OLGanluCard>();

	General *oljie_yujin = new General(this, "oljie_yujin", "wei", 4);
	oljie_yujin->addSkill(new OLJieZhenjun);
	oljie_yujin->addSkill(new OLJieYizhong);












	MigrateToOLStYJ2011(this);
}
ADD_PACKAGE(OLStYJ2011)

OLStYJ2012Package::OLStYJ2012Package()
	: Package("ol_st_yj2012")
{
	General *oljie_liaohua = new General(this, "oljie_liaohua", "shu", 4);
	oljie_liaohua->addSkill(new OLDangxian);
	oljie_liaohua->addSkill(new OLFuli);

	General *oljie_caozhang = new General(this, "oljie_caozhang", "wei", 4);
	oljie_caozhang->addSkill(new OLJiangchi);

	General *oljie_chengpu = new General(this, "oljie_chengpu", "wu", 4);
	oljie_chengpu->addSkill(new OLLihuo);
	oljie_chengpu->addSkill(new OLChunlao);
	addMetaObject<OLChunlaoCard>();

	General *oljie_liubiao = new General(this, "oljie_liubiao", "qun", 3);
	oljie_liubiao->addSkill(new OLJieZishou);
	oljie_liubiao->addSkill(new OLZongshi);
    oljie_liubiao->addSkill(new OLZongshiMax);
    related_skills.insert("olzongshi", "#olzongshi-max");

	General *oljie_guanxingzhangbao = new General(this, "oljie_guanxingzhangbao", "shu", 4);
	oljie_guanxingzhangbao->addSkill(new OLFuhun);

	General *oljie_madai = new General(this, "oljie_madai", "qun", 4);
	oljie_madai->addSkill("mashu");
	oljie_madai->addSkill(new OLJieQianxi);
	oljie_madai->addSkill(new OLJieQianxiLimit);

	MigrateToOLStYJ2012(this);
}
ADD_PACKAGE(OLStYJ2012)

OLStYJ2013Package::OLStYJ2013Package()
	: Package("ol_st_yj2013")
{
	General *oljie_caochong = new General(this, "oljie_caochong", "wei", 3);
	oljie_caochong->addSkill(new OLChengxiang);
	oljie_caochong->addSkill(new OLRenxin);

	General *oljie_guohuai = new General(this, "oljie_guohuai", "wei", 3);
	oljie_guohuai->addSkill(new OLJieJingce);

	General *oljie_yufan = new General(this, "oljie_yufan", "wu", 3);
	oljie_yufan->addSkill(new OLZongxuan);
	oljie_yufan->addSkill(new OLZhiyan);
	addMetaObject<OLZongxuanCard>();

	General *oljie_fuhuanghou = new General(this, "oljie_fuhuanghou", "qun", 3,false);
	oljie_fuhuanghou->addSkill(new OLZhuikong);
	oljie_fuhuanghou->addSkill(new OLQiuyuan);

	General *oljie_liru = new General(this, "oljie_liru", "qun", 3);
	oljie_liru->addSkill("juece");
	oljie_liru->addSkill(new OLMieji);
	oljie_liru->addSkill("tenyearfencheng");
	addMetaObject<OLMiejiCard>();
	MigrateToOLStYJ2013(this);

	General *oljie_guanping = new General(this, "oljie_guanping", "shu", 4);
	oljie_guanping->addSkill("longyin");
	oljie_guanping->addSkill(new OLJieJieyong);

}
ADD_PACKAGE(OLStYJ2013)

OLStYJ2014Package::OLStYJ2014Package()
	: Package("ol_st_yj2014")
{
	General *oljie_caifuren = new General(this, "oljie_caifuren", "qun", 3,false);
	oljie_caifuren->addSkill(new OLQieting);
	oljie_caifuren->addSkill("xianzhou");

	General *oljie_sunluban = new General(this, "oljie_sunluban", "wu", 3,false);
	oljie_sunluban->addSkill(new OLZenhui);
	oljie_sunluban->addSkill(new OLJiaojin);
	MigrateToOLStYJ2014(this);

	General *oljie_zhangsong = new General(this, "oljie_zhangsong", "shu", 3);
	oljie_zhangsong->addSkill(new OLJieQiangshi);
	oljie_zhangsong->addSkill(new OLJieXiantu);

}
ADD_PACKAGE(OLStYJ2014)

OLStYC2016Package::OLStYC2016Package()
	: Package("ol_st_yc2016")
{

	General *oljie_guohuanghou = new General(this, "oljie_guohuanghou", "wei", 3,false);
	oljie_guohuanghou->addSkill(new OLJiaozhao);
	oljie_guohuanghou->addSkill(new OLDanxin);
	addMetaObject<OLJiaozhaoCard>();






}
ADD_PACKAGE(OLStYC2016)
