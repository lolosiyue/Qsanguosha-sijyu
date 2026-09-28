#include "jiange-defense.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
#include "wrapped-card.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
#include "room.h"
#include "roomthread.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>

bool isJianGeFriend(const Player *a, const Player *b)
{
    return a->getRole() == b->getRole();
}

// WEI Souls

class JGChiying : public TriggerSkillV2
{
public:
    JGChiying() : TriggerSkillV2("jgchiying")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &data) const override
    {
        if (event != DamageInflicted || !target || !target->isAlive() || data.value<DamageStruct>().damage <= 1) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (isJianGeFriend(owner, target)) result.insert(owner, {objectName()});
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return ctx.invoker; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *zidan = ctx.owner;
        if (zidan && damage.to && isJianGeFriend(zidan, damage.to) && damage.damage > 1) {
            LogMessage log;
            log.type = "#JGChiying";
            log.from = zidan;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = objectName();
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(zidan, objectName());

            damage.damage = 1;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class JGJingfan : public DistanceSkillV2
{
public:
    JGJingfan() : DistanceSkillV2("jgjingfan")
    {
        setBaseAmount(-1);
        setHolderSelector(CorrectSkill_AllHolders);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *holder = ctx.getHolder();
        const Player *from = ctx.getPrimary();
        const Player *to = ctx.getSecondary();
        if (!holder || !from || holder == from || !isJianGeFriend(holder, from)
            || (to && isJianGeFriend(from, to)))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.getCurrentAmount());
    }
};

class JGKonghun : public TriggerSkillV2
{
public:
    JGKonghun() : TriggerSkillV2("jgkonghun") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isWounded()) return false;
        for (ServerPlayer *p : room->getAlivePlayers())
            if (!isJianGeFriend(p, ctx.owner)) ctx.targets << p;
        const int enemies = ctx.targets.size();
        if (ctx.owner->getLostHp() < enemies || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.extra_data = enemies;
        // Recovery follows every damage; it is not interleaved with the damage loop.
        ctx.targets << ctx.owner;
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    { room->broadcastSkillInvoke(objectName()); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target == ctx.owner)
            room->recover(target, RecoverStruct(objectName(), ctx.owner, ctx.extra_data.toInt() * getEffectiveAmount(ctx)));
        else
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
        return false;
    }
};

class JGFanshi : public TriggerSkillV2
{
public:
    JGFanshi() : TriggerSkillV2("jgfanshi")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
    {
        return event == EventPhaseStart && target && target->isAlive() && target->getPhase() == Player::Finish
            && target->hasSkill(objectName()) ? TriggerList{{target, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {

        if (!target) return false;
        if (target->getPhase() != Player::Finish) return false;

        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(target, objectName());

        room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
        return false;
    }
};

class JGXuanlei : public TriggerSkillV2
{
public:
    JGXuanlei() : TriggerSkillV2("jgxuanlei")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
        setBaseAmount(1);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Start && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !room) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers()) if (!isJianGeFriend(p, owner) && !p->getJudgingArea().isEmpty()) candidates << p;
        ctx.targets = candidates;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Targets selected in cost remain visible to V2 confirmation and interception.
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
        return false;
    }
};

class JGChuanyun : public TriggerSkillV2
{
public:
    JGChuanyun() : TriggerSkillV2("jgchuanyun")
    {
        events << EventPhaseStart;

        setBaseAmount(1);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Finish && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !room) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers()) if (p->getHp() >= owner->getHp()) candidates << p;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(), "jgchuanyun-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Targets selected in cost remain visible to V2 confirmation and interception.
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};

class JGLeili : public TriggerSkillV2
{
public:
    JGLeili() : TriggerSkillV2("jgleili") { events << Damage; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return event == Damage && player && player->isAlive() && player->hasSkill(objectName())
            && damage.card && damage.card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers())
            if (!isJianGeFriend(p, ctx.owner) && p != damage.to) candidates << p;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "jgleili-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
        return false;
    }
};

class JGFengxing : public TriggerSkillV2
{
public:
    JGFengxing() : TriggerSkillV2("jgfengxing")
    {
        events << EventPhaseStart;

        setBaseAmount(1);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Start && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !room) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers()) if (!isJianGeFriend(p, owner) && owner->canSlash(p, false)) candidates << p;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(), "jgfengxing-invoke", true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Targets selected in cost remain visible to V2 confirmation and interception.
        Slash *slash = new Slash(Card::NoSuit, 0); slash->setSkillName(objectName()); CardUseStruct use(slash, ctx.owner, target); use.setOwnedCard(slash);
        slash->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        use.activationRef = ctx.activationRef; use.sourceRef = ctx.sourceRef; use.skillExecutionID = ctx.executionID;
        room->useCardFromSkillEffect(use, ctx, true);
        return false;
    }
};

class JGHuodi : public TriggerSkillV2
{
public:
    JGHuodi() : TriggerSkillV2("jghuodi")
    {
        events << EventPhaseStart;

        setBaseAmount(1);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Finish && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !room) return false;
        QList<ServerPlayer *> candidates;
        bool turnedFriend = false; for (ServerPlayer *p : room->getAlivePlayers()) { if (isJianGeFriend(p, owner)) turnedFriend |= !p->faceUp(); else candidates << p; }
        if (!turnedFriend || candidates.isEmpty()) return false; ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(), "jghuodi-invoke", true); if (!target) return false; ctx.targets = {target}; return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Targets selected in cost remain visible to V2 confirmation and interception.
        target->turnOver();
        return false;
    }
};

class JGJueji : public TriggerSkillV2
{
public:
    JGJueji() : TriggerSkillV2("jgjueji")
    {
        events << DrawNCards;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &data) const override
    {
        if (event != DrawNCards || !room || !target || !target->isAlive()) return {};
        DrawStruct draw = data.value<DrawStruct>();
        if (draw.reason != "draw_phase") return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->isAlive() && !isJianGeFriend(owner, target) && target->isWounded())
                result.insert(owner, {objectName()});
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.invoker};
        return ctx.owner && ctx.invoker && ctx.original_data
            && ctx.owner->askForSkillInvoke(this, *ctx.original_data, ctx.invoker);
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!ctx.original_data || !ctx.invoker) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        if (draw.reason != "draw_phase" || !ctx.invoker->isWounded()) return false;
        draw.num -= getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

JGJiaoxieCard::JGJiaoxieCard()
{
    setSkillName("jgjiaoxie");
}

bool JGJiaoxieCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length()<2 && to_select->getGeneralName().contains("jg_machine_")
	&& to_select->getKingdom() != Self->getKingdom();
}



