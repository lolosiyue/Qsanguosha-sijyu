#include "yjcm2014.h"
#include <QJsonDocument>
#include <QScopeGuard>
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "yjcm2013.h"

DingpinCard::DingpinCard()
{
    setSkillName("dingpin");
}

bool DingpinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->isWounded() && to_select->getMark("dingpin-Clear")<1;
}

void DingpinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();

    JudgeStruct judge;
    judge.who = effect.to;
    judge.good = true;
    judge.pattern = ".|black";
    judge.reason = "dingpin";

    room->judge(judge);

    if (judge.isGood()) {
        room->addPlayerMark(effect.to, "dingpin-Clear");
        effect.to->drawCards(effect.to->getLostHp(), "dingpin");
    } else {
        effect.from->turnOver();
    }
}

class Dingpin : public ViewAsSkillV2
{
public:
    Dingpin() : ViewAsSkillV2("dingpin", 1) { }
    QString historyKey(const ActiveSkillRequest &) const override { return "DingpinCard"; }
    static QVariantMap usedTypes(Room *room, const Player *player)
    {
        const QVariantMap cards = room->queryCardHistory(player, "turn");
        if (cards.contains("error") || !cards.value("complete").toBool()) return {{"known", false}};
        QSet<int> types;
        for (const QVariant &entry : cards.value("items").toList()) types.insert(entry.toMap().value("type").toInt());
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return {{"known", false}};
        QVariantMap filter{{"from", player->objectName()}, {"turn_id", turn}, {"limit", 128}};
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return {{"known", false}};
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap data = entry.toMap().value("data").toMap();
                if (!data.contains("reason")) return {{"known", false}};
                if ((data.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) continue;
                const QVariantMap card = data.value("card_before").toMap();
                if (!card.contains("type")) return {{"known", false}};
                types.insert(card.value("type").toInt());
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        QVariantList result;
        for (int type : types) result << type;
        return {{"known", true}, {"types", result}};
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || !request.initiator->canDiscard(request.initiator, "h")) return false;
        for (const Player *target : request.initiator->getAliveSiblings(true))
            if (canSelectTarget(request, {}, target)) return true;
        return false;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->isVirtualCard() || !request.selectedCardIds.isEmpty()
            || card->hasFlag("using") || request.initiator->isJilei(card)
            || !request.initiator->handCards().contains(card->getEffectiveId())) return false;
        QVariantMap used = QJsonDocument::fromJson(request.initiator->property("DingpinTypes").toByteArray()).toVariant().toMap();
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator)) used = usedTypes(server->getRoom(), server);
        return used.value("known").toBool() && !used.value("types").toList().contains(int(card->getTypeId()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return request.initiator && target && target->isAlive() && target->isWounded() && selected.isEmpty()
            && !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID,
                "successful_targets").toStringList().contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest empty = request;
        empty.selectedCardIds.clear();
        if (!canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()))) return false;
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        for (ServerPlayer *target : ctx.targets) {
            SkillContext judgement = ctx;
            judgement.extra_data = false;
            skillEffect(judgement, target);
            if (judgement.extra_data.toBool()) {
                SkillContext flip = ctx;
                flip.choice = "flip";
                skillEffect(flip, ctx.initiator);
            }
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (ctx.choice == "flip") { target->turnOver(); return ContinueEffects; }
        JudgeStruct judge;
        judge.who = target;
        judge.good = true;
        judge.pattern = ".|black";
        judge.reason = objectName();
        target->getRoom()->judge(judge);
        if (judge.isGood()) {
            QStringList successful = ctx.initiator->getSkillInstanceStateValue(ctx.activationRef.key.skillName,
                ctx.activationRef.key.instanceID, "successful_targets").toStringList();
            if (!successful.contains(target->objectName())) successful << target->objectName();
            ctx.initiator->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID,
                "successful_targets", successful);
            if (target->isAlive()) target->drawCards(qMax(0, target->getLostHp()) * getEffectiveAmount(ctx), objectName());
        } else ctx.extra_data = true;
        return ContinueEffects;
    }
};

class DingpinBf : public TriggerSkillV2
{
public:
    DingpinBf() : TriggerSkillV2("#dingpinbf")
    { events << CardUsed << CardResponded << CardsMoveOneTime << EventPhaseStart << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == EventPhaseChanging) {
            const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.from == Player::Play)
                for (const SkillInstance &instance : player->getSkillInstances())
                    if (instance.skillName == "dingpin") player->removeSkillInstanceStateValue(instance.skillName,
                        instance.instanceID, "successful_targets");
            if (change.to == Player::NotActive)
                for (ServerPlayer *owner : room->getAllPlayers(true))
                    room->setPlayerProperty(owner, "DingpinTypes", QString::fromUtf8(QJsonDocument::fromVariant(QVariantMap{{"known", true}, {"types", QVariantList()}}).toJson(QJsonDocument::Compact)));
        } else if (event != CardsMoveOneTime || data.value<CardsMoveOneTimeStruct>().from == player) {
            // This public projection only supports selection preview; server admission reads Room history.
            room->setPlayerProperty(player, "DingpinTypes", QString::fromUtf8(QJsonDocument::fromVariant(Dingpin::usedTypes(room, player)).toJson(QJsonDocument::Compact)));
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class Faen : public TriggerSkillV2
{
public:
    Faen() : TriggerSkillV2("faen")
    {
        events << TurnedOver << ChainStateChanged;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || (event == ChainStateChanged && !player->isChained())) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(this)) result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!ctx.owner || !player || !player->isAlive()) return false;
        ctx.targets = {player};
        return room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(player), false);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // The turned/chained actor receives the effect of the selected owner's instance.
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

SidiCard::SidiCard()
{
    // Preserve the legacy AI/metaobject entry while the server resolves it through Skill V2.
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
    setSkillName("sidi");
}

class SidiVS : public ViewAsSkillV2
{
public:
    SidiVS() : ViewAsSkillV2("sidi", 1)
    {
        expand_pile = "sidi";
        response_pattern = "@@sidi";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@sidi"
            && !request.initiator->getPile("sidi").isEmpty();
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && !card->hasFlag("using")
            && getExpandPileCardIds(request.initiator).contains(card->getEffectiveId());
    }

    bool willThrowSelectedCards() const override { return false; }

    QString historyKey(const ActiveSkillRequest &) const override { return "SidiCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!room || !ctx.initiator || request.selectedCardIds.size() != 1) return false;
        const int id = request.selectedCardIds.first();
        if (!ctx.initiator->getPile("sidi").contains(id) || room->getCardOwner(id) != ctx.initiator
            || room->getCardPlace(id) != Player::PlaceSpecial || Sanguosha->getCard(id)->hasFlag("using")) return false;
        room->throwCard(Sanguosha->getCard(id),
            CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "sidi", ""), nullptr);
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.activationRef.isValid()) return FinishSkill;
        const int instance = ctx.activationRef.key.instanceID;
        const QString skill = ctx.activationRef.key.skillName;
        const QVariantMap state = ctx.initiator->getSkillInstanceState(skill, instance);
        const QString targetName = state.value("pending_reduction_target").toString();
        const qint64 phaseId = state.value("pending_reduction_phase").toLongLong();
        ctx.initiator->removeSkillInstanceStateValue(skill, instance, "pending_reduction_target");
        ctx.initiator->removeSkillInstanceStateValue(skill, instance, "pending_reduction_phase");
        Room *room = ctx.initiator->getRoom();
        ServerPlayer *target = room->findPlayerByObjectName(targetName);
        if (!target || !target->isAlive() || target->getPhase() != Player::Play || phaseId <= 0
            || room->historyScopes().value("phase_id").toLongLong() != phaseId) return FinishSkill;
        ctx.extra_data = phaseId;
        skillEffect(ctx, target);
        return FinishSkill;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator || !target || !target->isAlive() || target->getPhase() != Player::Play) return FinishSkill;
        const qint64 phaseId = ctx.extra_data.toLongLong();
        Room *room = target->getRoom();
        if (phaseId <= 0 || room->historyScopes().value("phase_id").toLongLong() != phaseId) return FinishSkill;
        QVariantMap phaseUsage = target->property("SidiPhaseUsage").toMap();
        const QString key = QString::number(phaseId);
        const int count = phaseUsage.value(key).toInt() + 1;
        phaseUsage.insert(key, count);
        room->setPlayerProperty(target, "SidiPhaseUsage", phaseUsage);
        room->setPlayerMark(target, "sidi", count);
        return ContinueEffects;
    }
};

