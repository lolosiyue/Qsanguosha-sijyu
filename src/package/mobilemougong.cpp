#include "mobilemougong.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include <memory>
#include <QScopeGuard>
#include "skill-instance-utils.h"
#include "card-lifetime-manager.h"
//#include "json.h"

MobileMouZhihengCard::MobileMouZhihengCard()
{
    setSkillName("mobilemouzhiheng");
	target_fixed = true;
	will_throw = true;
	mute = true;
}

void MobileMouZhihengCard::onUse(Room *room, CardUseStruct &card_use) const
{
	if (card_use.from->hasSkill("jilve",true))
		room->broadcastSkillInvoke("jilve", 4);
	else
		room->broadcastSkillInvoke("mobilemouzhiheng");

	bool allhand = !card_use.from->isKongcheng();
	if (allhand) {
		foreach(int id, card_use.from->handCards()) {
			if (!subcards.contains(id)) {
				allhand = false;
				break;
			}
		}
	}
	if (allhand)
		room->setCardFlag(this, "mobilemouzhiheng_all_handcard_" + card_use.from->objectName());
	SkillCard::onUse(room, card_use);
}

void MobileMouZhihengCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int x = subcardsLength();
	bool all = hasFlag("mobilemouzhiheng_all_handcard_" + source->objectName());
	if (all)
		x = x + source->getMark("&mobilemouye") + 1;
	source->drawCards(x, "mobilemouzhiheng");
	if (all)
		source->loseMark("&mobilemouye");
}

class MobileMouZhiheng : public ViewAsSkillV2
{
public:
	MobileMouZhiheng() : ViewAsSkillV2("mobilemouzhiheng", 999)
	{
        setPhaseName("Play");
	}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouZhihengCard"; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && !request.selectedCardIds.contains(card->getEffectiveId()) && !request.initiator->isJilei(card)
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
	}
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return !request.selectedCardIds.isEmpty(); }
    bool canActivate(const ActiveSkillRequest &request) const override
	{
        return request.initiator && request.initiator->canDiscard(request.initiator, "he")
            && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern == "@mobilemouzhiheng");
	}
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request); if (!card || !request.initiator) return card;
        bool all = !request.initiator->isKongcheng();
        for (int id : request.initiator->handCards()) if (!request.selectedCardIds.contains(id)) all = false;
        card->setTag("MobileMouZhihengAll", all); return card;
    }
    EffectFlow effect(SkillContext &ctx) const override
	{
        ctx.manual_effect = true;
        ctx.targets = {ctx.invoker};
        return skillEffect(ctx, ctx.invoker);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const bool all = ctx.use_card->getTag("MobileMouZhihengAll").toBool();
        int count = ctx.use_card->subcardsLength();
        if (all) count += target->getMark("&mobilemouye") + 1;
        count *= getEffectiveAmount(ctx);
        target->drawCards(count, objectName());
        if (all) ctx.invoker->loseMark("&mobilemouye");
        return ContinueEffects;
	}
};

class MobileMouTongye : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouTongye() : TriggerSkillV2("mobilemoutongye") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->getPhase() == Player::Finish && actor->hasSkill(objectName()) ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "gaibian+bubian");
        ctx.targets << ctx.invoker; return ctx.choice == "gaibian" || ctx.choice == "bubian";
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.choice.isEmpty()) ctx.choice = room->askForChoice(ctx.invoker, objectName(), "gaibian+bubian");
        return skillEffect(event, room, actor, ctx, ctx.invoker);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        int count = 0; for (ServerPlayer *p : room->getAlivePlayers()) count += p->getEquips().size();
        const qint64 serial = room->getTag("MobileMouTongyeSequence").toLongLong() + 1; room->setTag("MobileMouTongyeSequence", serial);
        QVariantList receipts = target->getTag("MobileMouTongyeReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"equipment", count}, {"choice", ctx.choice}, {"amount", getEffectiveAmount(ctx)},
            {"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        target->setTag("MobileMouTongyeReceipts", receipts);
        LogMessage log; log.type = "#FumianFirstChoice"; log.from = target; log.arg = objectName() + ":" + ctx.choice; room->sendLog(log);
        return false;
    }
};

class MobileMouTongyeEquip : public TriggerSkillV2
{
public:
    MobileMouTongyeEquip() : TriggerSkillV2("#mobilemoutongye") { events << EventPhaseStart << EventSkillEffectFinished << Death; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == Death) { if (data.value<DeathStruct>().who) data.value<DeathStruct>().who->removeTag("MobileMouTongyeReceipts"); }
        else if (event == EventSkillEffectFinished) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.skill_name == objectName() && accepted.invoker) {
                QVariantList receipts = accepted.invoker->getTag("MobileMouTongyeReceipts").toList(); receipts.removeOne(accepted.extra_data);
                accepted.invoker->setTag("MobileMouTongyeReceipts", receipts);
            }
        }
        Q_UNUSED(actor); return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !actor || !actor->isAlive() || actor->getPhase() != Player::Start) return true;
        for (const QVariant &value : actor->getTag("MobileMouTongyeReceipts").toList()) {
            const QVariantMap receipt = value.toMap(); SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true); ctx.initiator = ctx.owner; ctx.invoker = actor;
            ctx.instanceID = receipt.value("instance").toInt(); ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.current_event = event; ctx.original_data = &data; ctx.extra_data = receipt; ctx.amount = receipt.value("amount").toInt();
            ctx.instanceID = receipt.value("serial").toInt(); ctx.trigger_count = 0; ctx.is_forced = true; ctx.targets << actor; contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getTag("MobileMouTongyeReceipts").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        QVariantList receipts = target->getTag("MobileMouTongyeReceipts").toList(); receipts.removeOne(ctx.extra_data); target->setTag("MobileMouTongyeReceipts", receipts);
        int count = 0; for (ServerPlayer *p : room->getAlivePlayers()) count += p->getEquips().size();
        const QVariantMap receipt = ctx.extra_data.toMap();
        const bool correct = (count != receipt.value("equipment").toInt()) == (receipt.value("choice").toString() == "gaibian");
        // Ye is a printed, shared player resource; each prediction retains its own source and snapshot.
        const int amount = getEffectiveAmount(ctx);
        if (correct) room->setPlayerMark(target, "&mobilemouye", qMin(2, target->getMark("&mobilemouye") + amount));
        else room->removePlayerMark(target, "&mobilemouye", amount);
        room->broadcastSkillInvoke("mobilemoutongye"); return false;
    }
};

class MobileMouJiuyuan : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

	MobileMouJiuyuan() : TriggerSkillV2("mobilemoujiuyuan$")
	{
		events << CardUsed << PreHpRecover;
		frequency = Compulsory;
	}

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || player->isDead()) return result;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.card->isKindOf("Peach") || player->getKingdom() != "wu") return result;
            for (ServerPlayer *owner : room->getOtherPlayers(player))
                if (owner->hasLordSkill(this)) result[owner] << objectName();
        } else {
            const RecoverStruct rec = data.value<RecoverStruct>();
            if (player->hasLordSkill(this) && rec.card && rec.card->isKindOf("Peach")
                && rec.who && rec.who != player && rec.who->getKingdom() == "wu")
                result[player] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // The lord receives the benefit, not the Wu player who used Peach.
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, actor, ctx, ctx.invoker);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QString name = ctx.owner->isWeidi() ? QString("weidi") : objectName();
        if (event == CardUsed) {
            room->sendCompulsoryTriggerLog(ctx.owner, name, true, true);
            target->drawCards(getEffectiveAmount(ctx), objectName());
        } else {
            if (target != ctx.invoker) return false;
            RecoverStruct rec = ctx.original_data->value<RecoverStruct>();
            LogMessage log;
            log.type = "#JiuyuanExtraRecover";
            log.from = ctx.owner;
            log.to << rec.who;
            log.arg = name;
            room->sendLog(log);
            room->broadcastSkillInvoke(name);
            room->notifySkillInvoked(ctx.owner, name);
            rec.recover += getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(rec);
        }
        return false;
    }
};

MobileMouLeijiCard::MobileMouLeijiCard()
{
    setSkillName("mobilemouleiji");
}

bool MobileMouLeijiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select!=Self;
}

void MobileMouLeijiCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	if(!effect.from->hasFlag("CurrentPlayer"))
		room->addPlayerMark(effect.from,"ban_daobing");
	effect.from->loseMark("&mou_daobing",4);
	room->damage(DamageStruct(getSkillName(), effect.from, effect.to, 1,DamageStruct::Thunder));
}

class MobileMouLeiji : public ViewAsSkillV2
{
public:
	MobileMouLeiji() : ViewAsSkillV2("mobilemouleiji")
	{
	}
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouLeijiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
	{
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("&mou_daobing") >= 4;
	}
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
	{
        return target && target != request.initiator && selected.isEmpty();
	}
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (ctx.initiator->getMark("&mou_daobing") < 4) return false;
        // Daobing is a printed shared resource, paid even when its target effect is canceled.
        ctx.initiator->loseMark("&mou_daobing", 4);
        if (!ctx.initiator->hasFlag("CurrentPlayer")) room->addPlayerMark(ctx.initiator, "ban_daobing");
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target,
            getEffectiveAmount(ctx), DamageStruct::Thunder));
        return ContinueEffects;
    }
};

class MobileMouGuidao : public TriggerSkillV2
{
public:
	MobileMouGuidao() : TriggerSkillV2("mobilemouguidao")
	{
		events << GameStart << Damaged << DamageInflicted << EventPhaseChanging;
	}
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.is_forced = ctx.current_event != DamageInflicted;
        ctx.initiator = ctx.owner; ctx.invoker = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // Spending the shared resource leaves a restriction until the next turn, even after skill removal.
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().from == Player::NotActive)
            room->setPlayerMark(player, "ban_daobing", 0);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == Damaged) {
            if (data.value<DamageStruct>().nature == DamageStruct::Normal) return result;
            foreach (ServerPlayer *owner, room->getAlivePlayers()) {
                if (owner->hasSkill(objectName()) && owner->getMark("&mou_daobing") < 8
                    && owner->getMark("ban_daobing") == 0) result.insert(owner, {objectName()});
            }
        } else if (player && player->isAlive() && player->hasSkill(objectName())
            && (event == GameStart || (event == DamageInflicted && player->getMark("&mou_daobing") >= 2)))
            result.insert(player, {objectName()});
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return event != DamageInflicted || ctx.owner->askForSkillInvoke(this, *ctx.original_data);
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != DamageInflicted) return true;
        if (ctx.owner->getMark("&mou_daobing") < 2) return false;
        ctx.owner->loseMark("&mou_daobing", 2);
        if (!ctx.owner->hasFlag("CurrentPlayer")) room->addPlayerMark(ctx.owner, "ban_daobing");
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, actor, ctx, event == DamageInflicted ? ctx.original_data->value<DamageStruct>().to : ctx.invoker);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DamageInflicted) {
            if (target != ctx.original_data->value<DamageStruct>().to) return false;
            ctx.owner->peiyin(this);
            return getEffectiveAmount(ctx) > 0;
        }
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        const int count = qMin((event == GameStart ? 2 : 1) * getEffectiveAmount(ctx),
            qMax(0, 8 - target->getMark("&mou_daobing")));
        if (count > 0) target->gainMark("&mou_daobing", count);
        return false;
    }
};

class MobileMouHuangtian : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouHuangtian() : TriggerSkillV2("mobilemouhuangtian$")
    {
        events << EventPhaseStart << Damage << RoundStart;
        frequency = Compulsory;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event != RoundStart) return false;
        const QVariant round = room->historyScopes().value("round_id");
        if (round.toLongLong() <= 0 || room->getTag("MobileMouHuangtianRound") == round) return false;
        room->setTag("MobileMouHuangtianRound", round);
        for (ServerPlayer *holder : room->getAllPlayers(true))
            for (const SkillInstance &instance : holder->getSkillInstances())
                if (holder->getSkillInstanceStateValue(instance.skillName, instance.instanceID,
                    "mobilemouhuangtian_round_gained").toInt() > 0)
                    holder->removeSkillInstanceStateValue(instance.skillName, instance.instanceID, "mobilemouhuangtian_round_gained");
        return true;
    }
    int gained(const SkillContext &ctx) const
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return 4;
        const ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        return holder ? holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
            "mobilemouhuangtian_round_gained").toInt() : 4;
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        return ctx.current_event == EventPhaseStart || ctx.choice == "equip" || gained(ctx) < 4;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID))
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
                "mobilemouhuangtian_round_gained", gained(ctx) + ctx.extra_data.toInt());
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (holder) holder->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mobilemouhuangtian_round_gained");
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || player->isDead()) return result;
        if (event == EventPhaseStart) {
            if (player->getPhase() == Player::RoundStart && room->getTag("TurnLengthCount").toInt() == 1
                && player->hasLordSkill(this)) result[player] << objectName();
        } else if (event == Damage && player->getKingdom() == "qun" && data.value<DamageStruct>().from == player) {
            for (ServerPlayer *owner : room->getOtherPlayers(player))
                if (owner->hasLordSkill(this) && owner->hasSkill("mobilemouguidao", true)
                    && owner->getMark("&mou_daobing") < 8) result[owner] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.choice = event == EventPhaseStart ? "equip" : "gain";
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ctx.choice = event == EventPhaseStart ? "equip" : "gain";
        return skillEffect(event, room, actor, ctx, ctx.invoker);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        if (ctx.choice == "equip") {
            target->getDerivativeCard("_taipingyaoshu");
        } else {
            const int count = qMin(qMin(2 * getEffectiveAmount(ctx), 4 - gained(ctx)), 8 - target->getMark("&mou_daobing"));
            if (count <= 0) return false;
            // This quota counts markers actually granted, not attempted activations or the damage dealer.
            ctx.extra_data = count;
            addUsage(ctx);
            target->gainMark("&mou_daobing", count);
        }
        return false;
    }
};
class MobileMouZishou : public TriggerSkillV2
{
public:
	MobileMouZishou() : TriggerSkillV2("mobilemouzishou")
	{
		events << EventPhaseStart;
		frequency = Compulsory;
	}
    bool eligible(Room *room, ServerPlayer *owner, ServerPlayer *actor) const
    {
        for (ServerPlayer *source : QList<ServerPlayer *>{owner, actor}) {
            ServerPlayer *victim = source == owner ? actor : owner;
            // The printed rule is mutual damage over the game, with no turn qualifier.
            const QVariantMap page = room->queryActualDamage({{"from", source->objectName()},
                {"to", victim->objectName()}, {"limit", 1}});
            if (page.contains("error") || !page.value("complete").toBool()
                || !page.value("items").toList().isEmpty()) return false;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->isDead() || player->getPhase() != Player::Finish || player->getCardCount() < 1)
            return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && eligible(room, owner, player)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.invoker};
        return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getCardCount() > 0;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "give") {
            const QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString()); QList<int> ids;
            if (from) for (const QVariant &value : receipt.value("ids").toList()) if (room->getCardOwner(value.toInt()) == from && (room->getCardPlace(value.toInt()) == Player::PlaceHand || room->getCardPlace(value.toInt()) == Player::PlaceEquip)) ids << value.toInt();
            if (!ids.isEmpty()) { DummyCard cards(ids); room->giveCard(from, target, &cards, objectName()); } return false;
        }
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        const int count = qMin(getEffectiveAmount(ctx), target->getCardCount());
        if (count <= 0 || ctx.owner->isDead()) return false;
        const Card *gift = room->askForExchange(target, objectName(), count, count, true,
            "mobilemouzishou0:" + ctx.owner->objectName());
        if (gift) { ctx.choice = "give"; ctx.extra_data = QVariantMap{{"from", target->objectName()}, {"ids", ListI2V(gift->getSubcards())}}; skillEffect(event, room, actor, ctx, ctx.owner); }
        return false;
    }
};

class MobileMouZongshi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

	MobileMouZongshi() : TriggerSkillV2("mobilemouzongshi")
	{
		events << Damaged << EventSkillInvoking;
        frequency = Compulsory;
	}
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && damage.from && damage.from->isAlive()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.activationRef.isValid()) return false;
        ServerPlayer *attacker = ctx.original_data->value<DamageStruct>().from;
        const SkillInstanceRef ref = getUsageRef(ctx);
        const ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        return attacker && holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)
            && !holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
            "punished_players").toStringList().contains(attacker->objectName());
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.original_data->value<DamageStruct>().from};
        return checkCustomUsage(ctx);
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ctx.original_data || !ref.isValid()) return;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        ServerPlayer *attacker = ctx.original_data->value<DamageStruct>().from;
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID) || !attacker) return;
        QStringList punished = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
            "punished_players").toStringList();
        // Per-attacker, per-instance game quota, independent of other Zongshi owners.
        if (!punished.contains(attacker->objectName())) punished << attacker->objectName();
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "punished_players", punished);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, actor, ctx, ctx.original_data->value<DamageStruct>().from);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        target->throwAllHandCards(objectName());
        return false;
    }
};

class MobileMouWansha : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouWansha() : TriggerSkillV2("mobilemouwansha") { events << Dying << EventSkillInvoking; global = true; }
    LimitScope getLimitScope() const override { return Limit_Round; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) { const SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.skill_name == objectName() && accepted.activationRef.key.skillName == objectName()) addUsage(accepted); }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result; if (event != Dying || !data.value<DyingStruct>().who) return result;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ServerPlayer *target = ctx.original_data->value<DyingStruct>().who;
        if (!target || !room->askForSkillInvoke(ctx.owner, objectName() + "$-1", QVariant::fromValue(target))) return false;
        ctx.targets << target;
        ctx.extra_data = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "upgraded").toBool(); return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ctx.extra_data = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "upgraded").toBool();
        return skillEffect(event, room, actor, ctx, ctx.original_data->value<DyingStruct>().who);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (!target || !target->isAlive() || !ctx.invoker) return false;
        if (ctx.choice == "give") {
            const QVariantMap receipt = ctx.extra_data.toMap(); const int id = receipt.value("id", -1).toInt();
            ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (from && room->getCardOwner(id) == from && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip || room->getCardPlace(id) == Player::PlaceDelayedTrick))
                room->obtainCard(target, id, false);
            return false;
        }
        const QString zones = ctx.extra_data.toBool() ? "hej" : "h";
        room->doGongxin(ctx.invoker, target, {}, objectName());
        QList<int> available; for (const Card *card : target->getCards(zones)) available << card->getEffectiveId();
        QList<int> selected;
        for (int i = 0; i < 2 * getEffectiveAmount(ctx) && !available.isEmpty(); ++i) {
            room->fillAG(available, ctx.invoker); auto clear = qScopeGuard([&] { room->clearAG(ctx.invoker); });
            const int id = room->askForAG(ctx.invoker, available, true, objectName());
            if (!available.removeOne(id)) break; selected << id;
        }
        if (!target->isAlive()) return false;
        if (room->askForChoice(target, objectName(), "mobilemouwansha1+mobilemouwansha2", QVariant::fromValue(ctx.invoker)) == "mobilemouwansha1") {
            for (int id : selected) {
                if (!ctx.invoker->isAlive() || room->getCardOwner(id) != target) continue;
                const QList<ServerPlayer *> recipients = room->getOtherPlayers(target); if (recipients.isEmpty()) break;
                ServerPlayer *recipient = room->askForPlayerChosen(ctx.invoker, recipients, objectName());
                ctx.extra_data = QVariantMap{{"id", id}, {"from", target->objectName()}}; ctx.choice = "give";
                if (recipient) skillEffect(event, room, actor, ctx, recipient);
            }
        } else {
            DummyCard cards;
            for (const Card *card : target->getCards(zones)) if (!selected.contains(card->getEffectiveId()) && target->canDiscard(target, card->getEffectiveId())) cards.addSubcard(card);
            if (cards.subcardsLength() > 0) room->throwCard(&cards, objectName(), target);
        }
        return false;
    }
};

class MobileMouLuanwu : public ViewAsSkillV2
{
public:
    MobileMouLuanwu() : ViewAsSkillV2("mobilemouluanwu", 0) { frequency = Limited; limit_mark = "@chaos"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouLuanwuCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker) return FinishSkill;
        Room *room = ctx.invoker->getRoom(); room->setPlayerMark(ctx.invoker, "@chaos", 0);
        const QList<ServerPlayer *> targets = room->getOtherPlayers(ctx.invoker);
        ctx.choice = "chaos"; for (ServerPlayer *target : targets) if (target->isAlive()) skillEffect(ctx, target);
        ctx.choice = "upgrade"; if (ctx.invoker->isAlive()) skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (!target || !target->isAlive()) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "upgrade") {
            QStringList choices;
            for (const QString &name : {QString("mobilemouwansha"), QString("mobilemouweimu")})
                for (int id : target->getValidSkillInstanceIds(name))
                    if (!target->getSkillInstanceStateValue(name, id, "upgraded").toBool()) choices << SkillInstanceUtils::formatName(name, id);
            if (choices.isEmpty()) return ContinueEffects;
            const QString selected = room->askForChoice(target, objectName(), choices.join("+"));
            if (!choices.contains(selected)) return ContinueEffects;
            for (const QString &name : {QString("mobilemouwansha"), QString("mobilemouweimu")})
                for (int id : target->getSkillInstanceIds(name)) if (SkillInstanceUtils::formatName(name, id) == selected) {
                    target->setSkillInstanceStateValue(name, id, "upgraded", true); room->changeTranslation(target, name, 1, id);
                }
            return ContinueEffects;
        }
        QList<ServerPlayer *> nearest; int distance = 100000;
        for (ServerPlayer *other : room->getOtherPlayers(target)) {
            const int current = target->distanceTo(other);
            if (current < distance) { distance = current; nearest.clear(); }
            if (current == distance) nearest << other;
        }
        if (!room->askForUseSlashTo(target, nearest, "@luanwu-slash", false)) room->loseHp(target, getEffectiveAmount(ctx));
        return ContinueEffects;
    }
};

MobileMouLuanwuCard::MobileMouLuanwuCard() { setSkillName("mobilemouluanwu"); target_fixed = true; }
void MobileMouLuanwuCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const {}

class MobileMouWeimu : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouWeimu() : TriggerSkillV2("mobilemouweimu") { events << TargetConfirming << RoundStart; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event != RoundStart) return false;
        const QVariant round = room->historyScopes().value("round_id");
        if (round.toLongLong() > 0 && room->getTag("MobileMouWeimuRound") != round) {
            room->setTag("MobileMouWeimuPreviousRound", room->getTag("MobileMouWeimuRound")); room->setTag("MobileMouWeimuRound", round);
        }
        return false;
    }
    static int targeted(Room *room, ServerPlayer *owner)
    {
        const QVariant round = room->getTag("MobileMouWeimuPreviousRound"); if (round.toLongLong() <= 0) return -1;
        QVariantMap filter{{"kind", "use_card_targets"}, {"round_id", round}, {"limit", 100}}; int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter); if (!page.value("complete").toBool() || page.contains("error")) return -1;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap data = value.toMap().value("data").toMap();
                if (data.value("from").toString() != owner->objectName() && data.value("card").toMap().value("type").toInt() != Card::TypeSkill
                    && data.value("targets").toStringList().contains(owner->objectName())) ++count;
            }
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after"); filter["watermark"] = page.value("watermark");
        }
        return count;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result; if (!actor || !actor->isAlive()) return result;
        if (event == TargetConfirming) { const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && use.card->isKindOf("TrickCard") && use.card->isBlack() && use.to.contains(actor) && actor->hasSkill(objectName())) result[actor] << objectName();
        } else {
            const int count = targeted(room, actor); if (count < 0 || count > 1) return result;
            for (int id : actor->getValidSkillInstanceIds(objectName())) if (actor->getSkillInstanceStateValue(objectName(), id, "upgraded").toBool()) result[actor] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return ctx.invoker != nullptr; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (!target || !target->isAlive()) return false;
        if (event == TargetConfirming) { CardUseStruct use = ctx.original_data->value<CardUseStruct>(); use.to.removeAll(target); *ctx.original_data = QVariant::fromValue(use); }
        else {
            QList<int> ids = room->getDiscardPile(); qsanShuffle(ids); int count = getEffectiveAmount(ctx);
            for (int id : ids) { const Card *card = Sanguosha->getCard(id);
                if (count <= 0 || !target->isAlive()) break;
                if (room->getCardPlace(id) == Player::DiscardPile && (card->isKindOf("Armor") || (card->isKindOf("TrickCard") && card->isBlack()))) { room->obtainCard(target, id); --count; }
            }
        }
        return false;
    }
};