class JGJiaoxie : public ViewAsSkillV2
{
public:
    JGJiaoxie() : ViewAsSkillV2("jgjiaoxie")
    {
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->isAlive();
    }

    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && candidate && selected.size() < 2
            && candidate->property("jiange_defense_type").toString() == "machine"
            && !isJianGeFriend(candidate, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() > 0 && targets.size() <= 2; }
    const Card *createCard(const ActiveSkillRequest &) const override
    {
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "JGJiaoxieCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !source->isAlive() || !target->isAlive()) return ContinueEffects;
        Room *room = source->getRoom();
        if (ctx.choice == "receive") {
            const QVariantMap transfer = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(transfer.value("giver").toString());
            const int id = transfer.value("id", -1).toInt();
            if (id < 0) return ContinueEffects;
            const Card *material = Sanguosha->getCard(id);
            if (giver && material && !material->hasFlag("using") && room->getCardOwner(id) == giver
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                room->giveCard(giver, target, material, objectName());
            return ContinueEffects;
        }
        const Card *card = room->askForExchange(target, objectName(), 1, 1, true,
            "jgjiaoxie0:" + source->objectName());
        if (card && card->subcardsLength() == 1) {
            const QString previousChoice = ctx.choice;
            const QVariant previousData = ctx.extra_data;
            const auto restore = qScopeGuard([&]() { ctx.choice = previousChoice; ctx.extra_data = previousData; });
            ctx.choice = "receive";
            ctx.extra_data = QVariantMap{{"giver", target->objectName()}, {"id", card->getSubcards().first()}};
            skillEffect(ctx, source);
        }
        return ContinueEffects;
    }
};

class JGShuailing : public TriggerSkillV2
{
public:
    JGShuailing() : TriggerSkillV2("jgshuailing")
    {
		events << EventPhaseStart;
		frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &) const override
    {
        if (event != EventPhaseStart || !target || !target->isAlive() || target->getPhase() != Player::Draw) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (isJianGeFriend(owner, target)) result.insert(owner, {objectName()});
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return ctx.invoker; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {

        ServerPlayer *owner = ctx.owner;
        if (!target || !owner) return false;
        if (target->getPhase() != Player::Draw) return false;
			if (isJianGeFriend(owner, target)){
				room->sendCompulsoryTriggerLog(owner,this);
				JudgeStruct judge;
				judge.pattern = ".|black";
				judge.good = true;
				judge.who = target;
				judge.reason = objectName();
				room->judge(judge);
				if(judge.isGood()&&!room->getCardOwner(judge.card->getEffectiveId())&&target->isAlive())
					 target->obtainCard(judge.card);
				}
        return false;
    }
};

class JGBashi : public TriggerSkillV2
{
public:
    JGBashi() : TriggerSkillV2("jgbashi")
    {
        events << TargetConfirming;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &data) const override
    {
        if (event != TargetConfirming || !target || !target->isAlive() || !target->hasSkill(objectName())) return {};
        CardUseStruct use = data.value<CardUseStruct>();
        return use.card && (use.card->isKindOf("Slash") || use.card->isNDTrick()) && target != use.from
            && target->faceUp() ? TriggerList{{target, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(this, *ctx.original_data);
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.owner->turnOver(); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player || !ctx.original_data) return false;
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")||use.card->isNDTrick()) {
                room->broadcastSkillInvoke(objectName());

				use.nullified_list << target->objectName();
				data.setValue(use);
        }
        return false;
    }
};

class JGDanjing : public TriggerSkillV2
{
public:
    JGDanjing() : TriggerSkillV2("jgdanjing") { events << AskForPeaches; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DyingStruct dying = data.value<DyingStruct>();
        return event == AskForPeaches && player && player->isAlive() && player->hasSkill(objectName())
            && player->getHp() > 1 && dying.who && dying.who != player && isJianGeFriend(dying.who, player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *dying = ctx.original_data->value<DyingStruct>().who;
        Peach peach(Card::NoSuit, 0);
        peach.setSkillName("_jgdanjing");
        if (!dying || !ctx.owner->canUse(&peach, dying) || !ctx.owner->askForSkillInvoke(this, *ctx.original_data, dying)) return false;
        ctx.targets = {dying}; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { room->loseHp(ctx.owner, 1, true, ctx.owner, objectName()); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->isAlive() || target->getHp() > 0) return false;
        Peach *peach = new Peach(Card::NoSuit, 0);
        peach->setSkillName("_jgdanjing");
        CardUseStruct use(peach, ctx.owner, target);
        use.setOwnedCard(peach);
        peach->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        peach->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        use.activationRef = ctx.activationRef; use.sourceRef = ctx.sourceRef; use.skillExecutionID = ctx.executionID;
        if (ctx.owner->canUse(peach, target)) room->useCardFromSkillEffect(use, ctx, true);
        return false;
    }
};

class JGTongjun : public AttackRangeSkillV2
{
public:
    JGTongjun() : AttackRangeSkillV2("jgtongjun")
    {
        frequency = Compulsory;
        setBaseAmount(1);
        setHolderSelector(CorrectSkill_AllHolders);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *target = ctx.getPrimary();
        const Player *holder = ctx.getHolder();
        if (!target || !holder || target->property("jiange_defense_type").toString() != "machine"
            || !isJianGeFriend(holder, target))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.getCurrentAmount());
    }
};

JGYingjiCard::JGYingjiCard()
{
    setSkillName("jgyingji");
}

bool JGYingjiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("jgyingji");
	dc->deleteLater();
	return dc->targetFilter(targets,to_select,Self);
}



class JGYingjivs : public ViewAsSkillV2
{
public:
    JGYingjivs() : ViewAsSkillV2("jgyingji") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || request.initiator->isKongcheng()) return false;
        Card *slash = Sanguosha->cloneCard("slash");
        if (!slash) return false;
        slash->setSkillName(objectName());
        const bool available = slash->isAvailable(request.initiator);
        delete slash;
        return available;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Card *slash = Sanguosha->cloneCard("slash");
        if (slash) slash->setSkillName(objectName());
        return slash;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!room || !ctx.initiator || ctx.initiator->isKongcheng() || !ctx.use_card) return false;
        QStringList types;
        for (const Card *card : ctx.initiator->getHandcards())
            if (!types.contains(card->getType())) types << card->getType();
        // The accepted Slash carries its own damage receipt through the ordinary pipeline.
        ctx.use_card->setTag("JGYingjiReceipt", QVariantMap{{"owner", ctx.activationRef.ownerObjectName},
            {"instance", ctx.instanceID}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID},
            {"amount", types.size() * getEffectiveAmount(ctx)}});
        room->showAllCards(ctx.initiator);
        return true;
    }
};
class JGYingji : public TriggerSkillV2
{
public:
    JGYingji() : TriggerSkillV2("jgyingji")
    { events << DamageCaused; view_as_skill = new JGYingjivs; global = true; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (event != DamageCaused || !player || !damage.card || !damage.card->isKindOf("Slash")) return true;
        const QVariantMap receipt = damage.card->getTag("JGYingjiReceipt").toMap();
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner || receipt.value("amount").toInt() < 1) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player; ctx.initiator = player;
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.instanceID = receipt.value("instance").toInt(); ctx.amount = receipt.value("amount").toInt();
        ctx.extra_data = receipt; ctx.targets = {damage.to}; ctx.original_data = &data; ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.card && damage.card->getTag("JGYingjiReceipt") == ctx.extra_data;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        damage.damage = getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class JGZhene : public TriggerSkillV2
{
public:
    JGZhene() : TriggerSkillV2("jgzhene") { events << TargetSpecified; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return event == TargetSpecified && player && player->isAlive() && player->hasSkill(objectName())
            && use.card && use.card->getTypeId() > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        for (ServerPlayer *target : ctx.original_data->value<CardUseStruct>().to)
            if (target->getHandcardNum() <= ctx.owner->getHandcardNum()) ctx.targets << target;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { room->sendCompulsoryTriggerLog(ctx.owner, this); return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class JGWeizhu : public TriggerSkillV2
{
public:
    JGWeizhu() : TriggerSkillV2("jgweizhu")
    {
        events << DamageInflicted;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &data) const override
    {
        if (event != DamageInflicted || !target || !target->isAlive()) return {};
        DamageStruct damage = data.value<DamageStruct>();
        TriggerList result;
        for (ServerPlayer *owner : room->getAllPlayers())
            if (owner->isAlive() && owner->hasSkill(objectName()) && isJianGeFriend(owner, target)
                && owner->canDiscard(owner, "h")) result.insert(owner, {objectName()});
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !ctx.original_data) return false;
        const Card *selected = room->askForCard(ctx.owner, ".|.|.|hand", "jgweizhu0:" + ctx.invoker->objectName(),
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!selected) return false;
        ctx.targets = {ctx.invoker};
        ctx.extra_data = selected->getEffectiveId();
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.extra_data.isValid()) return false;
        const int id = ctx.extra_data.toInt();
        const Card *card = Sanguosha->getCard(id);
        if (!card || room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
            || card->hasFlag("using") || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(card, CardMoveReason(CardMoveReason::S_REASON_DISCARD,
            ctx.owner->objectName(), objectName(), ""), ctx.owner);
        return true;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ctx.owner->peiyin(this);
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return ctx.owner->damageRevises(*ctx.original_data, -damage.damage);
    }
};

class JGZhenxi : public TriggerSkillV2
{
public:
    JGZhenxi() : TriggerSkillV2("jgzhenxi") { events << Damaged << DrawNCards << EventSkillInvoking << EventSkillEffectFinished; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == Damaged && player && player->isAlive())
            for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
                if (isJianGeFriend(owner, player)) result.insert(owner, {objectName()});
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != DrawNCards) return false;
        if (!player || !player->isAlive() || data.value<DrawStruct>().reason != "draw_phase") return true;
        for (const QVariant &value : player->getTag("JGZhenxiReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player;
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt;
            ctx.targets = {player}; ctx.current_event = event; ctx.original_data = &data;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.invoker && ctx.invoker->getTag("JGZhenxiReceipts").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return ctx.invoker; }
    static void consume(Room *room, const SkillContext &ctx)
    {
        if (!ctx.invoker) return;
        QVariantList receipts = ctx.invoker->getTag("JGZhenxiReceipts").toList();
        const bool removed = receipts.removeAll(ctx.extra_data) > 0;
        ctx.invoker->setTag("JGZhenxiReceipts", receipts);
        if (removed) room->removePlayerMark(ctx.invoker, "&jgzhenxi", ctx.extra_data.toMap().value("amount").toInt());
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking && event != EventSkillEffectFinished) return false;
        const SkillContext finished = data.value<SkillContext>();
        if (finished.skill_name == objectName() && !finished.activationRef.isValid()) consume(room, finished);
        return false;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { if (event == DrawNCards) consume(room, ctx); return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList receipts = target->getTag("JGZhenxiReceipts").toList();
        if (event == Damaged) {
            int amount = getEffectiveAmount(ctx);
            for (int i = receipts.size() - 1; i >= 0; --i) {
                const QVariantMap old = receipts.at(i).toMap();
                if (old.value("owner").toString() == ctx.activationRef.ownerObjectName
                    && old.value("instance").toInt() == ctx.instanceID) {
                    amount += old.value("amount").toInt(); receipts.removeAt(i);
                }
            }
            receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.instanceID},
                {"actor", ctx.owner->objectName()}, {"source_owner", ctx.sourceRef.ownerObjectName},
                {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID},
                {"amount", amount}, {"execution", ctx.executionID}};
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            room->addPlayerMark(target, "&jgzhenxi", getEffectiveAmount(ctx));
        } else {

            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(draw);
        }
        target->setTag("JGZhenxiReceipts", receipts);
        return false;
    }
};

JGHanjunCard::JGHanjunCard()
{
    setSkillName("jghanjun");
	target_fixed = true;
}



class JGHanjun : public ViewAsSkillV2
{
public:
    JGHanjun() : ViewAsSkillV2("jghanjun") {}
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.isEmpty(); }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->hasTurn(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "JGHanjunCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        QList<ServerPlayer *> enemies;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (!isJianGeFriend(target, ctx.invoker)) enemies << target;
        ctx.extra_data = QVariantList();
        // The collection step follows all random discards, while each victim still gets its target hook.
        for (ServerPlayer *target : enemies) {
            if (!ctx.invoker->isAlive()) break;
            skillEffect(ctx, target);
        }
        ctx.choice = "obtain";
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "obtain") {
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList())
            if (room->getCardPlace(value.toInt()) == Player::DiscardPile) ids << value.toInt();
        if (ids.isEmpty() || !ctx.invoker->isAlive()) return ContinueEffects;
        room->fillAG(ids, ctx.invoker);
        const auto clearAG = qScopeGuard([&]() { room->clearAG(ctx.invoker); });
        const int id = room->askForAG(ctx.invoker, ids, true, objectName());
        room->clearAG(ctx.invoker);
        if (!ids.contains(id)) return ContinueEffects;
        const bool equipment = Sanguosha->getCard(id)->getTypeId() == Card::TypeEquip;
        QList<int> obtain;
        for (int cardId : ids)
            if (room->getCardPlace(cardId) == Player::DiscardPile
                && (Sanguosha->getCard(cardId)->getTypeId() == Card::TypeEquip) == equipment) obtain << cardId;
        if (!obtain.isEmpty()) { DummyCard cards(obtain); room->obtainCard(ctx.invoker, &cards); }
            return ContinueEffects;
        }
        QList<const Card *> cards = target->getCards("he");
        qsanShuffle(cards);
        for (const Card *card : cards) {
            const int id = card->getEffectiveId();
            if (card->hasFlag("using") || !ctx.invoker->canDiscard(target, id)) continue;
            room->throwCard(card, objectName(), target, ctx.invoker);
            if (room->getCardPlace(id) == Player::DiscardPile) {
                QVariantList discarded = ctx.extra_data.toList(); discarded << id; ctx.extra_data = discarded;
            }
            break;
        }
        return ContinueEffects;
    }
};