class Sidi : public TriggerSkillV2
{
public:
    Sidi() : TriggerSkillV2("sidi")
    {
        events << CardUsed << EventPhaseStart << EventPhaseEnd << EventPhaseChanging << TurnBroken;
        global = true;
        view_as_skill = new SidiVS;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player) return true;
        const QVariant phase = room->historyScopes().value("phase_id");
        const QString phaseKey = phase.toLongLong() > 0 ? QString::number(phase.toLongLong()) : QString();
        if (event == EventPhaseEnd && player->getPhase() == Player::Play) {
            QVariantMap phaseUsage = player->property("SidiPhaseUsage").toMap();
            if (!phaseKey.isEmpty()) phaseUsage.remove(phaseKey);
            room->setPlayerProperty(player, "SidiPhaseUsage", phaseUsage);
            room->setPlayerMark(player, "sidi", 0);
        } else if (event == TurnBroken) {
            QVariantMap phaseUsage = player->property("SidiPhaseUsage").toMap();
            if (!phaseKey.isEmpty()) phaseUsage.remove(phaseKey);
            room->setPlayerProperty(player, "SidiPhaseUsage", phaseUsage);
            room->setPlayerMark(player, "sidi", 0);
        } else if (event == EventPhaseChanging) {
            room->setPlayerMark(player, "sidi", phaseKey.isEmpty() ? 0
                : player->property("SidiPhaseUsage").toMap().value(phaseKey).toInt());
        } else if (event == EventPhaseStart && player->getPhase() == Player::Play) {
            room->setPlayerMark(player, "sidi", player->property("SidiPhaseUsage").toMap()
                .value(QString::number(phase.toLongLong())).toInt());
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive()) return {};
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.card->isKindOf("Jink")) return {};
            TriggerList result;
            for (ServerPlayer *owner : room->getAllPlayers())
                if (owner->isAlive() && owner->hasSkill(this)
                    && (owner == player || owner->hasFlag("CurrentPlayer")))
                    result[owner] << objectName();
            return result;
        }
        if (event == EventPhaseStart && player->getPhase() == Player::Play) {
            TriggerList result;
            for (ServerPlayer *owner : room->getOtherPlayers(player))
                if (owner->isAlive() && owner->hasSkill(this) && !owner->getPile("sidi").isEmpty())
                    result[owner] << objectName();
            return result;
        }
        return {};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !ctx.owner->isAlive() || !ctx.invoker->isAlive()) return false;
        if (event == CardUsed)
            if (ctx.original_data && room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) {
                ctx.targets = {ctx.owner};
                return true;
            }
            else return false;
        if (event == EventPhaseStart) {
            if (ctx.invoker->getPhase() != Player::Play || ctx.owner->getPile("sidi").isEmpty()) return false;
            ctx.targets = {ctx.owner};
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardUsed && ctx.owner) room->broadcastSkillInvoke(objectName(), 1);
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == CardUsed) {
            if (getEffectiveAmount(ctx) > 0) {
                const QList<int> ids = room->getNCards(getEffectiveAmount(ctx), false);
                if (!ids.isEmpty()) {
                    CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                        CardMoveReason(CardMoveReason::S_REASON_TURNOVER, ctx.owner->objectName(), "sidi", ""));
                    room->moveCardsAtomic(move, true);
                    if (target->isAlive()) target->addToPile("sidi", ids);
                }
            }
            return false;
        }
        if (event != EventPhaseStart || !ctx.invoker || !ctx.invoker->isAlive()
            || ctx.invoker->getPhase() != Player::Play) return false;
        const int attempts = qMax(0, getEffectiveAmount(ctx));
        for (int i = 0; i < attempts && target->isAlive() && !target->getPile("sidi").isEmpty(); ++i) {
            Room::AcceptedViewAsEffectScope accepted(room, target, objectName(), ctx);
            if (!accepted.isValid()) break;
            const qint64 phaseId = room->historyScopes().value("phase_id").toLongLong();
            if (phaseId <= 0 || ctx.invoker->getPhase() != Player::Play) break;
            const int id = accepted.activationRef().key.instanceID;
            target->setSkillInstanceStateValue(objectName(), id, "pending_reduction_target", ctx.invoker->objectName());
            target->setSkillInstanceStateValue(objectName(), id, "pending_reduction_phase", phaseId);
            const auto clearPending = qScopeGuard([target, id] {
                target->removeSkillInstanceStateValue("sidi", id, "pending_reduction_target");
                target->removeSkillInstanceStateValue("sidi", id, "pending_reduction_phase");
            });
            const QString phaseKey = QString::number(phaseId);
            const int before = ctx.invoker->property("SidiPhaseUsage").toMap().value(phaseKey).toInt();
            if (!room->askForUseCard(target, "@@sidi", "sidi_remove:remove", -1, Card::MethodNone)) break;
            const int after = ctx.invoker->property("SidiPhaseUsage").toMap().value(phaseKey).toInt();
            if (after <= before) break;
        }
        return false;
    }
};

class SidiTargetMod : public TargetModSkillV2
{
public:
    SidiTargetMod() : TargetModSkillV2("#sidi-target")
    {
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.primary || !ctx.card
            || !ctx.card->isKindOf("Slash")) return CorrectSkillResult::noEffect();
        const ServerPlayer *player = qobject_cast<const ServerPlayer *>(ctx.primary);
        int count = ctx.primary->getMark("sidi");
        if (player) {
            const qint64 phaseId = player->getRoom()->historyScopes().value("phase_id").toLongLong();
            if (phaseId <= 0) return CorrectSkillResult::noEffect();
            count = ctx.primary->property("SidiPhaseUsage").toMap()
                .value(QString::number(phaseId)).toInt();
        }
        return count > 0 ? CorrectSkillResult::signedAmount(-count) : CorrectSkillResult::noEffect();
    }
};

class ShenduanViewAsSkill : public OneCardViewAsSkill
{
public:
    ShenduanViewAsSkill() : OneCardViewAsSkill("shenduan")
    {
        response_pattern = "@@shenduan";
        expand_pile = "#shenduan";
    }

    bool viewFilter(const Card *to_select) const
    {
        return Self->getPile("#shenduan").contains(to_select->getEffectiveId());
    }

    const Card *viewAs(const Card *originalCard) const
    {
        SupplyShortage *ss = new SupplyShortage(originalCard->getSuit(), originalCard->getNumber());
        ss->addSubcard(originalCard);
        ss->setSkillName("shenduan");
        return ss;
    }
};

class Shenduan : public TriggerSkill
{
public:
    Shenduan() : TriggerSkill("shenduan")
    {
        events << CardsMoveOneTime;
        view_as_skill = new ShenduanViewAsSkill;
    }
    int getPriority(TriggerEvent) const
    {
        return 4;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::DiscardPile && move.from == player
            && ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {
            int i = 0;
            QList<int> shenduan_card;
            foreach (int id, move.card_ids) {
                const Card *c = Sanguosha->getCard(id);
                if (room->getCardOwner(id)==nullptr && c->isBlack() && c->getTypeId() == Card::TypeBasic
                    && (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip)) {
                    shenduan_card << id;
                }
                i++;
            }
            while (!shenduan_card.isEmpty()&&player->isAlive()) {
				room->notifyMoveToPile(player, shenduan_card, objectName(), Player::DiscardPile, true);
                const Card *c = room->askForUseCard(player, "@@shenduan", "@shenduan-use");
				if (!c) break;
				shenduan_card.removeOne(c->getEffectiveId());
				foreach (int id, shenduan_card) {
					if (room->getCardOwner(id))
						shenduan_card.removeOne(id);
				}
            }
        }
        return false;
    }
};

class ShenduanTargetMod : public TargetModSkillV2
{
public:
    ShenduanTargetMod() : TargetModSkillV2("#shenduan-target", "SupplyShortage")
    {
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == TargetModSkill::DistanceLimit && ctx.card
            && ctx.card->getSkillName() == "shenduan"
            ? CorrectSkillResult::useAmount(1000) : CorrectSkillResult::noEffect();
    }
};

class Yonglve : public TriggerSkillV2
{
public:
    Yonglve() : TriggerSkillV2("yonglve") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList list;
        if (!player || !player->isAlive() || player->getPhase() != Player::Judge || player->getJudgingArea().isEmpty()) return list;
        for (ServerPlayer *holder : room->getOtherPlayers(player))
            if (holder->isAlive() && holder->hasSkill(this) && holder->inMyAttackRange(player)) list[holder] << objectName();
        return list;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !ctx.invoker->isAlive() || ctx.invoker->getJudgingArea().isEmpty()
            || !ctx.owner->inMyAttackRange(ctx.invoker) || !room->askForSkillInvoke(ctx.owner, objectName())) return false;
        ctx.targets = {ctx.invoker};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        if (target->getJudgingArea().isEmpty()) return false;
        const int id = room->askForCardChosen(ctx.owner, target, "j", objectName(), false, Card::MethodDiscard);
        if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceDelayedTrick
            || !ctx.owner->canDiscard(target, id)) return false;
        room->throwCard(id, nullptr, ctx.owner);
        if (!ctx.owner->isAlive() || !target->isAlive()) return false;
        Slash slash(Card::NoSuit, 0);
        slash.setSkillName("_yonglve");
        if (!ctx.owner->canSlash(target, &slash, false)) return false;
        CardUseStruct use(&slash, ctx.owner, target);
        if (!room->useCardFromSkillEffect(use, ctx) || !use.cardFinished
            || use.targetModReveal.useHistoryEventId <= 0) return false;
        // Attribute damage to this exact completed use, excluding independent nested cards.
        const QVariantMap history = room->queryCardUseDamage(use.targetModReveal.useHistoryEventId);
        if (history.value("complete").toBool() && history.value("attribution_complete").toBool() && history.value("items").toList().isEmpty()) {
            SkillContext reward = ctx;
            reward.choice = "draw";
            skillEffect(event, room, player, reward, ctx.owner);
        }
        return false;
    }
};

class YonglveSlash : public TriggerSkillV2
{
public:
    YonglveSlash() : TriggerSkillV2("#yonglve") { }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};
class Benxi : public TriggerSkill
{
public:
    Benxi() : TriggerSkill("benxi")
    {
        events << EventPhaseChanging << CardFinished << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to >= Player::NotActive) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (p->hasFlag("benxiAN"+player->objectName())){
						p->setFlags("-benxiAN"+player->objectName());
						p->removeEquipsNullified("Armor");
					}
				}
            }else{
				bool can = true;
				foreach (const Player *p, player->getAliveSiblings()) {
					if (player->distanceTo(p) > 1){
						can = false;
						break;
					}
				}
				if (can){
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if (p->hasFlag("benxiAN"+player->objectName())) continue;
						p->setFlags("benxiAN"+player->objectName());
						p->addEquipsNullified("Armor");
					}
				}else{
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if (p->hasFlag("benxiAN"+player->objectName())){
							p->setFlags("-benxiAN"+player->objectName());
							p->removeEquipsNullified("Armor");
						}
					}
				}
			}
        } else if (triggerEvent == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId() != Card::TypeSkill && player->hasFlag("CurrentPlayer")) {
                room->addPlayerMark(player, "&benxi-Clear");
				bool can = true;
				foreach (const Player *p, player->getAliveSiblings()) {
					if (player->distanceTo(p) > 1){
						can = false;
						break;
					}
				}
				if (can){
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if (p->hasFlag("benxiAN"+player->objectName())) continue;
						p->setFlags("benxiAN"+player->objectName());
						p->addEquipsNullified("Armor");
					}
				}else{
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if (p->hasFlag("benxiAN"+player->objectName())){
							p->setFlags("-benxiAN"+player->objectName());
							p->removeEquipsNullified("Armor");
						}
					}
				}
            }
        } else if (triggerEvent == EventAcquireSkill || triggerEvent == EventLoseSkill) {
            if (data.toString() != objectName()) return false;
            int num = (triggerEvent == EventAcquireSkill) ? player->getMark("benxi-Clear") : 0;
            room->setPlayerMark(player, "&benxi-Clear", num);
        }
        return false;
    }
};