MobileMouQuhuCard::MobileMouQuhuCard()
{
    setSkillName("mobilemouquhu");
	will_throw = false;
}

bool MobileMouQuhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length()<2&&to_select!=Self&&to_select->getCardCount()>0;
}

bool MobileMouQuhuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length()==2;
}

void MobileMouQuhuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	QHash<ServerPlayer *,QList<int> > p2ids;
	foreach (ServerPlayer *p, targets) {
		const Card*dc = room->askForExchange(p,"mobilemouquhu",p->getCardCount(),1,true,"mobilemouquhu0");
		if(dc) p2ids[p] = dc->getSubcards();
	}
	source->addToPile("mobilemouquhu",subcards,false);
	bool has = true;
	foreach (ServerPlayer *p, targets) {
		p->addToPile("mobilemouquhu",p2ids[p],false);
		if(p2ids[p].length()<=subcardsLength())
			has = false;
	}
	ServerPlayer *tp = nullptr;
	foreach (ServerPlayer *p, targets) {
		if(!tp||p2ids[p].length()>p2ids[tp].length())
			tp = p;
	}
	if(has){
		if(tp->isAlive())
			tp->obtainCard(this,false);
		foreach (ServerPlayer *p, targets) {
			if(p->isAlive()){
				Card*dc = new DummyCard(p2ids[p]);
				room->obtainCard(p,dc,false);
				dc->deleteLater();
			}
		}
	}else{
		foreach (ServerPlayer *p, targets) {
			if(p!=tp)
				room->damage(DamageStruct("mobilemouquhu",tp,p));
		}
		if(tp->isAlive())
			tp->obtainCard(this,false);
		foreach (ServerPlayer *p, targets) {
			if(p->isAlive())
				room->throwCard(p2ids[p],"mobilemouquhu",p);
		}
	}
}

class MobileMouQuhu : public ViewAsSkillV2
{
public:
    MobileMouQuhu() : ViewAsSkillV2("mobilemouquhu", 999) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouQuhuCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isNude(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && card && !card->hasFlag("using") && !request.selectedCardIds.contains(card->getEffectiveId()) && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId())); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { if (request.selectedCardIds.isEmpty()) return false; ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear(); for (int id : request.selectedCardIds) { if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false; prefix.selectedCardIds << id; } return true; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && target && target != request.initiator && !target->isNude() && selected.size() < 2 && !selected.contains(target); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 2; }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.initiator || ctx.targets.size() != 2) return FinishSkill;
        Room *room = ctx.invoker->getRoom(); const QList<ServerPlayer *> opponents = ctx.targets;
        ctx.extra_data = QVariantMap{{"placed", QVariantMap()}};
        const auto cleanup = qScopeGuard([&] {
            const QVariantMap placed = ctx.extra_data.toMap().value("placed").toMap();
            for (auto it = placed.constBegin(); it != placed.constEnd(); ++it) {
                ServerPlayer *holder = room->findPlayerByObjectName(it.key(), true); if (!holder) continue;
                const QVariantMap receipt = it.value().toMap(); QList<int> pending;
                for (int id : ListV2I(receipt.value("ids").toList())) if (holder->getPile("mobilemouquhu").contains(id)) pending << id;
                if (pending.isEmpty()) continue;
                ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("return_to").toString(), true); DummyCard cards(pending);
                if (owner && owner->isAlive()) room->obtainCard(owner, &cards, false);
                else room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, QString(), objectName(), QString()), nullptr);
            }
        });
        ctx.choice = "place"; skillEffect(ctx, ctx.invoker);
        for (ServerPlayer *target : opponents) if (target->isAlive()) skillEffect(ctx, target);
        QVariantMap state = ctx.extra_data.toMap(); const QVariantMap placed = state.value("placed").toMap();
        if (!placed.contains(ctx.invoker->objectName()) || !placed.contains(opponents.first()->objectName()) || !placed.contains(opponents.last()->objectName())) return FinishSkill;
        auto count = [&](ServerPlayer *p) { int n = 0; for (int id : ListV2I(placed.value(p->objectName()).toMap().value("ids").toList())) if (p->getPile("mobilemouquhu").contains(id)) ++n; return n; };
        const int own = count(ctx.invoker), first = count(opponents.first()), second = count(opponents.last());
        ServerPlayer *winner = first >= second ? opponents.first() : opponents.last();
        if (first == second) {
            const int seats = room->getAllPlayers(true).size();
            const int left = (ctx.invoker->getSeat() - opponents.first()->getSeat() + seats) % seats;
            const int right = (ctx.invoker->getSeat() - opponents.last()->getSeat() + seats) % seats;
            winner = left <= right ? opponents.first() : opponents.last();
        }
        ServerPlayer *loser = opponents.first() == winner ? opponents.last() : opponents.first();
        state["winner"] = winner->objectName(); state["actor"] = ctx.invoker->objectName(); ctx.extra_data = state;
        const bool restore = own < first && own < second;
        if (!restore && loser->isAlive()) { ctx.choice = "damage"; skillEffect(ctx, loser); }
        if (winner->isAlive()) { ctx.choice = "gift"; skillEffect(ctx, winner); }
        ctx.choice = restore ? "return" : "discard";
        for (ServerPlayer *target : opponents) if (target->isAlive()) skillEffect(ctx, target);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (!target || !target->isAlive()) return ContinueEffects;
        Room *room = target->getRoom(); QVariantMap state = ctx.extra_data.toMap(); QVariantMap placed = state.value("placed").toMap();
        if (ctx.choice == "place") {
            QList<int> ids; ServerPlayer *owner = target;
            if (target == ctx.invoker) { ids = ctx.use_card->getSubcards(); owner = ctx.initiator; }
            else { const Card *chosen = room->askForExchange(target, objectName(), target->getCardCount(), 1, true, "mobilemouquhu0"); if (chosen) ids = chosen->getSubcards(); }
            if (ids.isEmpty()) return ContinueEffects;
            for (int id : ids) if (room->getCardOwner(id) != owner || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
            placed[target->objectName()] = QVariantMap{{"ids", ListI2V(ids)}, {"return_to", owner->objectName()}};
            state["placed"] = placed; ctx.extra_data = state; target->addToPile("mobilemouquhu", ids, false); return ContinueEffects;
        }
        if (ctx.choice == "damage") { ServerPlayer *winner = room->findPlayerByObjectName(state.value("winner").toString()); room->damage(DamageStruct(objectName(), winner && winner->isAlive() ? winner : nullptr, target, getEffectiveAmount(ctx))); return ContinueEffects; }
        ServerPlayer *holder = ctx.choice == "gift" ? room->findPlayerByObjectName(state.value("actor").toString(), true) : target;
        if (!holder) return ContinueEffects;
        QList<int> ids; for (int id : ListV2I(placed.value(holder->objectName()).toMap().value("ids").toList())) if (holder->getPile("mobilemouquhu").contains(id)) ids << id;
        if (ids.isEmpty()) return ContinueEffects; DummyCard cards(ids);
        if (ctx.choice == "discard") room->throwCard(&cards, objectName(), holder);
        else room->obtainCard(target, &cards, false);
        return ContinueEffects;
    }
};

class MobileMouJieming : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

	MobileMouJieming() : TriggerSkillV2("mobilemoujieming")
	{
		events << Damaged;
	}
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(),
            "mobilemoujieming0", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.targets.isEmpty()) { ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(), "mobilemoujieming0", false); if (target) ctx.targets << target; }
        const QList<ServerPlayer *> targets = ctx.targets; for (ServerPlayer *target : targets) if (target && target->isAlive()) skillEffect(event, room, actor, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "hp") { room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker)); return false; }
        ctx.owner->peiyin(this);
        target->drawCards(4 * getEffectiveAmount(ctx), objectName());
        if (ctx.owner->isDead()) return false;
        const int required = qMax(1, ctx.owner->getLostHp());
        const Card *discarded = nullptr;
        if (target->isAlive()) {
            const QVariant previous = target->getTag("mobilemoujieming_target");
            const auto restore = qScopeGuard([&] { target->setTag("mobilemoujieming_target", previous); });
            target->setTag("mobilemoujieming_target", QVariant::fromValue(ctx.owner));
            discarded = room->askForDiscard(target, objectName(), target->getCardCount(), 1, true, true,
                "mobilemoujieming1:" + ctx.owner->objectName() + ":" + QString::number(required));
        }
        // The owner's HP loss is a consequence of this accepted recipient's choice.
        if ((!discarded || discarded->subcardsLength() < required) && ctx.owner->isAlive())
            { ctx.choice = "hp"; skillEffect(event, room, actor, ctx, ctx.owner); ctx.choice.clear(); }
        return false;
    }
};


MobileMouZhiPackage::MobileMouZhiPackage()
	: Package("mobilemouzhi")
{
	General *mobilemou_sunquan = new General(this, "mobilemou_sunquan$", "wu", 4);
	mobilemou_sunquan->addSkill(new MobileMouZhiheng);
	mobilemou_sunquan->addSkill(new MobileMouTongye);
	mobilemou_sunquan->addSkill(new MobileMouTongyeEquip);
	mobilemou_sunquan->addSkill(new MobileMouJiuyuan);
	related_skills.insert("mobilemoutongye", "#mobilemoutongye");

	addMetaObject<MobileMouZhihengCard>();

	General *mobilemou_zhangjiao = new General(this, "mobilemou_zhangjiao$", "qun", 3);
	mobilemou_zhangjiao->addSkill(new MobileMouLeiji);
	mobilemou_zhangjiao->addSkill(new MobileMouGuidao);
	mobilemou_zhangjiao->addSkill(new MobileMouHuangtian);

	addMetaObject<MobileMouLeijiCard>();

	General *mobilemou_liubiao = new General(this, "mobilemou_liubiao", "qun", 3);
	mobilemou_liubiao->addSkill(new MobileMouZishou);
	mobilemou_liubiao->addSkill(new MobileMouZongshi);

	General *mobilemou_jiaxu = new General(this, "mobilemou_jiaxu", "qun", 3);
	mobilemou_jiaxu->addSkill(new MobileMouWansha);
	mobilemou_jiaxu->addSkill(new MobileMouLuanwu);
	mobilemou_jiaxu->addSkill(new MobileMouWeimu);
	addMetaObject<MobileMouLuanwuCard>();
	
	General *mobilemou_xunyu = new General(this, "mobilemou_xunyu", "wei", 3);
	mobilemou_xunyu->addSkill(new MobileMouQuhu);
	mobilemou_xunyu->addSkill(new MobileMouJieming);
	addMetaObject<MobileMouQuhuCard>();

}
ADD_PACKAGE(MobileMouZhi)


MobileMouDuanliangCard::MobileMouDuanliangCard()
{
    setSkillName("mobilemouduanliang");
}

void MobileMouDuanliangCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();

	QString str = "=" + to->objectName();
	QString choice1 = room->askForChoice(from, "mobilemouduanliang", "weicheng" + str + "+leigu" + str, QVariant::fromValue(to));
	str = "=" + from->objectName();
	QString choice2 = room->askForChoice(to, "mobilemouduanliang", "weicheng2" + str + "+leigu2" + str, QVariant::fromValue(from));

	choice1 = choice1.split("=").first();
	choice2 = choice2.split("=").first();
	if (choice2.startsWith(choice1)){
		from->peiyin("mobilemouduanliang",4);
		return;
	}
	from->peiyin("mobilemouduanliang",3);

	if (choice1 == "weicheng") {
		if (to->containsTrick("supply_shortage")) {
			if (to->isNude()) return;
			int card_id = room->askForCardChosen(from, to, "he", "mobilemouduanliang");
			room->obtainCard(from, card_id, false);
		} else {
			SupplyShortage *su = new SupplyShortage(Card::NoSuit, 0);
			su->setSkillName("_mobilemouduanliang");
			su->deleteLater();
			if (from->isProhibited(to,su)) return;
			su->addSubcard(room->drawCard());
			room->useCard(CardUseStruct(su, from, to), true);
		}
	} else {
		Duel *duel = new Duel(Card::NoSuit, 0);
		duel->setSkillName("_mobilemouduanliang");
		duel->deleteLater();
		if (from->canUse(duel, to, true))
			room->useCard(CardUseStruct(duel, from, to), true);
	}
}

class MobileMouDuanliang : public ViewAsSkillV2
{
public:
	MobileMouDuanliang() : ViewAsSkillV2("mobilemouduanliang")
	{
		setPhaseName("Play");
	}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouDuanliangCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target != request.initiator && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "obtain") {
            const QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = target->getRoom()->findPlayerByObjectName(receipt.value("from").toString()); const int id = receipt.value("id", -1).toInt();
            if (from && target->getRoom()->getCardOwner(id) == from && (target->getRoom()->getCardPlace(id) == Player::PlaceHand || target->getRoom()->getCardPlace(id) == Player::PlaceEquip)) target->getRoom()->obtainCard(target, id, false);
            return ContinueEffects;
        }
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        ServerPlayer *owner = ctx.invoker;
        Room *room = owner->getRoom();
        const QString first = room->askForChoice(owner, objectName(),
            "weicheng=" + target->objectName() + "+leigu=" + target->objectName(), QVariant::fromValue(target)).section('=', 0, 0);
        const QString second = room->askForChoice(target, objectName(),
            "weicheng2=" + owner->objectName() + "+leigu2=" + owner->objectName(), QVariant::fromValue(owner)).section('=', 0, 0);
        if (second.startsWith(first)) {
            owner->peiyin(objectName(), 4);
            return ContinueEffects;
        }
        owner->peiyin(objectName(), 3);
        if (owner->isDead() || target->isDead()) return ContinueEffects;
        if (first == "weicheng" && target->containsTrick("supply_shortage")) {
            for (int i = 0; i < getEffectiveAmount(ctx) && owner->isAlive() && target->isAlive() && !target->isNude(); ++i) {
                const int id = room->askForCardChosen(owner, target, "he", objectName());
                if (id < 0 || room->getCardOwner(id) != target
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) break;
                ctx.choice = "obtain"; ctx.extra_data = QVariantMap{{"from", target->objectName()}, {"id", id}}; skillEffect(ctx, owner); ctx.choice.clear();
            }
        } else for (int i = 0, count = getEffectiveAmount(ctx); i < count && owner->isAlive() && target->isAlive(); ++i) {
            Card *card = first == "weicheng" ? static_cast<Card *>(new SupplyShortage(Card::NoSuit, 0))
                                              : static_cast<Card *>(new Duel(Card::NoSuit, 0));
            CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card));
            card->deleteLater();
            card->setSkillName("_mobilemouduanliang");
            int drawn = -1;
            const auto restore = qScopeGuard([&] { if (drawn >= 0 && room->getCardPlace(drawn) == Player::DrawPile && !room->getDrawPile().contains(drawn)) room->returnToTopDrawPile({drawn}); });
            if (first == "weicheng") {
                if (owner->isProhibited(target, card)) return ContinueEffects;
                drawn = room->drawCard(); if (drawn < 0) return ContinueEffects; card->addSubcard(drawn);
            } else if (!owner->canUse(card, target, true)) return ContinueEffects;
            // Accepted skill effects create ordinary cards with the original activation provenance.
            CardUseStruct use(card, owner, target);
            use.sourceRef = ctx.sourceRef;
            use.activationRef = ctx.activationRef;
            room->useCardFromSkillEffect(use, ctx, true);
        }
        return ContinueEffects;
    }
	int getEffectIndex(const ServerPlayer *, const Card *) const
	{
		return qsanRandomBounded(2)+1;
	}
};

MobileMouShipoCard::MobileMouShipoCard()
{
    setSkillName("mobilemoushipo");
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void MobileMouShipoCard::onUse(Room *room, CardUseStruct &use) const
{
	ServerPlayer *player = use.from, *to = use.to.first();
	room->setPlayerProperty(player, "mobilemoushipo_card_ids", "");
	room->giveCard(player, to, this, "mobilemoushipo");
}

class MobileMouShipoVS : public ViewAsSkillV2
{
public:
    MobileMouShipoVS() : ViewAsSkillV2("mobilemoushipo", 999) { response_pattern = "@@mobilemoushipo"; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@mobilemoushipo"; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.initiator->handCards().contains(card->getEffectiveId())
            && request.initiator->property("mobilemoushipo_card_ids").toString().split("+").contains(QString::number(card->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { if (request.selectedCardIds.isEmpty()) return false; ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) { if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false; prefix.selectedCardIds << id; } return true; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && target && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator || !target || !target->isAlive()) return ContinueEffects;
        QList<int> ids; const QStringList allowed = ctx.initiator->property("mobilemoushipo_card_ids").toString().split("+");
        for (int id : ctx.use_card->getSubcards()) if (ctx.initiator->handCards().contains(id) && allowed.contains(QString::number(id))) ids << id;
        if (!ids.isEmpty()) { DummyCard cards(ids); target->getRoom()->giveCard(ctx.initiator, target, &cards, objectName()); }
        return ContinueEffects;
    }
};

class MobileMouShipo : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouShipo() : TriggerSkillV2("mobilemoushipo") { events << EventPhaseStart; view_as_skill = new MobileMouShipoVS; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->getPhase() == Player::Finish && actor->hasSkill(objectName()) ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> hp, judge;
        for (ServerPlayer *p : room->getOtherPlayers(ctx.owner)) { if (p->getHp() < ctx.owner->getHp()) hp << p; if (p->containsTrick("supply_shortage")) judge << p; }
        QStringList choices; if (!hp.isEmpty()) choices << "hp"; if (!judge.isEmpty()) choices << "judge";
        if (choices.isEmpty()) return false; choices << "cancel";
        const QString choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
        if (choice == "hp") { ServerPlayer *target = room->askForPlayerChosen(ctx.owner, hp, objectName(), "@mobilemoushipo-target"); if (target) ctx.targets << target; }
        else if (choice == "judge") ctx.targets = judge;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.targets.isEmpty() && !cost(event, room, actor, ctx)) return false;
        ctx.extra_data = QVariantMap{{"cards", QVariantList()}};
        const QList<ServerPlayer *> targets = ctx.targets;
        for (ServerPlayer *target : targets) if (target->isAlive()) { ctx.choice = "demand"; skillEffect(event, room, actor, ctx, target); }
        if (!ctx.invoker || !ctx.invoker->isAlive()) return false;
        QList<int> ids; for (const QVariant &value : ctx.extra_data.toMap().value("cards").toList()) if (ctx.invoker->handCards().contains(value.toInt())) ids << value.toInt();
        if (ids.isEmpty()) return false;
        const QVariant previous = ctx.invoker->property("mobilemoushipo_card_ids");
        auto clear = qScopeGuard([&] { ctx.invoker->setProperty("mobilemoushipo_card_ids", previous); room->notifyProperty(ctx.invoker, ctx.invoker, "mobilemoushipo_card_ids"); });
        ctx.invoker->setProperty("mobilemoushipo_card_ids", ListI2S(ids).join("+")); room->notifyProperty(ctx.invoker, ctx.invoker, "mobilemoushipo_card_ids");
        Room::AcceptedViewAsEffectScope prompt(room, ctx.invoker, objectName(), ctx);
        if (prompt.isValid()) room->askForUseCard(ctx.invoker, "@@mobilemoushipo", "@mobilemoushipo", -1, Card::MethodNone);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        QVariantMap state = ctx.extra_data.toMap();
        if (ctx.choice == "obtain") {
            ServerPlayer *from = room->findPlayerByObjectName(state.value("from").toString()); const int id = state.value("id", -1).toInt();
            if (from && from->handCards().contains(id)) { room->giveCard(from, target, Sanguosha->getCard(id), objectName());
                if (target->handCards().contains(id)) { QVariantList ids = state.value("cards").toList(); ids << id; state["cards"] = ids; ctx.extra_data = state; } }
            return false;
        }
        const Card *gift = ctx.invoker && ctx.invoker->isAlive() ? room->askForExchange(target, objectName(), 1, 1, false, "@mobilemoushipo-give:" + ctx.invoker->objectName(), true) : nullptr;
        if (gift && gift->subcardsLength() == 1 && target->handCards().contains(gift->getSubcards().first())) {
            state["id"] = gift->getSubcards().first(); state["from"] = target->objectName(); ctx.extra_data = state; ctx.choice = "obtain";
            skillEffect(event, room, actor, ctx, ctx.invoker);
        } else room->damage(DamageStruct(objectName(), nullptr, target, getEffectiveAmount(ctx)));
        return false;
    }
};

class MobileMouTieqi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouTieqi() : TriggerSkillV2("mobilemoutieqi") { events << TargetSpecifying; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &data) const override
    { const CardUseStruct use = data.value<CardUseStruct>(); return actor && actor->isAlive() && actor->hasSkill(objectName()) && use.card && use.card->isKindOf("Slash") && !use.to.isEmpty() ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        for (ServerPlayer *target : ctx.original_data->value<CardUseStruct>().to) if (target->isAlive() && ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) ctx.targets << target;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.bypass_cost && ctx.targets.isEmpty()) ctx.targets = ctx.original_data->value<CardUseStruct>().to;
        const QList<ServerPlayer *> targets = ctx.targets;
        for (ServerPlayer *target : targets) if (target && target->isAlive()) skillEffect(event, room, actor, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (!ctx.invoker || !ctx.invoker->isAlive() || !target || !target->isAlive()) return false;
        if (ctx.choice == "draw") { target->drawCards(2 * getEffectiveAmount(ctx), objectName()); return false; }
        if (ctx.choice == "obtain") {
            const QVariantMap receipt = ctx.extra_data.toMap(); const int id = receipt.value("id", -1).toInt(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (from && room->getCardOwner(id) == from && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) room->obtainCard(target, id, room->getCardPlace(id) != Player::PlaceHand);
            return false;
        }
        QVariantList receipts = target->getTag("MobileMouTieqiEffects").toList();
        receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        target->setTag("MobileMouTieqiEffects", receipts); room->setPlayerMark(target, "mobilemoutieqi", receipts.size()); room->addPlayerMark(target, "@skill_invalidity");
        room->filterCards(target, target->getCards("he"), true);
        CardUseStruct use = ctx.original_data->value<CardUseStruct>(); if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName(); *ctx.original_data = QVariant::fromValue(use);
        const QString mine = room->askForChoice(ctx.invoker, objectName(), "zhiqu=" + target->objectName() + "+raozhen=" + target->objectName(), QVariant::fromValue(target)).section('=', 0, 0);
        const QString theirs = room->askForChoice(target, objectName(), "zhiqu2=" + ctx.invoker->objectName() + "+raozhen2=" + ctx.invoker->objectName(), QVariant::fromValue(ctx.invoker)).section('=', 0, 0);
        if (theirs.startsWith(mine) || !target->isAlive() || !ctx.invoker->isAlive()) return false;
        if (mine == "zhiqu" && !target->isNude()) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName());
            ctx.extra_data = QVariantMap{{"id", id}, {"from", target->objectName()}}; ctx.choice = "obtain";
            skillEffect(event, room, actor, ctx, ctx.invoker);
        } else if (mine == "raozhen") { ctx.choice = "draw"; skillEffect(event, room, actor, ctx, ctx.invoker); }
        ctx.choice.clear(); return false;
    }
};