class JGPigua : public TriggerSkillV2
{
public:
    JGPigua() : TriggerSkillV2("jgpigua") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Start && !player->hasEquip()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { room->sendCompulsoryTriggerLog(ctx.owner, this); room->loseHp(ctx.owner, 1, true, ctx.owner, objectName()); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        for (QList<int> ids : {room->getDiscardPile(), room->getDrawPile()}) {
            qsanShuffle(ids);
            for (int id : ids) {
                if (Sanguosha->getCard(id)->getTypeId() != Card::TypeEquip) continue;
                room->obtainCard(target, id);
                return false;
            }
        }
        return false;
    }
};



// Offensive Machines

class JGJiguan : public ProhibitSkill
{
public:
    JGJiguan() : ProhibitSkill("jgjiguan")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("Indulgence")&&to->hasSkill(objectName());
    }
};

class JGTanshi : public TriggerSkillV2
{
public:
    JGTanshi() : TriggerSkillV2("jgtanshi")
    {
		events << DrawNCards;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DrawNCards || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const DrawStruct draw = data.value<DrawStruct>();
        return draw.reason == "draw_phase" ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!player || !ctx.original_data) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());
        draw.num -= getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class JGTunshi : public TriggerSkillV2
{
public:
    JGTunshi() : TriggerSkillV2("jgtunshi")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
        setBaseAmount(1);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Start && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !room) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers()) if (!isJianGeFriend(p, owner) && p->getHandcardNum() > owner->getHandcardNum()) candidates << p;
        ctx.targets = candidates;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Targets selected in cost remain visible to V2 confirmation and interception.
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};