// the part of Armor ignorance is coupled in Player::hasArmorEffect

class BenxiTargetMod : public TargetModSkillV2
{
public:
    BenxiTargetMod() : TargetModSkillV2("#benxi-target")
    {
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == TargetModSkill::ExtraTarget && ctx.primary && ctx.card
            && ctx.primary->hasSkill("benxi") && isAllAdjacent(ctx.primary, ctx.card)
            ? CorrectSkillResult::useAmount(1) : CorrectSkillResult::noEffect();
    }

private:
    bool isAllAdjacent(const Player *from, const Card *card) const
    {
        int rangefix = 0;
        if (card->isVirtualCard() && from->getOffensiveHorse()
            && card->getSubcards().contains(from->getOffensiveHorse()->getEffectiveId()))
            rangefix = 1;
        foreach (const Player *p, from->getAliveSiblings()) {
            if (from->distanceTo(p, rangefix) != 1)
                return false;
        }
        return true;
    }
};

class BenxiDistance : public DistanceSkillV2
{
public:
    BenxiDistance() : DistanceSkillV2("#benxi-dist")
    {
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasFlag("CurrentPlayer") || !ctx.primary->hasSkill("benxi"))
            return CorrectSkillResult::noEffect();
        const int count = ctx.primary->getMark("&benxi-Clear");
        return count > 0 ? CorrectSkillResult::signedAmount(-count) : CorrectSkillResult::noEffect();
    }
};

class Qiangzhi : public TriggerSkillV2
{
public:
    Qiangzhi() : TriggerSkillV2("qiangzhi")
    {
        events << EventPhaseStart << EventPhaseChanging << CardUsed << CardResponded;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && ((event == EventPhaseStart && player->getPhase() == Player::Play)
            || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play)))
            player->setTag("QiangzhiEffects", QVariantList());
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play
            || !player->hasSkill(this)) return {};
        for (ServerPlayer *target : room->getOtherPlayers(player))
            if (!target->isKongcheng()) return {{player, {objectName()}}};
        return {};
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        if (event != CardUsed && event != CardResponded) return false;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play) return true;
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card
            : (data.value<CardResponseStruct>().m_isUse ? data.value<CardResponseStruct>().m_card : nullptr);
        if (!card || card->getTypeId() == Card::TypeSkill) return true;
        for (const QVariant &entry : player->getTag("QiangzhiEffects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("type").toInt() != card->getTypeId() || receipt.value("amount").toInt() <= 0) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = sourceRef(receipt);
            if (!ctx.sourceRef.isValid()) continue;
            // Applied effects have a receipt identity, never a revived activation grant.
            ctx.instanceID = receipt.value("serial").toInt();
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive() || ctx.owner->getPhase() != Player::Play) return false;
        for (const QVariant &entry : ctx.owner->getTag("QiangzhiEffects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("serial").toInt() == ctx.instanceID && sourceRef(receipt) == ctx.sourceRef
                && receipt.value("amount").toInt() > 0) return true;
        }
        return false;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        if (event != EventPhaseStart) {
            if (!room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
            ctx.targets = {ctx.owner};
            return true;
        }
        QList<ServerPlayer *> targets;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner))
            if (!target->isKongcheng()) targets << target;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "qiangzhi-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event != EventPhaseStart) {
            room->broadcastSkillInvoke(objectName(), 2);
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        if (target->isKongcheng()) return false;
        const int id = room->askForCardChosen(ctx.owner, target, "h", objectName());
        if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) return false;
        const int type = Sanguosha->getCard(id)->getTypeId();
        room->broadcastSkillInvoke(objectName(), 1);
        room->showCard(target, id);
        if (!ctx.owner->isAlive() || ctx.owner->getPhase() != Player::Play || getEffectiveAmount(ctx) <= 0) return false;
        const int serial = room->getTag("QiangzhiReceiptSerial").toInt() + 1;
        room->setTag("QiangzhiReceiptSerial", serial);
        QVariantList receipts = ctx.owner->getTag("QiangzhiEffects").toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"serial", serial}, {"type", type}, {"amount", getEffectiveAmount(ctx)}};
        ctx.owner->setTag("QiangzhiEffects", receipts);
        return false;
    }
private:
    static SkillInstanceRef sourceRef(const QVariantMap &receipt)
    {
        return SkillInstanceRef(receipt.value("owner").toString(),
            SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
    }
};
class Xiantu : public TriggerSkillV2
{
public:
    Xiantu() : TriggerSkillV2("xiantu")
    {
        events << EventPhaseStart << EventPhaseEnd << EventPhaseChanging;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().from != Player::Play) return true;
        for (ServerPlayer *owner : room->getAllPlayers(true)) {
            QVariantList keep;
            for (const QVariant &entry : owner->getTag("XiantuEffects").toList())
                if (entry.toMap().value("actor").toString() != player->objectName()) keep << entry;
            owner->setTag("XiantuEffects", keep);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList list;
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play
            || room->historyScopes().value("phase_id").toLongLong() <= 0) return list;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->isAlive() && owner->hasSkill(this)) list[owner] << objectName();
        return list;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseEnd) return false;
        if (!player || player->getPhase() != Player::Play) return true;
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return true;
        const QVariantMap deaths = room->queryHistoryFacts({{"kind", "death"}, {"phase_id", phase},
            {"from", player->objectName()}, {"limit", 1}});
        // Unknown history cannot establish the negative condition required for HP loss.
        if (deaths.contains("error") || !deaths.value("complete").toBool()
            || !deaths.value("items").toList().isEmpty()) return true;
        for (ServerPlayer *owner : room->getAlivePlayers()) {
            for (const QVariant &entry : owner->getTag("XiantuEffects").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("actor").toString() != player->objectName()
                    || receipt.value("phase").toLongLong() != phase.toLongLong()) continue;
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = ctx.initiator = owner;
                ctx.invoker = player;
                ctx.sourceRef = sourceRef(receipt);
                if (!ctx.sourceRef.isValid()) continue;
                ctx.instanceID = receipt.value("serial").toInt();
                ctx.setModifiedAmount(receipt.value("amount").toInt());
                ctx.extra_data = receipt;
                ctx.current_event = event;
                ctx.original_data = &data;
                contexts << ctx;
            }
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        return ctx.owner && ctx.owner->isAlive()
            && ctx.owner->getTag("XiantuEffects").toList().contains(ctx.extra_data)
            && sourceRef(ctx.extra_data.toMap()) == ctx.sourceRef;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker) return false;
        if (event == EventPhaseStart) {
            if (!ctx.invoker->isAlive() || !room->askForSkillInvoke(ctx.owner, objectName())) return false;
            ctx.extra_data = room->historyScopes().value("phase_id");
        }
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (!target || !target->isAlive() || amount <= 0) return false;
        if (event == EventPhaseEnd) {
            QVariantList keep = target->getTag("XiantuEffects").toList();
            keep.removeAll(ctx.extra_data);
            target->setTag("XiantuEffects", keep);
            LogMessage log;
            log.type = "#Xiantu";
            log.from = ctx.invoker;
            log.to << target;
            log.arg = objectName();
            room->sendLog(log);
            room->loseHp(HpLostStruct(target, amount, objectName(), target));
            return false;
        }
        if (ctx.choice == "give") {
            DummyCard gift;
            for (const QVariant &value : ctx.extra_data.toList()) {
                const int id = value.toInt();
                if (room->getCardOwner(id) == ctx.owner
                    && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                    && !Sanguosha->getCard(id)->hasFlag("using")) gift.addSubcard(id);
            }
            if (gift.subcardsLength() > 0) room->obtainCard(target, &gift, false);
            return false;
        }
        const qint64 phase = ctx.extra_data.toLongLong();
        if (phase <= 0 || !ctx.invoker->isAlive()) return false;
        const int serial = room->getTag("XiantuReceiptSerial").toInt() + 1;
        room->setTag("XiantuReceiptSerial", serial);
        QVariantList receipts = target->getTag("XiantuEffects").toList();
        // The obligation belongs to the accepted effect and survives removal of its grant.
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"serial", serial}, {"amount", amount},
            {"phase", phase}, {"actor", ctx.invoker->objectName()}};
        target->setTag("XiantuEffects", receipts);
        target->drawCards(2 * amount, objectName());
        if (!target->isAlive() || !ctx.invoker->isAlive() || target->isNude()) return false;
        const int num = qMin(2 * amount, target->getCardCount(true));
        const Card *selected = room->askForExchange(target, objectName(), num, num, true,
            QString("@xiantu-give::%1:%2").arg(ctx.invoker->objectName()).arg(num));
        if (!selected) return false;
        SkillContext give = ctx;
        give.choice = "give";
        QVariantList ids;
        for (int id : selected->getSubcards()) ids << id;
        give.extra_data = ids;
        skillEffect(event, room, player, give, ctx.invoker);
        return false;
    }
private:
    static SkillInstanceRef sourceRef(const QVariantMap &receipt)
    {
        return SkillInstanceRef(receipt.value("owner").toString(),
            SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
    }
};