class MobileMouTieqiClear : public TriggerSkillV2
{
public:
    MobileMouTieqiClear() : TriggerSkillV2("#mobilemoutieqi") { events << EventPhaseChanging << Death; global = true; }
    int getPriority(TriggerEvent) const override { return 5; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventPhaseChanging ? data.value<PhaseChangeStruct>().to != Player::NotActive
            : !actor || data.value<DeathStruct>().who != actor || actor != room->getCurrent()) return false;
        for (ServerPlayer *target : room->getAllPlayers()) {
            const int count = target->getTag("MobileMouTieqiEffects").toList().size(); if (count <= 0) continue;
            target->removeTag("MobileMouTieqiEffects"); room->setPlayerMark(target, "mobilemoutieqi", 0); room->removePlayerMark(target, "@skill_invalidity", count);
            room->filterCards(target, target->getCards("he"), false);
        }
        JsonArray args; args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL; room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        return false;
    }
};

MobileMouXingshangCard::MobileMouXingshangCard()
{
    setSkillName("mobilemouxingshang");
}

bool MobileMouXingshangCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void MobileMouXingshangCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QStringList choices,tos;
	foreach (ServerPlayer *p, room->getPlayers()) {
		if(p->isDead()){
			if(!p->property("ZhuisiPlayer").toBool())
				tos << "ZhuisiPlayer="+p->objectName();
		}
	}
	if(effect.from->getMark("&mobilemouxingshang_song")>1)
		choices << "mobilemouxingshang0=2";
	if(effect.to->getMaxHp()<=9&&effect.from->getMark("&mobilemouxingshang_song")>2)
		choices << "mobilemouxingshang0=3";
	if(effect.from->getMark("&mobilemouxingshang_song")>3&&tos.length()>0
	&&effect.from==effect.to&&effect.from->hasSkill("mobilemouxingshang",true))
		choices << "mobilemouxingshang0=4";
	QString choice = room->askForChoice(effect.from,"mobilemouxingshang",choices.join("+"),QVariant::fromValue(effect.to));
	if(choice.contains("=2")){
		effect.from->loseMark("&mobilemouxingshang_song",2);
		effect.to->drawCards(3,"mobilemouxingshang");
		if(!effect.to->faceUp()) effect.to->turnOver();
		if(effect.to->isChained()) room->setPlayerChained(effect.to);
	}else if(choice.contains("=3")){
		effect.from->loseMark("&mobilemouxingshang_song",3);
		room->recover(effect.to,RecoverStruct("mobilemouxingshang",effect.from));
		room->gainMaxHp(effect.to,1,"mobilemouxingshang");
		QList<int>ids;
		for (int i = 0; i < 4; i++) {
			if(!effect.to->hasEquipArea(i))
				ids << i;
		}
		if(ids.length()>0){
			qsanShuffle(ids);
			effect.to->obtainEquipArea(ids.first());
		}
	}else{
		effect.from->loseMark("&mobilemouxingshang_song",4);
		choice = room->askForChoice(effect.from,"mobilemouxingshangZhuisi",tos.join("+"));
		ServerPlayer *t = room->findChild<ServerPlayer *>(choice.split("=").last());
		room->setPlayerProperty(t,"ZhuisiPlayer",true);
		QStringList sks;
		foreach (const Skill *s, t->getGeneral()->getVisibleSkillList())
			sks << s->objectName();
		if(t->getGeneral2()){
			foreach (const Skill *s, t->getGeneral2()->getVisibleSkillList())
				sks << s->objectName();
		}
		sks << "-mobilemouxingshang";
		sks << "-mobilemoufangzhu";
		sks << "-mobilemousongwei";
		room->handleAcquireDetachSkills(effect.to,sks);
	}
}

class MobileMouXingshangvs : public ViewAsSkillV2
{
public:
    MobileMouXingshangvs() : ViewAsSkillV2("mobilemouxingshang") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouXingshangCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getMark("&mobilemouxingshang_song") >= 2; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &targets, const Player *target) const override { return target && target->isAlive() && targets.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    bool choose(Room *room, SkillContext &ctx) const
    {
        if (!ctx.initiator || ctx.targets.size() != 1) return false; ServerPlayer *target = ctx.targets.first(); const int song = ctx.initiator->getMark("&mobilemouxingshang_song");
        QStringList choices, dead;
        for (ServerPlayer *p : room->getAllPlayers(true)) if (p->isDead() && !p->property("ZhuisiPlayer").toBool()) dead << "ZhuisiPlayer=" + p->objectName();
        // Preserve this package's C++ rules (2/3/4); the localization describes a different revision.
        if (song >= 2) choices << "mobilemouxingshang0=2";
        if (song >= 3 && target->getMaxHp() <= 9) choices << "mobilemouxingshang0=3";
        if (song >= 4 && target == ctx.initiator && ctx.initiator->hasSkill(objectName(), true) && !dead.isEmpty()) choices << "mobilemouxingshang0=4";
        if (choices.isEmpty()) return false;
        const QString choice = room->askForChoice(ctx.initiator, objectName(), choices.join("+"), QVariant::fromValue(target)); if (!choices.contains(choice)) return false;
        ctx.choice = choice.section('=', 1); ctx.extra_data = QVariantMap();
        if (ctx.choice == "4") {
            const QString selected = room->askForChoice(ctx.initiator, "mobilemouxingshangZhuisi", dead.join("+")); if (!dead.contains(selected)) return false;
            QVariantMap receipt{{"dead", selected.section('=', 1)}}; QVariantList detach;
            for (const SkillInstance &instance : ctx.initiator->getSkillInstances()) if (QStringList{"mobilemouxingshang", "mobilemoufangzhu", "mobilemousongwei"}.contains(instance.skillName)) detach << QVariantMap{{"skill", instance.skillName}, {"instance", instance.instanceID}};
            receipt["detach"] = detach; ctx.extra_data = receipt;
        }
        return true;
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override { return choose(room, ctx); }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    { const int count = ctx.choice.toInt(); if (!ctx.initiator || count < 2 || count > 4 || ctx.initiator->getMark("&mobilemouxingshang_song") < count) return false; ctx.initiator->loseMark("&mobilemouxingshang_song", count); return true; }
    EffectFlow effect(SkillContext &ctx) const override { if (ctx.choice.isEmpty() && !choose(ctx.invoker->getRoom(), ctx)) return FinishSkill; return ContinueEffects; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx); if (amount <= 0) return ContinueEffects;
        if (ctx.choice == "2") { target->drawCards(3 * amount, objectName()); if (target->isAlive() && !target->faceUp()) target->turnOver(); if (target->isAlive() && target->isChained()) room->setPlayerChained(target); }
        else if (ctx.choice == "3") {
            room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount)); if (target->isDead()) return ContinueEffects;
            room->gainMaxHp(target, amount, objectName()); QList<int> slot_list; for (int i = 0; i < 4; ++i) if (!target->hasEquipArea(i)) slot_list << i;
            qsanShuffle(slot_list); for (int i = 0; i < amount && i < slot_list.size() && target->isAlive(); ++i) target->obtainEquipArea(slot_list.at(i));
        } else if (ctx.choice == "4") {
            const QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *dead = room->findPlayerByObjectName(receipt.value("dead").toString(), true);
            if (!dead || !dead->isDead() || dead->property("ZhuisiPlayer").toBool()) return ContinueEffects;
            room->setPlayerProperty(dead, "ZhuisiPlayer", true); QStringList skills;
            if (dead->getGeneral()) for (const Skill *skill : dead->getGeneral()->getVisibleSkillList()) if (!skills.contains(skill->objectName())) skills << skill->objectName();
            if (dead->getGeneral2()) for (const Skill *skill : dead->getGeneral2()->getVisibleSkillList()) if (!skills.contains(skill->objectName())) skills << skill->objectName();
            for (const QString &skill : skills) room->acquireSkillFromEffect(target, skill, ctx);
            for (const QVariant &value : receipt.value("detach").toList()) { const QVariantMap source = value.toMap();
                if (ctx.initiator->hasSkillInstance(source.value("skill").toString(), source.value("instance").toInt())) room->detachSkillFromPlayer(ctx.initiator, SkillInstanceUtils::formatName(source.value("skill").toString(), source.value("instance").toInt())); }
        }
        return ContinueEffects;
    }
};

class MobileMouXingshang : public TriggerSkillV2
{
public:
    MobileMouXingshang() : TriggerSkillV2("mobilemouxingshang") { events << Damaged << Death << EventSkillInvoking; global = true; frequency = Compulsory; view_as_skill = new MobileMouXingshangvs; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool isUsable(const SkillContext &ctx) const override { return ctx.choice == "death" || TriggerSkillV2::isUsable(ctx); }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; ctx.choice = ctx.current_event == Death ? "death" : "damage"; return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    { if (event == EventSkillInvoking) { const SkillContext accepted = data.value<SkillContext>(); if (accepted.bypass_cost && !accepted.use_card && accepted.choice == "damage" && accepted.skill_name == objectName() && accepted.activationRef.key.skillName == objectName()) addUsage(accepted); } return false; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        if (event == Death) { if (actor && actor->isAlive() && actor != data.value<DeathStruct>().who && actor->hasSkill(objectName()) && actor->getMark("&mobilemouxingshang_song") < 9) result[actor] << objectName(); }
        else if (event == Damaged && actor && actor->hasTurn()) for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName()) && owner->getMark("&mobilemouxingshang_song") < 9) result[owner] << objectName();
        return result;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { if (ctx.choice == "death") return true; if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override { ctx.manual_effect = true; return skillEffect(event, room, actor, ctx, ctx.invoker); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { const int count = qMin(2 * getEffectiveAmount(ctx), 9 - target->getMark("&mobilemouxingshang_song")); if (count > 0) target->gainMark("&mobilemouxingshang_song", count); return false; }
};

MobileMouFangzhuCard::MobileMouFangzhuCard()
{
    setSkillName("mobilemoufangzhu");
}

bool MobileMouFangzhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select!=Self;
}

void MobileMouFangzhuCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QStringList choices;
	if(effect.from->getMark("&mobilemouxingshang_song")>1)
		choices << "mobilemouxingshang0=2";
	if(effect.from->getMark("&mobilemouxingshang_song")>3)
		choices << "mobilemouxingshang0=4";
	if(effect.from->getMark("&mobilemouxingshang_song")>5)
		choices << "mobilemouxingshang0=6";
	if(effect.from->getMark("&mobilemouxingshang_song")>7)
		choices << "mobilemouxingshang0=8";
	QString choice = room->askForChoice(effect.from,"mobilemoufangzhu",choices.join("+"),QVariant::fromValue(effect.to));
	if(choice.contains("=2")){
		effect.from->loseMark("&mobilemouxingshang_song",2);
		room->setPlayerMark(effect.to,"&mobilemoufangzhu+2-SelfClear",1);
	}else if(choice.contains("=4")){
		effect.from->loseMark("&mobilemouxingshang_song",4);
		ServerPlayer *tp = room->askForPlayerChosen(effect.from,room->getOtherPlayers(effect.to),"mobilemoufangzhu","mobilemoufangzhu4:"+effect.to->objectName());
		if(tp) effect.to->addMark(tp->objectName()+"mobilemoufangzhu4-SelfClear");
		room->setPlayerMark(effect.to,"&mobilemoufangzhu+4-SelfClear",1);
    }else if(choice.contains("=6")){
		effect.from->loseMark("&mobilemouxingshang_song",6);
		room->setPlayerMark(effect.to,"&mobilemoufangzhu+6-SelfClear",1);
	}else{
		effect.from->loseMark("&mobilemouxingshang_song",8);
		effect.to->turnOver();
		room->setPlayerMark(effect.to,"&mobilemoufangzhu+8-SelfClear",1);
	}
}

class MobileMouFangzhuvs : public ViewAsSkillV2
{
public:
    MobileMouFangzhuvs() : ViewAsSkillV2("mobilemoufangzhu") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouFangzhuCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->hasSkill("mobilemouxingshang", true) && request.initiator->getMark("&mobilemouxingshang_song") >= 2; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && targets.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    bool choose(Room *room, SkillContext &ctx) const
    {
        if (!ctx.initiator || ctx.targets.size() != 1) return false;
        QStringList choices; for (int count : {2, 4, 6, 8}) if (ctx.initiator->getMark("&mobilemouxingshang_song") >= count) choices << "mobilemouxingshang0=" + QString::number(count);
        if (choices.isEmpty()) return false;
        const QString choice = room->askForChoice(ctx.initiator, objectName(), choices.join("+"), QVariant::fromValue(ctx.targets.first()));
        if (!choices.contains(choice)) return false; ctx.choice = choice.section('=', 1);
        if (ctx.choice == "4") {
            ServerPlayer *exception = room->askForPlayerChosen(ctx.initiator, room->getOtherPlayers(ctx.targets.first()), objectName(), "mobilemoufangzhu4:" + ctx.targets.first()->objectName());
            if (!exception) return false; ctx.extra_data = exception->objectName();
        }
        return true;
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override { return choose(room, ctx); }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    { const int count = ctx.choice.toInt(); if (!ctx.initiator || !QList<int>{2,4,6,8}.contains(count) || ctx.initiator->getMark("&mobilemouxingshang_song") < count) return false; ctx.initiator->loseMark("&mobilemouxingshang_song", count); return true; }
    EffectFlow effect(SkillContext &ctx) const override { if (ctx.choice.isEmpty() && !choose(ctx.invoker->getRoom(), ctx)) return FinishSkill; return ContinueEffects; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom(); const qint64 serial = room->getTag("MobileMouFangzhuSequence").toLongLong() + 1; room->setTag("MobileMouFangzhuSequence", serial);
        // These are the existing 2/4/6/8 C++ effects, retained until this recipient's turn ends.
        QVariantMap receipt{{"serial", serial}, {"mode", ctx.choice}, {"except", ctx.extra_data.toString()}, {"amount", getEffectiveAmount(ctx)},
            {"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        QVariantList receipts = target->getTag("MobileMouFangzhuEffects").toList(); receipts << receipt; target->setTag("MobileMouFangzhuEffects", receipts);
        room->setPlayerMark(target, "&mobilemoufangzhu+" + ctx.choice + "-SelfClear", 1);
        if (ctx.choice == "8") target->turnOver();
        return ContinueEffects;
    }
};

class MobileMouFangzhu : public TriggerSkillV2
{
public:
    MobileMouFangzhu() : TriggerSkillV2("mobilemoufangzhu") { events << CardUsed << EventPhaseChanging << Death; global = true; frequency = Compulsory; view_as_skill = new MobileMouFangzhuvs; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event != Death && (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)) return false;
        ServerPlayer *target = event == Death ? data.value<DeathStruct>().who : actor;
        if (target) { target->removeTag("MobileMouFangzhuEffects"); for (int count : {2,4,6,8}) room->setPlayerMark(target, "&mobilemoufangzhu+" + QString::number(count) + "-SelfClear", 0); }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardUsed) return true; const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.from) return true;
        for (ServerPlayer *target : room->getOtherPlayers(use.from)) for (const QVariant &value : target->getTag("MobileMouFangzhuEffects").toList()) {
            const QVariantMap receipt = value.toMap(); if (receipt.value("mode").toString() != "4" || receipt.value("except").toString() == use.from->objectName()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true); ctx.initiator = ctx.owner; ctx.invoker = target;
            ctx.instanceID = receipt.value("serial").toInt(); ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.extra_data = receipt; ctx.amount = receipt.value("amount").toInt(); ctx.current_event = event; ctx.original_data = &data; ctx.is_forced = true; ctx.targets << target; contexts << ctx;
        }
        Q_UNUSED(actor); return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override { return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getTag("MobileMouFangzhuEffects").toList().contains(ctx.extra_data); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { if (getEffectiveAmount(ctx) > 0) { CardUseStruct use = ctx.original_data->value<CardUseStruct>(); if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName(); *ctx.original_data = QVariant::fromValue(use); } return false; }
};

MobileMouSongweiCard::MobileMouSongweiCard()
{
    setSkillName("mobilemousongwei");
}

bool MobileMouSongweiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select!=Self&&to_select->getKingdom()=="wei";
}

void MobileMouSongweiCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->addPlayerMark(effect.from,"mobilemousongweiUse");
	QStringList sks;
	foreach (const Skill *s, effect.to->getVisibleSkillList()) {
		if(!s->isAttachedLordSkill())
			sks << "-"+s->objectName();
	}
	room->handleAcquireDetachSkills(effect.to,sks);
}

class MobileMouSongweivs : public ViewAsSkillV2
{
public:
    MobileMouSongweivs() : ViewAsSkillV2("mobilemousongwei") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouSongweiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->hasLordSkill(objectName()); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override { return target && target->isAlive() && target != request.initiator && target->getKingdom() == "wei" && targets.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        const QList<SkillInstance> instances = target->getSkillInstances();
        for (const SkillInstance &instance : instances) { const Skill *skill = Sanguosha->getSkill(instance.skillName);
            if (instance.visible && skill && !skill->isAttachedLordSkill() && target->hasSkillInstance(instance.skillName, instance.instanceID))
                target->getRoom()->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(instance.skillName, instance.instanceID)); }
        return ContinueEffects;
    }
};

class MobileMouSongwei : public TriggerSkillV2
{
public:
    MobileMouSongwei() : TriggerSkillV2("mobilemousongwei$") { events << EventPhaseStart; view_as_skill = new MobileMouSongweivs; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->getPhase() == Player::Play && actor->hasLordSkill(this) && actor->hasSkill("mobilemouxingshang", true) && actor->getMark("&mobilemouxingshang_song") < 9 ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override { ctx.manual_effect = true; return skillEffect(event, room, actor, ctx, ctx.invoker); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        int count = 0; for (ServerPlayer *p : room->getOtherPlayers(target, true)) if (p->getKingdom() == "wei") count += 2;
        count = qMin(count * getEffectiveAmount(ctx), 9 - target->getMark("&mobilemouxingshang_song")); if (count > 0) target->gainMark("&mobilemouxingshang_song", count); return false;
    }
};

class MobileMouQiaobian : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouQiaobian() : TriggerSkillV2("mobilemouqiaobian")
    { events << EventPhaseChanging << EventPhaseStart << EventSkillInvoking << EventSkillEffectFinished << Death; global = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool isUsable(const SkillContext &ctx) const override { return ctx.choice == "deferred" || TriggerSkillV2::isUsable(ctx); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == Death) {
            ServerPlayer *dead = data.value<DeathStruct>().who;
            if (dead) { dead->removeTag("MobileMouQiaobianReceipts"); room->setPlayerMark(dead, "&mobilemouqiaobian", 0); }
        } else if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.skill_name == objectName() && accepted.activationRef.key.skillName == objectName()) addUsage(accepted);
        } else if (event == EventSkillEffectFinished) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.skill_name == objectName() && accepted.choice == "deferred" && accepted.invoker) consume(room, accepted);
        }
        return false;
    }
    void consume(Room *room, const SkillContext &ctx) const
    {
        QVariantList receipts = ctx.invoker->getTag("MobileMouQiaobianReceipts").toList(); receipts.removeOne(ctx.extra_data);
        ctx.invoker->setTag("MobileMouQiaobianReceipts", receipts); room->setPlayerMark(ctx.invoker, "&mobilemouqiaobian", receipts.size());
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart) return false;
        if (!actor || actor->isDead() || actor->getPhase() != Player::Start) return true;
        for (const QVariant &value : actor->getTag("MobileMouQiaobianReceipts").toList()) {
            const QVariantMap receipt = value.toMap(); SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true); ctx.initiator = ctx.owner; ctx.invoker = actor;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.current_event = event; ctx.original_data = &data; ctx.extra_data = receipt; ctx.amount = receipt.value("amount").toInt();
            ctx.choice = "deferred"; ctx.is_forced = true; ctx.instanceID = receipt.value("serial").toInt(); ctx.trigger_count = 0; ctx.targets << actor; contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.choice == "deferred" ? ctx.invoker && ctx.invoker->isAlive()
            && ctx.invoker->getTag("MobileMouQiaobianReceipts").toList().contains(ctx.extra_data) : TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !actor || actor->isDead() || !actor->hasSkill(objectName())) return {};
        const Player::Phase phase = data.value<PhaseChangeStruct>().to;
        return phase > Player::Start && phase < Player::Discard && !actor->isSkipped(phase) ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "deferred") return true;
        const Player::Phase phase = ctx.original_data->value<PhaseChangeStruct>().to;
        if (!room->askForSkillInvoke(ctx.owner, objectName() + "$-1", QString("b%1").arg(phase))) return false;
        ctx.extra_data = int(phase); ctx.targets << ctx.invoker; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.choice == "deferred") return true; if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool canTransfer(Room *room, ServerPlayer *invoker, ServerPlayer *from, ServerPlayer *to, int id) const
    {
        const Card *card = Sanguosha->getCard(id);
        if (!invoker || invoker->isDead() || !from || !to || from == to || from->isDead() || to->isDead() || !card
            || room->getCardOwner(id) != from || !from->canMove(from, id) || !invoker->canMove(from, id) || invoker->isProhibited(to, card)) return false;
        if (room->getCardPlace(id) == Player::PlaceDelayedTrick) return to->hasJudgeArea() && !to->containsTrick(card->objectName());
        if (room->getCardPlace(id) != Player::PlaceEquip) return false;
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard()); if (!equip) return false;
        for (int slot : equip->getOccupyLocations()) if (!to->hasEquipArea(slot) || to->getEquip(slot)) return false;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.choice != "deferred") ctx.extra_data = int(ctx.original_data->value<PhaseChangeStruct>().to);
        return skillEffect(event, room, actor, ctx, ctx.invoker);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "deferred") {
            consume(room, ctx); target->drawCards(5 * getEffectiveAmount(ctx), objectName());
            if (target->isAlive()) room->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
            return false;
        }
        if (ctx.choice == "judge") {
            const QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (!from) return false;
            for (const QVariant &value : receipt.value("ids").toList()) {
                const int id = value.toInt(); const Card *card = Sanguosha->getCard(id);
                if (room->getCardOwner(id) != from || room->getCardPlace(id) != Player::PlaceDelayedTrick) continue;
                if (target->hasJudgeArea() && !target->containsTrick(card->objectName()) && !from->isProhibited(target, card)) room->moveCardTo(card, target, Player::PlaceDelayedTrick, true);
                else room->throwCard(card, objectName(), nullptr);
            }
            return false;
        }
        if (ctx.choice == "field_from") {
            QVariantMap receipt = ctx.extra_data.toMap(); receipt["allowed"] = target->objectName(); ctx.extra_data = receipt; return false;
        }
        if (ctx.choice == "field_to") {
            const QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("allowed").toString()); const int id = receipt.value("id", -1).toInt();
            if (canTransfer(room, ctx.invoker, from, target, id)) room->moveCardTo(Sanguosha->getCard(id), from, target, room->getCardPlace(id),
                CardMoveReason(CardMoveReason::S_REASON_TRANSFER, ctx.invoker->objectName(), objectName(), ""), true);
            return false;
        }
        const Player::Phase phase = Player::Phase(ctx.extra_data.toInt()); target->skip(phase);
        if (phase == Player::Judge) {
            room->loseHp(target, getEffectiveAmount(ctx), true, ctx.invoker, objectName());
            if (target->isDead()) return false;
            QVariantList ids; for (const Card *card : target->getJudgingArea()) ids << card->getEffectiveId();
            if (ids.isEmpty()) return false;
            ServerPlayer *recipient = room->askForPlayerChosen(ctx.invoker, room->getOtherPlayers(target), objectName(), "mobilemouqiaobian1");
            if (recipient) { ctx.choice = "judge"; ctx.extra_data = QVariantMap{{"from", target->objectName()}, {"ids", ids}}; skillEffect(event, room, actor, ctx, recipient); }
        } else if (phase == Player::Draw) {
            const qint64 serial = room->getTag("MobileMouQiaobianSequence").toLongLong() + 1; room->setTag("MobileMouQiaobianSequence", serial);
            QVariantList receipts = target->getTag("MobileMouQiaobianReceipts").toList();
            receipts << QVariantMap{{"serial", serial}, {"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
            target->setTag("MobileMouQiaobianReceipts", receipts); room->setPlayerMark(target, "&mobilemouqiaobian", receipts.size());
        } else if (phase == Player::Play) {
            const int discard = target->getHandcardNum() - 6; if (discard > 0) room->askForDiscard(target, objectName(), discard, discard);
            target->skip(Player::Discard);
            QList<ServerPlayer *> froms;
            for (ServerPlayer *from : room->getAlivePlayers()) if (room->canMoveField("ej", {from})) froms << from;
            ServerPlayer *from = room->askForPlayerChosen(ctx.invoker, froms, objectName() + "_from", "@movefield-from-optional", true);
            if (!from) return false;
            QList<int> disabled;
            for (const Card *card : from->getCards("ej")) { bool movable = false; for (ServerPlayer *to : room->getAlivePlayers()) if (canTransfer(room, ctx.invoker, from, to, card->getEffectiveId())) movable = true; if (!movable) disabled << card->getEffectiveId(); }
            const int id = room->askForCardChosen(ctx.invoker, from, "ej", objectName(), false, Card::MethodNone, disabled);
            if (id < 0 || disabled.contains(id)) return false;
            QList<ServerPlayer *> tos; for (ServerPlayer *to : room->getAlivePlayers()) if (canTransfer(room, ctx.invoker, from, to, id)) tos << to;
            if (tos.isEmpty()) return false;
            ServerPlayer *to = room->askForPlayerChosen(ctx.invoker, tos, objectName() + "_to", "@movefield-to:" + Sanguosha->getCard(id)->objectName());
            ctx.choice = "field_from"; ctx.extra_data = QVariantMap{{"id", id}}; skillEffect(event, room, actor, ctx, from);
            if (to && ctx.extra_data.toMap().contains("allowed")) { ctx.choice = "field_to"; skillEffect(event, room, actor, ctx, to); }
        }
        return false;
    }
};