class JGLianyu : public TriggerSkillV2
{
public:
    JGLianyu() : TriggerSkillV2("jglianyu") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : room->getAlivePlayers())
            if (!isJianGeFriend(target, ctx.owner)) ctx.targets << target;
        return !ctx.targets.isEmpty() && ctx.owner->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    { room->broadcastSkillInvoke(objectName()); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        return false;
    }
};

class JGDidong : public TriggerSkillV2
{
public:
    JGDidong() : TriggerSkillV2("jgdidong")
    {
        events << EventPhaseStart;

        setBaseAmount(1);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Finish && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !room) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers()) if (!isJianGeFriend(p, owner)) candidates << p;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(), "jgdidong-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Targets selected in cost remain visible to V2 confirmation and interception.
        target->turnOver();
        return false;
    }
};

class JGDixian : public TriggerSkillV2
{
public:
    JGDixian() : TriggerSkillV2("jgdixian") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : room->getAlivePlayers())
            if (!isJianGeFriend(target, ctx.owner) && target->hasEquip()) ctx.targets << target;
        return ctx.owner->askForSkillInvoke(this);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.owner->turnOver(); return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    { room->broadcastSkillInvoke(objectName()); return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    { target->throwAllEquips(); return false; }
};

// SHU Souls

class JGJizhen : public TriggerSkillV2
{
public:
    JGJizhen() : TriggerSkillV2("jgjizhen")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
        setBaseAmount(1);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Finish && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !room) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers()) if (isJianGeFriend(p, owner) && p->isWounded()) candidates << p;
        ctx.targets = candidates;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Targets selected in cost remain visible to V2 confirmation and interception.
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class JGLingfeng : public TriggerSkillV2
{
public:
    JGLingfeng() : TriggerSkillV2("jglingfeng") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Draw
            && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner && ctx.owner->askForSkillInvoke(this); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ctx.choice = "reveal";
        skillEffect(event, room, ctx.owner, ctx, ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice != "reveal") {
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
            return false;
        }
        const QList<int> ids = room->getNCards(2 * getEffectiveAmount(ctx), false);
        if (ids.isEmpty()) return false;
        const bool different = ids.size() >= 2
            && Sanguosha->getCard(ids.first())->getColor() != Sanguosha->getCard(ids.last())->getColor();
        const auto cleanup = [&]() {
            QList<int> remaining;
            for (int id : ids) if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
            if (!remaining.isEmpty()) {
                DummyCard cards(remaining);
                room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                    ctx.owner->objectName(), objectName(), ""), nullptr);
            }
        };
        try {
            CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), ""));
            room->moveCardsAtomic(move, true);
            QList<int> remaining;
            for (int id : ids) if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
            if (target->isAlive() && !remaining.isEmpty()) {
                DummyCard cards(remaining); room->obtainCard(target, &cards);
            }
            cleanup();
        } catch (...) {
            // Preserve the original interrupted resolution after cleaning only this reveal's cards.
            try { cleanup(); } catch (...) {}
            throw;
        }
        if (different && ctx.owner->isAlive()) {
            QList<ServerPlayer *> enemies;
            for (ServerPlayer *candidate : room->getAlivePlayers())
                if (!isJianGeFriend(candidate, ctx.owner)) enemies << candidate;
            if (!enemies.isEmpty()) {
                ServerPlayer *enemy = room->askForPlayerChosen(ctx.owner, enemies, objectName(), "@jglingfeng");
                if (enemy) { ctx.choice = "damage"; skillEffect(event, room, ctx.owner, ctx, enemy); }
            }
        }
        return false;
    }
};
class JGBiantian : public TriggerSkillV2
{
public:
    JGBiantian() : TriggerSkillV2("jgbiantian") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Start
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        skillEffect(event, room, ctx.owner, ctx, ctx.owner);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice.isEmpty()) {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        JudgeStruct judge;
        judge.good = true;
        judge.play_animation = false;
        judge.who = ctx.owner;
        judge.reason = objectName();
        room->judge(judge);
        if (!ctx.owner->isAlive()) return false;
        const QString weather = judge.card->isRed() ? "kuangfeng" : judge.card->isBlack() ? "dawu" : QString();
        if (weather.isEmpty()) return false;
        const TriggerSkill *helper = Sanguosha->getTriggerSkill(weather);
        if (!helper) return false;
        room->getThread()->addTriggerSkill(helper);
        ctx.choice = weather;
        for (ServerPlayer *recipient : room->getAlivePlayers())
            if (isJianGeFriend(recipient, ctx.owner) == (weather == "dawu"))
                skillEffect(event, room, ctx.owner, ctx, recipient);
            return false;
        }
        const QString key = "FireWeather_" + ctx.choice;
        QVariantList receipts = target->getTag(key).toList();
        // Reuse the weather helpers' paid-effect receipt contract. Expiry follows
        // this actor's next RoundStart/death even if this granting instance is lost.
        for (int i = receipts.size() - 1; i >= 0; --i) {
            const QVariantMap old = receipts.at(i).toMap();
            if (old.value("owner").toString() == ctx.activationRef.ownerObjectName
                && old.value("activation_skill").toString() == ctx.activationRef.key.skillName
                && old.value("instance").toInt() == ctx.activationRef.key.instanceID) {
                receipts.removeAt(i);
                room->removePlayerMark(target, "&" + ctx.choice);
            }
        }
        const QString nextKey = "FireWeatherNextReceipt_" + ctx.choice;
        const int dispatch = target->getTag(nextKey).toInt() + 1;
        target->setTag(nextKey, dispatch);
        receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"dispatch", dispatch},
            {"activation_skill", ctx.activationRef.key.skillName},
            {"instance", ctx.activationRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)},
            {"actor", ctx.owner->objectName()}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        target->setTag(key, receipts);
        room->addPlayerMark(target, "&" + ctx.choice);
        return false;
    }
};

class JGGongshen : public TriggerSkillV2
{
public:
    JGGongshen() : TriggerSkillV2("jggongshen") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *enemy = nullptr, *friendMachine = nullptr;
        for (ServerPlayer *target : room->getAlivePlayers()) {
            if (target->property("jiange_defense_type").toString() != "machine") continue;
            if (isJianGeFriend(target, ctx.owner)) friendMachine = target;
            else enemy = target;
        }
        QStringList choices;
        if (friendMachine && friendMachine->isWounded()) choices << "recover";
        if (enemy) choices << "damage";
        if (choices.isEmpty()) return false;
        choices << "cancel";
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
        if (ctx.choice == "cancel") return false;
        ctx.targets = {ctx.choice == "recover" ? friendMachine : enemy};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        if (ctx.choice == "recover") room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        else room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        return false;
    }
};