class Zhongyong : public TriggerSkillV2
{
public:
    Zhongyong() : TriggerSkillV2("zhongyong") { events << CardOffset; }
    static QList<int> materials(Room *room, const CardEffectStruct &effect)
    {
        QList<int> ids;
        if (!effect.card || !effect.card->isKindOf("Slash") || !effect.offset_card) return ids;
        const QList<int> original = effect.offset_card->isVirtualCard() ? effect.offset_card->getSubcards()
            : QList<int>{effect.offset_card->getEffectiveId()};
        for (int id : original)
            if (id >= 0 && room->getCardPlace(id) == Player::DiscardPile && !room->getCardOwner(id)) ids << id;
        return ids;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return player && player->isAlive() && player->hasSkill(this) && effect.from == player
            && effect.to && !materials(room, effect).isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        const QList<int> ids = materials(room, effect);
        if (ids.isEmpty() || !effect.to) return false;
        room->fillAG(ids, ctx.owner);
        const auto cleanup = qScopeGuard([&] { room->clearAG(ctx.owner); });
        ServerPlayer *recipient = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(effect.to), objectName(),
            "zhongyong-invoke:" + effect.to->objectName(), true, true);
        if (!recipient) return false;
        ctx.targets = {recipient};
        ctx.extra_data = ListI2V(ids);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (ctx.targets.isEmpty()) return false;
        ctx.manual_effect = true;
        SkillContext give = ctx;
        skillEffect(event, room, player, give, ctx.targets.first());
        const QString recipient = give.extra_data.toMap().value("recipient").toString();
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        if (recipient.isEmpty() || recipient == ctx.owner->objectName() || !ctx.owner->isAlive()
            || !effect.to || !effect.to->isAlive() || !ctx.owner->canSlash(effect.to, nullptr, false)) return false;
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
        room->askForUseSlashTo(ctx.owner, effect.to, "zhongyong-slash:" + effect.to->objectName(), false, true);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        DummyCard cards;
        for (const QVariant &entry : ctx.extra_data.toList()) {
            const int id = entry.toInt();
            if (room->getCardPlace(id) == Player::DiscardPile && !room->getCardOwner(id)) cards.addSubcard(id);
        }
        if (cards.subcardsLength() <= 0) return false;
        room->obtainCard(target, &cards);
        ctx.extra_data = QVariantMap{{"recipient", target->objectName()}};
        return false;
    }
};
ShenxingCard::ShenxingCard()
{
    setSkillName("shenxing");
    target_fixed = true;
}

void ShenxingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->isAlive())
        room->drawCards(source, 1, "shenxing");
}

class Shenxing : public ViewAsSkillV2
{
public:
    Shenxing() : ViewAsSkillV2("shenxing", 2)
    {
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && !candidate->hasFlag("using") && request.selectedCardIds.size() < 2
            && !request.selectedCardIds.contains(candidate->getEffectiveId())
            && (request.initiator->handCards().contains(candidate->getEffectiveId())
                || request.initiator->getEquipsId().contains(candidate->getEffectiveId()))
            && !request.initiator->isJilei(candidate);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getCardCount(true) >= 2 && request.initiator->canDiscard(request.initiator, "he");
    }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ShenxingCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        skillEffect(ctx, ctx.initiator);
        return FinishSkill;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && getEffectiveAmount(ctx) > 0) target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

BingyiCard::BingyiCard()
{
    setSkillName("bingyi");
}

bool BingyiCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card::Color color = Card::Colorless;
    foreach (const Card *c, Self->getHandcards()) {
        if (color == Card::Colorless)
            color = c->getColor();
        else if (c->getColor() != color)
            return targets.isEmpty();
    }
    return targets.length() <= Self->getHandcardNum();
}

bool BingyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    Card::Color color = Card::Colorless;
    foreach (const Card *c, Self->getHandcards()) {
        if (color == Card::Colorless)
            color = c->getColor();
        else if (c->getColor() != color)
            return false;
    }
    return targets.length() < Self->getHandcardNum();
}

void BingyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->showAllCards(source);
    foreach(ServerPlayer *p, targets)
        room->drawCards(p, 1, "bingyi");
}

class BingyiViewAsSkill : public ViewAsSkillV2
{
public:
    BingyiViewAsSkill() : ViewAsSkillV2("bingyi") { response_pattern = "@@bingyi"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && !request.initiator->isKongcheng()
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@bingyi"
            && request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
                request.activationRef.key.instanceID, "prompt", false).toBool();
    }
    static bool sameColor(const Player *player)
    {
        const QList<const Card *> cards = player ? player->getHandcards() : QList<const Card *>();
        if (cards.isEmpty()) return false;
        for (const Card *card : cards) if (card->getColor() != cards.first()->getColor()) return false;
        return true;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && !selected.contains(candidate) && sameColor(request.initiator)
            && selected.size() < request.initiator->getHandcardNum();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return selected.isEmpty() || (sameColor(request.initiator) && selected.size() <= request.initiator->getHandcardNum()); }
    QString historyKey(const ActiveSkillRequest &) const override { return "BingyiCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        SkillContext reveal = ctx;
        reveal.choice = "reveal";
        reveal.extra_data = 0;
        skillEffect(reveal, ctx.initiator);
        const int limit = reveal.extra_data.toInt();
        if (limit <= 0) return FinishSkill;
        int count = 0;
        for (ServerPlayer *target : ctx.targets) {
            if (count++ >= limit) break;
            SkillContext draw = ctx;
            draw.choice = "draw";
            skillEffect(draw, target);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (ctx.choice == "reveal") {
            // Capture the actual revealed hand before show-card callbacks can change it.
            const int limit = sameColor(target) ? target->getHandcardNum() : 0;
            target->getRoom()->showAllCards(target);
            ctx.extra_data = limit;
        } else target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class Bingyi : public TriggerSkillV2
{
public:
    Bingyi() : TriggerSkillV2("bingyi") { events << EventPhaseStart; view_as_skill = new BingyiViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish && !player->isKongcheng()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        Room::AcceptedViewAsEffectScope prompt(room, ctx.owner, objectName(), ctx);
        if (!prompt.isValid()) return false;
        const SkillInstanceRef ref = prompt.activationRef();
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "prompt", true);
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
        room->askForUseCard(ctx.owner, "@@bingyi", "@bingyi-card");
        return false;
    }
};
class Zenhui : public TriggerSkill
{
public:
    Zenhui() : TriggerSkill("zenhui")
    {
        events << TargetSpecifying << CardFinished;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (triggerEvent == CardFinished && (use.card->isKindOf("Slash") || (use.card->isNDTrick() && use.card->isBlack()))) {
            use.from->setFlags("-ZenhuiUser_" + use.card->toString());
            return false;
        }
        if (!TriggerSkill::triggerable(player) || player->getPhase() != Player::Play || player->hasFlag(objectName()))
            return false;

        if (use.to.length() == 1 && !use.card->targetFixed()
            && (use.card->isKindOf("Slash") || (use.card->isNDTrick() && use.card->isBlack()))) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p != player && p != use.to.first() && !room->isProhibited(player, p, use.card) && use.card->targetFilter(QList<const Player *>(), p, player))
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            use.from->setTag("zenhui", data);
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "zenhui-invoke:" + use.to.first()->objectName(), true, true);
            use.from->removeTag("zenhui");
            if (target) {
                player->setFlags(objectName());
                room->broadcastSkillInvoke(objectName());

                bool extra_target = true;
                if (!target->isNude()) {
                    const Card *card = room->askForCard(target, "..", "@zenhui-give:" + player->objectName(), data, Card::MethodNone);
                    if (card) {
                        extra_target = false;
                        player->obtainCard(card);

                        if (target->isAlive()) {
                            LogMessage log;
                            log.type = "#BecomeUser";
                            log.from = target;
                            log.card_str = use.card->toString();
                            room->sendLog(log);

                            target->setFlags("ZenhuiUser_" + use.card->toString()); // For AI
                            use.from = target;
                            data = QVariant::fromValue(use);
                        }
                    }
                }
                if (extra_target) {
                    LogMessage log;
                    log.type = "#BecomeTarget";
                    log.from = target;
                    log.card_str = use.card->toString();
                    room->sendLog(log);

                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                    use.to.append(target);
                    room->sortByActionOrder(use.to);
                    data = QVariant::fromValue(use);
                }
            }
        }
        return false;
    }
};

class Jiaojin : public TriggerSkillV2
{
public:
    Jiaojin() : TriggerSkillV2("jiaojin") { events << DamageInflicted; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this) && player->canDiscard(player, "he")
            && damage.from && damage.from->isMale() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const Card *card = room->askForCard(ctx.owner, ".Equip", "@jiaojin", *ctx.original_data,
            Card::MethodNone, nullptr, false, objectName());
        if (!card) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets = {ctx.owner};
        ctx.manual_effect = true;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        const Card *card = Sanguosha->getCard(id);
        if (!ctx.owner || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)
            || !card->isKindOf("EquipCard") || card->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.extra_data = false;
        skillEffect(event, room, player, ctx, ctx.owner);
        return ctx.extra_data.toBool();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!ctx.original_data) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#Jiaojin";
        log.from = ctx.owner;
        log.arg = QString::number(damage.damage);
        damage.damage = qMax(0, damage.damage - getEffectiveAmount(ctx));
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        ctx.original_data->setValue(damage);
        ctx.extra_data = damage.damage == 0;
        return false;
    }
};