class MobileMouGongqi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouGongqi() : TriggerSkillV2("mobilemougongqi")
    { events << EventPhaseStart << EventPhaseChanging << CardUsed << CardFinished << Death; global = true; }
    void project(Room *room, ServerPlayer *target, const QVariantList &receipts) const
    {
        target->setTag("MobileMouGongqiRestrictions", receipts);
        QStringList suits; for (const QVariant &value : receipts) suits << value.toMap().value("suit").toString();
        room->setPlayerProperty(target, "mobilemougongqi_suits", suits);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if ((event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play) || event == Death) {
            ServerPlayer *holder = event == Death ? data.value<DeathStruct>().who : actor;
            if (holder) {
                holder->removeTag("MobileMouGongqiPhases");
                for (ServerPlayer *p : room->getAllPlayers(true)) {
                    QVariantList kept; for (const QVariant &value : p->getTag("MobileMouGongqiRestrictions").toList())
                        if (value.toMap().value("beneficiary").toString() != holder->objectName()) kept << value;
                    project(room, p, kept);
                }
            }
        }
        if (event == CardFinished) {
            const Card *card = data.value<CardUseStruct>().card; if (!card) return false;
            const qint64 serial = card->getTag("MobileMouGongqiCard").toLongLong(); if (!serial) return false;
            for (ServerPlayer *p : room->getAllPlayers(true)) {
                QVariantList kept; for (const QVariant &value : p->getTag("MobileMouGongqiRestrictions").toList())
                    if (value.toMap().value("card").toLongLong() != serial) kept << value;
                project(room, p, kept);
            }
            card->removeTag("MobileMouGongqiCard");
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardUsed) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!actor || actor->isDead() || use.from != actor || !use.card || use.card->getTypeId() == Card::TypeSkill) return true;
        const QVariantList receipts = actor->getTag("MobileMouGongqiPhases").toList(); if (receipts.isEmpty()) return true;
        qint64 serial = use.card->getTag("MobileMouGongqiCard").toLongLong();
        if (!serial) { serial = room->getTag("MobileMouGongqiSequence").toLongLong() + 1; room->setTag("MobileMouGongqiSequence", serial); use.card->setTag("MobileMouGongqiCard", serial); }
        for (const QVariant &value : receipts) {
            QVariantMap receipt = value.toMap(); receipt["card"] = serial; receipt["beneficiary"] = actor->objectName();
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = ctx.owner; ctx.invoker = actor; ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.original_data = &data; ctx.current_event = event; ctx.choice = "respond"; ctx.extra_data = receipt; ctx.is_forced = true;
            ctx.amount = receipt.value("amount").toInt(); ctx.instanceID = receipt.value("serial").toInt(); ctx.trigger_count = 0; ctx.targets = room->getOtherPlayers(actor); contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.choice != "respond") return TriggerSkillV2::isSourceAvailable(room, ctx);
        QVariantMap receipt = ctx.extra_data.toMap(); receipt.remove("card"); receipt.remove("beneficiary");
        return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getTag("MobileMouGongqiPhases").toList().contains(receipt);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &) const override
    { return event == EventPhaseStart && actor && actor->isAlive() && actor->getPhase() == Player::Play && actor->hasSkill(objectName()) && actor->canDiscard(actor, "he") ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "respond") return true;
        const Card *card = room->askForCard(ctx.owner, "..", "mobilemougongqi0", *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card || card->getEffectiveId() < 0) return false;
        ctx.extra_data = QVariantMap{{"id", card->getEffectiveId()}, {"suit", card->getSuitString()}}; ctx.targets << ctx.invoker; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "respond") return true;
        const int id = ctx.extra_data.toMap().value("id", -1).toInt();
        if (!ctx.initiator || room->getCardOwner(id) != ctx.initiator || !ctx.initiator->canDiscard(ctx.initiator, id)
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        room->throwCard(id, objectName(), ctx.initiator); return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "respond") {
            if (getEffectiveAmount(ctx) <= 0) return false;
            QVariantList receipts = target->getTag("MobileMouGongqiRestrictions").toList(); receipts << ctx.extra_data; project(room, target, receipts); return false;
        }
        if (getEffectiveAmount(ctx) <= 0) return false;
        QVariantMap receipt = ctx.extra_data.toMap(); receipt.remove("id");
        const qint64 serial = room->getTag("MobileMouGongqiSequence").toLongLong() + 1; room->setTag("MobileMouGongqiSequence", serial); receipt["serial"] = serial;
        if (receipt.value("suit").toString().isEmpty()) receipt["suit"] = room->askForChoice(ctx.invoker, objectName(), "spade+heart+club+diamond");
        receipt["owner"] = ctx.activationRef.ownerObjectName; receipt["skill"] = ctx.activationRef.key.skillName; receipt["instance"] = ctx.activationRef.key.instanceID;
        receipt["source_owner"] = ctx.sourceRef.ownerObjectName; receipt["source_skill"] = ctx.sourceRef.key.skillName; receipt["source_instance"] = ctx.sourceRef.key.instanceID; receipt["amount"] = getEffectiveAmount(ctx);
        QVariantList receipts = target->getTag("MobileMouGongqiPhases").toList(); receipts << receipt; target->setTag("MobileMouGongqiPhases", receipts);
        return false;
    }
};

class MobileMouGongqiBf : public AttackRangeSkillV2
{
public:
	MobileMouGongqiBf() : AttackRangeSkillV2("#MobileMouGongqiBf")
	{
		frequency = NotFrequent;
        setBaseAmount(4);
	}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
        return ctx.holder ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
	}
};

class MobileMouGongqiLimit : public CardLimitSkill
{
public:
    MobileMouGongqiLimit() : CardLimitSkill("#MobileMouGongqiLimit") { frequency = NotFrequent; }
    QString limitList(const Player *) const override { return "use,response"; }
    QString limitPattern(const Player *target, const Card *card) const override
    {
        // Public projection of accepted, card-scoped restrictions; virtual cards remain allowed.
        if (!target || !card || card->isVirtualCard() || !target->handCards().contains(card->getEffectiveId())) return QString();
        for (const QString &suit : target->property("mobilemougongqi_suits").toStringList())
            if (suit != card->getSuitString()) return card->toString();
        return QString();
    }
};

MobileMouJiefanCard::MobileMouJiefanCard()
{
    setSkillName("mobilemoujiefan");
}

bool MobileMouJiefanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void MobileMouJiefanCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QList<ServerPlayer *>tps;
	foreach (ServerPlayer *p, room->getAllPlayers()) {
		if(p->inMyAttackRange(effect.to)) tps << p;
	}
	QString choice = "mobilemoujiefan1+mobilemoujiefan2="+QString::number(tps.length())+"+beishui";
	choice = room->askForChoice(effect.to,"mobilemoujiefan",choice,QVariant::fromValue(effect));
	if(!choice.contains("mobilemoujiefan2")){
		foreach (ServerPlayer *p, tps)
			room->askForDiscard(p,"mobilemoujiefan",1,1,false,true);
	}
	if(choice!="mobilemoujiefan1"){
		effect.to->drawCards(tps.length(),objectName());
	}
	if(choice=="beishui")
		room->setPlayerMark(effect.from,"&mobilemoujiefan+beishui",1);
}

class MobileMouJiefanvs : public ViewAsSkillV2
{
public:
    MobileMouJiefanvs() : ViewAsSkillV2("mobilemoujiefan") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouJiefanCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
                request.activationRef.key.instanceID, "beishui").toBool();
    }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "discard") {
            const int amount = getEffectiveAmount(ctx);
            if (amount > 0) room->askForDiscard(target, objectName(), amount, amount, false, true);
            return ContinueEffects;
        }
        QList<ServerPlayer *> participants;
        for (ServerPlayer *p : room->getAlivePlayers()) if (p->inMyAttackRange(target)) participants << p;
        const QString choice = room->askForChoice(target, objectName(),
            "mobilemoujiefan1+mobilemoujiefan2=" + QString::number(participants.size()) + "+beishui", QVariant::fromValue(ctx));
        // The disabling state belongs to the declared activation, even after an invoker takeover.
        if (choice == "beishui") {
            ServerPlayer *holder = room->findPlayerByObjectName(ctx.activationRef.ownerObjectName, true);
            if (holder && holder->findSkillInstance(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) {
                holder->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "beishui", true);
                room->setPlayerMark(holder, "&mobilemoujiefan+beishui", 1);
            }
        }
        const int draw = participants.size() * getEffectiveAmount(ctx);
        if (!choice.startsWith("mobilemoujiefan2")) {
            const QString previous = ctx.choice; ctx.choice = "discard";
            for (ServerPlayer *p : participants) if (p->isAlive()) skillEffect(ctx, p);
            ctx.choice = previous;
        }
        if (choice != "mobilemoujiefan1" && target->isAlive() && draw > 0) target->drawCards(draw, objectName());
        return ContinueEffects;
    }
};

class MobileMouJiefan : public TriggerSkillV2
{
public:
    MobileMouJiefan() : TriggerSkillV2("mobilemoujiefan") { events << Death; global = true; view_as_skill = new MobileMouJiefanvs; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        if (!killer || killer == death.who) return false;
        for (const SkillInstance &instance : killer->getSkillInstances())
            if (instance.skillName == objectName()) killer->removeSkillInstanceStateValue(objectName(), instance.instanceID, "beishui");
        room->setPlayerMark(killer, "&mobilemoujiefan+beishui", 0);
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

MobileMouShiPackage::MobileMouShiPackage()
	: Package("mobilemoushi")
{
	General *mobilemou_xuhuang = new General(this, "mobilemou_xuhuang", "wei", 4);
	mobilemou_xuhuang->addSkill(new MobileMouDuanliang);
	mobilemou_xuhuang->addSkill(new MobileMouShipo);

	General *mobilemou_machao = new General(this, "mobilemou_machao", "shu", 4);
	mobilemou_machao->addSkill(new MobileMouTieqi);
	mobilemou_machao->addSkill(new MobileMouTieqiClear);
	mobilemou_machao->addSkill("mashu");
	related_skills.insert("mobilemoutieqi", "#mobilemoutieqi");

	addMetaObject<MobileMouDuanliangCard>();
	addMetaObject<MobileMouShipoCard>();
	
	General *mobilemou_caopi = new General(this, "mobilemou_caopi$", "wei", 3);
	mobilemou_caopi->addSkill(new MobileMouXingshang);
	mobilemou_caopi->addSkill(new MobileMouFangzhu);
	mobilemou_caopi->addSkill(new MobileMouSongwei);
	addMetaObject<MobileMouXingshangCard>();
	addMetaObject<MobileMouFangzhuCard>();
	addMetaObject<MobileMouSongweiCard>();

	General *mobilemou_zhanghe = new General(this, "mobilemou_zhanghe", "wei", 4);
	mobilemou_zhanghe->addSkill(new MobileMouQiaobian);

	General *mobilemou_handang = new General(this, "mobilemou_handang", "wu", 4);
	mobilemou_handang->addSkill(new MobileMouGongqi);
	mobilemou_handang->addSkill(new MobileMouGongqiBf);
	mobilemou_handang->addSkill(new MobileMouGongqiLimit);
    related_skills.insert("mobilemougongqi", "#MobileMouGongqiBf");
    related_skills.insert("mobilemougongqi", "#MobileMouGongqiLimit");
	mobilemou_handang->addSkill(new MobileMouJiefan);
	addMetaObject<MobileMouJiefanCard>();

}
ADD_PACKAGE(MobileMouShi)


static void projectMobileMouLiegong(Room *room, ServerPlayer *owner)
{
    QStringList suits; for (const SkillInstance &instance : owner->getSkillInstances()) if (instance.skillName == "mobilemouliegong")
        for (const QString &suit : owner->getSkillInstanceStateValue(instance.skillName, instance.instanceID, "suits").toStringList()) if (!suits.contains(suit)) suits << suit;
    for (const QString &mark : owner->getMarkNames()) if (mark.startsWith("&mobilemouliegong+")) room->setPlayerMark(owner,mark,0);
    if (!suits.isEmpty()) { QString mark = "&mobilemouliegong"; for (const QString &suit : suits) mark += "+" + suit + "_char"; room->setPlayerMark(owner,mark,1); }
}

static void projectMobileMouLiegongRestrictions(Room *room, ServerPlayer *target, const QVariantList &receipts)
{
    target->setTag("MobileMouLiegongRestrictions",receipts); QStringList suits;
    for (const QVariant &value : receipts) for (const QString &suit : value.toMap().value("suits").toStringList()) if (!suits.contains(suit)) suits << suit;
    room->setPlayerProperty(target,"MobileMouLiegongTargetRecords",suits.join(","));
}

class MobileMouLiegong : public TriggerSkillV2
{
public:
    MobileMouLiegong() : TriggerSkillV2("mobilemouliegong") { events << TargetSpecified << CardUsed << TargetConfirmed; }
    bool prepareSource(Room *room, SkillContext &ctx) const override { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; ctx.is_forced = ctx.current_event != TargetSpecified; return TriggerSkillV2::prepareSource(room,ctx); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (!actor || actor->isDead() || !actor->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>(); if (!use.card || use.card->getTypeId() == Card::TypeSkill) return {};
        if (event == TargetSpecified) return use.from == actor && use.card->isKindOf("Slash") && use.to.size() == 1 ? TriggerList{{actor,{objectName()}}} : TriggerList();
        return use.card->hasSuit() && (event == CardUsed ? use.from == actor : use.from != actor && use.to.contains(actor)) ? TriggerList{{actor,{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetSpecified) { if (!ctx.owner->askForSkillInvoke(this,use.to.first())) return false; ctx.choice = "attack"; ctx.extra_data = ctx.owner->getSkillInstanceStateValue(objectName(),ctx.instanceID,"suits"); }
        else { const QString suit = use.card->getSuitString(); if (ctx.owner->getSkillInstanceStateValue(objectName(),ctx.instanceID,"suits").toStringList().contains(suit)) return false; ctx.choice = "record"; ctx.extra_data = suit; ctx.is_forced = true; }
        ctx.targets << ctx.invoker; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "restrict") { QVariantList receipts = target->getTag("MobileMouLiegongRestrictions").toList(); receipts << ctx.extra_data; projectMobileMouLiegongRestrictions(room,target,receipts); return false; }
        if (ctx.choice == "record") {
            QStringList suits = ctx.owner->getSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"suits").toStringList(); if (!suits.contains(ctx.extra_data.toString())) suits << ctx.extra_data.toString();
            ctx.owner->setSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"suits",suits); projectMobileMouLiegong(room,ctx.owner); return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>(); if (!use.card || use.to.size() != 1) return false;
        const qint64 useId = room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong(); if (useId <= 0) return false;
        // The accepted attack retains its declared suits even if its grant retires in a hook.
        const QStringList suits = ctx.extra_data.toStringList();
        const qint64 serial = room->getTag("MobileMouLiegongSequence").toLongLong()+1; room->setTag("MobileMouLiegongSequence",serial);
        QVariantMap receipt{{"serial",serial},{"use",useId},{"turn",room->historyScopes().value("turn_id")},{"suits",suits},{"damage",0},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID}};
        // Publish before revealing cards: interrupted/nested movement must still see the accepted attack.
        QVariantList receipts = use.card->getTag("MobileMouLiegongReceipts").toList(); receipts << receipt;
        use.card->setTag("MobileMouLiegongReceipts", receipts);
        const int count = qMax(0,int(suits.size())-1) * getEffectiveAmount(ctx); int damage = 0;
        if (count > 0) for (int id : room->showDrawPile(target,count,objectName(),false)) if (suits.contains(Sanguosha->getCard(id)->getSuitString())) ++damage;
        receipts = use.card->getTag("MobileMouLiegongReceipts").toList(); bool retained = false;
        for (QVariant &value : receipts) if (value.toMap().value("serial").toLongLong() == serial) {
            receipt = value.toMap(); receipt["damage"] = damage; value = receipt; retained = true; break;
        }
        use.card->setTag("MobileMouLiegongReceipts", receipts);
        if (retained && count > 0 && use.to.first()->isAlive()) { ctx.choice = "restrict"; ctx.extra_data = receipt; skillEffect(event,room,actor,ctx,use.to.first()); }
        return false;
    }
};

class MobileMouLiegongEffect : public TriggerSkillV2
{
public:
    MobileMouLiegongEffect() : TriggerSkillV2("#mobilemouliegong") { events << ConfirmDamage << CardFinished << CardOffset << CardOnEffect << EventLoseSkill << EventPhaseChanging << Death; global = true; frequency = Compulsory; }
    int getPriority(TriggerEvent event) const override { return event == CardOffset || event == CardOnEffect || event == CardFinished ? 5 : TriggerSkillV2::getPriority(event); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventLoseSkill) { if (actor) projectMobileMouLiegong(room,actor); return false; }
        if (event == Death) {
            ServerPlayer *dead = data.value<DeathStruct>().who;
            if (dead) projectMobileMouLiegongRestrictions(room,dead,{});
            return false;
        }
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            if (turn > 0) for (ServerPlayer *target : room->getAllPlayers(true)) {
                QVariantList kept;
                for (const QVariant &value : target->getTag("MobileMouLiegongRestrictions").toList())
                    if (value.toMap().value("turn").toLongLong() != turn) kept << value;
                projectMobileMouLiegongRestrictions(room,target,kept);
            }
            return false;
        }
        if (event != CardFinished && event != CardOffset && event != CardOnEffect) return false;
        const Card *card = event == CardFinished ? data.value<CardUseStruct>().card : data.value<CardEffectStruct>().card; if (!card) return false;
        const qint64 useId = room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong(); if (useId <= 0) return false;
        QVariantList kept, matching;
        for (const QVariant &value : card->getTag("MobileMouLiegongReceipts").toList()) {
            if (value.toMap().value("use").toLongLong() == useId) matching << value;
            else kept << value;
        }
        // Publish the removal before state/mark callbacks may create a nested receipt.
        if (event == CardFinished) card->setTag("MobileMouLiegongReceipts", kept);
        for (const QVariant &value : matching) {
            const QVariantMap receipt = value.toMap();
            if (event == CardFinished) { ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(),true);
                if (owner) { owner->removeSkillInstanceStateValue(receipt.value("skill").toString(),receipt.value("instance").toInt(),"suits"); projectMobileMouLiegong(room,owner); } }
            for (ServerPlayer *target : room->getAllPlayers(true)) {
                if (event != CardFinished && target != data.value<CardEffectStruct>().to) continue;
                QVariantList remaining; for (const QVariant &restriction : target->getTag("MobileMouLiegongRestrictions").toList()) if (restriction.toMap().value("serial") != receipt.value("serial")) remaining << restriction;
                projectMobileMouLiegongRestrictions(room,target,remaining);
            }
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != ConfirmDamage) return true; const DamageStruct damage = data.value<DamageStruct>(); if (!damage.card || !damage.to || damage.to->isDead()) return true;
        const qint64 useId = room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong(); if (useId <= 0) return true;
        for (const QVariant &value : damage.card->getTag("MobileMouLiegongReceipts").toList()) {
            const QVariantMap receipt = value.toMap(); if (receipt.value("use").toLongLong() != useId || receipt.value("damage").toInt() <= 0) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(),true); ctx.initiator = ctx.owner; ctx.invoker = damage.from;
            ctx.instanceID = receipt.value("serial").toInt(); ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),SkillInstanceKey(receipt.value("source_skill").toString(),receipt.value("source_instance").toInt()));
            ctx.extra_data = receipt; ctx.amount = receipt.value("damage").toInt(); ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true; ctx.targets << damage.to; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { const DamageStruct damage = ctx.original_data->value<DamageStruct>(); return damage.card && damage.card->getTag("MobileMouLiegongReceipts").toList().contains(ctx.extra_data); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { DamageStruct damage = ctx.original_data->value<DamageStruct>(); if (damage.to == target) { damage.damage += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(damage); } return false; }
};

class MobileMouLiegongLimit : public CardLimitSkill
{
public:
	MobileMouLiegongLimit() : CardLimitSkill("#mobilemouliegong-limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target, const Card *card) const
	{
		if(target->getMark("&mobilemoufangzhu+2-SelfClear")>0&&card->getTypeId()!=1&&target->handCards().contains(card->getId()))
			return card->toString();
		if(target->getMark("&mobilemoufangzhu+6-SelfClear")>0&&card->getTypeId()!=2&&target->handCards().contains(card->getId()))
			return card->toString();
		if(target->getMark("&mobilemoufangzhu+8-SelfClear")>0&&card->getTypeId()!=3&&target->handCards().contains(card->getId()))
			return card->toString();
		QString record = target->property("MobileMouLiegongTargetRecords").toString();
		if (record.isEmpty()) return "";
		return "Jink|" + record;
	}
};

MobileMouKejiCard::MobileMouKejiCard()
{
    setSkillName("mobilemoukeji");
	target_fixed = true;
}

void MobileMouKejiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->addPlayerMark(source, "mobilemoukeji-PlayClear", subcardsLength() + 1);
	if (subcards.isEmpty()) {
		room->loseHp(HpLostStruct(source, 1, "mobilemoukeji", source));
		if (source->isAlive())
			source->gainHujia(2, 5);
	} else
		source->gainHujia(1, 5);
}