class JGZhinang : public TriggerSkillV2
{
public:
    JGZhinang() : TriggerSkillV2("jgzhinang") { events << EventPhaseStart; frequency = Frequent; setBaseAmount(3); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Start ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner->askForSkillInvoke(this); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QList<int> ids = room->getNCards(getEffectiveAmount(ctx), false);
        if (ids.isEmpty()) return false;
        const auto cleanup = [&]() {
            QList<int> remainder;
            for (int id : ids) if (room->getCardPlace(id) == Player::PlaceTable) remainder << id;
            if (!remainder.isEmpty()) {
                DummyCard cards(remainder);
                room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                    ctx.owner->objectName(), objectName(), ""), nullptr);
            }
        };
        try {
        CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, ctx.owner->objectName(), objectName(), ""));
        room->moveCardsAtomic(move, true);
        QList<int> discard, give;
        for (int id : ids) {
            if (room->getCardPlace(id) != Player::PlaceTable) continue;
            if (Sanguosha->getCard(id)->getTypeId() == Card::TypeBasic) discard << id;
            else give << id;
        }
        if (!discard.isEmpty()) {
            DummyCard cards(discard);
            room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                ctx.owner->objectName(), objectName(), ""), nullptr);
        }
        QList<ServerPlayer *> friends;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (isJianGeFriend(target, ctx.owner)) friends << target;
        if (!give.isEmpty() && ctx.owner->isAlive() && !friends.isEmpty()) {
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, friends, objectName(), "@jgzhinang");
            if (target) {
                QVariantList selected;
                for (int id : give) selected << id;
                ctx.extra_data = selected; ctx.targets = {target}; ctx.manual_effect = true;
                skillEffect(event, room, ctx.owner, ctx, target);
            }
        }
        cleanup();
        } catch (...) {
            try { cleanup(); } catch (...) {}
            throw;
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> ids;
        for (const QVariant &id : ctx.extra_data.toList())
            if (room->getCardPlace(id.toInt()) == Player::PlaceTable) ids << id.toInt();
        if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards); }
        return false;
    }
};

class JGJingmiao : public TriggerSkillV2
{
public:
    JGJingmiao() : TriggerSkillV2("jgjingmiao") { events << CardFinished; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (event != CardFinished || !player || !player->isAlive() || !use.card
            || !use.card->isKindOf("Nullification")) return result;
        // The Nullification user is the recipient; each enemy provider owns its invocation.
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (!isJianGeFriend(owner, player)) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive()) return false;
        ctx.targets = {ctx.invoker};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
        return false;
    }
};

class JGYuhuo : public TriggerSkillV2
{
public:
    JGYuhuo() : TriggerSkillV2("jgyuhuo")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DamageInflicted || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.nature == DamageStruct::Fire ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!ctx.original_data) return false;
        QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.nature == DamageStruct::Fire) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#JGYuhuoProtect";
            log.from = player;
            log.arg = QString::number(damage.damage);
            log.arg2 = "fire_nature";
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            return true;
        }
        return false;
    }
};

class JGQiwu : public TriggerSkillV2
{
public:
    JGQiwu() : TriggerSkillV2("jgqiwu") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return {};
        QStringList triggers;
        for (int id : move.card_ids)
            if (Sanguosha->getCard(id)->getSuit() == Card::Club) triggers << objectName();
        return triggers.isEmpty() ? TriggerList()
            : TriggerList{{player, {objectName() + "*" + QString::number(triggers.size())}}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> friends;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (isJianGeFriend(target, ctx.owner) && target->isWounded()) friends << target;
        if (friends.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, friends, objectName(), "jgqiwu-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx))); return false; }
};

class JGTianyu : public TriggerSkillV2
{
public:
    JGTianyu() : TriggerSkillV2("jgtianyu")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
        setBaseAmount(1);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Finish && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !room) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers()) if (!isJianGeFriend(p, owner) && !p->isChained()) candidates << p;
        ctx.targets = candidates;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Targets selected in cost remain visible to V2 confirmation and interception.
        if (!target->isChained()) room->setPlayerChained(target);
        return false;
    }
};

class JGXiaorui : public TriggerSkillV2
{
public:
    JGXiaorui() : TriggerSkillV2("jgxiaorui")
    {
        events << Damage; frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &data) const override
    {
        if (event != Damage || !target || !target->isAlive()) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash") || !target->hasFlag("CurrentPlayer")) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (isJianGeFriend(owner, target)) result.insert(owner, {objectName()});
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return ctx.invoker; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!ctx.original_data) return false;
        QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *eventPlayer = ctx.invoker;
        if(damage.card&&damage.card->isKindOf("Slash")&&eventPlayer
            && eventPlayer->hasFlag("CurrentPlayer")){
            if (ctx.owner && isJianGeFriend(ctx.owner, eventPlayer)) {
                room->sendCompulsoryTriggerLog(ctx.owner,this);
                room->addPlayerMark(eventPlayer,"&jgxiaorui-Clear",getEffectiveAmount(ctx));
			}
		}
        return false;
    }
};

class JGHuchen : public TriggerSkillV2
{
public:
    JGHuchen() : TriggerSkillV2("jghuchen") { events << DrawNCards; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        // Kills are game facts, including kills made before this instance was acquired.
        QVariantMap filter{{"kind", "death"}, {"from", ctx.owner->objectName()}};
        int kills = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap body = value.toMap().value("data").toMap();
                if (!body.contains("killer_role") || !body.contains("victim_role")) return false;
                if (body.value("killer_role") != body.value("victim_role")) ++kills;
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        ctx.extra_data = kills;
        ctx.targets = {ctx.owner};
        return kills > 0;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        draw.num += ctx.extra_data.toInt() * getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class JGTianjiang : public TriggerSkillV2
{
public:
    JGTianjiang() : TriggerSkillV2("jgtianjiang") { events << Damage; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damage || !player || !player->isAlive()) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash")) return {};
        const qint64 current = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (!current) return {};
        QVariantMap filter{{"turn_id", room->historyScopes().value("turn_id")}, {"from", player->objectName()}};
        qint64 first = 0;
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return {};
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap();
                const QVariantMap body = fact.value("data").toMap();
                if (body.value("card").toMap().value("classes").toStringList().contains("Slash")
                    && body.value("amount").toInt() > 0) {
                    const qint64 id = fact.value("event_id").toLongLong();
                    if (!first || id < first) first = id;
                }
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        if (first != current) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (isJianGeFriend(owner, player)) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return ctx.invoker; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { room->sendCompulsoryTriggerLog(ctx.owner, this); target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};

class JGFengjian : public TriggerSkillV2
{
public:
    JGFengjian() : TriggerSkillV2("jgfengjian")
    { events << Damage << EventPhaseChanging; global = true; frequency = Compulsory; waked_skills = "#JGFengjianProhibit"; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        QVariantList retained;
        QSet<QString> expiredOwners;
        for (const QVariant &value : player->getTag("JGFengjianReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("turn").toLongLong() == turn) retained << value;
            else expiredOwners.insert(receipt.value("actor").toString());
        }
        player->setTag("JGFengjianReceipts", retained);
        for (const QVariant &value : retained) expiredOwners.remove(value.toMap().value("actor").toString());
        for (const QString &owner : expiredOwners) room->setPlayerMark(player, "&jgfengjian+#" + owner, 0);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return event == Damage && player && player->isAlive() && player->hasSkill(objectName())
            && damage.to && damage.to->isAlive() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.original_data->value<DamageStruct>().to}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Applied restrictions outlive the granting instance; only the recipient's next turn clears them.
        QVariantList receipts = target->getTag("JGFengjianReceipts").toList();
        receipts << QVariantMap{{"actor", ctx.owner->objectName()}, {"owner", ctx.activationRef.ownerObjectName},
            {"instance", ctx.instanceID}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID},
            {"turn", room->historyScopes().value("turn_id")}};
        target->setTag("JGFengjianReceipts", receipts);
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        room->setPlayerMark(target, "&jgfengjian+#" + ctx.owner->objectName(), 1);
        return false;
    }
};

class JGFengjianProhibit : public ProhibitSkill
{
public:
    JGFengjianProhibit() : ProhibitSkill("#JGFengjianProhibit")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->getTypeId()>0 && from->getMark("&jgfengjian+#"+to->objectName())>0;
    }
};

JGKedingCard::JGKedingCard()
{
    setSkillName("jgkeding");
}

bool JGKedingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	QStringList user = Self->property("jgkedingUser").toString().split("+");
    if (user.contains(to_select->objectName())||targets.length()>=subcardsLength()) return false;
    const Card*dc = Card::Parse(user.first());
    return dc->targetFilter(QList<const Player *>(),to_select,Self);
}