class Youdi : public TriggerSkillV2
{
public:
    Youdi() : TriggerSkillV2("youdi") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish && !player->isNude()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner)) if (target->canDiscard(ctx.owner, "he")) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "youdi-invoke", true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner || !ctx.owner->isAlive() || !target->canDiscard(ctx.owner, "he")) return false;
        const int id = room->askForCardChosen(target, ctx.owner, "he", objectName(), false, Card::MethodDiscard);
        if (id < 0 || room->getCardOwner(id) != ctx.owner || !target->canDiscard(ctx.owner, id)) return false;
        // Card faces may reset as the discarded physical card moves.
        const bool slash = Sanguosha->getCard(id)->isKindOf("Slash");
        room->throwCard(id, ctx.owner, target);
        if (slash) return false;
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive() && !target->isNude(); ++i) {
            const int obtained = room->askForCardChosen(ctx.owner, target, "he", "youdi_obtain");
            if (obtained >= 0 && room->getCardOwner(obtained) == target
                && (room->getCardPlace(obtained) == Player::PlaceHand || room->getCardPlace(obtained) == Player::PlaceEquip))
                room->obtainCard(ctx.owner, obtained, false);
        }
        return false;
    }
};
class Qieting : public TriggerSkillV2
{
public:
    Qieting() : TriggerSkillV2("qieting") { events << EventPhaseChanging; }
    static bool noOtherTarget(Room *room, ServerPlayer *actor)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return false;
        QVariantMap filter{{"kind", "use_card_targets"}, {"turn_id", turn}, {"from", actor->objectName()}, {"limit", 128}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap data = value.toMap().value("data").toMap();
                const QVariantMap card = data.value("card").toMap();
                if (!card.contains("type") || !data.contains("targets")) return false;
                if (card.value("type").toInt() == Card::TypeSkill) continue;
                for (const QString &name : data.value("targets").toStringList())
                    if (name != actor->objectName()) return false;
            }
            if (!page.value("has_more").toBool()) return true;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList list;
        if (!player || !player->isAlive() || data.value<PhaseChangeStruct>().to != Player::NotActive
            || !noOtherTarget(room, player)) return list;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->isAlive() && owner->hasSkill(this)) list[owner] << objectName();
        return list;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !ctx.invoker->isAlive() || !noOtherTarget(room, ctx.invoker)) return false;
        QStringList choices;
        for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i)
            if (ctx.invoker->getEquip(i) && !ctx.owner->getEquip(i) && ctx.owner->hasEquipArea(i))
                choices << QString::number(i);
        choices << "draw" << "cancel";
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), QVariant::fromValue(ctx.invoker));
        if (!choices.contains(ctx.choice) || ctx.choice == "cancel") return false;
        if (ctx.choice != "draw") {
            const Card *card = ctx.invoker->getEquip(ctx.choice.toInt());
            if (!card) return false;
            ctx.extra_data = QVariantMap{{"id", card->getEffectiveId()}};
        }
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.choice == "draw") {
            SkillContext draw = ctx;
            skillEffect(event, room, player, draw, ctx.owner);
        } else {
            // Both sides of an equipment transfer have independent recipient hooks.
            SkillContext donor = ctx;
            QVariantMap admission = ctx.extra_data.toMap();
            admission.insert("admitted", false);
            donor.extra_data = admission;
            skillEffect(event, room, player, donor, ctx.invoker);
            if (!donor.extra_data.toMap().value("admitted").toBool()) return false;
            SkillContext recipient = ctx;
            admission.insert("admitted", true);
            recipient.extra_data = admission;
            skillEffect(event, room, player, recipient, ctx.owner);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "draw") {
            room->broadcastSkillInvoke(objectName(), 2);
            target->drawCards(getEffectiveAmount(ctx), objectName());
        } else if (!ctx.extra_data.toMap().value("admitted").toBool()) {
            QVariantMap admission = ctx.extra_data.toMap();
            admission.insert("admitted", true);
            ctx.extra_data = admission;
        } else if (ctx.extra_data.toMap().contains("id") && ctx.invoker && ctx.invoker->isAlive()) {
            const int id = ctx.extra_data.toMap().value("id").toInt(), area = ctx.choice.toInt();
            if (room->getCardOwner(id) != ctx.invoker || room->getCardPlace(id) != Player::PlaceEquip
                || !target->hasEquipArea(area) || target->getEquip(area) || Sanguosha->getCard(id)->hasFlag("using")) return false;
            room->broadcastSkillInvoke(objectName(), 1);
            room->moveCardTo(Sanguosha->getCard(id), target, Player::PlaceEquip);
        }
        return false;
    }
};

XianzhouDamageCard::XianzhouDamageCard()
{
    setSkillName("xianzhou-damage");
    mute = true;
}

void XianzhouDamageCard::onUse(Room *room, CardUseStruct &card_use) const
{
    CardUseStruct use = card_use;
    QVariant data = QVariant::fromValue(use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, use.from, data);
    use = data.value<CardUseStruct>();
    thread->trigger(CardUsed, room, use.from, data);
    use = data.value<CardUseStruct>();
    thread->trigger(CardFinished, room, use.from, data);
}

bool XianzhouDamageCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    return targets.length() == Self->getMark("xianzhou");
}

bool XianzhouDamageCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length() < Self->getMark("xianzhou") && Self->inMyAttackRange(to_select);
}

void XianzhouDamageCard::onEffect(CardEffectStruct &effect) const
{
    effect.from->getRoom()->damage(DamageStruct("xianzhou", effect.from, effect.to));
}

XianzhouCard::XianzhouCard()
{
    setSkillName("xianzhou");
}

bool XianzhouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void XianzhouCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->removePlayerMark(effect.from, "@handover");
    //room->doLightbox("$XianzhouAnimate");
    room->doSuperLightbox(effect.from, "xianzhou");

    int len = 0;
    DummyCard *dummy = new DummyCard;
    foreach (const Card *c, effect.from->getEquips()) {
        dummy->addSubcard(c);
        len++;
    }
    room->setPlayerMark(effect.to, "xianzhou", len);
    effect.to->obtainCard(dummy);
    dummy->deleteLater();

    bool rec = true;
    int count = 0;
    foreach (ServerPlayer *p, room->getOtherPlayers(effect.to)) {
        if (effect.to->inMyAttackRange(p)) {
            count++;
            if (count >= len) {
                rec = false;
                break;
            }
        }
    }

    if ((rec || !room->askForUseCard(effect.to, "@xianzhou", "@xianzhou-damage:::" + QString::number(len))))
        room->recover(effect.from, RecoverStruct(effect.to, nullptr, qMin(len, effect.from->getMaxHp() - effect.from->getHp()), "xianzhou"));
}

class XianzhouDamage : public ViewAsSkillV2
{
public:
    XianzhouDamage() : ViewAsSkillV2("xianzhou-damage", 0) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@xianzhou" && count(request) > 0; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return request.initiator && target && target->isAlive() && target != request.initiator && !selected.contains(target)
            && selected.size() < count(request) && request.initiator->inMyAttackRange(target);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return count(request) > 0 && selected.size() == count(request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && getEffectiveAmount(ctx) > 0)
            target->getRoom()->damage(DamageStruct("xianzhou", ctx.initiator, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
private:
    static int count(const ActiveSkillRequest &request)
    {
        return request.initiator ? request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
            request.activationRef.key.instanceID, "count").toInt() : 0;
    }
};

class Xianzhou : public ViewAsSkillV2
{
public:
    Xianzhou() : ViewAsSkillV2("xianzhou", 0) { frequency = Limited; limit_mark = "@handover"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "XianzhouCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->hasEquip(); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        QVariantList ids;
        if (request.initiator) for (int id : request.initiator->getEquipsId()) ids << id;
        if (card) card->setTag("XianzhouEquips", ids);
        return card;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || !ctx.use_card) return false;
        const QVariantList ids = ctx.use_card->getTag("XianzhouEquips").toList();
        if (ids.isEmpty()) return false;
        for (const QVariant &value : ids) {
            const int id = value.toInt();
            if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceEquip || Sanguosha->getCard(id)->hasFlag("using")) return false;
        }
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || ctx.targets.size() != 1) return FinishSkill;
        Room *room = ctx.initiator->getRoom(); ServerPlayer *recipient = ctx.targets.first();
        room->setPlayerMark(ctx.initiator, "@handover", 0); room->doSuperLightbox(ctx.initiator, objectName());
        Room::AcceptedViewAsEffectScope prompt(room, recipient, "xianzhou-damage", ctx);
        if (!prompt.isValid()) return FinishSkill;
        SkillContext gift = ctx; gift.choice = "gift"; gift.extra_data = QVariantMap();
        skillEffect(gift, recipient);
        const int count = gift.extra_data.toMap().value("count").toInt();
        if (count <= 0) return FinishSkill;
        const SkillInstanceRef ref = prompt.activationRef();
        recipient->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "count", count);
        SkillContext choice = ctx; choice.choice = "choose"; choice.extra_data = QVariantMap{{"count", count}};
        skillEffect(choice, recipient);
        if (choice.extra_data.toMap().value("recover").toBool()) {
            SkillContext recovery = ctx; recovery.choice = "recover";
            recovery.extra_data = QVariantMap{{"count", count}, {"healer", recipient->objectName()}};
            skillEffect(recovery, ctx.initiator);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom(); QVariantMap details = ctx.extra_data.toMap();
        if (ctx.choice == "gift") {
            DummyCard gift;
            for (const QVariant &value : ctx.use_card->getTag("XianzhouEquips").toList()) {
                const int id = value.toInt();
                if (room->getCardOwner(id) == ctx.initiator && room->getCardPlace(id) == Player::PlaceEquip && !Sanguosha->getCard(id)->hasFlag("using")) gift.addSubcard(id);
            }
            if (gift.subcardsLength() == 0) return ContinueEffects;
            const qint64 cause = room->currentHistoryEventId();
            const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
            room->obtainCard(target, &gift, CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.initiator->objectName(), target->objectName(), objectName(), ""));
            details["count"] = givenEquips(room, ctx.initiator, target, gift.getSubcards(), cause, before);
        } else if (ctx.choice == "choose") {
            const int count = details.value("count").toInt();
            int available = 0;
            for (ServerPlayer *other : room->getOtherPlayers(target)) if (target->inMyAttackRange(other)) ++available;
            bool damage = false;
            if (available >= count) {
                RoomState *state = Sanguosha->currentRoomState();
                const auto reason = state->getCurrentCardUseReason(); const QString pattern = state->getCurrentCardUsePattern();
                const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
                damage = room->askForUseCard(target, "@xianzhou", "@xianzhou-damage:::" + QString::number(count)) != nullptr;
            }
            details["recover"] = !damage;
        } else if (ctx.choice == "recover") {
            ServerPlayer *healer = room->findPlayerByObjectName(details.value("healer").toString(), true);
            room->recover(target, RecoverStruct(healer, nullptr, qMin(details.value("count").toInt() * getEffectiveAmount(ctx), target->getLostHp()), objectName()));
        }
        ctx.extra_data = details;
        return ContinueEffects;
    }
private:
    static int givenEquips(Room *room, ServerPlayer *from, ServerPlayer *to, const QList<int> &ids, qint64 cause, const QVariantMap &before)
    {
        if (cause <= 0 || before.contains("error") || !before.value("complete").toBool() || !before.contains("watermark")) return 0;
        QVariantMap filter{{"from", from->objectName()}, {"after", before.value("watermark")}};
        QSet<int> committed;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return 0;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
                const int id = move.value("card_id", -1).toInt();
                if (ids.contains(id) && move.value("to").toString() == to->objectName() && move.value("from_place").toInt() == Player::PlaceEquip
                    && move.value("to_place").toInt() == Player::PlaceHand
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause) committed.insert(id);
            }
            if (!page.value("has_more").toBool()) return committed.size();
            filter.insert("after", page.value("next_after")); filter.insert("watermark", page.value("watermark"));
        }
    }
};