class MobileMouKejiVS : public ViewAsSkillV2
{
public:
    MobileMouKejiVS() : ViewAsSkillV2("mobilemoukeji", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouKejiCard"; }
    bool available(const Player *holder, const SkillInstanceRef &ref, const QString &mode = QString()) const
    {
        if (!holder || !ref.isValid()) return false;
        const QStringList used = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "phase_modes").toStringList();
        if (holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "dujiang").toBool()) return used.isEmpty();
        return mode.isEmpty() ? used.size() < 2 : !used.contains(mode);
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && available(request.initiator, request.activationRef); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && card && request.selectedCardIds.isEmpty() && !card->isVirtualCard() && !card->hasFlag("using")
        && request.initiator->handCards().contains(card->getEffectiveId()) && !request.initiator->isJilei(card) && available(request.initiator, request.activationRef, "discard"); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { if (request.selectedCardIds.isEmpty()) return available(request.initiator, request.activationRef, "hp");
        if (request.selectedCardIds.size() != 1) return false; ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear(); return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first())); }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.initiator) return false; const SkillInstanceRef ref = getUsageRef(ctx);
        const Player *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        return available(holder, ref, ctx.use_card ? (ctx.use_card->subcardsLength() ? "discard" : "hp") : QString());
    }
    void addUsage(const SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.use_card) return; const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true); if (!holder || !ref.isValid()) return;
        QStringList used = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "phase_modes").toStringList();
        const QString mode = ctx.use_card->subcardsLength() ? "discard" : "hp"; if (!used.contains(mode)) used << mode;
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "phase_modes", used);
    }
    void resetUsage(const SkillContext &ctx) const override
    { if (ctx.initiator) { const SkillInstanceRef ref = getUsageRef(ctx); ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true); if (holder) holder->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "phase_modes"); } }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { if (!ctx.initiator || !isUsable(ctx) || !cardSelectionFeasible(request)) return false; addUsage(ctx);
        if (request.selectedCardIds.isEmpty()) { room->loseHp(HpLostStruct(ctx.initiator, 1, objectName(), ctx.initiator)); return true; }
        return ViewAsSkillV2::pay(room, ctx, request); }
    EffectFlow effect(SkillContext &ctx) const override
    { ctx.manual_effect = true; return skillEffect(ctx, ctx.invoker); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const bool hp = ctx.use_card->subcardsLength() == 0; const int amount = getEffectiveAmount(ctx);
        if (target->isAlive()) target->gainHujia((hp ? 2 : 1) * amount, 5);
        return ContinueEffects;
    }
};

class MobileMouKeji : public TriggerSkillV2
{
public:
    MobileMouKeji() : TriggerSkillV2("mobilemoukeji") { events << EventPhaseChanging << EventSkillInvoking; global = true; view_as_skill = new MobileMouKejiVS; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventSkillInvoking) { const SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.activationRef.key.skillName == objectName() && accepted.skill_name == objectName() && accepted.use_card && accepted.use_card->getTypeId() == Card::TypeSkill) view_as_skill->addUsage(accepted);
        } else if (actor && data.value<PhaseChangeStruct>().from == Player::Play) {
            for (const SkillInstance &instance : actor->getSkillInstances()) if (instance.skillName == objectName()) actor->removeSkillInstanceStateValue(objectName(), instance.instanceID, "phase_modes");
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class MobileMouKejiMax : public MaxCardsSkillV2
{
public:
	MobileMouKejiMax() : MaxCardsSkillV2("#mobilemoukeji-max")
	{
		frequency = NotFrequent;
	}

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
        return ctx.holder ? CorrectSkillResult::useAmount(qMax(ctx.holder->getHujia(), 0) * ctx.currentAmount)
            : CorrectSkillResult::noEffect();
	}
};

class MobileMouKejiLimit : public CardLimitSkill
{
public:
	MobileMouKejiLimit() : CardLimitSkill("#mobilemoukeji-limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (!target->hasFlag("Global_Dying") && target->hasSkill("mobilemoukeji"))
			return "Peach";
		return "";
	}
};

class MobileMouDujiang : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouDujiang() : TriggerSkillV2("mobilemoudujiang") { events << EventPhaseStart << EventSkillInvoking; frequency = Wake; waked_skills = "mobilemouduojing"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    { if (event == EventSkillInvoking) { const SkillContext accepted = data.value<SkillContext>(); if (accepted.bypass_cost && accepted.skill_name == objectName() && accepted.activationRef.key.skillName == objectName()) addUsage(accepted); } return false; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &) const override
    { return event == EventPhaseStart && actor && actor->isAlive() && actor->getPhase() == Player::Start && actor->hasSkill(objectName()) && (actor->getHujia() >= 3 || actor->canWake(objectName())) ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->peiyin(this); room->doSuperLightbox(target, objectName()); room->setPlayerMark(target, objectName(), 1);
        for (const SkillInstance &instance : target->getSkillInstances()) if (instance.skillName == "mobilemoukeji") target->setSkillInstanceStateValue(instance.skillName, instance.instanceID, "dujiang", true);
        if (room->changeMaxHpForAwakenSkill(target, 0, objectName())) room->acquireSkillFromEffect(target, "mobilemouduojing", ctx);
        return false;
    }
};

class MobileMouDuojiang : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouDuojiang() : TriggerSkillV2("mobilemouduojing") { events << TargetSpecifying; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>(); TriggerList result;
        if (!actor || actor->isDead() || !actor->hasSkill(objectName()) || use.from != actor || !use.card || !use.card->isKindOf("Slash") || actor->getHujia() <= 0) return result;
        for (ServerPlayer *target : use.to) if (target != actor && target->isAlive()) result[actor] << objectName(); return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> targets; for (ServerPlayer *p : ctx.original_data->value<CardUseStruct>().to) if (p != ctx.owner && p->isAlive()) targets << p;
        ServerPlayer *target = targets.value(ctx.trigger_count, nullptr); if (!target || ctx.owner->getHujia() <= 0 || !room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(target))) return false;
        ctx.targets << target; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.initiator || ctx.initiator->getHujia() < 1) return false; ctx.initiator->loseHujia(1); return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "benefit") {
            const QVariantMap receipt = ctx.extra_data.toMap(); const int id = receipt.value("id", -1).toInt();
            ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (from && from->handCards().contains(id)) room->obtainCard(target, id, false);
            if (target->isAlive()) room->addSlashCishu(target, getEffectiveAmount(ctx)); return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>(); if (!use.card || !use.to.contains(target)) return false;
        target->addQinggangTag(use.card);
        const int id = target->isKongcheng() ? -1 : room->askForCardChosen(ctx.invoker, target, "h", objectName());
        ctx.extra_data = QVariantMap{{"from", target->objectName()}, {"id", id}}; ctx.choice = "benefit";
        if (ctx.invoker->isAlive()) skillEffect(event, room, actor, ctx, ctx.invoker);
        return false;
    }
};

class MobileMouXiayuan : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }

	MobileMouXiayuan() : TriggerSkillV2("mobilemouxiayuan")
	{
		events << Damaged << EventSkillInvoking;
	}
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Round; }
    QStringList payable(ServerPlayer *player) const
    {
        QStringList result;
        for (int id : player->handCards())
            if (player->canDiscard(player, id)) result << QString::number(id);
        return result;
    }
    int depletedArmor(Room *room, ServerPlayer *victim) const
    {
        const QVariantMap event = room->historyParent(room->currentHistoryEventId(), "damage", true);
        if (event.value("id").toLongLong() <= 0) return 0;
        const QVariantMap page = room->queryActualDamage({{"event_id", event.value("id")},
            {"to", victim->objectName()}, {"limit", 1}});
        if (page.contains("error") || !page.value("complete").toBool()
            || page.value("has_more").toBool()) return 0;
        const QVariantList items = page.value("items").toList();
        if (items.size() != 1) return 0;
        const QVariantMap damage = items.first().toMap().value("data").toMap();
        // Armor depletion is frozen at its committed mutation, before nested LostHujia callbacks.
        if (!damage.contains("armor_before") || !damage.contains("armor_after")
            || damage.value("absorbed").toInt() <= 0 || damage.value("armor_after").toInt() != 0) return 0;
        return qMax(0, damage.value("armor_before").toInt());
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        TriggerList result;
        if (!player || player->isDead() || depletedArmor(room, player) <= 0) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && payable(owner).size() >= 2) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
        if (!victim || victim->isDead()) return false;
        const int armor = depletedArmor(room, victim);
        const QStringList ids = payable(ctx.owner);
        if (armor <= 0 || ids.size() < 2) return false;
        const Card *cards = room->askForExchange(ctx.owner, objectName(), 2, 2, false,
            "@mobilemouxiayuan:" + victim->objectName() + "::" + QString::number(armor), true, ids.join(","));
        if (!cards || cards->subcardsLength() != 2) return false;
        ctx.extra_data = QVariantMap{{"armor", armor}, {"materials", ListI2V(cards->getSubcards())}};
        ctx.targets = {victim};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QList<int> ids = ListV2I(ctx.extra_data.toMap().value("materials").toList());
        const QStringList available = payable(ctx.owner);
        if (ids.size() != 2 || ids.first() == ids.last()) return false;
        for (int id : ids)
            if (!available.contains(QString::number(id))) return false;
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        DummyCard discard(ids);
        room->throwCard(&discard, ctx.owner, nullptr);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
        if (!victim) return false;
        QVariantMap receipt = ctx.extra_data.toMap(); receipt["armor"] = depletedArmor(room, victim); ctx.extra_data = receipt;
        return skillEffect(event, room, actor, ctx, victim);
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->gainHujia(ctx.extra_data.toMap().value("armor").toInt() * getEffectiveAmount(ctx), 5);
        return false;
    }
};

class MobileMouJieyue : public TriggerSkillV2
{
public:
	MobileMouJieyue() : TriggerSkillV2("mobilemoujieyue")
	{
        events << EventPhaseStart;
	}
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "@mobilemoujieyue-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.targets.isEmpty() && !cost(event, room, actor, ctx)) return false;
        return skillEffect(event, room, actor, ctx, ctx.targets.value(0, nullptr));
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "give") {
            const QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString()); QList<int> ids;
            if (from) for (const QVariant &value : receipt.value("ids").toList()) if (room->getCardOwner(value.toInt()) == from && (room->getCardPlace(value.toInt()) == Player::PlaceHand || room->getCardPlace(value.toInt()) == Player::PlaceEquip)) ids << value.toInt();
            if (!ids.isEmpty()) { DummyCard cards(ids); room->giveCard(from, target, &cards, objectName()); } return false;
        }
        room->broadcastSkillInvoke(this);
        target->gainHujia(getEffectiveAmount(ctx), 5);
        if (target->isAlive()) target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        if (target->isAlive() && !target->isNude() && ctx.owner->isAlive()) {
            const int count = qMin(2 * getEffectiveAmount(ctx), target->getCardCount());
            if (count <= 0) return false;
            const Card *gift = room->askForExchange(target, objectName(), count, count, true,
                "@mobilemoujieyue-give:" + ctx.owner->objectName());
            if (gift) { ctx.extra_data = QVariantMap{{"from", target->objectName()}, {"ids", ListI2V(gift->getSubcards())}}; ctx.choice = "give"; skillEffect(event, room, actor, ctx, ctx.owner); }
        }
        return false;
    }
};

MobileMouXianzhenCard::MobileMouXianzhenCard()
{
    setSkillName("mobilemouxianzhen");
}

bool MobileMouXianzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->getHp()<Self->getHp();
}

void MobileMouXianzhenCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->setPlayerMark(effect.from,effect.to->objectName()+"mobilemouxianzhenTo-PlayClear",1);
	room->setPlayerMark(effect.to,"&mobilemouxianzhen+#"+effect.from->objectName(),1);
}

class MobileMouXianzhenvs : public ViewAsSkillV2
{
public:
    MobileMouXianzhenvs() : ViewAsSkillV2("mobilemouxianzhen") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouXianzhenCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { return request.initiator && target && target != request.initiator && target->isAlive() && targets.isEmpty() && target->getHp() < request.initiator->getHp(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice != "apply") { SkillContext benefit = ctx; benefit.choice = "apply"; benefit.extra_data = target->objectName(); skillEffect(benefit,ctx.invoker); return ContinueEffects; }
        target = room->findPlayerByObjectName(ctx.extra_data.toString()); if (!target || target->isDead()) return ContinueEffects;
        QVariantList receipts = ctx.invoker->getTag("MobileMouXianzhenRelations").toList(); QVariant turn;
        for (const QVariant &value : receipts) { const QVariantMap receipt = value.toMap(); if (receipt.value("owner").toString() == ctx.activationRef.ownerObjectName && receipt.value("instance").toInt() == ctx.activationRef.key.instanceID) turn = receipt.value("damage_turn"); }
        bool exists = false; for (const QVariant &value : receipts) { const QVariantMap receipt = value.toMap(); if (receipt.value("owner").toString() == ctx.activationRef.ownerObjectName && receipt.value("instance").toInt() == ctx.activationRef.key.instanceID && receipt.value("target").toString() == target->objectName()) exists = true; }
        if (!exists) {
            const qint64 serial = room->getTag("MobileMouXianzhenSequence").toLongLong()+1; room->setTag("MobileMouXianzhenSequence",serial);
            receipts << QVariantMap{{"serial",serial},{"target",target->objectName()},{"amount",getEffectiveAmount(ctx)},{"damage_turn",turn},
                {"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
                {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID}};
            ctx.invoker->setTag("MobileMouXianzhenRelations",receipts);
        }
        room->setPlayerMark(ctx.invoker,target->objectName()+"mobilemouxianzhenTo-PlayClear",1); room->setPlayerMark(target,"&mobilemouxianzhen+#"+ctx.invoker->objectName(),1); return ContinueEffects;
    }
};

class MobileMouXianzhen : public TriggerSkillV2
{
public:
    MobileMouXianzhen() : TriggerSkillV2("mobilemouxianzhen") { events << TargetSpecified << Pindian; global = true; view_as_skill = new MobileMouXianzhenvs; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        QList<ServerPlayer *> beneficiaries; if (event == TargetSpecified) { const CardUseStruct use = data.value<CardUseStruct>(); if (!actor || actor->isDead() || use.from != actor || !use.card || !use.card->isKindOf("Slash")) return true; beneficiaries << actor; }
        else { PindianStruct *pd = data.value<PindianStruct *>(); if (!pd) return true; beneficiaries << pd->from << pd->to; }
        for (ServerPlayer *beneficiary : beneficiaries) {
            if (!beneficiary || beneficiary->isDead()) continue;
            for (const QVariant &value : beneficiary->getTag("MobileMouXianzhenRelations").toList()) {
                QVariantMap receipt = value.toMap(); ServerPlayer *opponent = room->findPlayerByObjectName(receipt.value("target").toString()); if (!opponent) continue;
                if (event == TargetSpecified) { if (!data.value<CardUseStruct>().to.contains(opponent) || !beneficiary->canPindian(opponent)) continue; }
                else { PindianStruct *pd = data.value<PindianStruct *>(); const Card *card = nullptr;
                    if (pd->from == beneficiary && pd->to == opponent) card = pd->to_card; else if (pd->to == beneficiary && pd->from == opponent) card = pd->from_card;
                    if (!card || !card->isKindOf("Slash") || room->getCardOwner(card->getEffectiveId()) || room->getCardPlace(card->getEffectiveId()) != Player::PlaceTable) continue;
                    receipt["card"] = card->getEffectiveId(); }
                SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(),true); ctx.initiator = ctx.owner; ctx.invoker = beneficiary;
                ctx.instanceID = receipt.value("serial").toInt(); ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),SkillInstanceKey(receipt.value("source_skill").toString(),receipt.value("source_instance").toInt()));
                ctx.choice = event == Pindian ? "obtain" : "pindian"; ctx.is_forced = event == Pindian; ctx.extra_data = receipt; ctx.amount = receipt.value("amount").toInt(); ctx.current_event = event; ctx.original_data = &data;
                ctx.targets << (event == Pindian ? beneficiary : opponent); contexts << ctx;
            }
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { if (!ctx.invoker || ctx.invoker->isDead()) return false; for (const QVariant &value : ctx.invoker->getTag("MobileMouXianzhenRelations").toList()) if (value.toMap().value("serial") == ctx.extra_data.toMap().value("serial")) return true; return false; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.choice == "obtain" || (!ctx.targets.isEmpty() && ctx.invoker->canPindian(ctx.targets.first()) && ctx.invoker->askForSkillInvoke(this,ctx.targets.first())); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "obtain") { const int id = ctx.extra_data.toMap().value("card",-1).toInt(); if (id >= 0 && !room->getCardOwner(id) && room->getCardPlace(id) == Player::PlaceTable) room->obtainCard(target,id,true); return false; }
        if (ctx.choice == "damage") { room->damage(DamageStruct(objectName(),ctx.invoker,target,getEffectiveAmount(ctx))); return false; }
        if (ctx.choice == "armor") { const CardUseStruct use = ctx.original_data->value<CardUseStruct>(); if (use.card) target->addQinggangTag(use.card); return false; }
        if (ctx.choice == "history") { CardUseStruct use = ctx.original_data->value<CardUseStruct>(); use.m_addHistory = false; *ctx.original_data = QVariant::fromValue(use); return false; }
        if (!ctx.invoker->canPindian(target) || !ctx.invoker->pindian(target,objectName())) return false;
        const QVariantMap receipt = ctx.extra_data.toMap(); const QVariant turn = room->historyScopes().value("turn_id"); bool unused = turn.toLongLong() > 0;
        QVariantList receipts = ctx.invoker->getTag("MobileMouXianzhenRelations").toList();
        for (const QVariant &value : receipts) { const QVariantMap current = value.toMap(); if (current.value("owner") == receipt.value("owner") && current.value("instance") == receipt.value("instance") && current.value("damage_turn") == turn) unused = false; }
        if (unused) {
            // This consumes only the established relation's damage subeffect, not a new invocation.
            // Commit before its recipient hook; cancellation never reopens the once-per-turn damage.
            for (QVariant &value : receipts) { QVariantMap current = value.toMap(); if (current.value("owner") == receipt.value("owner") && current.value("instance") == receipt.value("instance")) { current["damage_turn"] = turn; value = current; } }
            ctx.invoker->setTag("MobileMouXianzhenRelations",receipts); SkillContext damage = ctx; damage.choice = "damage"; skillEffect(event,room,actor,damage,target);
        }
        SkillContext benefit = ctx; benefit.choice = "history"; skillEffect(event,room,actor,benefit,ctx.invoker);
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>(); for (ServerPlayer *recipient : use.to) { SkillContext armor = ctx; armor.choice = "armor"; skillEffect(event,room,actor,armor,recipient); }
        return false;
    }
};

class MobileMouJinjiuvs : public ViewAsSkillV2
{
public:
    MobileMouJinjiuvs() : ViewAsSkillV2("mobilemoujinjiu", 1) { response_pattern = "slash"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern == "slash"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
        && request.initiator->handCards().contains(card->getEffectiveId()) && card->isKindOf("Analeptic"); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { if (request.selectedCardIds.size() != 1) return false; ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear(); return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first())); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Slash *slash = new Slash(material->getSuit(), material->getNumber()); slash->addSubcard(material); slash->setSkillName(objectName()); return slash;
    }
};

class MobileMouJinjiu : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouJinjiu() : TriggerSkillV2("mobilemoujinjiu")
    { events << DamageInflicted << PindianVerifying; view_as_skill = new MobileMouJinjiuvs; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        if (event == DamageInflicted) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (actor && actor->isAlive() && actor->hasSkill(objectName()) && damage.to == actor && damage.card && damage.card->isKindOf("Slash") && damage.card->hasFlag("drank")) result[actor] << objectName();
        } else {
            PindianStruct *pd = data.value<PindianStruct *>(); if (!pd) return result;
            if (pd->from && pd->from->isAlive() && pd->from->hasSkill(objectName()) && pd->to_card && pd->to_card->isKindOf("Analeptic")) result[pd->from] << objectName();
            if (pd->to && pd->to->isAlive() && pd->to->hasSkill(objectName()) && pd->from_card && pd->from_card->isKindOf("Analeptic")) result[pd->to] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageInflicted) ctx.targets << ctx.original_data->value<DamageStruct>().to;
        else { PindianStruct *pd = ctx.original_data->value<PindianStruct *>(); if (!pd) return false;
            ctx.choice = pd->from == ctx.owner ? "to" : "from"; ctx.targets << (ctx.choice == "to" ? pd->to : pd->from); }
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DamageInflicted) { DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to == target) { damage.damage = getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(damage); return damage.damage <= 0; }
        } else { PindianStruct *pd = ctx.original_data->value<PindianStruct *>(); if (!pd) return false;
            if (ctx.choice == "to" && target == pd->to) pd->to_number = qBound(1, getEffectiveAmount(ctx), 13);
            else if (ctx.choice == "from" && target == pd->from) pd->from_number = qBound(1, getEffectiveAmount(ctx), 13);
            *ctx.original_data = QVariant::fromValue(pd);
        }
        return false;
    }
};

class MobileMouJinjiuLimit : public CardLimitSkill
{
public:
	MobileMouJinjiuLimit() : CardLimitSkill("#MobileMouJinjiuLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use,response";
	}

	QString limitPattern(const Player *target) const
	{
		if(target->hasSkill("mobilemoujinjiu"))
			return "Analeptic";
		return "";
	}
};

class MobileMouJinjiuLimit2 : public CardLimitSkill
{
public:
	MobileMouJinjiuLimit2() : CardLimitSkill("#MobileMouJinjiuLimit2")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		foreach (const Player *p, target->getAliveSiblings()){
			if(p->hasFlag("CurrentPlayer")&&p->hasSkill("mobilemoujinjiu"))
				return "Analeptic";
		}
		return "";
	}
};

class MobileMouQianxunvs : public ViewAsSkillV2
{
public:
    MobileMouQianxunvs() : ViewAsSkillV2("mobilemouqianxun") { response_pattern = "@@mobilemouqianxun"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@mobilemouqianxun" && !request.initiator->property("mobilemouqianxunTrick").toString().isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !request.selectedCardIds.isEmpty()) return nullptr;
        Card *card = Sanguosha->cloneCard(request.initiator->property("mobilemouqianxunTrick").toString());
        if (!card) return nullptr; if (!card->isNDTrick()) { delete card; return nullptr; }
        card->setSkillName(objectName()); return card;
    }
};