bool JGKedingCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == subcardsLength();
}



class JGKedingVs : public ViewAsSkillV2
{
public:
    JGKedingVs() : ViewAsSkillV2("jgkeding") {}
    QVariantMap payload(const ActiveSkillRequest &request) const
    {
        return request.initiator ? request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "selection").toMap() : QVariantMap();
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@jgkeding" && request.activationRef.isValid()
            && request.initiator->getMark(borrowedActivationMarkName(objectName())) == request.activationRef.key.instanceID
            && !payload(request).isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.initiator->handCards().contains(card->getEffectiveId())
            && !card->hasFlag("using") && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return !request.selectedCardIds.isEmpty(); }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        if (!request.initiator || !candidate || selected.contains(candidate)
            || selected.size() >= request.selectedCardIds.size()) return false;
        const QVariantMap state = payload(request);
        if (state.value("forbidden").toStringList().contains(candidate->objectName())) return false;
        const Card *card = Card::Parse(state.value("card").toString());
        // Additional targets are checked independently of the original card's one-target cap.
        return card && !request.initiator->isProhibited(candidate, card)
            && card->targetFilter({}, candidate, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return !request.selectedCardIds.isEmpty() && targets.size() == request.selectedCardIds.size(); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantMap state = ctx.initiator->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "selection").toMap();
        QStringList names = state.value("chosen").toStringList();
        names << target->objectName(); state.insert("chosen", names);
        ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "selection", state);
        // The native V2 payment already discards the selected materials exactly once.
        return ContinueEffects;
    }
};

class JGKeding : public TriggerSkillV2
{
public:
    JGKeding() : TriggerSkillV2("jgkeding") { events << TargetSpecifying; view_as_skill = new JGKedingVs; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecifying || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.card && (use.card->isKindOf("Slash") || use.card->isNDTrick()) && use.to.size() == 1
            && player->canDiscard(player, "h") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        Room::AcceptedViewAsEffectScope response(room, owner, objectName(), ctx);
        if (!response.isValid()) return false;
        const int responseId = response.activationRef().key.instanceID;
        const QVariant previous = owner->getSkillInstanceStateValue(objectName(), responseId, "selection");
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previousSelector = owner->getMark(selector);
        const QVariant previousUser = owner->property("jgkedingUser");
        auto restore = qScopeGuard([&] {
            if (previous.isValid()) owner->setSkillInstanceStateValue(objectName(), responseId, "selection", previous);
            else owner->removeSkillInstanceStateValue(objectName(), responseId, "selection");
            room->setPlayerMark(owner, selector, previousSelector);
            room->setPlayerProperty(owner, "jgkedingUser", previousUser);
        });
        QStringList forbidden;
        for (ServerPlayer *target : use.to) forbidden << target->objectName();
        owner->setSkillInstanceStateValue(objectName(), responseId, "selection",
            QVariantMap{{"card", use.card->toString()}, {"forbidden", forbidden}});
        room->setPlayerMark(owner, selector, responseId);
        room->setPlayerProperty(owner, "jgkedingUser", use.card->toString() + "+" + forbidden.join("+"));
        if (!room->askForUseCard(owner, "@@jgkeding", "jgkeding0:" + use.card->objectName())) return false;
        const QStringList chosen = owner->getSkillInstanceStateValue(objectName(), responseId, "selection")
            .toMap().value("chosen").toStringList();
        for (ServerPlayer *target : room->getAlivePlayers())
            if (chosen.contains(target->objectName())) ctx.targets << target;
        ctx.manual_effect = true;
        for (ServerPlayer *target : ctx.targets) skillEffect(event, room, owner, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.to.contains(target)) use.to << target;
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};


class JGLongwei : public TriggerSkillV2
{
public:
    JGLongwei() : TriggerSkillV2("jglongwei") { events << Dying; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DyingStruct dying = data.value<DyingStruct>();
        // Dying is dispatched once for each rescuer, so only this event actor owns the invocation.
        return event == Dying && player && player->isAlive() && player->hasSkill(objectName())
            && dying.who && dying.who->getHp() < 1 && isJianGeFriend(dying.who, player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *dying = ctx.original_data->value<DyingStruct>().who;
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data, dying)) return false;
        ctx.targets = {dying}; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { room->loseMaxHp(ctx.owner, 1, objectName()); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        if (target->getHp() < 1) room->recover(target, RecoverStruct(objectName(), ctx.owner, 1 - target->getHp()));
        return false;
    }
};

class JGMengwu : public TriggerSkillV2
{
public:
    JGMengwu() : TriggerSkillV2("jgmengwu")
    {
        events << CardOffset;
		frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardOffset || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return effect.card && effect.card->isKindOf("Slash") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data) return false;
        QVariant &data = *ctx.original_data;
        CardEffectStruct effect = data.value<CardEffectStruct>();
		if(effect.card->isKindOf("Slash")){
			room->sendCompulsoryTriggerLog(player,this);
			target->drawCards(getEffectiveAmount(ctx),objectName());
		}
        return false;
    }
};

class JGHupo : public FilterSkill
{
public:
    JGHupo() : FilterSkill("jghupo")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->getTypeId()==2;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class JGShuhun : public TriggerSkillV2
{
public:
    JGShuhun() : TriggerSkillV2("jgshuhun") { events << Damage; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == Damage && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> targets;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (isJianGeFriend(target, ctx.owner)) targets << target;
        if (targets.isEmpty()) return false;
        qsanShuffle(targets);
        ctx.targets = {targets.first()}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { room->sendCompulsoryTriggerLog(ctx.owner, this); room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx))); return false; }
};

class JGQinzhen : public TargetModSkillV2
{
public:
    JGQinzhen() : TargetModSkillV2("jgqinzhen", "Slash")
    { setBaseAmount(1); setHolderSelector(CorrectSkill_AllHolders); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.getModType() == TargetModSkill::Residue && ctx.getHolder() && ctx.getPrimary()
            && isJianGeFriend(ctx.getHolder(), ctx.getPrimary())
            ? CorrectSkillResult::useAmount(ctx.getCurrentAmount()) : CorrectSkillResult::noEffect();
    }
};