Jianying::Jianying(const QString &name) : TriggerSkillV2(name)
{
    frequency = Frequent;
    events << CardUsed << CardResponded << EventPhaseChanging << EventLoseSkill;
    jianying = "Jianying";
    global = true;
}

bool Jianying::recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
{
    if (!player) return true;
    const Card *card = event == CardUsed ? data.value<CardUseStruct>().card
        : event == CardResponded && data.value<CardResponseStruct>().m_isUse
            ? data.value<CardResponseStruct>().m_card : nullptr;
    bool clear = event == EventLoseSkill && data.toString().section(':', 0, 0) == objectName()
        && !player->hasSkill(objectName(), true);
    if (event == EventPhaseChanging && jianying != "TenyearJianying")
        clear = data.value<PhaseChangeStruct>().from == Player::Play;
    const bool display = card && card->getTypeId() != Card::TypeSkill
        && (card->hasSuit() || card->getNumber() > 0)
        && (jianying == "TenyearJianying" || player->getPhase() == Player::Play)
        && player->hasSkill(objectName(), true);
    if (clear || display) {
        // These marks are presentation only; historical matching uses immutable Room facts.
        for (const QString &mark : player->getMarkNames())
            if (mark.startsWith("&" + objectName() + "+") && mark.endsWith("+#record"))
                room->setPlayerMark(player, mark, 0);
    }
    if (display)
        room->setPlayerMark(player, QString("&%1+%2+%3+#record").arg(objectName())
            .arg(card->getSuitString() + "_char").arg(card->getNumberString()), 1);
    return true;
}

bool Jianying::consecutiveMatch(TriggerEvent event, Room *room, ServerPlayer *owner) const
{
    const QString kind = event == CardUsed ? "use_card" : "respond_card";
    const qint64 eventID = room->historyParent(room->currentHistoryEventId(), kind, true).value("id").toLongLong();
    if (eventID <= 0) return false;
    const QVariantMap current = room->queryHistoryFacts({{"kind", kind}, {"event_id", eventID}, {"limit", 1}});
    const QVariantList currentItems = current.value("items").toList();
    if (current.contains("error") || !current.value("complete").toBool() || currentItems.size() != 1) return false;
    const QVariantMap currentFact = currentItems.first().toMap();
    const QVariant phase = currentFact.value("phase_id");
    const qint64 frontier = currentFact.value("sequence").toLongLong();
    if ((jianying != "TenyearJianying" && phase.toLongLong() <= 0) || frontier <= 0) return false;
    QMap<qint64, QVariantMap> cards;
    for (const QString &factKind : {QString("use_card"), QString("respond_card")}) {
        QVariantMap filter{{"kind", factKind}, {"watermark", frontier}, {"limit", 128},
            {factKind == "use_card" ? "from" : "player", owner->objectName()}};
        if (jianying != "TenyearJianying") filter.insert("phase_id", phase);
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap fact = item.toMap();
                const QVariantMap data = fact.value("data").toMap();
                if (factKind == "respond_card" && !data.value("is_use").toBool()) continue;
                const QVariantMap card = data.value("card").toMap();
                if (!card.contains("type") || !card.contains("suit") || !card.contains("number")) return false;
                if (card.value("type").toInt() != Card::TypeSkill
                    && ((card.value("suit").toInt() >= Card::Spade && card.value("suit").toInt() <= Card::Diamond)
                        || card.value("number").toInt() > 0))
                    cards.insert(fact.value("sequence").toLongLong(), card);
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
    }
    if (cards.size() < 2 || cards.lastKey() != frontier) return false;
    auto it = cards.constEnd();
    const QVariantMap latest = (--it).value();
    const QVariantMap previous = (--it).value();
    const int suit = latest.value("suit").toInt(), number = latest.value("number").toInt();
    return (suit >= Card::Spade && suit <= Card::Diamond && suit == previous.value("suit").toInt())
        || (number > 0 && number == previous.value("number").toInt());
}


TriggerList Jianying::triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
{
    if ((event != CardUsed && event != CardResponded) || !player || player->isDead()
        || (jianying != "TenyearJianying" && player->getPhase() != Player::Play)
        || !player->hasSkill(objectName())) return {};
    if (event == CardResponded && !data.value<CardResponseStruct>().m_isUse) return {};
    return consecutiveMatch(event, room, player) ? TriggerList{{player, {objectName()}}} : TriggerList();
}

bool Jianying::cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    if (!ctx.owner || !consecutiveMatch(event, room, ctx.owner)
        || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
    ctx.targets = {ctx.owner};
    return true;
}

bool Jianying::effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const
{
    if (target->isAlive() && getEffectiveAmount(ctx) > 0)
        target->drawCards(getEffectiveAmount(ctx), objectName());
    return false;
}

class Shibei : public TriggerSkillV2
{
public:
    Shibei() : TriggerSkillV2("shibei")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        ServerPlayer *current = room->getCurrent();
        return player && player->isAlive() && player->hasSkill(this) && current && current->isAlive()
            && current->getPhase() != Player::NotActive
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() == 0) return false;
        const QVariantMap history = room->queryActualDamage({{"turn_id", turn},
            {"to", ctx.owner->objectName()}, {"limit", 2}});
        if (history.contains("error") || !history.value("complete").toBool()
            || !history.value("attribution_complete").toBool()) return false;
        const int count = history.value("items").toList().size();
        // Two records suffice to distinguish first damage from every later damage.
        if (count == 0) return false;
        ctx.extra_data = count == 1;
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, this, qsanRandomBounded(2) + 1);
        const int amount = getEffectiveAmount(ctx);
        if (ctx.extra_data.toBool()) {
            RecoverStruct recover(objectName(), ctx.owner);
            recover.recover = amount;
            room->recover(ctx.owner, recover);
        } else if (amount > 0) {
            room->loseHp(HpLostStruct(ctx.owner, amount, objectName(), ctx.owner));
        }
        return false;
    }
};

class ShibeiRecord : public TriggerSkillV2
{
public:
    ShibeiRecord() : TriggerSkillV2("#shibei-record")
    {
        // Keep the related-skill identity for compatibility; Room history owns the facts.
    }
};

class OLSidi : public TriggerSkill
{
public:
    OLSidi() : TriggerSkill("ol_sidi")
    {
        events << EventPhaseEnd << EventPhaseStart << PreCardUsed << EventPhaseChanging << Death;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->isDead() || player->getPhase() != Player::Play) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->isDead()) return false;
                if (p->isDead() || !p->hasSkill(objectName()) || p->getEquips().isEmpty() || !p->canDiscard(p, "he")) continue;
                QStringList pattern;
                foreach (const Card *c, p->getEquips()) {
                    if (c->isRed() && !pattern.contains("red"))
                        pattern << "red";
                    else if (c->isBlack() && !pattern.contains("black"))
                        pattern << "black";
                    if (pattern.contains("red") && pattern.contains("black")) break;
                }
                if (pattern.isEmpty()) continue;
                const Card *card = room->askForCard(p, "^BasicCard|" + pattern.join(","), "@ol_sidi-discard:" + player->objectName(), data, objectName());
                if (!card) continue;
                room->broadcastSkillInvoke(objectName());
                QString colour = "";
                if (card->isBlack())
                    colour = "black";
                else if (card->isRed())
                    colour = "red";
                if (colour == "") continue;
                QStringList colours = player->property("ol_sidi_colour").toStringList();
                if (!colours.contains(colour)) {
                    colours << colour;
                    room->setPlayerProperty(player, "ol_sidi_colour", colours);
                    room->setPlayerCardLimitation(player, "use,response", QString(".|%1").arg(colour), true);
                    room->addPlayerMark(player, "&ol_sidi+" + colour + "-Clear");
                }
                QStringList sidis = player->property("ol_sidi_from").toStringList();
                if (sidis.contains(p->objectName())) continue;
                sidis << p->objectName();
                room->setPlayerProperty(player, "ol_sidi_from", sidis);
            }
        } else if (event == EventPhaseEnd) {
            if (player->isDead() || player->getPhase() != Player::Play) return false;
            if (player->getMark("ol_sidi_slash-PlayClear") > 0) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->isDead()) return false;
                if (p->isDead() || !p->hasSkill(objectName())) continue;
                QStringList sidis = player->property("ol_sidi_from").toStringList();
                if (!sidis.contains(p->objectName())) continue;
                sidis.removeOne(p->objectName());
                room->setPlayerProperty(player, "ol_sidi_from", sidis);
                Slash *slash = new Slash(Card::NoSuit, 0);
                slash->setSkillName("_ol_sidi");
                slash->deleteLater();
                if (!p->canSlash(player, slash, false)) continue;
                room->sendCompulsoryTriggerLog(p, objectName(), true);
                room->useCard(CardUseStruct(slash, p, player));
            }
        } else if (event == PreCardUsed) {
            if (player->isDead() || player->getPhase() != Player::Play) return false;
            const Card *card = data.value<CardUseStruct>().card;
            if (!card->isKindOf("Slash")) return false;
            room->addPlayerMark(player, "ol_sidi_slash-PlayClear");
        } else {
            if (event == EventPhaseChanging) {
                if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            }
            room->setPlayerProperty(player, "ol_sidi_colour", QStringList());
            room->setPlayerProperty(player, "ol_sidi_from", QStringList());
        }
        return false;
    }
};

PindiCard::PindiCard()
{
    setSkillName("pindi");
}

bool PindiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getMark("pindi_to-PlayClear") <=0 && to_select != Self;
}

void PindiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->addPlayerMark(effect.to, "pindi_to-PlayClear");
    room->addPlayerMark(effect.from, "&pindi-PlayClear");

    int type = Sanguosha->getCard(getSubcards().first())->getTypeId();
    room->addPlayerMark(effect.from, "pindi_card" + QString::number(type) + "-PlayClear");

    if (effect.from->isDead() || effect.to->isDead()) return;
    QStringList choices;
    choices << "draw";
    if (!effect.to->isNude())
        choices << "discard";
    QString choice = room->askForChoice(effect.from, "pindi", choices.join("+"));
    int n = effect.from->getMark("&pindi-PlayClear");
    if (choice == "draw")
        effect.to->drawCards(n, "pindi");
    else
        room->askForDiscard(effect.to, "pindi", n, n, false, true);
    if (effect.from->isDead() || effect.to->isDead() || !effect.to->isWounded() || effect.from->isChained()) return;
    room->setPlayerChained(effect.from);
}