class MobileMouQianxun : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouQianxun() : TriggerSkillV2("mobilemouqianxun")
    { events << CardEffected << EventPhaseStart << EventPhaseChanging << EventSkillEffectFinished; global = true; frequency = Compulsory; view_as_skill = new MobileMouQianxunvs; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) { const SkillContext accepted = data.value<SkillContext>();
            if (accepted.skill_name == objectName() && accepted.choice == "return" && accepted.invoker) {
                QVariantList receipts = accepted.invoker->getTag("MobileMouQianxunReturns").toList(); receipts.removeOne(accepted.extra_data); accepted.invoker->setTag("MobileMouQianxunReturns", receipts);
            }
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging) return false;
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        for (ServerPlayer *target : room->getAlivePlayers()) {
                for (const QVariant &value : target->getTag("MobileMouQianxunReturns").toList()) {
                const QVariantMap receipt = value.toMap(); SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
                ctx.initiator = ctx.owner; ctx.invoker = target; ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
                ctx.extra_data = receipt; ctx.choice = "return"; ctx.original_data = &data; ctx.current_event = event; ctx.amount = receipt.value("amount").toInt(); ctx.is_forced = true; ctx.instanceID = receipt.value("serial").toInt(); ctx.trigger_count = 0;
                ctx.targets << target; contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.choice == "return" ? ctx.invoker && ctx.invoker->getTag("MobileMouQianxunReturns").toList().contains(ctx.extra_data) : TriggerSkillV2::isSourceAvailable(room, ctx); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (!actor || actor->isDead() || !actor->hasSkill(objectName())) return {};
        if (event == CardEffected) { const CardEffectStruct effect = data.value<CardEffectStruct>(); if (effect.to == actor && effect.from != actor && effect.card && effect.card->isKindOf("TrickCard")) return {{actor, {objectName()}}}; }
        else if (event == EventPhaseStart && actor->getPhase() == Player::Play) return {{actor, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "return") return true;
        const QStringList names = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "names").toStringList();
        if (event == CardEffected) { ctx.choice = ctx.original_data->value<CardEffectStruct>().card->objectName(); if (names.contains(ctx.choice)) return false; }
        else { if (names.isEmpty() || !room->askForSkillInvoke(ctx.owner, objectName() + "$-1", names)) return false;
            ctx.choice = room->askForChoice(ctx.owner, objectName(), names.join("+")); if (!names.contains(ctx.choice)) return false; }
        ctx.targets << ctx.invoker; return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.choice.isEmpty()) {
            if (event == CardEffected) ctx.choice = ctx.original_data->value<CardEffectStruct>().card->objectName();
            else { const QStringList names = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "names").toStringList();
                if (names.isEmpty()) return false; ctx.choice = room->askForChoice(ctx.invoker, objectName(), names.join("+")); }
        }
        return skillEffect(event, room, actor, ctx, ctx.invoker);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "return") {
            const QVariantMap receipt = ctx.extra_data.toMap(); QList<int> ids;
            for (const QVariant &value : receipt.value("ids").toList()) if (target->getPile(objectName()).contains(value.toInt())) ids << value.toInt();
            QVariantList receipts = target->getTag("MobileMouQianxunReturns").toList(); receipts.removeOne(ctx.extra_data); target->setTag("MobileMouQianxunReturns", receipts);
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); } return false;
        }
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.activationRef.ownerObjectName, true); if (!holder) return false;
        QStringList names = holder->getSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "names").toStringList();
        if (event == CardEffected) {
            if (names.contains(ctx.choice)) return false; names << ctx.choice; holder->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "names", names);
            const int count = qMin(5, names.size()) * getEffectiveAmount(ctx); if (count <= 0 || target->isNude()) return false;
            const Card *selected = room->askForExchange(target, objectName(), count, 1, true, "mobilemouqianxun0:" + QString::number(count), true);
            if (!selected) return false; QList<int> ids;
            for (int id : selected->getSubcards()) if (room->getCardOwner(id) == target && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip) && !ids.contains(id)) ids << id;
            if (ids.isEmpty()) return false;
            const qint64 serial = room->getTag("MobileMouQianxunSequence").toLongLong() + 1; room->setTag("MobileMouQianxunSequence", serial);
            QVariantList receipts = target->getTag("MobileMouQianxunReturns").toList();
            receipts << QVariantMap{{"serial", serial}, {"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}, {"ids", ListI2V(ids)}};
            target->setTag("MobileMouQianxunReturns", receipts); target->addToPile(objectName(), ids, false);
        } else {
            if (!names.removeOne(ctx.choice)) return false; holder->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "names", names);
            Card *probe = Sanguosha->cloneCard(ctx.choice); if (!probe) return false;
            CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(probe)); probe->deleteLater(); if (!probe->isNDTrick()) return false;
            const QVariant previous = target->property("mobilemouqianxunTrick"); target->setProperty("mobilemouqianxunTrick", ctx.choice); room->notifyProperty(target, target, "mobilemouqianxunTrick");
            const auto restore = qScopeGuard([&] { target->setProperty("mobilemouqianxunTrick", previous); room->notifyProperty(target, target, "mobilemouqianxunTrick"); });
            Room::AcceptedViewAsEffectScope prompt(room, target, objectName(), ctx);
            if (prompt.isValid()) room->askForUseCard(target, "@@mobilemouqianxun", "mobilemouqianxun1:" + ctx.choice);
        }
        return false;
    }
};

class MobileMouLianying : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouLianying() : TriggerSkillV2("mobilemoulianying") { events << EventPhaseChanging; global = true; }
    int lostCards(Room *room, ServerPlayer *owner) const
    {
        const QVariant turn = room->historyScopes().value("turn_id"); if (turn.toLongLong() <= 0) return -1;
        QVariantMap query{{"turn_id", turn}, {"from", owner->objectName()}, {"limit", 128}}; QVariant watermark; int count = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query); if (page.contains("error") || !page.value("complete").toBool()) return -1;
            if (!watermark.isValid()) watermark = page.value("watermark");
            for (const QVariant &value : page.value("items").toList()) { const QVariantMap move = value.toMap().value("data").toMap(); const int place = move.value("from_place").toInt();
                if (place == Player::PlaceHand || place == Player::PlaceEquip) ++count; }
            if (!page.value("has_more").toBool()) break; query["after"] = page.value("next_after"); query["watermark"] = watermark;
        }
        return count;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result; if (!actor || data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(actor)) if (owner->hasSkill(objectName()) && lostCards(room, owner) > 0) result[owner] << objectName(); return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { const int count = lostCards(room, ctx.owner); if (count <= 0 || !room->askForSkillInvoke(ctx.owner, objectName())) return false; ctx.extra_data = qMin(5, count); ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "give") {
            QList<int> ids; for (const QVariant &value : ctx.extra_data.toList()) if (room->getCardPlace(value.toInt()) == Player::DrawPile && !room->getDrawPile().contains(value.toInt())) ids << value.toInt();
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); } return false;
        }
        const QList<int> shown = room->getNCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx)); QList<int> pending = shown;
        const auto restore = qScopeGuard([&] { room->clearAG(target); QList<int> remaining; for (int id : shown) if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id; room->returnToTopDrawPile(remaining); });
        room->fillAG(shown, target);
        while (!pending.isEmpty() && target->isAlive()) {
            const QList<int> allowed = pending; QList<int> candidates = pending;
            const CardsMoveStruct selected = room->askForYijiStruct(target, candidates, objectName(), true, false, false, pending.size(), room->getAlivePlayers(), CardMoveReason(), "mobilemoulianying0", false, false);
            ServerPlayer *recipient = qobject_cast<ServerPlayer *>(selected.to); QList<int> ids;
            for (int id : selected.card_ids) if (allowed.contains(id) && !ids.contains(id)) ids << id;
            if (!recipient || recipient->isDead() || ids.isEmpty()) { recipient = target; ids = allowed; }
            for (int id : ids) pending.removeOne(id);
            ctx.choice = "give"; ctx.extra_data = ListI2V(ids); skillEffect(event, room, actor, ctx, recipient); ctx.choice.clear();
        }
        return false;
    }
};

MobileMouJingceCard::MobileMouJingceCard()
{
    setSkillName("mobilemoujingce");
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool MobileMouJingceCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.length()<3;
}

bool MobileMouJingceCard::targetsFeasible(const QList<const Player *> &, const Player *) const
{
	return true;
}

void MobileMouJingceCard::onUse(Room *room, CardUseStruct &use) const
{
	int n = 0;
	QHash<int,QString>id2p;
	foreach (ServerPlayer *p, use.to){
		id2p[subcards[n]] = p->objectName();
		room->setCardTip(subcards[n],p->getGeneralName());
		n++;
	}
	QStringList id2ps;
	QList<int>ids = room->askForGuanxing(use.from,subcards,Room::GuanxingUpOnly,false);
	for (int i = 0; i < 3; i++) {
		if(id2p.contains(ids[i])){
			id2ps << QString("%1|%2").arg(ids[i]).arg(id2p[ids[i]]);
		}else{
			id2ps << QString::number(ids[i]);
		}
		room->moveCardsInToDrawpile(use.from,ids[i],"mobilemoujingce",(i+1)*3,true);
	}
	use.from->setTag("mobilemoujingceId2ps", id2ps);
}

class MobileMouJingcevs : public ViewAsSkillV2
{
public:
    MobileMouJingcevs() : ViewAsSkillV2("mobilemoujingce", 3) { response_pattern = "@@mobilemoujingce"; expand_pile = "mobilemoujingce"; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouJingceCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@mobilemoujingce"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator);
        const QVariantMap origin = server ? server->getTag("MobileMouJingcePromptOrigin").toMap() : QVariantMap();
        if (server && origin.isEmpty()) return nullptr;
        const Card *card = ViewAsSkillV2::createCard(request); if (card) card->setTag("MobileMouJingceOrigin", origin); return card;
    }
    bool willThrowSelectedCards() const override { return false; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && card && !card->isVirtualCard() && request.selectedCardIds.size() < 3 && !request.selectedCardIds.contains(card->getEffectiveId()) && request.initiator->getPile(objectName()).contains(card->getEffectiveId()); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { if (request.selectedCardIds.size() != 3) return false; ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear(); for (int id : request.selectedCardIds) { if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false; prefix.selectedCardIds << id; } return true; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &targets, const Player *target) const override { return target && target->isAlive() && targets.size() < 3; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() <= 3; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true; if (!ctx.invoker || !ctx.use_card || getEffectiveAmount(ctx) <= 0) return FinishSkill;
        QVariantMap predictions; const QList<int> ids = ctx.use_card->getSubcards();
        for (int i = 0; i < ctx.targets.size() && i < ids.size(); ++i) {
            ctx.choice = "predict"; ctx.extra_data = false; skillEffect(ctx, ctx.targets.at(i));
            if (ctx.extra_data.toBool()) predictions[QString::number(ids.at(i))] = ctx.targets.at(i)->objectName();
        }
        ctx.choice = "place"; ctx.extra_data = predictions; skillEffect(ctx, ctx.invoker); return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "predict") { ctx.extra_data = getEffectiveAmount(ctx) > 0; return ContinueEffects; }
        Room *room = target->getRoom(); if (!ctx.initiator || !ctx.use_card) return ContinueEffects;
        const QList<int> material = ctx.use_card->getSubcards(); if (material.size() != 3) return ContinueEffects;
        for (int id : material) if (!ctx.initiator->getPile(objectName()).contains(id)) return ContinueEffects;
        const QVariantMap predictions = ctx.extra_data.toMap(); const QList<int> ids = room->askForGuanxing(target, material, Room::GuanxingUpOnly, false);
        if (ids.size() != 3) return ContinueEffects; QSet<int> seen;
        for (int id : ids) { if (!material.contains(id) || seen.contains(id)) return ContinueEffects; seen.insert(id); }
        for (int i = 0; i < ids.size(); ++i) {
            const int id = ids.at(i); if (!ctx.initiator->getPile(objectName()).contains(id)) continue;
            const qint64 serial = room->getTag("MobileMouJingceSequence").toLongLong() + 1; room->setTag("MobileMouJingceSequence", serial);
            const QVariantMap origin = ctx.use_card->getTag("MobileMouJingceOrigin").toMap();
            QVariantMap receipt{{"main_owner",origin.value("owner")},{"main_instance",origin.value("instance")},{"serial", serial}, {"card", id}, {"target", predictions.value(QString::number(id))}, {"amount", (i + 1) * getEffectiveAmount(ctx)},
                {"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
            // Install before movement: nested card movement may already fulfill this prediction.
            QVariantList receipts = target->getTag("MobileMouJingcePredictions").toList(); receipts << receipt; target->setTag("MobileMouJingcePredictions", receipts);
            room->moveCardsInToDrawpile(ctx.initiator, id, objectName(), (i + 1) * 3, true);
        }
        return ContinueEffects;
    }
};

class MobileMouJingce : public TriggerSkillV2
{
public:
    MobileMouJingce() : TriggerSkillV2("mobilemoujingce") { events << CardsMoveOneTime << EventPhaseChanging << EventSkillEffectFinished << Death; global = true; frequency = Compulsory; view_as_skill = new MobileMouJingcevs; }
    void consume(const SkillContext &ctx) const
    { if (ctx.choice != "reward" || !ctx.invoker) return; QVariantList receipts = ctx.invoker->getTag("MobileMouJingcePredictions").toList(); receipts.removeAll(ctx.extra_data); ctx.invoker->setTag("MobileMouJingcePredictions", receipts); }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    { if (event == EventSkillEffectFinished) { const SkillContext done = data.value<SkillContext>(); if (done.skill_name == objectName()) consume(done); } else if (event == Death && data.value<DeathStruct>().who) data.value<DeathStruct>().who->removeTag("MobileMouJingcePredictions"); return false; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; } return TriggerSkillV2::prepareSource(room, ctx); }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardsMoveOneTime) return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>(); if (!actor || actor->isDead() || !move.to || move.to_place != Player::PlaceHand) return true;
        for (const QVariant &value : actor->getTag("MobileMouJingcePredictions").toList()) {
            const QVariantMap receipt = value.toMap(); if (!move.card_ids.contains(receipt.value("card").toInt()) || receipt.value("target").toString() != move.to->objectName()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true); ctx.initiator = ctx.owner; ctx.invoker = actor;
            ctx.instanceID = receipt.value("serial").toInt(); ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.choice = "reward"; ctx.extra_data = receipt; ctx.amount = receipt.value("amount").toInt(); ctx.is_forced = true; ctx.original_data = &data; ctx.current_event = event; ctx.targets << actor; contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.choice == "reward" ? ctx.invoker && ctx.invoker->getTag("MobileMouJingcePredictions").toList().contains(ctx.extra_data) : TriggerSkillV2::isSourceAvailable(room, ctx); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !actor || actor->isDead() || !actor->hasSkill(objectName())) return {};
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        return (change.to == Player::NotActive && actor->getPile(objectName()).size() >= 3) || (change.from == Player::NotActive && actor->getPile(objectName()).isEmpty()) ? TriggerList{{actor,{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.choice != "reward") { ctx.choice = ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive ? "place" : "fill"; ctx.targets << ctx.invoker; } return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { consume(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "reward") target->drawCards(getEffectiveAmount(ctx), objectName());
        else if (ctx.choice == "place") {
            const QVariant previous = target->getTag("MobileMouJingcePromptOrigin");
            const auto restoreOrigin = qScopeGuard([&] { target->setTag("MobileMouJingcePromptOrigin",previous); });
            target->setTag("MobileMouJingcePromptOrigin",QVariantMap{{"owner",ctx.activationRef.ownerObjectName},{"instance",ctx.activationRef.key.instanceID}});
            Room::AcceptedViewAsEffectScope prompt(room, target, objectName(), ctx); if (prompt.isValid()) room->askForUseCard(target, "@@mobilemoujingce", "mobilemoujingce0");
        }
        else {
            QVariantList kept; for (const QVariant &value : target->getTag("MobileMouJingcePredictions").toList()) { const QVariantMap receipt = value.toMap();
                if (receipt.value("main_owner").toString() != ctx.activationRef.ownerObjectName || receipt.value("main_instance").toInt() != ctx.activationRef.key.instanceID) kept << value; }
            target->setTag("MobileMouJingcePredictions", kept);
            const QList<int> ids = room->getNCards(3 * getEffectiveAmount(ctx));
            const auto restore = qScopeGuard([&] { QList<int> remaining; for (int id : ids) if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id; room->returnToTopDrawPile(remaining); });
            target->addToPile(objectName(), ids);
        }
        return false;
    }
};

MobileMouYuPackage::MobileMouYuPackage()
	: Package("mobilemouyu")
{
	General *mobilemou_huangzhong = new General(this, "mobilemou_huangzhong", "shu", 4);
	mobilemou_huangzhong->addSkill(new MobileMouLiegong);
	mobilemou_huangzhong->addSkill(new MobileMouLiegongEffect);
	mobilemou_huangzhong->addSkill(new MobileMouLiegongLimit);
	mobilemou_huangzhong->addSkill("#tenyearliegongmod");
	related_skills.insert("mobilemouliegong", "#mobilemouliegong");
	related_skills.insert("mobilemouliegong", "#mobilemouliegong-limit");
	related_skills.insert("mobilemouliegong", "#tenyearliegongmod");

	General *mobilemou_lvmeng = new General(this, "mobilemou_lvmeng", "wu", 4);
	mobilemou_lvmeng->addSkill(new MobileMouKeji);
	mobilemou_lvmeng->addSkill(new MobileMouKejiMax);
	mobilemou_lvmeng->addSkill(new MobileMouKejiLimit);
	mobilemou_lvmeng->addSkill(new MobileMouDujiang);
	mobilemou_lvmeng->addRelateSkill("mobilemouduojing");
	related_skills.insert("mobilemoukeji", "#mobilemoukeji-max");
	related_skills.insert("mobilemoukeji", "#mobilemoukeji-limit");

	General *mobilemou_yujin = new General(this, "mobilemou_yujin", "wei", 4);
	mobilemou_yujin->addSkill(new MobileMouXiayuan);
	mobilemou_yujin->addSkill(new MobileMouJieyue);

	addMetaObject<MobileMouKejiCard>();

	skills << new MobileMouDuojiang;

	General *mobilemou_gaoshun = new General(this, "mobilemou_gaoshun", "qun", 4);
	mobilemou_gaoshun->addSkill(new MobileMouXianzhen);
	mobilemou_gaoshun->addSkill(new MobileMouJinjiu);
	mobilemou_gaoshun->addSkill(new MobileMouJinjiuLimit);
	mobilemou_gaoshun->addSkill(new MobileMouJinjiuLimit2);
	addMetaObject<MobileMouXianzhenCard>();

	General *mobilemou_luxun = new General(this, "mobilemou_luxun", "wu", 3);
	mobilemou_luxun->addSkill(new MobileMouQianxun);
	mobilemou_luxun->addSkill(new MobileMouLianying);

	General *mobilemou_guohuai = new General(this, "mobilemou_guohuai", "wei", 4);
	mobilemou_guohuai->addSkill(new MobileMouJingce);
	addMetaObject<MobileMouJingceCard>();

}
ADD_PACKAGE(MobileMouYu)


MobileMouYangweiCard::MobileMouYangweiCard()
{
    setSkillName("mobilemouyangwei");
	target_fixed = true;
}

void MobileMouYangweiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->drawCards(2, "mobilemouyangwei");
	room->addPlayerMark(source, "mobilemouyangwei-PlayClear");
	room->setPlayerMark(source, "mobilemouyangweiUsed", 2);
}

class MobileMouYangweiVS : public ViewAsSkillV2
{
public:
    MobileMouYangweiVS() : ViewAsSkillV2("mobilemouyangwei") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouYangweiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
        && request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName, request.activationRef.key.instanceID, "cooldown").toInt() <= 0; }
    EffectFlow effect(SkillContext &ctx) const override { ctx.manual_effect = true; return skillEffect(ctx, ctx.invoker); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); ServerPlayer *holder = room->findPlayerByObjectName(ctx.activationRef.ownerObjectName, true);
        if (holder) holder->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "cooldown", 2);
        QVariantList receipts = target->getTag("MobileMouYangweiEffects").toList();
        const qint64 serial = room->getTag("MobileMouYangweiSequence").toLongLong() + 1; room->setTag("MobileMouYangweiSequence", serial);
        receipts << QVariantMap{{"serial", serial}, {"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
        target->setTag("MobileMouYangweiEffects", receipts); room->addPlayerMark(target, "mobilemouyangwei-PlayClear", getEffectiveAmount(ctx));
        target->drawCards(2 * getEffectiveAmount(ctx), objectName()); return ContinueEffects;
    }
};

class MobileMouYangwei : public TriggerSkillV2
{
public:
    MobileMouYangwei() : TriggerSkillV2("mobilemouyangwei") { events << EventPhaseStart << EventPhaseChanging << Death; global = true; view_as_skill = new MobileMouYangweiVS; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventPhaseStart && actor && actor->getPhase() == Player::Finish) {
            for (const SkillInstance &instance : actor->getSkillInstances()) if (instance.skillName == objectName()) {
                const int remaining = actor->getSkillInstanceStateValue(objectName(), instance.instanceID, "cooldown").toInt();
                if (remaining > 0) actor->setSkillInstanceStateValue(objectName(), instance.instanceID, "cooldown", remaining - 1);
            }
        } else if (event == Death || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play)) {
            ServerPlayer *target = event == Death ? data.value<DeathStruct>().who : actor;
            if (target) { target->removeTag("MobileMouYangweiEffects"); room->setPlayerMark(target, "mobilemouyangwei-PlayClear", 0); }
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class MobileMouYangweiEffect : public TriggerSkillV2
{
public:
    MobileMouYangweiEffect() : TriggerSkillV2("#mobilemouyangwei") { events << CardUsed; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!actor || actor->isDead() || actor != use.from || !use.card || !use.card->isKindOf("Slash")) return true;
        for (const QVariant &value : actor->getTag("MobileMouYangweiEffects").toList()) {
            const QVariantMap receipt = value.toMap(); SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true); ctx.initiator = ctx.owner; ctx.invoker = actor;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.current_event = event; ctx.original_data = &data; ctx.extra_data = receipt; ctx.amount = receipt.value("amount").toInt(); ctx.is_forced = true;
            ctx.instanceID = receipt.value("serial").toInt(); ctx.trigger_count = 0; ctx.targets = use.to; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getTag("MobileMouYangweiEffects").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (getEffectiveAmount(ctx) > 0 && use.card && use.to.contains(target)) target->addQinggangTag(use.card); return false;
    }
};

class MobileMouYangweiTargetMod : public TargetModSkillV2
{
public:
    MobileMouYangweiTargetMod() : TargetModSkillV2("#mobilemouyangwei-target", ".") { setHolderSelector(CorrectSkill_System); frequency = NotFrequent; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *from = ctx.primary; if (!from || from->getPhase() != Player::Play || !ctx.card) return CorrectSkillResult::noEffect();
        const int amount = from->getMark("mobilemouyangwei-PlayClear");
        if (ctx.modType == Residue && ctx.card->isKindOf("Slash")) return CorrectSkillResult::useAmount(amount);
        if (ctx.modType == DistanceLimit && ((ctx.card->isKindOf("Slash") && amount > 0)
            || (ctx.secondary && from->getMark(ctx.secondary->objectName() + "mobilemouxianzhenTo-PlayClear") > 0))) return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

MobileMouTiaoxinCard::MobileMouTiaoxinCard()
{
    setSkillName("mobilemoutiaoxin");
}

bool MobileMouTiaoxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length()<Self->getMark("&charge_num") && to_select!=Self;
}

void MobileMouTiaoxinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->loseMark("&charge_num",targets.length());
	foreach (ServerPlayer *p, targets){
		source->addMark("mobilemoutiaoxinTo"+p->objectName());
		if(room->askForUseSlashTo(p,source,"mobilemoutiaoxin0:"+source->objectName(),false))
			continue;
		const Card*dc = room->askForExchange(p,"mobilemoutiaoxin",1,1,true,"mobilemoutiaoxin1:"+source->objectName());
		if(dc) room->giveCard(p,source,dc,"mobilemoutiaoxin");
	}
}