class JGSlashRules : public TargetModSkillV2
{
public:
    JGSlashRules() : TargetModSkillV2("#jg-slash-rules", "Slash")
    { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *from = ctx.getPrimary();
        if (!from) return CorrectSkillResult::noEffect();
        if (ctx.getModType() == TargetModSkill::DistanceLimit)
            return from->hasSkill("jgmengwu") || from->hasSkill("jgjinggong")
                ? CorrectSkillResult::useAmount(999) : CorrectSkillResult::noEffect();
        if (ctx.getModType() == TargetModSkill::Residue) {
            if (from->hasSkill("jgmengwu")) return CorrectSkillResult::unlimitedResidue();
            const int amount = from->getMark("&jgxiaorui-Clear");
            if (amount > 0) return CorrectSkillResult::useAmount(amount);
        }
        return CorrectSkillResult::noEffect();
    }
};

class JGQixian : public TriggerSkillV2
{
public:
    JGQixian() : TriggerSkillV2("jgqixian")
    { frequency = Compulsory; global = true; events << ConfirmDamage << CardsMoveOneTime << EventPhaseChanging << EventSkillInvoking << EventSkillEffectFinished; }
    static void consume(const SkillContext &ctx)
    {
        if (!ctx.owner) return;
        QVariantList receipts = ctx.owner->getTag("JGQixianReceipts").toList();
        receipts.removeAll(ctx.extra_data); ctx.owner->setTag("JGQixianReceipts", receipts);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking || event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && finished.choice == "damage") consume(finished);
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QVariant turn = room->historyScopes().value("turn_id");
            for (ServerPlayer *holder : room->getAllPlayers(true)) {
                QVariantList keep;
                for (const QVariant &receipt : holder->getTag("JGQixianReceipts").toList())
                    if (receipt.toMap().value("turn") != turn) keep << receipt;
                holder->setTag("JGQixianReceipts", keep);
            }
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return move.to == player && move.to_place == Player::PlaceHand && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != ConfirmDamage) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !damage.card || !damage.card->isKindOf("Slash") || !damage.to) return true;
        for (const QVariant &value : player->getTag("JGQixianReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = player; ctx.invoker = player; ctx.initiator = player;
            ctx.instanceID = receipt.value("dispatch").toInt(); ctx.choice = "damage";
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt; ctx.targets = {damage.to};
            ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.choice == "damage" ? ctx.owner && ctx.owner->getTag("JGQixianReceipts").toList().contains(ctx.extra_data)
            : TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (event == CardsMoveOneTime) ctx.targets = {ctx.owner}; return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.choice == "damage") consume(ctx); return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == CardsMoveOneTime) {
            const QVariant turn = room->historyScopes().value("turn_id");
            QVariantList receipts = target->getTag("JGQixianReceipts").toList();
            int amount = ctx.original_data->value<CardsMoveOneTimeStruct>().card_ids.size() * getEffectiveAmount(ctx);
            for (int i = receipts.size() - 1; i >= 0; --i) {
                const QVariantMap old = receipts.at(i).toMap();
                if (old.value("turn") == turn && old.value("owner") == ctx.activationRef.ownerObjectName
                    && old.value("instance").toInt() == ctx.activationRef.key.instanceID) {
                    amount += old.value("amount").toInt(); receipts.removeAt(i);
                }
            }
            const int dispatch = target->getTag("JGQixianNextReceipt").toInt() + 1;
            target->setTag("JGQixianNextReceipt", dispatch);
            receipts << QVariantMap{{"dispatch", dispatch}, {"turn", turn}, {"amount", amount},
                {"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_instance", ctx.sourceRef.key.instanceID}};
            target->setTag("JGQixianReceipts", receipts);
        } else {
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            ctx.owner->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        }
        return false;
    }
};
class JGJinggong : public TriggerSkillV2
{
public:
    JGJinggong() : TriggerSkillV2("jgjinggong") { events << EventPhaseChanging; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::NotActive) return {};
        // An unavailable journal returns -1 and must not be treated as an empty turn.
        return room->countHistoryCards(player, "turn", "Slash") == 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
        return false;
    }
};


// Defensive Machines

class JGMojian : public TriggerSkillV2
{
public:
    JGMojian() : TriggerSkillV2("jgmojian")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
    {
        return event == EventPhaseStart && target && target->isAlive() && target->getPhase() == Player::Play
            && target->hasSkill(objectName()) ? TriggerList{{target, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target) return false;
        if (target->getPhase() != Player::Play) return false;

        ArcheryAttack *aa = new ArcheryAttack(Card::NoSuit, 0);
        aa->setSkillName("_" + objectName());
        bool can_invoke = false;
        if (!target->isCardLimited(aa, Card::MethodUse)) {
            foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                if (!room->isProhibited(target, p, aa)) {
                    can_invoke = true;
                    break;
                }
            }
        }
        if (!can_invoke) {
            delete aa;
            return false;
        }

        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(target, objectName());
        CardUseStruct use(aa, target, QList<ServerPlayer *>());
        use.setOwnedCard(aa);
        aa->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        aa->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        use.activationRef = ctx.activationRef; use.sourceRef = ctx.sourceRef; use.skillExecutionID = ctx.executionID;
        room->useCardFromSkillEffect(use, ctx, true);
        return false;
    }
};

class JGMojianProhibit : public ProhibitSkill
{
public:
    JGMojianProhibit() : ProhibitSkill("#jgmojian-prohibit")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("ArcheryAttack") && card->getSkillName() == "jgmojian" && isJianGeFriend(from, to);
    }
};

class JGBenlei : public TriggerSkillV2
{
public:
    JGBenlei() : TriggerSkillV2("jgbenlei")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
        setBaseAmount(2);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Start && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !room) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers()) if (!isJianGeFriend(p, owner) && p->property("jiange_defense_type").toString() == "machine") { candidates << p; break; }
        ctx.targets = candidates;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Targets selected in cost remain visible to V2 confirmation and interception.
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
        return false;
    }
};

class JGLingyu : public TriggerSkillV2
{
public:
    JGLingyu() : TriggerSkillV2("jglingyu") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : room->getAlivePlayers())
            if (target != ctx.owner && isJianGeFriend(target, ctx.owner) && target->isWounded()) ctx.targets << target;
        return ctx.owner->askForSkillInvoke(this);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.owner->turnOver(); return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    { room->broadcastSkillInvoke(objectName()); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx))); return false; }
};