class PindiVS : public ViewAsSkillV2
{
public:
    PindiVS() : ViewAsSkillV2("pindi", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 3; }
    QString historyKey(const ActiveSkillRequest &) const override { return "PindiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    static QVariantMap state(const ActiveSkillRequest &request)
    {
        return request.initiator && request.activationRef.isValid()
            ? request.initiator->getSkillInstanceState(request.activationRef.key.skillName, request.activationRef.key.instanceID)
            : QVariantMap();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.isEmpty() && request.initiator->hasCard(card)
            && !request.initiator->isJilei(card)
            && !state(request).value("types").toList().contains(QVariant(int(card->getTypeId())));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
        const Player *target) const override
    {
        return target && target->isAlive() && target != request.initiator && selected.isEmpty()
            && !state(request).value("targets").toStringList().contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (card && request.selectedCardIds.size() == 1)
            card->setTag("PindiType", int(Sanguosha->getCard(request.selectedCardIds.first())->getTypeId()));
        return card;
    }
    static void commitAccepted(SkillContext &ctx)
    {
        if (!ctx.initiator || !ctx.activationRef.isValid() || !ctx.use_card
            || ctx.extra_data.toMap().value("committed").toBool()) return;
        const auto &key = ctx.activationRef.key;
        QVariantMap state = ctx.initiator->getSkillInstanceState(key.skillName, key.instanceID);
        QVariantList types = state.value("types").toList();
        const QVariant type = ctx.use_card->getTag("PindiType");
        if (type.isValid() && !types.contains(type)) types << type;
        QStringList targets = state.value("targets").toStringList();
        for (ServerPlayer *target : ctx.targets)
            if (target && !targets.contains(target->objectName())) targets << target->objectName();
        const int count = state.value("count").toInt() + 1;
        state["types"] = types; state["targets"] = targets; state["count"] = count;
        ctx.initiator->setSkillInstanceState(key.skillName, key.instanceID, state);
        ctx.extra_data = QVariantMap{{"committed", true}, {"count", count}};
        ctx.initiator->getRoom()->setPlayerMark(ctx.initiator, "&pindi-PlayClear", count);
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest current = request; current.selectedCardIds.clear();
        if (!canSelectCard(current, Sanguosha->getCard(request.selectedCardIds.first()))) return false;
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        commitAccepted(ctx);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        for (ServerPlayer *target : ctx.targets) {
            SkillContext recipient = ctx;
            recipient.choice = "recipient";
            recipient.extra_data = QVariantMap{{"count", ctx.extra_data.toMap().value("count")}, {"chain", false}};
            skillEffect(recipient, target);
            if (recipient.extra_data.toMap().value("chain").toBool()) {
                SkillContext self = ctx; self.choice = "chain";
                skillEffect(self, ctx.initiator);
            }
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.initiator || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "chain") {
            if (!target->isChained()) room->setPlayerChained(target);
            return ContinueEffects;
        }
        if (!ctx.initiator->isAlive()) return ContinueEffects;
        QStringList choices{"draw"};
        if (!target->isNude()) choices << "discard";
        const QString choice = room->askForChoice(ctx.initiator, objectName(), choices.join("+"));
        const int count = ctx.extra_data.toMap().value("count").toInt() * getEffectiveAmount(ctx);
        if (target->isAlive() && count > 0) {
            if (choice == "discard") room->askForDiscard(target, objectName(), count, count, false, true);
            else target->drawCards(count, objectName());
        }
        QVariantMap result = ctx.extra_data.toMap();
        result["chain"] = target->isAlive() && target->isWounded() && ctx.initiator->isAlive();
        ctx.extra_data = result;
        return ContinueEffects;
    }
};

class Pindi : public TriggerSkillV2
{
public:
    Pindi() : TriggerSkillV2("pindi")
    { view_as_skill = new PindiVS; events << EventSkillInvoking << EventPhaseChanging; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            SkillContext ctx = data.value<SkillContext>();
            if (ctx.activationRef.key.skillName == objectName() && ctx.use_card && ctx.use_card->getTypeId() == Card::TypeSkill) {
                // Choice restrictions belong to acceptance, even when payment or the whole effect is waived.
                PindiVS::commitAccepted(ctx);
                data = QVariant::fromValue(ctx);
            }
        } else if (player && data.value<PhaseChangeStruct>().from == Player::Play) {
            for (const SkillInstance &instance : player->getSkillInstances()) if (instance.skillName == objectName()) {
                player->removeSkillInstanceStateValue(instance.skillName, instance.instanceID, "types");
                player->removeSkillInstanceStateValue(instance.skillName, instance.instanceID, "targets");
                player->removeSkillInstanceStateValue(instance.skillName, instance.instanceID, "count");
            }
        }
        return true;
    }
};

class OLFaen : public TriggerSkillV2
{
public:
    OLFaen() : TriggerSkillV2("ol_faen")
    {
        events << TurnedOver << ChainStateChanged;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || (event == ChainStateChanged && !player->isChained())
            || (event == TurnedOver && !player->faceUp())) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(this)) result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!ctx.owner || !player || !player->isAlive()) return false;
        ctx.targets = {player};
        return room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(player), false);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class OLZhongyong : public TriggerSkill
{
public:
    OLZhongyong() : TriggerSkill("ol_zhongyong")
    {
        events << CardOffset << CardFinished;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardOffset) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card->isKindOf("Slash")) return false;
            if (!effect.offset_card||!effect.offset_card->isKindOf("Jink")) return false;
            QVariantList jink = effect.from->getTag("ol_zhongyong_jink" + effect.card->toString()).toList();
            if (effect.offset_card->isVirtualCard() && effect.offset_card->subcardsLength() > 0) {
                foreach (int id, effect.offset_card->getSubcards()) {
                    if (jink.contains(QVariant(id))) continue;
                    jink << id;
                }
            } else if (!effect.offset_card->isVirtualCard()) {
                if (!jink.contains(QVariant(effect.offset_card->getEffectiveId())))
                    jink << effect.offset_card->getEffectiveId();
            }
            effect.from->setTag("ol_zhongyong_jink" + effect.card->toString(), jink);
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash")) return false;

            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (use.to.contains(p)) continue;
                targets << p;
            }
            if (targets.isEmpty()) return false;

            QVariantList jink = player->getTag("ol_zhongyong_jink" + use.card->toString()).toList();
            QList<int> slash_ids,jink_ids = ListV2I(jink);

            foreach (int id, use.card->getSubcards()) {
                if (room->getCardPlace(id) != Player::DiscardPile)
                    slash_ids.removeOne(id);
            }
            foreach (int id, jink_ids) {
                if (room->getCardPlace(id) != Player::DiscardPile)
                    jink_ids.removeOne(id);
            }

            QStringList choices;
            if (!slash_ids.isEmpty()) choices << "slash";
            if (!jink_ids.isEmpty()) choices << "jink";
            if (choices.isEmpty()) return false;

            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@ol_zhongyong-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());

            QList<int> give_list = jink_ids;
            if (room->askForChoice(player, objectName(), choices.join("+"), data) == "slash")
                give_list = slash_ids;
            room->giveCard(player, target, give_list, objectName(), true);

            if (target->isDead()) return false;
            bool red = false;
            foreach (int id, give_list) {
                if (Sanguosha->getCard(id)->isRed()) {
                    red = true;
                    break;
                }
            }
            if (!red) return false;

            QList<ServerPlayer *> tos;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (player->inMyAttackRange(p) && target->canSlash(p, true))
                    tos << p;
            }
            if (tos.isEmpty()) return false;
            room->askForUseSlashTo(target, tos, "@ol_zhongyong-slash");
        }
        return false;
    }
};

class Fenli : public TriggerSkillV2
{
public:
    Fenli() : TriggerSkillV2("fenli") { events << EventPhaseChanging; }
    bool eligible(Room *room, ServerPlayer *player, Player::Phase phase) const
    {
        if (!player || !player->isAlive() || player->isSkipped(phase)) return false;
        if (phase != Player::Draw && phase != Player::Play && phase != Player::Discard) return false;
        const int value = phase == Player::Draw ? player->getHandcardNum() : phase == Player::Play ? player->getHp() : player->getEquips().size();
        if (phase == Player::Discard && value <= 0) return false;
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            const int candidate = phase == Player::Draw ? other->getHandcardNum() : phase == Player::Play ? other->getHp() : other->getEquips().size();
            if (candidate > value) return false;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->hasSkill(this) && eligible(room, player, data.value<PhaseChangeStruct>().to)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const Player::Phase phase = ctx.original_data->value<PhaseChangeStruct>().to;
        if (!eligible(room, ctx.owner, phase)) return false;
        const QString prompt = phase == Player::Draw ? "draw" : phase == Player::Play ? "play" : "discard";
        if (!room->askForSkillInvoke(ctx.owner, objectName(), prompt)) return false;
        ctx.extra_data = int(phase);
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->skip(static_cast<Player::Phase>(ctx.extra_data.toInt()));
        return false;
    }
};
PingkouCard::PingkouCard()
{
    setSkillName("pingkou");
}

bool PingkouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length() < Self->getMark("pingkou_phase_skipped-Clear") && to_select != Self;
}

void PingkouCard::onEffect(CardEffectStruct &effect) const
{
    effect.from->getRoom()->damage(DamageStruct("pingkou", effect.from, effect.to));
}