class MobileMouTiaoxinvs : public ViewAsSkillV2
{
public:
    MobileMouTiaoxinvs() : ViewAsSkillV2("mobilemoutiaoxin") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouTiaoxinCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getMark("&charge_num") > 0; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && target && target->isAlive() && target != request.initiator && !selected.contains(target) && selected.size() < request.initiator->getMark("&charge_num"); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return request.initiator && !targets.isEmpty() && targets.size() <= request.initiator->getMark("&charge_num"); }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    { const int count = ctx.targets.size(); if (!ctx.initiator || count <= 0 || ctx.initiator->getMark("&charge_num") < count) return false; ctx.initiator->loseMark("&charge_num", count); return true; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return ContinueEffects;
        if (ctx.choice == "give") {
            const QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            QList<int> ids; if (from) for (const QVariant &value : receipt.value("ids").toList())
                if (room->getCardOwner(value.toInt()) == from && (room->getCardPlace(value.toInt()) == Player::PlaceHand || room->getCardPlace(value.toInt()) == Player::PlaceEquip)) ids << value.toInt();
            if (!ids.isEmpty()) { DummyCard cards(ids); room->giveCard(from, target, &cards, objectName()); } return ContinueEffects;
        }
        if (!ctx.invoker || ctx.invoker->isDead() || room->askForUseSlashTo(target, ctx.invoker, "mobilemoutiaoxin0:" + ctx.invoker->objectName(), false)) return ContinueEffects;
        if (target->isDead() || ctx.invoker->isDead()) return ContinueEffects;
        const Card *gift = room->askForExchange(target, objectName(), amount, amount, true, "mobilemoutiaoxin1:" + ctx.invoker->objectName());
        if (gift) { ctx.extra_data = QVariantMap{{"from", target->objectName()}, {"ids", ListI2V(gift->getSubcards())}}; ctx.choice = "give"; skillEffect(ctx, ctx.invoker); ctx.choice.clear(); }
        return ContinueEffects;
    }
};

	static int getChargeMax(const Player *player)
	{
		int n = 0;
		foreach (const Skill *s, player->getVisibleSkillList()){
			QString cn = s->property("ChargeNum").toString();
			if(cn.contains("/")) n += cn.split("/").last().toInt();
		}
		return n;
	}

class MobileMouTiaoxin : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouTiaoxin() : TriggerSkillV2("mobilemoutiaoxin") { events << CardsMoveOneTime; view_as_skill = new MobileMouTiaoxinvs; setProperty("ChargeNum", "4/4"); frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return actor && actor->isAlive() && actor->hasSkill(objectName()) && move.from == actor && actor->getPhase() == Player::Discard
            && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
            && getChargeMax(actor) > actor->getMark("&charge_num") ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = qMin(ctx.original_data->value<CardsMoveOneTimeStruct>().card_ids.size() * getEffectiveAmount(ctx), getChargeMax(target) - target->getMark("&charge_num"));
        if (count > 0) target->gainMark("&charge_num", count); return false;
    }
};

class MobileMouZhiji : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouZhiji() : TriggerSkillV2("mobilemouzhiji") { events << EventPhaseStart << EventSkillInvoking; frequency = Wake; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    int selectedCount(Room *room, ServerPlayer *player) const
    {
        QVariantMap query{{"kind", "use_card"}, {"from", player->objectName()}, {"limit", 128}}; QStringList targets; QVariant watermark;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query); if (page.contains("error") || !page.value("complete").toBool()) return -1;
            if (!watermark.isValid()) watermark = page.value("watermark");
            for (const QVariant &value : page.value("items").toList()) { const QVariantMap fact = value.toMap().value("data").toMap();
                if (fact.value("history_key").toString() != "MobileMouTiaoxinCard") continue;
                for (const QVariant &name : fact.value("targets").toList()) if (!targets.contains(name.toString())) targets << name.toString();
            }
            if (!page.value("has_more").toBool()) break; query["after"] = page.value("next_after"); query["watermark"] = watermark;
        }
        return targets.size();
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    { if (event == EventSkillInvoking) { const SkillContext accepted = data.value<SkillContext>(); if (accepted.bypass_cost && accepted.skill_name == objectName() && accepted.activationRef.key.skillName == objectName()) addUsage(accepted); } return false; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &) const override
    { return event == EventPhaseStart && actor && actor->isAlive() && actor->getPhase() == Player::Start && actor->hasSkill(objectName()) && (actor->canWake(objectName()) || selectedCount(room, actor) >= 4) ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "beifa") {
            if (getEffectiveAmount(ctx) <= 0) return false;
            QVariantList receipts = target->getTag("MobileMouZhijiEffects").toList();
            receipts << QVariantMap{{"beneficiary", ctx.invoker->objectName()}, {"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
            target->setTag("MobileMouZhijiEffects", receipts); room->addPlayerMark(target, "&mou_beifa+#" + ctx.invoker->objectName(), getEffectiveAmount(ctx)); return false;
        }
        room->doSuperLightbox(target, objectName()); room->setPlayerMark(target, objectName(), 1);
        room->changeMaxHpForAwakenSkill(target, -getEffectiveAmount(ctx), objectName());
        if (target->isDead()) return false;
        const QList<ServerPlayer *> recipients = room->askForPlayersChosen(target, room->getAlivePlayers(), objectName(), 1, room->alivePlayerCount(), "mobilemouzhiji0");
        ctx.choice = "beifa"; for (ServerPlayer *recipient : recipients) if (recipient->isAlive()) skillEffect(event, room, actor, ctx, recipient); return false;
    }
};

class MobileMouZhijiBf : public TriggerSkillV2
{
public:
    MobileMouZhijiBf() : TriggerSkillV2("#MobileMouZhijiBf") { events << EventPhaseStart << Death; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        ServerPlayer *beneficiary = event == Death ? data.value<DeathStruct>().who : actor;
        if (!beneficiary || (event != Death && beneficiary->getPhase() != Player::RoundStart)) return false;
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            QVariantList kept; for (const QVariant &value : target->getTag("MobileMouZhijiEffects").toList()) if (value.toMap().value("beneficiary").toString() != beneficiary->objectName()) kept << value;
            target->setTag("MobileMouZhijiEffects", kept); room->setPlayerMark(target, "&mou_beifa+#" + beneficiary->objectName(), 0);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class MobileMouZhijiPro : public ProhibitSkill
{
public:
    MobileMouZhijiPro() : ProhibitSkill("#MobileMouZhijiPro") {}
    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        if (!from || !to || from == to || !card || card->getTypeId() == Card::TypeSkill) return false;
        for (const Player *beneficiary : from->getAliveSiblings())
            if (to != beneficiary && from->getMark("&mou_beifa+#" + beneficiary->objectName()) > 0) return true;
        // A beneficiary may also select itself for Beifa; that holder can only target itself.
        return from->getMark("&mou_beifa+#" + from->objectName()) > 0;
    }
};

class MobileMouYicong : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouYicong() : TriggerSkillV2("mobilemouyicong") { events << RoundStart; global = true; setProperty("ChargeNum", "2/4"); }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        // RoundStart is broadcast once to every player; expire only that player's prior-round projection.
        if (actor) { actor->removeTag("MobileMouYicongEffects"); room->setPlayerMark(actor, "mouyicong1_lun", 0); room->setPlayerMark(actor, "mouyicong2_lun", 0); }
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getMark("&charge_num") > 0 && actor->getPile("&mou_hu").size() < 4 ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.bypass_cost && !room->askForSkillInvoke(ctx.owner, objectName())) return false;
        QStringList counts; const int cap = qMin(ctx.owner->getMark("&charge_num"), 4 - ctx.owner->getPile("&mou_hu").size());
        for (int i = 1; i <= cap; ++i) counts << QString::number(i); if (counts.isEmpty()) return false;
        const QString selected = room->askForChoice(ctx.owner, objectName(), counts.join("+")); if (!counts.contains(selected)) return false;
        ctx.extra_data = selected.toInt(); ctx.choice = room->askForChoice(ctx.owner, objectName(), "mouyicong1+mouyicong2"); ctx.targets << ctx.invoker;
        return ctx.choice == "mouyicong1" || ctx.choice == "mouyicong2";
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { const int count = ctx.extra_data.toInt(); if (!ctx.initiator || count <= 0 || ctx.initiator->getMark("&charge_num") < count) return false; ctx.initiator->loseMark("&charge_num", count); return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (!ctx.extra_data.isValid() && !cost(event, room, actor, ctx)) return false;
        return skillEffect(event, room, actor, ctx, ctx.invoker);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        QVariantList receipts = target->getTag("MobileMouYicongEffects").toList();
        receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}, {"choice", ctx.choice}, {"amount", amount}};
        target->setTag("MobileMouYicongEffects", receipts); room->addPlayerMark(target, ctx.choice + "_lun", amount);
        QList<int> ids; const int count = qMin(4 - target->getPile("&mou_hu").size(), ctx.extra_data.toInt() * amount);
        for (int id : room->getDrawPile()) if (ids.size() < count && Sanguosha->getCard(id)->isKindOf(ctx.choice == "mouyicong1" ? "Slash" : "Jink")) ids << id;
        if (!ids.isEmpty()) target->addToPile("&mou_hu", ids);
        return false;
    }
};

class MobileMouYicongBf : public DistanceSkillV2
{
public:
    MobileMouYicongBf() : DistanceSkillV2("#MobileMouYicongBf") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // Applied round effects outlive their source; their public amounts are the correction projection.
        if (!ctx.primary || !ctx.secondary) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(ctx.secondary->getMark("mouyicong2_lun") - ctx.primary->getMark("mouyicong1_lun"));
    }
};

class MobileMouQiaomeng : public TriggerSkillV2
{
public:
	MobileMouQiaomeng() : TriggerSkillV2("mobilemouqiaomeng")
	{
		events << Damage;
	}
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && player->hasSkill("mobilemouyicong", true)
            && damage.from == player && damage.card && damage.card->isKindOf("Slash")
            && (getChargeMax(player) > player->getMark("&charge_num") || (damage.to && player->canDiscard(damage.to, "hej")))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
        const int id = victim && victim->isAlive() && ctx.owner->canDiscard(victim, "hej")
            ? room->askForCardChosen(ctx.owner, victim, "hej", objectName(), false, Card::MethodDiscard, {}, true) : -1;
        ctx.extra_data = id;
        ctx.choice = id >= 0 ? "discard" : "charge";
        ctx.targets = {id >= 0 ? victim : ctx.owner};
        return id >= 0 || getChargeMax(ctx.owner) > ctx.owner->getMark("&charge_num");
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        ctx.owner->peiyin(this);
        if (ctx.choice == "charge") {
            const int count = qMin(3 * getEffectiveAmount(ctx), getChargeMax(target) - target->getMark("&charge_num"));
            if (count > 0) target->gainMark("&charge_num", count);
        } else {
            const int id = ctx.extra_data.toInt();
            // A redirected recipient does not authorize discarding the original victim's material.
            if (room->getCardOwner(id) != target || !ctx.owner->canDiscard(target, id)) return false;
            room->throwCard(id, objectName(), target, ctx.owner);
            if (ctx.owner->isAlive()) { ctx.choice = "draw"; skillEffect(event, room, actor, ctx, ctx.owner); }
        }
        return false;
    }
};

class MobileMouHuanshi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouHuanshi() : TriggerSkillV2("mobilemouhuanshi") { events << AskForRetrial; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result; JudgeStruct *judge = data.value<JudgeStruct *>();
        if (judge && judge->who && judge->who->isAlive() && actor && actor->isAlive() && actor->hasSkill(objectName()))
            result[actor] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>(); if (!judge || !judge->who) return false;
        const QList<int> top = room->getNCards(1);
        // Restore immediately: a canceled or intercepted invocation never strands a detached deck card.
        room->returnToTopDrawPile(top);
        room->fillAG(top, ctx.owner);
        const auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
        if (!ctx.bypass_cost && !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        room->clearAG(ctx.owner);
        QList<int> candidates = ctx.owner->handCards();
        if (!top.isEmpty() && !room->getDrawPile().isEmpty() && room->getDrawPile().first() == top.first()) candidates << top.first();
        if (candidates.isEmpty()) return false;
        const QVariant oldJudge = ctx.owner->getTag("mobilemouhuanshiJudge"); ctx.owner->setTag("mobilemouhuanshiJudge", *ctx.original_data);
        const auto restore = qScopeGuard([&] { ctx.owner->setTag("mobilemouhuanshiJudge", oldJudge); });
        room->fillAG(candidates, ctx.owner);
        const int id = room->askForAG(ctx.owner, candidates, false, objectName(), "mobilemouhuanshi0");
        if (!candidates.contains(id)) return false;
        ctx.extra_data = QVariantMap{{"id", id}, {"top", !top.isEmpty() && id == top.first()}};
        ctx.targets << judge->who; return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (!ctx.extra_data.isValid() && !cost(event, room, actor, ctx)) return false;
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>(); if (!judge) return false;
        return skillEffect(event, room, actor, ctx, judge->who);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (!ctx.original_data || !ctx.invoker || !ctx.initiator) return false;
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>(); if (!judge || !judge->who || !judge->card) return false;
        const QVariantMap receipt = ctx.extra_data.toMap(); const int id = receipt.value("id", -1).toInt();
        const bool fromTop = receipt.value("top").toBool();
        if (id < 0) return false;
        if (!fromTop && ctx.choice != "exchange") {
            if (target != judge->who) return false;
            ctx.choice = "exchange";
            skillEffect(event, room, actor, ctx, ctx.invoker);
            return false;
        }
        if (fromTop) {
            if (target != judge->who || room->getDrawPile().isEmpty() || room->getDrawPile().first() != id) return false;
            if (room->getCardPlace(judge->card->getEffectiveId()) != Player::PlaceJudge) return false;
            const int oldJudge = judge->card->getEffectiveId();
            room->moveCardTo(judge->card, nullptr, Player::DrawPile, true);
            if (room->getCardPlace(id) != Player::DrawPile || !room->getDrawPile().contains(id)) {
                // The replacement disappeared during movement callbacks. Restore only our still-unclaimed old judge.
                if (judge->card->getEffectiveId() == oldJudge && room->getCardPlace(oldJudge) == Player::DrawPile && room->getDrawPile().contains(oldJudge))
                    room->moveCardTo(Sanguosha->getCard(oldJudge), judge->who, Player::PlaceJudge, true);
                return false;
            }
        } else if (target != ctx.invoker || !ctx.initiator->handCards().contains(id)) return false;
        ctx.invoker->peiyin(this);
        room->retrial(Sanguosha->getCard(id), ctx.invoker, judge, objectName(), !fromTop);
        return false;
    }
};

class MobileMouHongyuan : public TriggerSkillV2
{
public:
	MobileMouHongyuan() : TriggerSkillV2("mobilemouhongyuan")
	{
		events << CardsMoveOneTime;
		setProperty("ChargeNum","1/3");
	}
    bool receivesCards(const CardsMoveOneTimeStruct &move, const Player *owner) const
    { return move.to == owner && move.to_place == Player::PlaceHand && move.card_ids.size() > 1; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName()) || player->getMark("&charge_num") < 1) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (receivesCards(move, player)) return {{player, {objectName()}}};
        if (!move.from || move.from == player || move.from->isDead()) return {};
        int count = 0;
        for (Player::Place place : move.from_places)
            if (place == Player::PlaceHand || place == Player::PlaceEquip) ++count;
        return count > 1 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (receivesCards(move, ctx.owner)) {
            ctx.targets = room->askForPlayersChosen(ctx.owner, room->getAlivePlayers(), objectName(), 0, 2,
                "mobilemouhongyuan0", true);
            ctx.extra_data = 1;
            return !ctx.targets.isEmpty();
        }
        ServerPlayer *from = room->findPlayerByObjectName(move.from->objectName());
        if (!from || from->isDead() || !ctx.owner->askForSkillInvoke(this, from)) return false;
        ctx.targets = {from};
        ctx.extra_data = isNormalGameMode(room->getMode()) ? 1 : 2;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getMark("&charge_num") < 1) return false;
        // Charge is shared public currency; unlike usage it is not instance-private.
        ctx.owner->loseMark("&charge_num");
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->peiyin(this);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MobileMouMingzhe : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }

	MobileMouMingzhe() : TriggerSkillV2("mobilemoumingzhe")
	{
		events << CardsMoveOneTime << EventSkillInvoking;
		frequency = Compulsory;
	}
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Round; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && !player->hasFlag("CurrentPlayer")
            && move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QVariantMap parent = room->historyParent(room->currentHistoryEventId(), "move_cards", true);
        if (parent.value("id").toString().isEmpty() || parent.value("id").toString() == "0") return false;
        QVariantMap filter{{"event_id", parent.value("id")}, {"from", ctx.owner->objectName()}, {"limit", 128}};
        bool draw = false;
        QSet<int> observed;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()
               ) return false;
            foreach (const QVariant &item, page.value("items").toList()) {
                const QVariantMap fact = item.toMap().value("data").toMap();
                const int id = fact.value("card_id", -1).toInt();
                if (!move.card_ids.contains(id)) continue;
                const QVariantMap before = fact.value("card_before").toMap();
                if (!before.contains("type")) return false;
                observed.insert(id);
                if (before.value("type").toInt() > Card::TypeBasic) draw = true;
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        foreach (int id, move.card_ids) {
            if (!observed.contains(id)) return false;
        }
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "mobilemoumingzhe0");
        if (!target) return false;
        ctx.targets = {target};
        // Moving cards may already have lost their filter; use their immutable source type.
        ctx.extra_data = draw;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.targets.isEmpty() && !cost(event, room, actor, ctx)) return false;
        return skillEffect(event, room, actor, ctx, ctx.targets.value(0, nullptr));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        room->doAnimate(1, ctx.owner->objectName(), target->objectName());
        for (const Skill *skill : target->getVisibleSkillList()) {
            if (skill->property("ChargeNum").toString().contains("/")) {
                const int count = qMin(getEffectiveAmount(ctx), getChargeMax(target) - target->getMark("&charge_num"));
                if (count > 0) target->gainMark("&charge_num", count);
                break;
            }
        }
        if (ctx.extra_data.toBool()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MobileMouWushuang : public TriggerSkillV2
{
public:
    MobileMouWushuang() : TriggerSkillV2("mobilemouwushuang") { events << TargetSpecified << CardResponded << CardUsed << ConfirmDamage << CardFinished; frequency = Compulsory; global = true; }
    QString key(Room *room) const
    { const qint64 id = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong(); return id > 0 ? "MobileMouWushuang:" + QString::number(id) : QString(); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        const QString receiptKey = key(room); if (receiptKey.isEmpty()) return false;
        const Card *card = nullptr;
        if (event == CardFinished) { card = data.value<CardUseStruct>().card; if (card) card->removeTag(receiptKey); return false; }
        if (event == CardResponded) { const CardResponseStruct response = data.value<CardResponseStruct>(); if (response.m_card && response.m_card->isKindOf("Slash")) card = response.m_toCard; }
        else if (event == CardUsed) { const CardUseStruct use = data.value<CardUseStruct>(); if (use.card && use.card->isKindOf("Jink")) card = use.whocard; }
        if (card && actor) { QVariantMap receipt = card->getTag(receiptKey).toMap(); if (receipt.isEmpty()) return false;
            QStringList used = receipt.value("used").toStringList(); if (!used.contains(actor->objectName())) used << actor->objectName(); receipt["used"] = used; card->setTag(receiptKey, receipt); }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardResponded && event != ConfirmDamage) return false;
        const Card *card = event == CardResponded ? data.value<CardResponseStruct>().m_toCard : data.value<DamageStruct>().card;
        ServerPlayer *target = event == CardResponded ? actor : data.value<DamageStruct>().to;
        const QString receiptKey = key(room); if (!card || !target || target->isDead() || receiptKey.isEmpty()) return true;
        const QVariantMap state = card->getTag(receiptKey).toMap();
        if (event == CardResponded && (!card->isKindOf("Duel") || state.value("responding").toStringList().contains(target->objectName()))) return true;
        if (event == ConfirmDamage && state.value("used").toStringList().contains(target->objectName())) return true;
        QVariantList receipts;
        if (event == CardResponded) {
            QVariantMap strongest;
            for (const QVariant &value : state.value("sources").toList()) { const QVariantMap receipt = value.toMap();
                if (receipt.value("target").toString() == target->objectName() && receipt.value("amount").toInt() > strongest.value("amount").toInt()) strongest = receipt; }
            if (!strongest.isEmpty()) receipts << strongest;
        } else receipts = state.value("sources").toList();
        for (const QVariant &value : receipts) {
            QVariantMap receipt = value.toMap(); if (receipt.value("target").toString() != target->objectName()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true); ctx.initiator = ctx.owner; ctx.invoker = target;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.instanceID = receipt.value("serial").toInt(); ctx.amount = receipt.value("amount").toInt(); receipt["key"] = receiptKey; ctx.extra_data = receipt;
            ctx.current_event = event; ctx.choice = event == CardResponded ? "respond" : "damage"; ctx.original_data = &data; ctx.is_forced = true; ctx.targets << target; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.original_data || !ctx.invoker || ctx.invoker->isDead()) return false;
        const Card *card = ctx.choice == "respond" ? ctx.original_data->value<CardResponseStruct>().m_toCard : ctx.original_data->value<DamageStruct>().card;
        if (!card) return false; QVariantMap receipt = ctx.extra_data.toMap(); const QString receiptKey = receipt.take("key").toString();
        return card->getTag(receiptKey).toMap().value("sources").toList().contains(receipt);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result; if (event != TargetSpecified || !actor || actor->isDead()) return result; const CardUseStruct use = data.value<CardUseStruct>(); if (!use.card) return result;
        if ((use.card->isKindOf("Slash") || use.card->isKindOf("Duel")) && actor->hasSkill(objectName())) result[actor] << objectName();
        if (use.card->isKindOf("Duel")) for (ServerPlayer *target : use.to) if (target != actor && target->isAlive() && target->hasSkill(objectName())) result[target] << objectName();
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (event != TargetSpecified) return skillEffect(event, room, actor, ctx, ctx.invoker);
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const QList<ServerPlayer *> targets = ctx.owner == use.from ? use.to : QList<ServerPlayer *>{use.from};
        for (ServerPlayer *target : targets) if (target && target->isAlive()) skillEffect(event, room, actor, ctx, target); return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == ConfirmDamage) { DamageStruct damage = ctx.original_data->value<DamageStruct>(); if (damage.to == target) { damage.damage += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(damage); } return false; }
        if (event == CardResponded) {
            CardResponseStruct response = ctx.original_data->value<CardResponseStruct>(); const Card *card = response.m_toCard; if (!card || !response.m_who) return false;
            const QString receiptKey = ctx.extra_data.toMap().value("key").toString(); QVariantMap state = card->getTag(receiptKey).toMap(); const QStringList previous = state.value("responding").toStringList();
            QStringList responding = previous; responding << target->objectName(); state["responding"] = responding; card->setTag(receiptKey, state);
            const auto restore = qScopeGuard([&] { QVariantMap current = card->getTag(receiptKey).toMap(); current["responding"] = previous; card->setTag(receiptKey, current); });
            CardEffectStruct effect; effect.card = card; effect.from = response.m_who; effect.to = target;
            for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i)
                if (!room->askForCard(target, "slash", "duel-slash:" + response.m_who->objectName(), QVariant::fromValue(effect), Card::MethodResponse, response.m_who, false, "", false, card)) {
                    response.nullified = true; *ctx.original_data = QVariant::fromValue(response); break; }
            return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>(); const QString receiptKey = key(room); const int amount = getEffectiveAmount(ctx);
        if (!use.card || !use.from || receiptKey.isEmpty() || amount <= 0) return false;
        if (use.card->isKindOf("Slash")) {
            QVariantList jinks = use.from->getTag("Jink_" + use.card->toString()).toList(); const int index = use.to.indexOf(target);
            if (index >= 0 && index < jinks.size() && jinks.at(index).toInt() > 0) { jinks[index] = qMax(jinks.at(index).toInt(), 1 + amount); use.from->setTag("Jink_" + use.card->toString(), jinks); }
        }
        const qint64 serial = room->getTag("MobileMouWushuangSequence").toLongLong() + 1; room->setTag("MobileMouWushuangSequence", serial);
        QVariantMap state = use.card->getTag(receiptKey).toMap(); QVariantList sources = state.value("sources").toList();
        sources << QVariantMap{{"serial", serial}, {"target", target->objectName()}, {"amount", amount}, {"owner", ctx.activationRef.ownerObjectName}, {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        state["sources"] = sources; use.card->setTag(receiptKey, state); return false;
    }
};

class MobileMouLiyu : public TriggerSkillV2
{
public:
    MobileMouLiyu() : TriggerSkillV2("mobilemouliyu") { events << Damage << EventPhaseChanging << Death; global = true; }
    bool prepareSource(Room *room, SkillContext &ctx) const override { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; return TriggerSkillV2::prepareSource(room, ctx); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event != EventPhaseChanging && event != Death) return false;
        ServerPlayer *target = event == Death ? data.value<DeathStruct>().who : actor; if (!target) return false;
        const PhaseChangeStruct change = event == EventPhaseChanging ? data.value<PhaseChangeStruct>() : PhaseChangeStruct(); QVariantList kept; QList<int> expired;
        for (const QVariant &value : target->getTag("MobileMouLiyuGrants").toList()) {
            QVariantMap receipt = value.toMap(); const bool expire = event == Death || (change.to == Player::NotActive && receipt.value("entered").toBool());
            if (expire) expired << receipt.value("instance").toInt();
            else { if (change.from == Player::NotActive) receipt["entered"] = true; kept << receipt; }
        }
        target->setTag("MobileMouLiyuGrants", kept);
        for (int id : expired) if (target->hasSkillInstance("wushuang", id)) room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName("wushuang", id));
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event != Damage || !actor || actor->isDead() || !actor->hasSkill(objectName())) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.from == actor && damage.to && damage.to != actor && damage.to->isAlive() && !damage.to->hasFlag("Global_DebutFlag") && !damage.to->isAllNude() && damage.card && damage.card->isKindOf("Slash") ? TriggerList{{actor,{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>(); if (!ctx.owner->askForSkillInvoke(this, damage.to)) return false;
        const QVariant id = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id"); if (id.toLongLong() <= 0) return false;
        const QVariantMap page = room->queryActualDamage({{"event_id", id}, {"from", ctx.owner->objectName()}, {"to", damage.to->objectName()}, {"limit", 1}});
        if (page.contains("error") || !page.value("complete").toBool() || page.value("has_more").toBool() || page.value("items").toList().isEmpty()) return false;
        ctx.extra_data = page.value("items").toList().first().toMap().value("data").toMap().value("amount"); ctx.targets << damage.to; return ctx.extra_data.toInt() > 0;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "obtain") {
            QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *victim = room->findPlayerByObjectName(receipt.value("victim").toString()); if (!victim) return false;
            QList<int> ids; for (const QVariant &value : receipt.value("selected").toList()) { const int id = value.toInt(); const Player::Place place = room->getCardPlace(id);
                if (room->getCardOwner(id) == victim && (place == Player::PlaceHand || place == Player::PlaceEquip || place == Player::PlaceDelayedTrick)) ids << id; }
            const QVariantMap before = room->queryHistoryMoves({{"limit",1}}); if (before.contains("error") || !before.value("complete").toBool() || ids.isEmpty()) return false;
            DummyCard cards(ids); room->obtainCard(target, &cards, false);
            QVariantMap query{{"after", before.value("watermark")}, {"from", victim->objectName()}, {"to", target->objectName()}, {"limit",128}}; QVariant watermark; QSet<int> obtained;
            for (;;) { const QVariantMap page = room->queryHistoryMoves(query); if (page.contains("error") || !page.value("complete").toBool()) return false;
                if (!watermark.isValid()) watermark = page.value("watermark");
                for (const QVariant &value : page.value("items").toList()) { const QVariantMap move = value.toMap().value("data").toMap(); const int id = move.value("card_id", -1).toInt(); if (ids.contains(id) && move.value("to_place").toInt() == Player::PlaceHand) obtained.insert(id); }
                if (!page.value("has_more").toBool()) break; query["after"] = page.value("next_after"); query["watermark"] = watermark;
            }
            QVariantList actual; for (int id : obtained) actual << id; receipt["obtained"] = actual; ctx.extra_data = receipt; return false;
        }
        if (ctx.choice == "draw") {
            QVariantMap receipt = ctx.extra_data.toMap(); QVariantList cards = receipt.value("obtained").toList(); const QList<int> drawn = target->drawCardsList(cards.size(), objectName()); for (int id : drawn) cards << id;
            QStringList types; const QVariantMap frozen = receipt.value("types").toMap(); for (const QVariant &value : cards) { const int id = value.toInt(); const QString type = frozen.contains(QString::number(id)) ? frozen.value(QString::number(id)).toString() : Sanguosha->getCard(id)->getType(); if (!types.contains(type)) types << type; }
            if (types.size() < 3 || target->isDead() || ctx.invoker->isDead()) return false;
            Duel *duel = new Duel(Card::NoSuit, 0); CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(duel)); duel->deleteLater(); duel->setSkillName(objectName());
            QList<ServerPlayer *> candidates; for (ServerPlayer *p : room->getOtherPlayers(ctx.invoker)) if (p != target && ctx.invoker->canUse(duel,p)) candidates << p;
            ServerPlayer *chosen = room->askForPlayerChosen(target, candidates, objectName(), "mobilemouliyu0:" + ctx.invoker->objectName(), true);
            if (chosen) { SkillContext follow = ctx; follow.choice = "duel"; skillEffect(event, room, actor, follow, chosen); }
            else {
                room->acquireSkillFromEffect(target, "wushuang", ctx, [target](int id) { QVariantList grants = target->getTag("MobileMouLiyuGrants").toList(); grants << QVariantMap{{"instance",id},{"entered",false}}; target->setTag("MobileMouLiyuGrants",grants); });
            }
            return false;
        }
        if (ctx.choice == "duel") {
            if (getEffectiveAmount(ctx) <= 0 || ctx.invoker->isDead()) return false;
            Duel *duel = new Duel(Card::NoSuit,0); CardLifetimeLease lease(globalCardLifetimeManager(),globalCardLifetimeManager().observeCard(duel)); duel->deleteLater(); duel->setSkillName(objectName());
            if (ctx.invoker->canUse(duel,target)) room->useCardFromSkillEffect(CardUseStruct(duel,ctx.invoker,target),ctx); return false;
        }
        const int count = qMin(ctx.extra_data.toInt() * getEffectiveAmount(ctx), target->getCardCount(true,true)); QList<int> ids; QVariantMap types;
        for (int i = 0; i < count && target->isAlive() && ctx.invoker->isAlive(); ++i) { const int id = room->askForCardChosen(ctx.invoker,target,"hej",objectName(),false,Card::MethodNone,ids,i>0);
            if (id < 0 || ids.contains(id)) break; const Player::Place place = room->getCardPlace(id);
            if (room->getCardOwner(id) != target || (place != Player::PlaceHand && place != Player::PlaceEquip && place != Player::PlaceDelayedTrick)) continue;
            ids << id; types[QString::number(id)] = Sanguosha->getCard(id)->getType(); }
        if (ids.isEmpty()) return false;
        ctx.extra_data = QVariantMap{{"victim",target->objectName()},{"selected",ListI2V(ids)},{"types",types}}; ctx.choice = "obtain"; skillEffect(event,room,actor,ctx,ctx.invoker);
        if (!ctx.extra_data.toMap().value("obtained").toList().isEmpty() && target->isAlive()) { ctx.choice = "draw"; skillEffect(event,room,actor,ctx,target); }
        return false;
    }
};