class JGTianyun : public TriggerSkillV2
{
public:
    JGTianyun() : TriggerSkillV2("jgtianyun") { events << EventPhaseStart; setBaseAmount(2); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> enemies;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (!isJianGeFriend(target, ctx.owner)) enemies << target;
        if (enemies.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, enemies, objectName(), "jgtianyun-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { room->loseHp(HpLostStruct(ctx.owner, 1, objectName(), ctx.owner)); return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    { room->broadcastSkillInvoke(objectName()); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        if (target->isAlive()) target->throwAllEquips();
        return false;
    }
};

JianGeDefensePackage::JianGeDefensePackage()
    : Package("~JianGeDefense")
{
    typedef General Soul;
    typedef General Machine;

    Soul *jg_soul_caozhen = new Soul(this, "jg_soul_caozhen", "wei", 5, true, true);
    jg_soul_caozhen->addSkill(new JGChiying);
    jg_soul_caozhen->addSkill(new JGJingfan);
    jg_soul_caozhen->addRelateSkill("jgzhenxi");
	skills << new JGZhenxi;

    Soul *jg_soul_simayi = new Soul(this, "jg_soul_simayi", "wei", 5, true, true);
    jg_soul_simayi->addSkill(new JGKonghun);
    jg_soul_simayi->addSkill(new JGFanshi);
    jg_soul_simayi->addSkill(new JGXuanlei);

    Soul *jg_soul_xiahouyuan = new Soul(this, "jg_soul_xiahouyuan", "wei", 4, true, true);
    jg_soul_xiahouyuan->addSkill(new JGChuanyun);
    jg_soul_xiahouyuan->addSkill(new JGLeili);
    jg_soul_xiahouyuan->addSkill(new JGFengxing);

    Soul *jg_soul_zhanghe = new Soul(this, "jg_soul_zhanghe", "wei", 4, true, true);
    jg_soul_zhanghe->addSkill(new JGHuodi);
    jg_soul_zhanghe->addSkill(new JGJueji);

    Soul *jg_soul_zhangliao = new Soul(this, "jg_soul_zhangliao", "wei", 5, true, true);
    jg_soul_zhangliao->addSkill(new JGJiaoxie);
    jg_soul_zhangliao->addRelateSkill("jgshuailing");
    addMetaObject<JGJiaoxieCard>();
	skills << new JGShuailing;

    Soul *jg_soul_xiahoudun = new Soul(this, "jg_soul_xiahoudun", "wei", 5, true, true);
    jg_soul_xiahoudun->addSkill(new JGBashi);
    jg_soul_xiahoudun->addSkill(new JGDanjing);
    jg_soul_xiahoudun->addRelateSkill("jgtongjun");
	skills << new JGTongjun;

    Soul *jg_soul_dianwei = new Soul(this, "jg_soul_dianwei", "wei", 5, true, true);
    jg_soul_dianwei->addSkill(new JGYingji);
    jg_soul_dianwei->addSkill(new JGZhene);
    jg_soul_dianwei->addSkill(new JGWeizhu);
    addMetaObject<JGYingjiCard>();

    Soul *jg_soul_yujin = new Soul(this, "jg_soul_yujin", "wei", 5, true, true);
    jg_soul_yujin->addSkill(new JGHanjun);
    jg_soul_yujin->addSkill(new JGPigua);
    addMetaObject<JGHanjunCard>();

    Machine *jg_machine_tuntianchiwen = new Machine(this, "jg_machine_tuntianchiwen", "wei", 5, true, true);
    jg_machine_tuntianchiwen->addSkill(new JGJiguan);
    jg_machine_tuntianchiwen->addSkill(new JGTanshi);
    jg_machine_tuntianchiwen->addSkill(new JGTunshi);

    Machine *jg_machine_shihuosuanni = new Machine(this, "jg_machine_shihuosuanni", "wei", 3, true, true);
    jg_machine_shihuosuanni->addSkill("jgjiguan");
    jg_machine_shihuosuanni->addSkill(new JGLianyu);

    Machine *jg_machine_fudibian = new Machine(this, "jg_machine_fudibian", "wei", 4, true, true);
    jg_machine_fudibian->addSkill("jgjiguan");
    jg_machine_fudibian->addSkill(new JGDidong);

    Machine *jg_machine_lieshiyazi = new Machine(this, "jg_machine_lieshiyazi", "wei", 4, true, true);
    jg_machine_lieshiyazi->addSkill("jgjiguan");
    jg_machine_lieshiyazi->addSkill(new JGDixian);

    Soul *jg_soul_liubei = new Soul(this, "jg_soul_liubei", "shu", 5, true, true);
    jg_soul_liubei->addSkill(new JGJizhen);
    jg_soul_liubei->addSkill(new JGLingfeng);
    jg_soul_liubei->addRelateSkill("jgqinzhen");
	skills << new JGQinzhen << new JGSlashRules;

    Soul *jg_soul_zhugeliang = new Soul(this, "jg_soul_zhugeliang", "shu", 4, true, true);
    jg_soul_zhugeliang->addSkill(new JGBiantian);
    jg_soul_zhugeliang->addSkill("bazhen");


    Soul *jg_soul_huangyueying = new Soul(this, "jg_soul_huangyueying", "shu", 4, false, true);
    jg_soul_huangyueying->addSkill(new JGGongshen);
    jg_soul_huangyueying->addSkill(new JGZhinang);
    jg_soul_huangyueying->addSkill(new JGJingmiao);

    Soul *jg_soul_pangtong = new Soul(this, "jg_soul_pangtong", "shu", 4, true, true);
    jg_soul_pangtong->addSkill(new JGYuhuo);
    jg_soul_pangtong->addSkill(new JGQiwu);
    jg_soul_pangtong->addSkill(new JGTianyu);

    Soul *jg_soul_guanyu = new Soul(this, "jg_soul_guanyu", "shu", 5, true, true);
    jg_soul_guanyu->addSkill(new JGXiaorui);
    jg_soul_guanyu->addSkill(new JGHuchen);
    jg_soul_guanyu->addRelateSkill("jgtianjiang");
	skills << new JGTianjiang;

    Soul *jg_soul_zhaoyun = new Soul(this, "jg_soul_zhaoyun", "shu", 5, true, true);
    jg_soul_zhaoyun->addSkill(new JGFengjian);
    jg_soul_zhaoyun->addSkill(new JGFengjianProhibit);
    jg_soul_zhaoyun->addSkill(new JGKeding);
    addMetaObject<JGKedingCard>();
    jg_soul_zhaoyun->addRelateSkill("jglongwei");
	skills << new JGLongwei;

    Soul *jg_soul_zhangfei = new Soul(this, "jg_soul_zhangfei", "shu", 4, true, true);
    jg_soul_zhangfei->addSkill(new JGMengwu);
    jg_soul_zhangfei->addSkill(new JGHupo);
    jg_soul_zhangfei->addSkill(new JGShuhun);

    Soul *jg_soul_huangzhong = new Soul(this, "jg_soul_huangzhong", "shu", 4, true, true);
    jg_soul_huangzhong->addSkill(new JGQixian);
    jg_soul_huangzhong->addSkill(new JGJinggong);

    Machine *jg_machine_yunpingqinglong = new Machine(this, "jg_machine_yunpingqinglong", "shu", 4, true, true);
    jg_machine_yunpingqinglong->addSkill("jgjiguan");
    jg_machine_yunpingqinglong->addSkill(new JGMojian);
    jg_machine_yunpingqinglong->addSkill(new JGMojianProhibit);
    related_skills.insert("jgmojian", "#jgmojian-prohibit");

    Machine *jg_machine_jileibaihu = new Machine(this, "jg_machine_jileibaihu", "shu", 4, true, true);
    jg_machine_jileibaihu->addSkill("jgjiguan");
    jg_machine_jileibaihu->addSkill("zhenwei");
    jg_machine_jileibaihu->addSkill(new JGBenlei);

    Machine *jg_machine_lingjiaxuanwu = new Machine(this, "jg_machine_lingjiaxuanwu", "shu", 5, true, true);
    jg_machine_lingjiaxuanwu->addSkill("jgjiguan");
    jg_machine_lingjiaxuanwu->addSkill("yizhong");
    jg_machine_lingjiaxuanwu->addSkill(new JGLingyu);

    Machine *jg_machine_chiyuzhuque = new Machine(this, "jg_machine_chiyuzhuque", "shu", 5, true, true);
    jg_machine_chiyuzhuque->addSkill("jgjiguan");
    jg_machine_chiyuzhuque->addSkill("jgyuhuo");
    jg_machine_chiyuzhuque->addSkill(new JGTianyun);
}

ADD_PACKAGE(JianGeDefense)