class PingkouVS : public ViewAsSkillV2
{
public:
    PingkouVS() : ViewAsSkillV2("pingkou", 0) { response_pattern = "@@pingkou"; }
    static int maximum(const ActiveSkillRequest &request)
    {
        return request.initiator && request.activationRef.isValid()
            ? request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
                request.activationRef.key.instanceID, "maximum").toInt() : 0;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@pingkou" && maximum(request) > 0; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
        const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && !selected.contains(target) && selected.size() < maximum(request); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return !selected.isEmpty() && selected.size() <= maximum(request); }
    QString historyKey(const ActiveSkillRequest &) const override { return "PingkouCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && ctx.initiator && getEffectiveAmount(ctx) > 0)
            target->getRoom()->damage(DamageStruct(objectName(), ctx.initiator, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class Pingkou : public TriggerSkillV2
{
public:
    Pingkou() : TriggerSkillV2("pingkou") { events << EventPhaseChanging; view_as_skill = new PingkouVS; }
    static int skippedPhases(Room *room, const ServerPlayer *player)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (!player || turn <= 0) return -1;
        QVariantMap filter{{"kind", "phase"}, {"turn_id", turn}, {"player", player->objectName()}, {"limit", 128}};
        int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryEvents(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList())
                if (entry.toMap().value("outcome").toString() == "skipped") ++count;
            if (!page.value("has_more").toBool()) return count;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(this)
            && data.value<PhaseChangeStruct>().to == Player::NotActive && skippedPhases(room, player) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int maximum = qMin(skippedPhases(room, ctx.owner), room->alivePlayerCount() - 1);
        ctx.extra_data = maximum;
        ctx.manual_effect = true;
        return maximum > 0;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        // Freeze the prompt's exact instance before callbacks; the accepted parent may already be retired.
        Room::AcceptedViewAsEffectScope prompt(room, ctx.owner, objectName(), ctx);
        if (!prompt.isValid()) return false;
        const SkillInstanceRef ref = prompt.activationRef();
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "maximum", ctx.extra_data);
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
        room->askForUseCard(ctx.owner, "@@pingkou", "@pingkou:" + QString::number(ctx.extra_data.toInt()));
        return false;
    }
};

class OLBenxiVS : public ZeroCardViewAsSkill
{
public:
    OLBenxiVS() : ZeroCardViewAsSkill("olbenxi")
    {
        response_pattern = "@@olbenxi!";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    const Card *viewAs() const
    {
        return new ExtraCollateralCard;
    }
};

class OLBenxi : public TriggerSkill
{
public:
    OLBenxi() : TriggerSkill("olbenxi")
    {
        events << CardUsed << Damage << PreCardUsed << CardResponded;
        view_as_skill =new OLBenxiVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!player->hasFlag("CurrentPlayer") || use.card->isKindOf("SkillCard")) return false;
            //room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            room->notifySkillInvoked(player, objectName());
            room->addDistance(player, -1);
            room->addPlayerMark(player, "&olbenxi-Clear");
        } else if (event == CardResponded) {
            CardResponseStruct res = data.value<CardResponseStruct>();
            if (!player->hasFlag("CurrentPlayer") || res.m_card->isKindOf("SkillCard")) return false;
            if (!res.m_isUse) return false;
            room->notifySkillInvoked(player, objectName());
            room->addDistance(player, -1);
            room->addPlayerMark(player, "&olbenxi-Clear");
        } else if (event == PreCardUsed) {
            if (!player->hasFlag("CurrentPlayer")) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash") && !use.card->isNDTrick()) return false;
            if (use.to.length() != 1) return false;
            bool allone = true;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->distanceTo(p) != 1) {
                    allone = false;
                    break;
                }
            }
            if (!allone) return false;
            QStringList choices, excepts;
            QList<ServerPlayer *> available_targets;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (use.to.contains(p) || player->isProhibited(p, use.card)) continue;
                if (use.from == p && use.card->isKindOf("AOE")) continue;
                if (use.card->targetFixed()) {
                    if (!use.card->isKindOf("Peach") || p->isWounded())
                        available_targets << p;
                } else {
                    if (use.card->isKindOf("Collateral")) {
						int x = 0;
						if (use.card->targetFilter(QList<const Player *>(), p, player, x)||x>0)
							available_targets << p;
					}else if (use.card->targetFilter(QList<const Player *>(), p, player))
                        available_targets << p;
                }
            }
            if (!available_targets.isEmpty()) choices << "extra";
            choices << "ignore" << "noresponse" << "draw" <<"cancel";
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);

            for (int i = 1; i <= 2; i++) {
                if (choices.isEmpty()) break;
                QString choice = room->askForChoice(player, objectName(), choices.join("+"), data, excepts.join("+"));
                if (choice == "cancel") break;
                choices.removeOne(choice);
                excepts << choice;
                LogMessage log;
                log.type = "#FumianFirstChoice";
                log.from = player;
                log.arg = "olbenxi:" + choice;
                room->sendLog(log);
                if (choice == "extra") {
                    ServerPlayer *target;
                    if (use.card->isKindOf("Collateral")){
						QStringList tos;
						tos.append(use.card->toString());
						foreach (ServerPlayer *t, use.to)
							tos.append(t->objectName());
						room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
                        room->askForUseCard(player, "@@olbenxi!", "@olbenxi-extra:" + use.card->objectName());
                        target = player->getTag("ExtraCollateralTarget").value<ServerPlayer *>();
						player->removeTag("ExtraCollateralTarget");
                        if (!target) {
                            target = available_targets.at(qsanRandomBounded(available_targets.length()));
                            foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                                if (target->canSlash(p)){
									target->setTag("attachTarget", QVariant::fromValue(p));
									break;
								}
                            }
                        }
					}else
                        target = room->askForPlayerChosen(player, available_targets, objectName(), "@olbenxi-extra:" + use.card->objectName());
                    use.to.append(target);
                    room->sortByActionOrder(use.to);

                    if (use.card->hasFlag("olbenxi_ignore"))
                        target->addQinggangTag(use.card);

                    LogMessage log;
                    log.type = "#QiaoshuiAdd";
                    log.from = player;
                    log.to << target;
                    log.card_str = use.card->toString();
                    log.arg = "olbenxi";
                    room->sendLog(log);
                    data = QVariant::fromValue(use);
                } else if (choice == "ignore") {
                    room->setCardFlag(use.card, "olbenxi_ignore");
                    foreach (ServerPlayer *p, use.to)
                        p->addQinggangTag(use.card);
                } else if (choice == "noresponse") {
                    use.no_offset_list << "_ALL_TARGETS";
                    data = QVariant::fromValue(use);
                } else
                    room->setCardFlag(use.card, "olbenxi_damage");
            }
        } else if (event == Damage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->hasFlag("olbenxi_damage")) return false;
            player->drawCards(1, objectName());
        }
        return false;
    }
};

YJCM2014Package::YJCM2014Package()
    : Package("YJCM2014")
{
    General *caifuren = new General(this, "caifuren", "qun", 3, false); // YJ 301
    caifuren->addSkill(new Qieting);
    caifuren->addSkill(new Xianzhou);
    skills << new XianzhouDamage;
    related_skills.insert("xianzhou", "xianzhou-damage");

    General *caozhen = new General(this, "caozhen", "wei"); // YJ 302
    caozhen->addSkill(new Sidi);
    caozhen->addSkill(new SidiTargetMod);
    related_skills.insert("sidi", "#sidi-target");

    General *ol_caozhen = new General(this, "ol_caozhen", "wei");
    ol_caozhen->addSkill(new OLSidi);

    General *chenqun = new General(this, "chenqun", "wei", 3); // YJ 303
    chenqun->addSkill(new Dingpin);
    chenqun->addSkill(new Faen);
    chenqun->addSkill(new DingpinBf);
    related_skills.insert("dingpin", "#dingpinbf");

    General *ol_chenqun = new General(this, "ol_chenqun", "wei", 3);
    ol_chenqun->addSkill(new Pindi);
    ol_chenqun->addSkill(new OLFaen);

    General *guyong = new General(this, "guyong", "wu", 3); // YJ 304
    guyong->addSkill(new Shenxing);
    guyong->addSkill(new Bingyi);

    General *hanhaoshihuan = new General(this, "hanhaoshihuan", "wei"); // YJ 305
    hanhaoshihuan->addSkill(new Shenduan);
    hanhaoshihuan->addSkill(new ShenduanTargetMod);
    hanhaoshihuan->addSkill(new Yonglve);
    hanhaoshihuan->addSkill(new YonglveSlash);
    related_skills.insert("shenduan", "#shenduan-target");
    related_skills.insert("yonglve", "#yonglve");

    General *jvshou = new General(this, "jvshou", "qun", 3); // YJ 306
    jvshou->addSkill(new Jianying);
    jvshou->addSkill(new Shibei);
    jvshou->addSkill(new ShibeiRecord);
    related_skills.insert("shibei", "#shibei-record");

    General *sunluban = new General(this, "sunluban", "wu", 3, false); // YJ 307
    sunluban->addSkill(new Zenhui);
    sunluban->addSkill(new Jiaojin);

    General *wuyi = new General(this, "wuyi", "shu"); // YJ 308
    wuyi->addSkill(new Benxi);
    wuyi->addSkill(new BenxiTargetMod);
    wuyi->addSkill(new BenxiDistance);
    related_skills.insert("benxi", "#benxi-target");
    related_skills.insert("benxi", "#benxi-dist");

    General *zhangsong = new General(this, "zhangsong", "shu", 3); // YJ 309
    zhangsong->addSkill(new Qiangzhi);
    zhangsong->addSkill(new Xiantu);

    General *zhoucang = new General(this, "zhoucang", "shu"); // YJ 310
    zhoucang->addSkill(new Zhongyong);

    General *ol_zhoucang = new General(this, "ol_zhoucang", "shu");
    ol_zhoucang->addSkill(new OLZhongyong);

    General *zhuhuan = new General(this, "zhuhuan", "wu"); // YJ 311
    zhuhuan->addSkill(new Fenli);
    zhuhuan->addSkill(new Pingkou);

    addMetaObject<DingpinCard>();
    addMetaObject<PindiCard>();
    addMetaObject<ShenxingCard>();
    addMetaObject<BingyiCard>();
    addMetaObject<XianzhouCard>();
    addMetaObject<XianzhouDamageCard>();
    addMetaObject<SidiCard>();
    addMetaObject<PingkouCard>();
}

ADD_PACKAGE(YJCM2014)

void MigrateToNostalgiaYJCM2014(Package *pkg)
{
    General *nos_zhuhuan = new General(pkg, "nos_zhuhuan", "wu"); // YJ 311
    nos_zhuhuan->addSkill(new Youdi);
}

void MigrateToOLStYJ2014(Package *pkg)
{
    General *ol_wuyi = new General(pkg, "ol_wuyi", "shu");
    ol_wuyi->addSkill(new OLBenxi);
}