MobileMouNengPackage::MobileMouNengPackage()
	: Package("mobilemouneng")
{
	General *mobilemou_huaxiong = new General(this, "mobilemou_huaxiong", "qun", 4);
	mobilemou_huaxiong->setStartHp(2);
	mobilemou_huaxiong->setStartHujia(1);
	mobilemou_huaxiong->addSkill("tenyearyaowu");
	mobilemou_huaxiong->addSkill(new MobileMouYangwei);
	mobilemou_huaxiong->addSkill(new MobileMouYangweiEffect);
	mobilemou_huaxiong->addSkill(new MobileMouYangweiTargetMod);
	related_skills.insert("mobilemouyangwei", "#mobilemouyangwei");
	related_skills.insert("mobilemouyangwei", "#mobilemouyangwei-target");

	addMetaObject<MobileMouYangweiCard>();

	General *mobilemou_jiangwei = new General(this, "mobilemou_jiangwei", "shu", 4);
	mobilemou_jiangwei->setStartHujia(1);
	mobilemou_jiangwei->addSkill(new MobileMouTiaoxin);
	mobilemou_jiangwei->addSkill(new MobileMouZhiji);
	mobilemou_jiangwei->addSkill(new MobileMouZhijiBf);
	mobilemou_jiangwei->addSkill(new MobileMouZhijiPro);
	addMetaObject<MobileMouTiaoxinCard>();

	General *mobilemou_gongsunzan = new General(this, "mobilemou_gongsunzan", "qun", 4);
	mobilemou_gongsunzan->addSkill(new MobileMouYicong);
	mobilemou_gongsunzan->addSkill(new MobileMouQiaomeng);
	mobilemou_gongsunzan->addSkill(new MobileMouYicongBf);

	General *mobilemou_zhugejin = new General(this, "mobilemou_zhugejin", "wu", 3);
	mobilemou_zhugejin->addSkill(new MobileMouHuanshi);
	mobilemou_zhugejin->addSkill(new MobileMouHongyuan);
	mobilemou_zhugejin->addSkill(new MobileMouMingzhe);

	General *mobilemou_lvbu = new General(this, "mobilemou_lvbu", "qun", 4);
	mobilemou_lvbu->addSkill(new MobileMouWushuang);
	mobilemou_lvbu->addSkill(new MobileMouLiyu);

}
ADD_PACKAGE(MobileMouNeng)




MobileMouGangLieCard::MobileMouGangLieCard()
{
    setSkillName("mobilemouganglie");
}

bool MobileMouGangLieCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->getMark("MobileMouGangLieDamage"+Self->objectName())>0
		&& to_select->getMark("MobileMouGangLieDamaged"+Self->objectName()) <= 0;
}

void MobileMouGangLieCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->addPlayerMark(effect.to, "MobileMouGangLieDamaged"+effect.from->objectName());

	room->damage(DamageStruct("mobilemouganglie", effect.from, effect.to, 2));
}

class MobileMouGangLieVS : public ViewAsSkillV2
{
public:
    MobileMouGangLieVS() : ViewAsSkillV2("mobilemouganglie")
    {
        setPhaseName("Play");
        m_baseAmount = 2;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouGangLieCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool hasDamaged(Room *room, const Player *attacker, const Player *victim) const
    {
        const QVariantMap page = room->queryActualDamage({{"from", attacker->objectName()},
            {"to", victim->objectName()}, {"limit", 1}});
        // History is game-wide; an incomplete journal is not evidence for a legal target.
        return !page.contains("error") && page.value("complete").toBool()
            && !page.value("items").toList().isEmpty();
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive() || !selected.isEmpty()
            || !request.activationRef.isValid()) return false;
        const Player *usageHolder = nullptr;
        for (const Player *holder : request.initiator->getSiblings(true))
            if (holder->objectName() == request.activationRef.ownerObjectName) usageHolder = holder;
        if (!usageHolder || usageHolder->getSkillInstanceStateValue(request.activationRef.key.skillName,
            request.activationRef.key.instanceID, "mobilemouganglie_game_targets").toStringList().contains(target->objectName()))
            return false;
        // Client selection has no Room journal; the server validates again before committing usage.
        const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator);
        return !server || hasDamaged(server->getRoom(), target, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return false;
        const ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return false;
        const QStringList used = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
            "mobilemouganglie_game_targets").toStringList();
        for (ServerPlayer *target : ctx.targets)
            if (target && used.contains(target->objectName())) return false;
        return true;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return;
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        QStringList used = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
            "mobilemouganglie_game_targets").toStringList();
        for (ServerPlayer *target : ctx.targets)
            if (target && !used.contains(target->objectName())) used << target->objectName();
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mobilemouganglie_game_targets", used);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return;
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (holder) holder->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mobilemouganglie_game_targets");
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.invoker || ctx.targets.size() != 1 || !checkCustomUsage(ctx)
            || !hasDamaged(room, ctx.targets.first(), ctx.invoker)) return false;
        addUsage(ctx);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class MobileMouGangLie : public TriggerSkillV2
{
public:
    MobileMouGangLie() : TriggerSkillV2("mobilemouganglie") { events << EventSkillInvoking; global = true; view_as_skill = new MobileMouGangLieVS; }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const SkillContext accepted = data.value<SkillContext>();
        if (accepted.bypass_cost && accepted.skill_name == objectName() && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && accepted.use_card && accepted.use_card->getTypeId() == Card::TypeSkill)
            view_as_skill->addUsage(accepted);
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class MobileMouTongQingjian : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; }
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }

    MobileMouTongQingjian() : TriggerSkillV2("mobilemouqingjian") { events << CardsMoveOneTime << EventPhaseEnd; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to_place != Player::DiscardPile || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE) return result;
            bool remaining = false; for (int id : move.card_ids) if (room->getCardPlace(id) == Player::DiscardPile) remaining = true;
            if (remaining && actor && actor->isAlive() && actor->hasSkill(objectName())
                && actor->getPile(objectName()).size() < qMax(1, actor->getHp())) result[actor] << objectName();
        } else if (actor && actor->isAlive() && actor->getPhase() == Player::Play && actor->hasSkill(objectName()) && !actor->getPile(objectName()).isEmpty()) result[actor] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return ctx.invoker; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "give") {
            QList<int> ids;
            for (const QVariant &value : ctx.extra_data.toList()) if (ctx.invoker->getPile(objectName()).contains(value.toInt())) ids << value.toInt();
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, true); }
            return false;
        }
        if (event == CardsMoveOneTime) {
            QList<int> ids; const int cap = qMax(1, target->getHp()) * getEffectiveAmount(ctx) - target->getPile(objectName()).size();
            for (int id : ctx.original_data->value<CardsMoveOneTimeStruct>().card_ids)
                if (ids.size() < cap && room->getCardPlace(id) == Player::DiscardPile) ids << id;
            if (!ids.isEmpty()) target->addToPile(objectName(), ids);
        } else {
            QList<int> pending = target->getPile(objectName());
            while (!pending.isEmpty() && target->isAlive()) {
                QList<int> candidates; for (int id : pending) if (target->getPile(objectName()).contains(id)) candidates << id;
                if (candidates.isEmpty()) break;
                const QList<int> allowed = candidates;
                const CardsMoveStruct chosen = room->askForYijiStruct(target, candidates, objectName(), false, true, false,
                    candidates.size(), room->getAlivePlayers(), CardMoveReason(), "mobilemouqingjian0", false, false);
                ServerPlayer *recipient = qobject_cast<ServerPlayer *>(chosen.to);
                QList<int> ids; for (int id : chosen.card_ids) if (allowed.contains(id) && !ids.contains(id)) ids << id;
                if (!recipient || recipient->isDead() || ids.isEmpty()) { recipient = target; ids = allowed; }
                QVariantList selected; for (int id : ids) { selected << id; pending.removeOne(id); }
                // Selection is separate from movement, so each actual recipient gets its target hook.
                ctx.choice = "give"; ctx.extra_data = selected; skillEffect(event, room, actor, ctx, recipient); ctx.choice.clear();
            }
        }
        return false;
    }
};

MobileMouShensuCard::MobileMouShensuCard()
{
    setSkillName("mobilemoushensu");
}

bool MobileMouShensuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(targets.length()>Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)) return false;
	return to_select->getMark("mobilemoushensuBan-Clear")<1&&Self->canSlash(to_select,false,0,targets);
}

const Card *MobileMouShensuCard::validate(CardUseStruct &use) const
{
	Room *room = use.from->getRoom();
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		room->setPlayerMark(p,"mobilemoushensuBan-Clear",use.to.contains(p)?1:0);
	}
	if(use.from->getTag("mobilemoushensu_choice").toString()!="mobilemoushensu1")
		use.no_respond_list << "_ALL_TARGETS";
	Card *card = Sanguosha->cloneCard("slash");
	card->setSkillName("mobilemoushensu");
	card->deleteLater();
	return card;
}

class MobileMouShensuVs : public ViewAsSkillV2
{
public:
    MobileMouShensuVs() : ViewAsSkillV2("mobilemoushensu") { response_pattern = "@@mobilemoushensu"; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMouShensuCard"; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@mobilemoushensu" && !request.initiator->property("mobilemoushensu_prompt").toString().isEmpty(); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        if (!request.initiator || !candidate || candidate->isDead() || request.initiator->property("mobilemoushensu_prompt").toString().section('|',1).split('+',Qt::SkipEmptyParts).contains(candidate->objectName())) return false;
        Slash slash(Card::NoSuit, 0); slash.setSkillName(objectName());
        return selected.size() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, request.initiator, &slash) && request.initiator->canSlash(candidate, false, 0, selected);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { if (selected.isEmpty()) return false; QList<const Player *> checked; for (const Player *target : selected) { if (!canSelectTarget(request, checked, target)) return false; checked << target; } return true; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        // Author-owned group traversal must bypass the generic SkillCard per-target dispatch.
        ctx.manual_effect = true;
        return effectOnTargetGroup(ctx, ctx.targets);
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        Room *room = ctx.invoker->getRoom(); const SkillContext accepted = ctx;
        const int amount = getEffectiveAmount(accepted);
        QList<ServerPlayer *> actual;
        // Each recipient may modify only its own admission; it cannot change the group's card count.
        for (ServerPlayer *target : targets) {
            SkillContext recipient = accepted; recipient.choice = "target"; recipient.extra_data = QStringList();
            skillEffect(recipient, target);
            if (recipient.extra_data.toStringList().contains(target->objectName()) && target->isAlive() && ctx.invoker->canSlash(target, false)) actual << target;
        }
        if (actual.isEmpty() || ctx.invoker->isDead() || amount <= 0) return FinishSkill;
        const QString prompt = ctx.initiator->property("mobilemoushensu_prompt").toString(); QStringList previous; for (ServerPlayer *target : actual) previous << target->objectName();
        const QString updated = prompt.section('|',0,0) + "|" + previous.join("+"); ctx.initiator->setProperty("mobilemoushensu_prompt", updated); room->notifyProperty(ctx.initiator, ctx.initiator, "mobilemoushensu_prompt");
        QStringList forbidden;
        if (prompt.section('|',0,0) != "mobilemoushensu1") {
            for (ServerPlayer *target : room->getAlivePlayers()) {
                SkillContext recipient = accepted; recipient.choice = "response"; recipient.extra_data = QStringList();
                skillEffect(recipient, target);
                if (recipient.extra_data.toStringList().contains(target->objectName())) forbidden << target->objectName();
            }
        }
        for (int i = 0; i < amount && ctx.invoker->isAlive(); ++i) {
            Slash *slash = new Slash(Card::NoSuit, 0); CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(slash)); slash->deleteLater(); slash->setSkillName(objectName());
            QList<ServerPlayer *> legal; for (ServerPlayer *target : actual) if (target->isAlive() && ctx.invoker->canSlash(target, false)) legal << target;
            if (legal.isEmpty()) break; CardUseStruct use(slash, ctx.invoker, legal); use.no_respond_list = forbidden; room->useCardFromSkillEffect(use, accepted, false);
        }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    { if (getEffectiveAmount(ctx) > 0) { QStringList names = ctx.extra_data.toStringList(); if (!names.contains(target->objectName())) names << target->objectName(); ctx.extra_data = names; } return ContinueEffects; }
};

class MobileMouShensu : public TriggerSkillV2
{
public:
    MobileMouShensu() : TriggerSkillV2("mobilemoushensu") { events << EventPhaseStart; view_as_skill = new MobileMouShensuVs; }
    bool prepareSource(Room *room, SkillContext &ctx) const override { ctx.initiator = ctx.owner; ctx.invoker = ctx.owner; return TriggerSkillV2::prepareSource(room, ctx); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase() == Player::RoundStart ? TriggerList{{actor,{objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return ctx.owner->askForSkillInvoke(objectName() + "$-1"); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        const QVariant saved = target->property("mobilemoushensu_prompt");
        const auto restore = qScopeGuard([&] { target->setProperty("mobilemoushensu_prompt", saved); room->notifyProperty(target, target, "mobilemoushensu_prompt"); });
        QStringList choices{"mobilemoushensu1", "mobilemoushensu2", "mobilemoushensu3"}; QSet<int> skipped; bool repeated = false; QStringList previous;
        while (target->isAlive() && choices.size() > (choices.contains("cancel") ? 1 : 0)) {
            const QString choice = room->askForChoice(target, objectName(), choices.join("+")); if (choice == "cancel" || !choices.contains(choice)) break; choices.removeOne(choice);
            QList<Player::Phase> phases; if (choice == "mobilemoushensu1") phases << Player::Judge << Player::Draw; else if (choice == "mobilemoushensu2") phases << Player::Draw << Player::Play; else phases << Player::Play << Player::Discard;
            for (Player::Phase phase : phases) { if (skipped.contains(int(phase))) repeated = true; skipped.insert(int(phase)); target->skip(phase); }
            const QString prompt = choice + "|" + previous.join("+"); target->setProperty("mobilemoushensu_prompt", prompt); room->notifyProperty(target, target, "mobilemoushensu_prompt");
            Room::AcceptedViewAsEffectScope scope(room, target, objectName(), ctx); if (scope.isValid()) room->askForUseCard(target, "@@mobilemoushensu", "mobilemoushensu0");
            previous = target->property("mobilemoushensu_prompt").toString().section('|',1).split('+',Qt::SkipEmptyParts); if (!choices.contains("cancel")) choices << "cancel";
        }
        // The printed condition is any repeated skipped phase, including choices 1+2 or 2+3.
        if (target->isAlive() && repeated) target->turnOver(); return false;
    }
};

class MobileMouZhengzi : public TriggerSkillV2
{
public:
	MobileMouZhengzi() : TriggerSkillV2("mobilemouzhengzi")
	{
		events << EventPhaseChanging;
	}
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::NotActive) return {};
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toString().isEmpty() || turn.toString() == "0") return {};
        QVariantMap filter{{"turn_id", turn}, {"from", player->objectName()}, {"limit", 128}};
        int amount = 0;
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (page.contains("error") || !page.value("complete").toBool()
               ) return {};
            foreach (const QVariant &item, page.value("items").toList())
                amount += item.toMap().value("data").toMap().value("amount").toInt();
            if (!page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        return amount >= player->getHp() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner->askForSkillInvoke(this, *ctx.original_data); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.owner->peiyin(this);
        room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        if (target->isAlive() && !target->faceUp()) target->turnOver();
        if (target->isAlive() && target->isChained()) room->setPlayerChained(target, false);
        return false;
    }
};



MobileMouTongPackage::MobileMouTongPackage()
	: Package("mobilemoutong")
{
	General *mobilemou_xiahoudun = new General(this, "mobilemou_xiahoudun", "wei", 4);
	mobilemou_xiahoudun->addSkill(new MobileMouGangLie);
	mobilemou_xiahoudun->addSkill(new MobileMouTongQingjian);

	General *mobilemou_xiahouyuan = new General(this, "mobilemou_xiahouyuan", "wei", 4);
	mobilemou_xiahouyuan->addSkill(new MobileMouShensu);
	mobilemou_xiahouyuan->addSkill(new MobileMouZhengzi);
	addMetaObject<MobileMouShensuCard>();

	addMetaObject<MobileMouGangLieCard>();
}
ADD_PACKAGE(MobileMouTong)
