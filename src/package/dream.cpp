#include "dream.h"
#include "skill-instance-utils.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"

static bool firstInstance(const SkillContext &ctx, const QString &skill)
{
    if (!ctx.owner) return false;
    const QList<int> ids = ctx.owner->getSkillInstanceIds(skill);
    return !ids.isEmpty() && ids.first() == ctx.instanceID;
}

static bool sponsorOnce(Room *room, const SkillContext &ctx, const QString &skill)
{
    if (!firstInstance(ctx, skill) || !room) return false;
    const QList<ServerPlayer *> holders = room->findPlayersBySkillName(skill);
    return !holders.isEmpty() && holders.first() == ctx.owner;
}

static void addPrimary(TriggerList &result, ServerPlayer *owner, const QString &skill)
{
    if (!owner || !owner->isAlive() || !owner->hasSkill(skill)) return;
    const QList<int> ids = owner->getValidSkillInstanceIds(skill);
    if (!ids.isEmpty())
        result[owner] << SkillInstanceUtils::formatName(skill, ids.first());
}

static bool ownsHand(const Player *player, const Card *card)
{
    if (!player || !card || card->isEquipped()) return false;
    const int id = card->getEffectiveId();
    return id >= 0 && player->handCards().contains(id);
}

static bool selectionAccepted(const ViewAsSkillV2 *skill, const ActiveSkillRequest &request)
{
    if (!skill || request.selectedCardIds.size() != skill->getN()) return false;
    ActiveSkillRequest prefix = request;
    prefix.selectedCardIds.clear();
    for (int id : request.selectedCardIds) {
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (!card || !skill->canSelectCard(prefix, card)) return false;
        prefix.selectedCardIds << id;
    }
    return true;
}

static bool watchOnce(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                      const QString &skill, QList<SkillContext> &out)
{
    if (!room || !player) return true;
    ServerPlayer *owner = nullptr;
    int id = 0;
    for (ServerPlayer *holder : room->findPlayersBySkillName(skill)) {
        const QList<int> ids = holder->getValidSkillInstanceIds(skill);
        if (ids.isEmpty()) continue;
        owner = holder;
        id = ids.first();
        break;
    }
    if (!owner) return true;
    SkillContext ctx;
    ctx.skill_name = skill;
    ctx.owner = owner;
    ctx.invoker = player;
    ctx.instanceID = id;
    ctx.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(skill, id));
    ctx.sourceRef = ctx.activationRef;
    ctx.original_data = &data;
    ctx.current_event = event;
    out << ctx;
    return true;
}

class IfAnxu : public TriggerSkillV2
{
public:
    IfAnxu() : TriggerSkillV2("ifanxu") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        addPrimary(result, player, objectName());
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->askForSkillInvoke(objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player || !player->isAlive()) return false;
        player->drawCards(2, objectName());
        Card *discarded = room->askForDiscard(player, objectName(), 2, 2, false, true);
        if (discarded && discarded->getSuit() == Card::NoSuit)
            room->recover(player, RecoverStruct(objectName(), player));
        else if (discarded && discarded->isBlack()) {
            ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "ifanxu0");
            if (target) {
                room->doAnimate(1, player->objectName(), target->objectName());
                room->loseHp(target, 1, true, player, objectName());
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
        room->addPlayerMark(target, "ifmishouBf-SelfClear");
        room->giveCard(ctx.invoker, target, ctx.use_card, objectName());
        return ContinueEffects;
    }
};

class IfMishou : public TriggerSkillV2
{
public:
    IfMishou() : TriggerSkillV2("ifmishou")
    {
        events << TargetSpecified << DamageCaused << EventPhaseStart;
        view_as_skill = new IfMishouViewAs;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event == EventPhaseStart) return false;
        bool happen = false;
        if (!player || !player->isAlive()) return true;
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            happen = use.card && use.card->isKindOf("Slash")
                && player->getMark("ifmishouBf-SelfClear") > 0
                && player->getMark("ifmishouBfto-SelfClear") < 1;
        } else if (event == DamageCaused) {
            const DamageStruct damage = data.value<DamageStruct>();
            happen = damage.card && damage.card->isKindOf("Slash")
                && player->getMark("ifmishouBf-SelfClear") > 0;
        }
        if (!happen) return true;
        return watchOnce(event, room, player, data, objectName(), out);
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->getPhase() == Player::Finish
            && player->getHandcardNum() > 0)
            addPrimary(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            if (ctx.owner && ctx.owner->isAlive() && ctx.owner->getHandcardNum() > 0)
                room->askForUseCard(ctx.owner, "@@ifmishou", "ifmishou0");
            return false;
        }
        if (!player || !ctx.original_data) return false;
        if (event == TargetSpecified) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.card || !use.card->isKindOf("Slash") || player->getMark("ifmishouBf-SelfClear") <= 0
                || player->getMark("ifmishouBfto-SelfClear") > 0) return false;
            player->addMark("ifmishouBfto-SelfClear");
            for (ServerPlayer *target : use.to)
                room->addPlayerMark(player, target->objectName() + "ifmishouBfto-SelfClear");
            return false;
        }
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash") && player->getMark("ifmishouBf-SelfClear") > 0) {
            room->loseHp(damage.to, damage.damage, true, player, damage.card->objectName());
            return true;
        }
        return false;
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
        if (!ctx.primary || !ctx.card) return CorrectSkillResult::noEffect();
        if (ctx.modType == TargetModSkill::DistanceLimit)
            return ctx.card->getSkillName() == "ifshenfeng"
                ? CorrectSkillResult::useAmount(999) : CorrectSkillResult::noEffect();
        if (ctx.modType != TargetModSkill::Residue) return CorrectSkillResult::noEffect();
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

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("@ifdianbian") > 0
            && request.initiator->getMark("ifdianbianCan") > 0;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IfDianbianCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || ctx.initiator->getMark("@ifdianbian") <= 0) return false;
        room->removePlayerMark(ctx.initiator, "@ifdianbian");
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive()) return FinishSkill;
        Room *room = source->getRoom();
        Card *obtained = dummyCard();
        for (int id : room->getDiscardPile()) {
            const Card *card = Sanguosha->getEngineCard(id);
            if ((card->isKindOf("Slash") || card->getTypeId() == 3)
                && source->getMark("ifdianbianId" + card->toString()) > 0)
                obtained->addSubcard(id);
        }
        source->obtainCard(obtained);
        room->setPlayerMark(source, "&ifdianbian-Clear", 1);
        return FinishSkill;
    }
};

class IfDianbian : public TriggerSkillV2
{
public:
    IfDianbian() : TriggerSkillV2("ifdianbian")
    {
        events << CardsMoveOneTime << SwappedPile << Death << EventPhaseChanging;
        limit_mark = "@ifdianbian";
        waked_skills = "ifpiyong";
        view_as_skill = new IfDianbianViewAs;
        frequency = Skill::Limited;
    }

    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data || !player) return;
        if (event == EventPhaseChanging) {
            if (ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive
                || !sponsorOnce(room, ctx, objectName())) return;
            for (ServerPlayer *marked : room->getAllPlayers()) {
                if (marked->getMark("&ifdianbian-Clear") <= 0) continue;
                int highest = 0;
                for (ServerPlayer *alive : room->getAlivePlayers())
                    highest = qMax(highest, alive->getMaxHp());
                if (marked->getMaxHp() >= highest)
                    room->handleAcquireDetachSkills(marked, "-ifanxu|ifpiyong");
            }
            return;
        }
        if (!firstInstance(ctx, objectName()) || ctx.owner != player || !player->isAlive()) return;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            if (move.to_place == Player::DiscardPile && player == move.from
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                for (int id : move.card_ids)
                    player->addMark(QString("ifdianbianId%1").arg(id));
            }
        } else if (event == SwappedPile) {
            for (const QString &mark : player->getMarkNames()) {
                if (mark.contains("ifdianbianId")) player->setMark(mark, 0);
            }
        } else if (event == Death) {
            const DeathStruct death = ctx.original_data->value<DeathStruct>();
            room->setPlayerMark(player, "ifdianbianCan", death.hplost && death.hplost->to ? 1 : 0);
            if (player->getMark("&ifdianbian-Clear") > 0)
                room->gainMaxHp(player, 1, objectName());
        }
    }
};

class IfPiyong : public ViewAsSkillV2
{
public:
    IfPiyong() : ViewAsSkillV2("ifpiyong$", 1) {}

    bool willThrowSelectedCards() const override { return false; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->hasLordSkill(objectName());
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || !card->isKindOf("Weapon") || !request.selectedCardIds.isEmpty())
            return false;
        const int id = card->getEffectiveId();
        return id >= 0 && (request.initiator->handCards().contains(id) || request.initiator->hasEquip(card));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return selectionAccepted(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IfPiyongCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.isEmpty();
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !ctx.use_card) return FinishSkill;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getEffectiveId();
        const Card *weaponCard = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        const Weapon *weapon = weaponCard ? qobject_cast<const Weapon *>(weaponCard->getRealCard()) : nullptr;
        if (!weapon) return FinishSkill;
        const int range = weapon->getRange();
        LogMessage log;
        log.type = "$RecastCard";
        log.from = source;
        log.card_str = ListI2S(ctx.use_card->getSubcards()).join("+");
        room->sendLog(log);
        CardMoveReason reason(CardMoveReason::S_REASON_RECAST, source->objectName(), objectName(), "");
        room->moveCardTo(ctx.use_card, nullptr, Player::DiscardPile, reason, true);
        source->drawCards(1, "recast");
        for (int drawn : source->drawCardsList(range, objectName()))
            room->setCardTip(drawn, objectName());
        return FinishSkill;
    }
};

class IfXiance : public TriggerSkillV2
{
public:
    IfXiance() : TriggerSkillV2("ifxiance")
    {
        events << EventPhaseChanging << TargetSpecified << CardUsed << CardResponded << DamageInflicted;
        frequency = Skill::Compulsory;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event != CardUsed && event != CardResponded) return false;
        if (!player || !player->isAlive()) return true;
        const Card *card = nullptr;
        if (event == CardUsed)
            card = data.value<CardUseStruct>().whocard;
        else
            card = data.value<CardResponseStruct>().m_toCard;
        if (!card || !card->hasFlag("ifxian_ce")) return true;
        return watchOnce(event, room, player, data, objectName(), out);
    }

    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventPhaseChanging || !ctx.original_data
            || ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive
            || !sponsorOnce(room, ctx, objectName())) return;
        for (ServerPlayer *player : room->getAllPlayers()) {
            if (player->getMark("&ifxian_ce") > 0) player->loseMark("&ifxian_ce");
        }
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && use.card->getTypeId() > 0 && !use.to.contains(player))
                addPrimary(result, player, objectName());
        } else if (event == DamageInflicted) {
            addPrimary(result, player, objectName());
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive()) return false;
        if (event == CardUsed || event == CardResponded) {
            room->loseHp(player, 1, true, nullptr, objectName());
            return false;
        }
        if (!firstInstance(ctx, objectName())) return false;
        if (event == TargetSpecified) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.card || use.card->getTypeId() <= 0 || use.to.contains(player)) return false;
            room->sendCompulsoryTriggerLog(player, this);
            player->gainMark("&ifxian_ce", use.to.length());
            room->setCardFlag(use.card, "ifxian_ce");
            return false;
        }
        player->addMark("ifxianceNum-Clear");
        if (player->getMark("ifxianceNum-Clear") != 1) return false;
        room->sendCompulsoryTriggerLog(player, objectName());
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const int bonus = player->getMark("&ifxian_ce") * damage.damage;
        return player->damageRevises(*ctx.original_data, bonus - damage.damage);
    }
};

class IfZhenshi : public TriggerSkillV2
{
public:
    IfZhenshi() : TriggerSkillV2("ifzhenshi")
    {
        events << EventPhaseStart << TargetConfirmed << CardsMoveOneTime;
        waked_skills = "ifjiusuo";
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event != TargetConfirmed) return false;
        if (!player || !player->isAlive()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isDamageCard() || !use.to.contains(player)) return true;
        bool marked = false;
        for (ServerPlayer *holder : room->getAlivePlayers()) {
            if (holder->getMark("ifzhenshiTo" + player->objectName()) > 0) {
                marked = true;
                break;
            }
        }
        if (!marked) return true;
        return watchOnce(event, room, player, data, objectName(), out);
    }

    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != CardsMoveOneTime || !firstInstance(ctx, objectName())
            || ctx.owner != player || !ctx.original_data) return;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile) return;
        for (int id : move.card_ids)
            player->addMark(QString("ifzhenshiId%1-Clear").arg(id));
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && (player->getPhase() == Player::RoundStart || player->getPhase() == Player::Finish))
            addPrimary(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == TargetConfirmed) {
            if (player && player->isAlive()) room->acquireSkill(player, "ifjiusuo");
            return false;
        }
        ServerPlayer *owner = ctx.owner;
        if (!owner || !owner->isAlive()) return false;
        if (owner->getPhase() == Player::RoundStart) {
            for (ServerPlayer *target : room->getAlivePlayers()) {
                if (owner->getMark("ifzhenshiTo" + target->objectName()) <= 0) continue;
                owner->setMark("ifzhenshiTo" + target->objectName(), 0);
                room->detachSkillFromPlayer(target, "ifjiusuo");
            }
            return false;
        }
        if (owner->getPhase() != Player::Finish) return false;
        QList<Card::Suit> suits;
        for (int id : room->getDiscardPile()) {
            if (owner->getMark(QString("ifzhenshiId%1-Clear").arg(id)) <= 0) continue;
            Card *card = Sanguosha->getCard(id);
            if (!suits.contains(card->getSuit())) suits << card->getSuit();
        }
        if (suits.isEmpty()) return false;
        const QList<ServerPlayer *> chosen = room->askForPlayersChosen(owner, room->getAlivePlayers(),
            objectName(), 0, suits.length(), QString("ifzhenshi0:%1").arg(suits.length()));
        if (chosen.isEmpty()) return false;
        owner->skillInvoked(objectName());
        for (ServerPlayer *target : chosen)
            owner->setMark("ifzhenshiTo" + target->objectName(), 1);
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
        addPrimary(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive() || !ctx.original_data) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from || !use.to.contains(player)) return false;
        room->sendCompulsoryTriggerLog(player, this);
        JudgeStruct judge;
        judge.reason = objectName();
        judge.who = use.from;
        judge.pattern = ".|.|0~8";
        judge.good = false;
        judge.negative = true;
        room->judge(judge);
        if (!judge.isBad()) return false;
        use.nullified_list << "_ALL_TARGETS";
        ctx.original_data->setValue(use);
        if (judge.card->getNumber() < use.from->getHp())
            room->loseHp(use.from, 1, true, player, objectName());
        if (use.from->isAlive() && judge.card->getNumber() < use.from->getCardCount()) {
            const int id = room->askForCardChosen(player, use.from, "he", objectName());
            if (id >= 0) room->obtainCard(player, id, false);
        }
        return false;
    }
};

class IfYinglveViewAs : public ViewAsSkillV2
{
public:
    IfYinglveViewAs() : ViewAsSkillV2("ifyinglve", 1) {}

    bool willThrowSelectedCards() const override { return false; }
    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::juguan(objectName(), "fire_slash,fire_attack");
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.pattern == "@@ifyinglve")
            return request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("ifyinglveUse-PlayClear") < 1;
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
        if (!cardSelectionFeasible(request) || !request.initiator) return nullptr;
        QString name = request.userString;
        if (request.pattern == "@@ifyinglve")
            name = request.initiator->property("ifyinglveCn").toString();
        else if (name.isEmpty() || name == "fire_slash" || name == "fire_attack") {
            if (name != "fire_slash" && name != "fire_attack") {
                const Card *declared = request.initiator->getTag(objectName()).value<const Card *>();
                name = declared ? declared->objectName() : QString();
            }
        } else {
            name.clear();
        }
        if (name != "fire_slash" && name != "fire_attack") return nullptr;
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return nullptr;
        card->setSkillName("_" + objectName());
        card->addSubcards(request.selectedCardIds);
        return card;
    }
};

class IfYinglve : public TriggerSkillV2
{
public:
    IfYinglve() : TriggerSkillV2("ifyinglve")
    {
        events << TargetConfirmed << CardFinished;
        view_as_skill = new IfYinglveViewAs;
    }

    SkillDialogInfo getDialogInfo() const override { return view_as_skill->getDialogInfo(); }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event != TargetConfirmed) return false;
        if (!player || !player->isAlive()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        const bool named = use.card && use.card->isDamageCard()
            && use.card->getSkillNames().contains(objectName())
            && use.from && use.from->hasSkill(objectName(), true);
        const bool happen = named && (player == use.from
            || (use.to.contains(player) && player->canDiscard(player, "he")));
        if (!happen) return true;
        return watchOnce(event, room, player, data, objectName(), out);
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardFinished || !player) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && use.card->isDamageCard() && use.card->getSkillNames().contains(objectName()))
            addPrimary(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !ctx.original_data) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        if (event == TargetConfirmed) {
            if (player == use.from)
                room->addPlayerMark(player, "ifyinglveUse-PlayClear");
            if (use.to.contains(player) && player->canDiscard(player, "he")
                && room->askForDiscard(player, objectName(), 2, 2, true, true,
                    "ifyinglve0:" + use.card->objectName())) {
                use.nullified_list << player->objectName();
                ctx.original_data->setValue(use);
            }
            return false;
        }
        if (!player->hasSkill(objectName(), true)) return false;
        if (use.card->hasFlag("DamageDone") || room->getCardOwner(use.card->getEffectiveId())) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
            "ifyinglve1:" + use.card->objectName(), true, true);
        if (!target) return false;
        room->giveCard(player, target, use.card, objectName());
        player->drawCards(1, objectName());
        if (target->isAlive() && target->getHandcardNum() > 0) {
            QString name = "fire_slash";
            if (use.card->objectName() == name) name = "fire_attack";
            room->setPlayerProperty(target, "ifyinglveCn", name);
            room->askForUseCard(target, "@@ifyinglve", "ifyinglve2:" + name);
        }
        return false;
    }
};

class IfBihe : public TriggerSkillV2
{
public:
    IfBihe() : TriggerSkillV2("ifbihe")
    {
        events << TargetSpecifying << ConfirmDamage << Damage;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event == TargetSpecifying) return false;
        if (!player || !player->isAlive()) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->hasFlag("ifbiheBf")) return true;
        return watchOnce(event, room, player, data, objectName(), out);
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecifying) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isDamageCard() || use.to.length() != 1) return result;
        for (ServerPlayer *holder : room->getAllPlayers()) {
            if (holder->hasFlag("CurrentPlayer") && holder->canDiscard(holder, "he"))
                addPrimary(result, holder, objectName());
        }
        return result;
    }

    bool pay(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != TargetSpecifying || !ctx.owner || !ctx.original_data) return event != TargetSpecifying;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        Card *discarded = room->askForDiscard(ctx.owner, objectName(), 2, 2, true, true,
            "ifbihe0:" + use.card->objectName(), ".", objectName());
        if (!discarded) return false;
        ctx.extra_data = discarded->isBlack() ? QString("black") : discarded->isRed() ? QString("red") : QString("none");
        ctx.targets = player ? QList<ServerPlayer *>{player} : QList<ServerPlayer *>();
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        if (event == ConfirmDamage) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (!damage.card || !damage.card->hasFlag("ifbiheBf") || !damage.from || !damage.to || !player)
                return false;
            int delta = qAbs(damage.from->getHandcardNum() - damage.to->getHandcardNum());
            delta = qMin(delta, 5);
            return player->damageRevises(*ctx.original_data, delta - damage.damage);
        }
        if (event == Damage) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (!damage.card || !damage.card->hasFlag("ifbiheBf")) return false;
            for (ServerPlayer *holder : room->getAlivePlayers()) {
                if (!damage.card->hasFlag("ifbiheOwner" + holder->objectName())) continue;
                const int draw = holder->getMaxHp() - holder->getHandcardNum();
                if (draw > 0) holder->drawCards(draw, objectName());
            }
            return false;
        }
        if (!ctx.owner || !player) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        const QString color = ctx.extra_data.toString();
        if (color == "black")
            use.no_respond_list << "_ALL_TARGETS";
        else if (color == "red") {
            room->setCardFlag(use.card, "ifbiheBf");
            room->setCardFlag(use.card, "ifbiheOwner" + ctx.owner->objectName());
        } else {
            ServerPlayer *extra = room->askForPlayerChosen(player,
                room->getCardTargets(player, use.card, use.to), objectName(),
                "ifbihe1:" + use.card->objectName(), true);
            if (extra) {
                use.to << extra;
                room->sortByActionOrder(use.to);
                use.extra_use++;
            }
        }
        ctx.original_data->setValue(use);
        return false;
    }
};

class IfShiji : public ViewAsSkillV2
{
public:
    IfShiji() : ViewAsSkillV2("ifshiji", 1) {}

    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "IfShijiCard"; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->usedTimes("IfShijiCard") < request.initiator->getHp();
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
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !ctx.use_card) return ContinueEffects;
        Room *room = source->getRoom();
        const int given = ctx.use_card->getEffectiveId();
        room->giveCard(source, target, ctx.use_card, objectName(), true);
        if (!source->isAlive() || !target->isAlive() || !source->canDiscard(target, "h")) return ContinueEffects;
        const int discarded = room->askForCardChosen(source, target, "h", objectName(), false, Card::MethodDiscard);
        if (discarded < 0) return ContinueEffects;
        room->throwCard(discarded, objectName(), target, source);
        const Card *first = Sanguosha->getEngineCard(given);
        const Card *second = Sanguosha->getEngineCard(discarded);
        if (first->getColor() != second->getColor())
            room->recover(target, RecoverStruct(objectName(), source));
        if (first->getType() == second->getType()) {
            QList<ServerPlayer *> drawers;
            drawers << source << target;
            room->drawCards(drawers, 1, objectName());
        }
        return ContinueEffects;
    }
};

IfAnjieCard::IfAnjieCard() {}

bool IfAnjieCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (user_string.isEmpty())
        return targets.isEmpty() && to_select != Self && Self->getMark("ifanjieBf" + to_select->objectName()) > 0;
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool IfAnjieCard::targetFixed() const
{
    if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) return true;
    if (user_string.isEmpty()) return false;
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    card->deleteLater();
    return card->targetFixed();
}

bool IfAnjieCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (user_string.isEmpty()) return !targets.isEmpty();
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *IfAnjieCard::validateInResponse(ServerPlayer *player) const
{
    Room *room = player->getRoom();
    QList<ServerPlayer *> targets;
    for (ServerPlayer *other : room->getOtherPlayers(player)) {
        if (player->getMark("ifanjieBf" + other->objectName()) > 0) targets << other;
    }
    ServerPlayer *target = room->askForPlayerChosen(player, targets, "ifanjie", "ifanjie0", true, true);
    if (!target) return nullptr;
    QList<int> ids;
    for (const Card *hand : target->getHandcards()) {
        if (player->isLocked(hand)) continue;
        for (const QString &name : user_string.split("+")) {
            if (hand->sameNameWith(name)) ids << hand->getId();
        }
    }
    const int id = room->doGongxin(player, target, ids, "ifanjie");
    if (id >= 0) return Sanguosha->getCard(id);
    room->addPlayerMark(player, "ifanjieBan_lun");
    return nullptr;
}

const Card *IfAnjieCard::validate(CardUseStruct &use) const
{
    Room *room = use.from->getRoom();
    if (user_string.isEmpty()) {
        use.from->skillInvoked("ifanjie");
        QList<int> ids;
        for (const Card *hand : use.to.last()->getHandcards()) {
            if (hand->isAvailable(use.from)) ids << hand->getId();
        }
        const int id = room->doGongxin(use.from, use.to.last(), ids, "ifanjie");
        if (id >= 0) {
            room->setPlayerMark(use.from, "ifanjieId", id);
            if (room->askForUseCard(use.from, "@@ifanjie", "ifanjie0:" + Sanguosha->getCard(id)->objectName()))
                return nullptr;
        }
        room->addPlayerMark(use.from, "ifanjieBan_lun");
        return nullptr;
    }
    return validateInResponse(use.from);
}

class IfAnjieViewAs : public ViewAsSkillV2
{
public:
    IfAnjieViewAs() : ViewAsSkillV2("ifanjie") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.pattern == "@@ifanjie") return request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
        if (player->getMark("ifanjieBan_lun") > 0) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
        bool marked = false;
        for (const Player *other : player->getAliveSiblings()) {
            if (player->getMark("ifanjieBf" + other->objectName()) > 0) {
                marked = true;
                break;
            }
        }
        if (!marked) return false;
        for (const QString &name : request.pattern.split("+")) {
            Card *card = Sanguosha->cloneCard(name);
            if (!card) continue;
            card->deleteLater();
            if (card->getTypeId() > 0) return true;
        }
        return false;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return nullptr;
        if (request.pattern == "@@ifanjie")
            return Sanguosha->getCard(request.initiator->getMark("ifanjieId"));
        IfAnjieCard *card = new IfAnjieCard;
        card->setSkillName(objectName());
        card->setUserString(request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? QString() : request.pattern);
        return card;
    }

    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &) const override { return true; }
};

class IfAnjie : public TriggerSkillV2
{
public:
    IfAnjie() : TriggerSkillV2("ifanjie")
    {
        events << HpRecover;
        view_as_skill = new IfAnjieViewAs;
    }

    void record(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!firstInstance(ctx, objectName()) || !ctx.original_data || !player) return;
        const RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
        if (recover.who == ctx.owner)
            room->setPlayerMark(ctx.owner, "ifanjieBf" + player->objectName(), 1);
    }
};

class IfLitian : public TriggerSkillV2
{
public:
    IfLitian() : TriggerSkillV2("iflitian")
    {
        events << EventPhaseStart;
        waked_skills = "ifhuangchu,_iffeileijia";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->getPhase() == Player::Start) addPrimary(result, player, objectName());
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->askForSkillInvoke(objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player || !player->isAlive()) return false;
        const QList<int> ids = room->getNCards(3);
        room->fillAG(ids, player);
        const int id = room->askForAG(player, ids, false, objectName());
        room->clearAG(player);
        room->returnToTopDrawPile(ids);
        Card *card = Sanguosha->getCard(id);
        room->moveCardTo(card, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), ""), true);
        room->getThread()->delay();
        room->obtainCard(player, id, true);
        if (card->getSuit() == Card::Spade) {
            room->detachSkillFromPlayer(player, objectName(), false, false, false);
            room->acquireSkill(player, "iflitian2", true, true, false);
        }
        return false;
    }
};

class IfLitian2 : public TriggerSkillV2
{
public:
    IfLitian2() : TriggerSkillV2("iflitian2")
    {
        events << EventPhaseStart << TargetConfirmed << PreHpRecover;
        shiming_skill = true;
        waked_skills = "ifhuangchu,_iffeileijia";
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != PreHpRecover || !sponsorOnce(ctx.owner ? ctx.owner->getRoom() : nullptr, ctx, objectName())
            || !ctx.original_data) return;
        RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
        if (!recover.card || !recover.card->hasFlag("iflitian2Bf")) return;
        recover.recover++;
        ctx.original_data->setValue(recover);
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return result;
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Start) return result;
            for (int id : player->getValidSkillInstanceIds(objectName())) {
                const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
                if (room->getShimingStatus(ref) >= 1) continue;
                bool ahead = false;
                for (ServerPlayer *other : room->getOtherPlayers(player)) {
                    if (other->getHandcardNum() > player->getHandcardNum() || other->getHp() > player->getHp()) {
                        ahead = true;
                        break;
                    }
                }
                if (!ahead) result[player] << SkillInstanceUtils::formatName(objectName(), id);
            }
        } else if (event == TargetConfirmed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.card->isKindOf("Peach") || !use.to.contains(player)) return result;
            for (int id : player->getValidSkillInstanceIds(objectName())) {
                const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
                if (room->getShimingStatus(ref) < 1)
                    result[player] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive()) return false;
        if (event == EventPhaseStart) {
            if (!room->sendShimingLog(ctx.activationRef, true)) return false;
            room->gainMaxHp(player, 1, objectName());
            room->handleAcquireDetachSkills(player, "-ifanjie|ifhuangchu");
            return false;
        }
        if (!ctx.original_data) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Peach") || !use.to.contains(player)) return false;
        if (room->getShimingStatus(ctx.activationRef) >= 1) return false;
        room->sendCompulsoryTriggerLog(player, this);
        if (use.from != player) {
            use.nullified_list << player->objectName();
            ctx.original_data->setValue(use);
        } else {
            room->setCardFlag(use.card, "iflitian2Bf");
        }
        return false;
    }
};

class IfHuangchu : public TriggerSkillV2
{
public:
    IfHuangchu() : TriggerSkillV2("ifhuangchu$")
    {
        events << EventPhaseStart;
        frequency = Skill::Compulsory;
        waked_skills = "_iffeileijia";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || (player->getPhase() != Player::Start && player->getPhase() != Player::Finish)) return result;
        if (!player->hasLordSkill(objectName())) return result;
        addPrimary(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player || !player->isAlive()) return false;
        if (player->getPhase() == Player::Start) {
            room->sendCompulsoryTriggerLog(player, this);
            for (ServerPlayer *other : room->getOtherPlayers(player))
                room->doAnimate(1, player->objectName(), other->objectName());
            QList<int> ids;
            for (ServerPlayer *other : room->getOtherPlayers(player)) {
                if (!other->isAlive()) continue;
                const int extra = other->getHandcardNum() - qMin(3, other->getMaxHp());
                if (extra <= 0) continue;
                Card *discarded = room->askForDiscard(other, objectName(), extra, extra);
                if (discarded) ids << discarded->getSubcards();
            }
            for (int id : QList<int>(ids)) {
                if (room->getCardOwner(id)) ids.removeOne(id);
            }
            if (!ids.isEmpty() && player->isAlive()) {
                const QList<ServerPlayer *> assigned = player->assignmentCards(ids, "ifhuangchu|ifhuangchu0",
                    room->getAlivePlayers(), ids.length(), ids.length(), true);
                if (assigned.contains(player)) return false;
            }
            room->recover(player, RecoverStruct(objectName(), player));
        } else if (player->getPhase() == Player::Finish) {
            const Card *treasure = player->getTreasure();
            if (treasure && treasure->objectName() == "_iffeileijia") return false;
            room->sendCompulsoryTriggerLog(player, this);
            player->getDerivativeCard("_iffeileijia");
        }
        return false;
    }
};

class IfRenli : public TriggerSkillV2
{
public:
    IfRenli() : TriggerSkillV2("ifrenli") { events << EventPhaseStart << Dying; }

    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::NotActive
            || !sponsorOnce(room, ctx, objectName())) return;
        for (ServerPlayer *marked : room->getAllPlayers()) {
            if (marked->getMark("ifrenliBf") <= 0) continue;
            marked->setMark("ifrenliBf", 0);
            marked->gainAnExtraTurn();
        }
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Dying || !player || !player->isAlive()) return result;
        const DyingStruct dying = data.value<DyingStruct>();
        if (dying.who && dying.who != player && player->getHandcardNum() > 0)
            addPrimary(result, player, objectName());
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const DyingStruct dying = ctx.original_data ? ctx.original_data->value<DyingStruct>() : DyingStruct();
        return ctx.owner && dying.who && ctx.owner->askForSkillInvoke(objectName(), dying.who);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !ctx.original_data) return false;
        const DyingStruct dying = ctx.original_data->value<DyingStruct>();
        if (!dying.who) return false;
        player->addMark(dying.who->objectName() + "ifrenliDying_lun");
        if (player->getMark(dying.who->objectName() + "ifrenliDying_lun") != 1) return false;
        Card *given = room->askForExchange(player, objectName(), 2, 2, false, "ifrenli0");
        if (!given) return false;
        room->giveCard(player, dying.who, given, objectName());
        if (dying.who->getHp() + 1 > 0) dying.who->addMark("ifrenliBf");
        room->recover(dying.who, RecoverStruct(objectName(), player));
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
            addPrimary(result, player, objectName());
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->askForSkillInvoke(objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player || !player->isAlive()) return false;
        for (ServerPlayer *other : room->getOtherPlayers(player))
            room->doAnimate(1, player->objectName(), other->objectName());
        int refused = 0;
        QList<ServerPlayer *> participants;
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            if (!other->isAlive()) continue;
            Card *given = room->askForExchange(other, objectName(), 1, 1, true, "ifmingduan0", true);
            if (given) {
                participants << other;
                room->moveCardsInToDrawpile(other, given, objectName(), 1);
            } else {
                refused++;
            }
        }
        if (!player->isAlive()) return false;
        const QList<int> shown = room->showDrawPile(player, player->aliveCount(), objectName());
        int red = 0;
        for (int id : shown) {
            Card *card = Sanguosha->getCard(id);
            if (card->isRed()) red++;
            else if (card->isBlack()) red--;
        }
        room->getThread()->delay();
        room->throwCard(shown, objectName(), nullptr);
        if (red > 0) {
            ServerPlayer *target = room->askForPlayerChosen(player, participants, "ifmingduan1", "ifmingduan1");
            if (target) {
                room->doAnimate(1, player->objectName(), target->objectName());
                room->recover(target, RecoverStruct(objectName(), player));
            }
        } else if (red < 0) {
            ServerPlayer *target = room->askForPlayerChosen(player, participants, "ifmingduan2", "ifmingduan2");
            if (target) {
                room->doAnimate(1, player->objectName(), target->objectName());
                room->damage(DamageStruct(objectName(), player, target));
            }
        }
        player->drawCards(refused, objectName());
        return false;
    }
};

IfSixiangCard::IfSixiangCard() {}

bool IfSixiangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool IfSixiangCard::targetFixed() const
{
    if (user_string.isEmpty() || Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
        return true;
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    card->deleteLater();
    return card->targetFixed();
}

bool IfSixiangCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card *card = Sanguosha->cloneCard(user_string.split("+").first());
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *IfSixiangCard::validateInResponse(ServerPlayer *player) const
{
    Room *room = player->getRoom();
    QList<int> legal, hidden;
    for (int id : room->getDrawPile()) {
        Card *card = Sanguosha->getCard(id);
        bool matches = false;
        for (const QString &name : user_string.split("+")) {
            if (card->sameNameWith(name)) matches = true;
        }
        if (player->isLocked(card)) matches = false;
        if (matches && player->getMark(card->getType() + "ifsixiangBan-Clear") < 1) legal << id;
        else hidden << id;
        if (legal.length() + hidden.length() > 2) break;
    }
    room->setPlayerFlag(player, "ifsixiangFailed");
    room->fillAG(legal + hidden, player, hidden);
    const int id = room->askForAG(player, legal, true, "ifsixiang");
    room->clearAG(player);
    if (id < 0) return nullptr;
    Card *card = Sanguosha->getCard(id);
    room->addPlayerMark(player, card->getType() + "ifsixiangBan-Clear");
    return card;
}

const Card *IfSixiangCard::validate(CardUseStruct &use) const
{
    Room *room = use.from->getRoom();
    if (user_string.isEmpty()) {
        QList<int> legal, hidden;
        for (int id : room->getDrawPile()) {
            Card *card = Sanguosha->getCard(id);
            if (use.from->getMark(card->getType() + "ifsixiangBan-Clear") < 1 && card->isAvailable(use.from))
                legal << id;
            else
                hidden << id;
            if (legal.length() + hidden.length() > 2) break;
        }
        room->fillAG(legal + hidden, use.from, hidden);
        const int id = room->askForAG(use.from, legal, true, "ifsixiang");
        room->clearAG(use.from);
        if (id >= 0) {
            Card *card = Sanguosha->getCard(id);
            room->setPlayerMark(use.from, "ifsixiangId", id);
            room->askForUseCard(use.from, "@@ifsixiang", "ifsixiang0:" + card->objectName(), -1,
                Card::MethodUse, true, nullptr, nullptr, "ifsixiangUse");
        }
        return nullptr;
    }
    return validateInResponse(use.from);
}

class IfSixiangViewAs : public ViewAsSkillV2
{
public:
    IfSixiangViewAs() : ViewAsSkillV2("ifsixiang") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.pattern == "@@ifsixiang") return request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
        bool dead = false;
        for (const Player *other : player->getSiblings()) {
            if (other->isDead()) {
                dead = true;
                break;
            }
        }
        if (!dead || player->hasFlag("ifsixiangFailed")) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
        for (const QString &name : request.pattern.split("+")) {
            Card *card = Sanguosha->cloneCard(name);
            if (!card) continue;
            card->deleteLater();
            if (player->getMark(card->getType() + "ifsixiangBan-Clear") < 1) return true;
        }
        return false;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return nullptr;
        if (request.pattern == "@@ifsixiang")
            return Sanguosha->getCard(request.initiator->getMark("ifsixiangId"));
        IfSixiangCard *card = new IfSixiangCard;
        card->setSkillName(objectName());
        card->setUserString(request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? QString() : request.pattern);
        return card;
    }

    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &) const override { return true; }
};

class IfSixiang : public TriggerSkillV2
{
public:
    IfSixiang() : TriggerSkillV2("ifsixiang")
    {
        events << PreCardUsed;
        view_as_skill = new IfSixiangViewAs;
    }

    void record(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!firstInstance(ctx, objectName()) || ctx.owner != player || !ctx.original_data) return;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card && use.card->hasFlag("ifsixiangUse"))
            room->addPlayerMark(player, use.card->getType() + "ifsixiangBan-Clear");
    }
};

IfJizhiCard::IfJizhiCard() { target_fixed = false; }

bool IfJizhiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *slash = Sanguosha->cloneCard("slash");
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

const Card *IfJizhiCard::validateInResponse(ServerPlayer *player) const
{
    Room *room = player->getRoom();
    QList<ServerPlayer *> lords;
    for (ServerPlayer *other : room->getOtherPlayers(player)) {
        if (other->hasLordSkill("ifjizhi") && other->getMark("ifjizhiBan-Clear") < 1) lords << other;
    }
    ServerPlayer *lord = room->askForPlayerChosen(player, lords, "ifjizhi", "ifjizhi0");
    if (!lord) return nullptr;
    room->addPlayerMark(lord, "ifjizhiBan-Clear");
    player->skillInvoked("ifjizhi", -1, lord);
    lord->drawCards(1, "ifjizhi");
    return room->askForCard(lord, "slash", "ifjizhi1:" + player->objectName(),
        QVariant::fromValue(player), Card::MethodResponse, player);
}

const Card *IfJizhiCard::validate(CardUseStruct &use) const
{
    return validateInResponse(use.from);
}

class IfJizhiViewAs : public ViewAsSkillV2
{
public:
    IfJizhiViewAs() : ViewAsSkillV2("ifjizhivs&") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        bool lord = false;
        for (const Player *other : player->getAliveSiblings()) {
            if (other->hasLordSkill("ifjizhi") && other->getMark("ifjizhiBan-Clear") < 1) {
                lord = true;
                break;
            }
        }
        if (!lord) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return Slash::IsAvailable(player);
        return request.pattern.contains("slash");
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        IfJizhiCard *card = new IfJizhiCard;
        card->setSkillName(objectName());
        Q_UNUSED(request);
        return card;
    }

    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &) const override { return true; }
};

class IfJizhi : public TriggerSkillV2
{
public:
    IfJizhi() : TriggerSkillV2("ifjizhi$")
    {
        events << GameStart << EventAcquireSkill << EventLoseSkill;
    }

    bool acceptsRemovalEvent(TriggerEvent event, const QVariant &) const override
    {
        return event == EventLoseSkill;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventLoseSkill) {
            SkillChangeStruct change;
            if (!player || !change.tryParse(data) || change.skillName != objectName()) return result;
            bool remains = false;
            for (ServerPlayer *alive : room->getAlivePlayers()) {
                if (alive->hasLordSkill(objectName(), true)) {
                    remains = true;
                    break;
                }
            }
            if (!remains) result[player] << objectName();
            return result;
        }
        for (ServerPlayer *lord : room->findPlayersBySkillName(objectName())) {
            if (lord->hasLordSkill(objectName())) {
                addPrimary(result, lord, objectName());
                break;
            }
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &) const override
    {
        if (event == EventLoseSkill) {
            for (ServerPlayer *alive : room->getAlivePlayers())
                room->detachSkillFromPlayer(alive, "ifjizhivs", false, true, false);
            return false;
        }
        for (ServerPlayer *alive : room->getAlivePlayers()) {
            if (alive->hasSkill("ifjizhivs")) continue;
            room->attachSkillToPlayer(alive, "ifjizhivs");
        }
        return false;
    }
};

class IfHaitian : public TriggerSkillV2
{
public:
    IfHaitian() : TriggerSkillV2("ifhantian")
    {
        events << TargetConfirmed << TargetSpecified << DamageDone << EventPhaseChanging;
    }

    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return;
        if (event == DamageDone) {
            if (!firstInstance(ctx, objectName()) || ctx.owner != player) return;
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.card && damage.card->getSkillNames().contains(objectName()))
                player->setTag("ifhantianDone", true);
            return;
        }
        if (event != EventPhaseChanging || ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive
            || !sponsorOnce(room, ctx, objectName())) return;
        for (ServerPlayer *alive : room->getAlivePlayers()) {
            if (alive->getMark("ifhantianBf-Clear") <= 0) continue;
            room->removePlayerCardLimitation(alive, "use,response", ".|.|.|hand");
            alive->setMark("ifhantianBf-Clear", 0);
        }
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetConfirmed && event != TargetSpecified) return result;
        if (!player || player->getMark("ifhantianUse-Clear") > 0) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || use.to.length() != 1 || !use.card || use.card->getTypeId() < 1) return result;
        if (event == TargetConfirmed) {
            if (!use.to.contains(player) || use.from == player) return result;
        } else if (use.to.last() == player) {
            return result;
        }
        addPrimary(result, player, objectName());
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive() || !ctx.original_data) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || use.to.isEmpty()) return false;
        ServerPlayer *other = event == TargetConfirmed ? use.from : use.to.last();
        player->addMark("ifhantianUse-Clear");
        player->setTag("ifhantianDone", false);
        Card *lightning = Sanguosha->cloneCard("lightning");
        lightning->setSkillName(objectName());
        lightning->deleteLater();
        CardEffectStruct effect;
        effect.card = lightning;
        effect.to = player;
        lightning->onEffect(effect);
        if (player->getTag("ifhantianDone").toBool() || !player->isAlive() || !other || !other->isAlive())
            return false;
        for (int i = 0; i < 2; ++i) {
            QList<ServerPlayer *> choices;
            if (player->canDiscard(player, "he")) choices << player;
            if (player->canDiscard(other, "he")) choices << other;
            ServerPlayer *chosen = room->askForPlayerChosen(player, choices, objectName(), "ifhantian0");
            if (!chosen) continue;
            room->doAnimate(1, player->objectName(), chosen->objectName());
            const int id = room->askForCardChosen(player, chosen, "he", objectName(), false, Card::MethodDiscard);
            if (id < 0) continue;
            if (other->getMark("ifhantianBf-Clear") < 1 && Sanguosha->getCard(id)->isKindOf("EquipCard")) {
                room->setPlayerCardLimitation(other, "use,response", ".|.|.|hand", false);
                other->addMark("ifhantianBf-Clear");
            }
            room->throwCard(id, objectName(), chosen, player);
        }
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
        if (!player) return ids;
        for (const Card *hand : player->getHandcards()) {
            if (!hand->isDamageCard()) ids << hand->getEffectiveId();
        }
        return ids;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || materials(request.initiator).isEmpty()) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return Slash::IsAvailable(request.initiator);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern.contains("slash");
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const QList<int> ids = materials(request.initiator);
        if (ids.isEmpty()) return nullptr;
        Card *slash = Sanguosha->cloneCard("slash");
        slash->setSkillName(objectName());
        slash->addSubcards(ids);
        return slash;
    }

    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &) const override { return true; }
};

class IfShenfeng : public TriggerSkillV2
{
public:
    IfShenfeng() : TriggerSkillV2("ifshenfeng")
    {
        events << PreCardUsed << Damage;
        view_as_skill = new IfShenfengViewAs;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event != Damage) return false;
        if (!player || !player->isAlive()) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->getSkillNames().contains(objectName()) || !player->canDiscard(player, "h"))
            return true;
        return watchOnce(event, room, player, data, objectName(), out);
    }

    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != PreCardUsed || !firstInstance(ctx, objectName()) || ctx.owner != player || !ctx.original_data)
            return;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.card->getSkillNames().contains(objectName())) return;
        player->addQinggangTag(use.card);
        QStringList types;
        for (int id : use.card->getSubcards()) {
            const QString type = Sanguosha->getCard(id)->getType();
            if (!types.contains(type)) types << type;
        }
        if (types.length() > 2) {
            use.m_addHistory = false;
            ctx.original_data->setValue(use);
        }
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive() || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to || !damage.to->canDiscard(damage.to, "h")) return false;
        const int count = damage.to->getHandcardNum() / 2;
        Card *discarded = room->askForDiscard(damage.to, objectName(), count, count);
        if (!discarded) return false;
        QList<int> ids;
        for (int id : discarded->getSubcards()) {
            if (!room->getCardOwner(id)) ids << id;
        }
        Card *obtained = dummyCard();
        room->fillAG(ids, player);
        while (!ids.isEmpty()) {
            const int chosen = room->askForAG(player, ids, false, objectName());
            obtained->addSubcard(chosen);
            const QString name = Sanguosha->getEngineCard(chosen)->objectName(false);
            for (int id : QList<int>(ids)) {
                if (Sanguosha->getEngineCard(id)->objectName(false) != name) continue;
                room->takeAG(player, id, false, QList<ServerPlayer *>() << player);
                ids.removeOne(id);
            }
        }
        room->clearAG();
        player->obtainCard(obtained);
        return false;
    }
};

class IfXiechang : public TriggerSkillV2
{
public:
    IfXiechang() : TriggerSkillV2("ifxiechang")
    {
        events << EventPhaseChanging << Dying;
        frequency = Compulsory;
    }

    Frequency getFrequency(const Player *target) const override
    {
        if (target && target->getMark("ifxiechangUp") > 0) return NotFrequent;
        return Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return result;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
            Card *slash = Sanguosha->cloneCard("fire_slash");
            slash->setSkillName(objectName());
            slash->deleteLater();
            for (ServerPlayer *other : room->getOtherPlayers(player)) {
                if (other->getEquips().length() >= player->getEquips().length() && player->canSlash(other, slash, false)) {
                    addPrimary(result, player, objectName());
                    break;
                }
            }
        } else {
            const DyingStruct dying = data.value<DyingStruct>();
            if (dying.damage && dying.damage->card && dying.damage->card->getSkillNames().contains(objectName())
                && dying.damage->from == player && dying.who && dying.who->getCardCount() > 0)
                addPrimary(result, player, objectName());
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive() || !ctx.original_data) return false;
        if (event == EventPhaseChanging) {
            Card *probe = Sanguosha->cloneCard("fire_slash");
            probe->setSkillName(objectName());
            probe->deleteLater();
            QList<ServerPlayer *> targets;
            for (ServerPlayer *other : room->getOtherPlayers(player)) {
                if (other->getEquips().length() >= player->getEquips().length() && player->canSlash(other, probe, false))
                    targets << other;
            }
            if (targets.isEmpty()) return false;
            if (getFrequency(player) == Compulsory) {
                room->sendCompulsoryTriggerLog(player, this);
                for (ServerPlayer *target : targets)
                    room->doAnimate(1, player->objectName(), target->objectName());
            } else {
                targets = room->askForPlayersChosen(player, targets, objectName(), 0, 9, "ifxiechang0", true, true);
                if (targets.isEmpty()) return false;
            }
            room->loseHp(player, 1, true, player, objectName());
            for (ServerPlayer *target : targets) {
                if (!player->isAlive()) break;
                Card *slash = Sanguosha->cloneCard("fire_slash");
                slash->setSkillName("_" + objectName());
                if (player->canSlash(target, slash, false))
                    room->useCard(CardUseStruct(slash, player, target));
                slash->deleteLater();
            }
            return false;
        }
        const DyingStruct dying = ctx.original_data->value<DyingStruct>();
        if (!dying.who) return false;
        const int id = room->askForCardChosen(player, dying.who, "he", objectName());
        if (id >= 0) room->obtainCard(player, id, false);
        if (getFrequency(player) != Compulsory)
            room->recover(player, RecoverStruct(objectName(), player));
        return false;
    }
};

class IfTianmin : public TriggerSkillV2
{
public:
    IfTianmin() : TriggerSkillV2("iftianmin")
    {
        events << Dying << Death;
        limit_mark = "@iftianmin";
        frequency = Limited;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != Death || !firstInstance(ctx, objectName()) || ctx.owner != player
            || !player->isAlive() || !ctx.original_data) return;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        if (death.damage && death.damage->from == player) player->addMark("iftianminKill");
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Dying || !player) return result;
        const DyingStruct dying = data.value<DyingStruct>();
        if (dying.who == player && player->getMark("@iftianmin") > 0 && player->getMark("iftianminKill") < 1)
            addPrimary(result, player, objectName());
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data);
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner->getMark(limit_mark) <= 0) return false;
        room->removePlayerMark(ctx.owner, limit_mark);
        room->doSuperLightbox(ctx.owner, objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player || !player->isAlive()) return false;
        room->recover(player, RecoverStruct(objectName(), player, player->getMaxHp() - player->getHp()));
        room->addPlayerMark(player, "ifxiechangUp");
        room->changeTranslation(player, "ifxiechang", 1);
        return false;
    }
};

class IfJianxiao : public TriggerSkillV2
{
public:
    IfJianxiao() : TriggerSkillV2("ifjianxiao") { events << TargetSpecified << ConfirmDamage; }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event != ConfirmDamage) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !damage.card || !damage.card->hasFlag("ifjianxiaoBf")) return true;
        return watchOnce(event, room, player, data, objectName(), out);
    }

    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != TargetSpecified || !firstInstance(ctx, objectName()) || ctx.owner != player
            || !ctx.original_data) return;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() <= 0 || !player->hasTurn()) return;
        QStringList previous;
        for (const QString &mark : player->getMarkNames()) {
            if (mark.contains("ifjianxiaoTo") && player->getMark(mark) > 0) previous << mark;
        }
        player->setTag("ifjianxiaoPrev", previous);
        for (const QString &mark : previous) player->setMark(mark, 0);
        for (ServerPlayer *target : use.to)
            player->addMark(target->objectName() + "ifjianxiaoTo-Clear");
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecified || !player || !player->hasTurn()) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash") || use.to.length() != 1 || use.to.last() == player) return result;
        const QStringList previous = player->getTag("ifjianxiaoPrev").toStringList();
        if (previous.contains(use.to.last()->objectName() + "ifjianxiaoTo-Clear"))
            addPrimary(result, player, objectName());
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        if (event == TargetSpecified) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (use.card) room->setCardFlag(use.card, "ifjianxiaoBf");
            return false;
        }
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.card || !damage.card->hasFlag("ifjianxiaoBf") || !player) return false;
        damage.damage++;
        ctx.original_data->setValue(damage);
        Q_UNUSED(room);
        return false;
    }
};

class IfShenwu : public TriggerSkillV2
{
public:
    IfShenwu() : TriggerSkillV2("ifshenwu") { events << EventPhaseStart << DamageCaused; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == EventPhaseStart) {
            if (player->getPhase() == Player::Play && player->canDiscard(player, "h"))
                addPrimary(result, player, objectName());
            return result;
        }
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || damage.card->getTypeId() <= 0) return result;
        for (const Card *hand : player->getHandcards()) {
            if (hand->isDamageCard()) return result;
        }
        addPrimary(result, player, objectName());
        return result;
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart) return true;
        return ctx.owner && ctx.owner->askForSkillInvoke(objectName());
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive()) return false;
        if (event == EventPhaseStart) {
            player->throwAllHandCards(objectName());
            if (!player->isAlive()) return false;
            Card *obtained = dummyCard();
            QStringList names;
            names << "slash" << "fire_slash" << "thunder_slash" << "duel";
            for (int id : room->getDrawPile()) {
                Card *card = Sanguosha->getCard(id);
                if (!names.contains(card->objectName())) continue;
                names.removeOne(card->objectName());
                obtained->addSubcard(id);
            }
            player->obtainCard(obtained);
            return false;
        }
        if (!ctx.original_data) return false;
        room->sendCompulsoryTriggerLog(player, this);
        return player->damageRevises(*ctx.original_data, 1);
    }
};

class IfBashi : public TriggerSkillV2
{
public:
    IfBashi() : TriggerSkillV2("ifbashi")
    {
        events << CardUsed << PreCardUsed;
        frequency = Compulsory;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != CardUsed || !firstInstance(ctx, objectName()) || ctx.owner != player || !ctx.original_data)
            return;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card && use.card->isDamageCard())
            player->addMark(use.card->objectName(false) + "ifbashiUse-Clear");
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardUsed || !player) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isDamageCard()) return result;
        if (player->getMark(use.card->objectName(false) + "ifbashiUse-Clear") == 1)
            addPrimary(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive() || !ctx.original_data) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || player->getMark(use.card->objectName(false) + "ifbashiUse-Clear") != 1) return false;
        room->sendCompulsoryTriggerLog(player, this);
        use.m_addHistory = false;
        use.to.clear();
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            if (player->isProhibited(other, use.card)) continue;
            room->doAnimate(1, player->objectName(), other->objectName());
            use.to << other;
        }
        QStringList suits;
        suits << "spade" << "club" << "diamond" << "heart" << "no_suit";
        for (ServerPlayer *target : use.to) {
            if (!target->isAlive()) continue;
            QStringList choices;
            choices << "1";
            for (const Card *hand : target->getHandcards()) {
                if (suits.contains(hand->getSuitString()) && target->canDiscard(target, hand->getId())) {
                    choices << "2";
                    break;
                }
            }
            if (target->getCardCount() > 1 && player->isAlive()) choices << "3=" + player->objectName();
            const QString choice = room->askForChoice(target, objectName(), choices.join("+"), *ctx.original_data);
            if (choice == "1") {
                use.no_respond_list << target->objectName();
            } else if (choice == "2") {
                Card *discarded = room->askForDiscard(target, objectName(), 1, 1, false, false, "", ".|" + suits.join(","));
                if (discarded) suits.removeOne(discarded->getSuitString());
            } else {
                Card *taken = dummyCard();
                for (int i = 0; i < 2; ++i) {
                    const int id = room->askForCardChosen(player, target, "he", objectName(), false,
                        Card::MethodNone, taken->getSubcards(), true);
                    if (id < 0) break;
                    taken->addSubcard(id);
                }
                player->obtainCard(taken, false);
                player->drawCards(1, objectName());
            }
        }
        ctx.original_data->setValue(use);
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
            addPrimary(result, holder, objectName());
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !owner->isAlive() || !player) return false;
        if (event == TurnedOver) {
            room->sendCompulsoryTriggerLog(owner, this);
            room->doAnimate(1, owner->objectName(), player->objectName());
            if (player->faceUp()) {
                const int id = room->askForCardChosen(owner, player, "he", objectName(), false, Card::MethodDiscard);
                if (id >= 0) room->throwCard(id, objectName(), player, owner);
            } else {
                player->drawCards(1, objectName());
            }
            return false;
        }
        if (!ctx.original_data || player->faceUp()) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to) return false;
        player->damageRevises(*ctx.original_data, damage.to->faceUp() ? 1 : -1);
        return ctx.original_data->value<DamageStruct>().damage < 1;
    }
};

class IfBaqiViewAs : public ViewAsSkillV2
{
public:
    IfBaqiViewAs() : ViewAsSkillV2("ifbaqi") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.pattern == "@@ifbaqi") return request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->usedTimes("IfBaqiCard") < 1;
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        return request.pattern == "@@ifbaqi" ? QString("Slash") : QString("IfBaqiCard");
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.pattern == "@@ifbaqi") {
            Card *slash = Sanguosha->cloneCard("slash");
            if (!slash) return nullptr;
            slash->setSkillName("_ifbaqi");
            return slash;
        }
        return ViewAsSkillV2::createCard(request);
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (request.pattern == "@@ifbaqi") return false;
        return selected.isEmpty() && target && target->isAlive() && target != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        if (request.pattern == "@@ifbaqi") return true;
        return targets.size() == 1;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        for (ServerPlayer *other : room->getAllPlayers()) {
            if (other == target || !other->isAlive()) continue;
            room->askForUseCard(other, "@@ifbaqi", "ifbaqi0");
        }
        return ContinueEffects;
    }
};

class IfBaqi : public TriggerSkillV2
{
public:
    IfBaqi() : TriggerSkillV2("ifbaqi")
    {
        events << PreCardUsed;
        view_as_skill = new IfBaqiViewAs;
    }

    bool collectTriggerContexts(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (!player || !player->isAlive()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash") || !use.card->getSkillNames().contains(objectName()))
            return true;
        return watchOnce(PreCardUsed, room, player, data, objectName(), out);
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        if (player && player->isAlive()) player->turnOver();
        return false;
    }
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
            addPrimary(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive() || !ctx.original_data) return false;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        if (!death.who) return false;
        room->sendCompulsoryTriggerLog(player, this);
        player->obtainCard(dummyCard(death.who->handCards()), false);
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
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == RoundEnd && player->getTag("iftunshi_skills").toStringList().isEmpty()) return result;
        addPrimary(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player) return false;
        if (event == Death) {
            if (!ctx.original_data) return false;
            const DeathStruct death = ctx.original_data->value<DeathStruct>();
            if (!death.who) return false;
            room->sendCompulsoryTriggerLog(player, this);
            QStringList skills = player->getTag("iftunshi_skills").toStringList();
            for (const Skill *skill : death.who->getVisibleSkillList()) {
                if (player->hasSkill(skill, true) || skill->isAttachedLordSkill()) continue;
                skills << skill->objectName();
            }
            player->setTag("iftunshi_skills", skills);
            room->handleAcquireDetachSkills(player, skills);
            return false;
        }
        const QStringList skills = player->getTag("iftunshi_skills").toStringList();
        QStringList lost;
        for (const QString &skill : skills) lost << "-" + skill;
        room->handleAcquireDetachSkills(player, lost);
        player->removeTag("iftunshi_skills");
        return false;
    }
};

class IfTianwei : public TriggerSkillV2
{
public:
    IfTianwei() : TriggerSkillV2("iftianwei") { events << TargetSpecified << Damage << RoundEnd; }

    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != RoundEnd || !firstInstance(ctx, objectName()) || ctx.owner != player) return;
        for (const QString &mark : player->getMarkNames()) {
            if (!mark.contains("iftianweiBanSkill_")) continue;
            const QString skill = mark.split("_").last();
            room->removePlayerMark(player, "Qingcheng_" + skill, player->getMark(mark));
            player->setMark(mark, 0);
        }
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && use.card->isBlack() && use.card->getTypeId() > 0)
                addPrimary(result, player, objectName());
        } else if (event == Damage) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.reason == objectName()) addPrimary(result, player, objectName());
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive() || !ctx.original_data) return false;
        if (event == Damage) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to) player->addMark(damage.to->objectName() + "iftianweiBan_lun", damage.damage);
            return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        for (ServerPlayer *target : use.to) {
            if (!player->isAlive() || !target || target == player || !target->isAlive()) continue;
            if (player->getMark(target->objectName() + "iftianweiBan_lun") >= 2) continue;
            int gap = 0;
            QStringList choices;
            for (const Skill *skill : target->getVisibleSkillList()) {
                if (skill->isAttachedLordSkill()) continue;
                choices << "1=" + skill->objectName();
                gap--;
            }
            for (const Skill *skill : player->getVisibleSkillList()) {
                if (skill->isAttachedLordSkill()) continue;
                gap++;
            }
            if (gap < 0 || !player->askForSkillInvoke(objectName(), target)) continue;
            gap = qMin(qMax(gap, 1), 3);
            choices << QString("2=%1").arg(gap);
            const QString choice = room->askForChoice(target, objectName(), choices.join("+"), *ctx.original_data);
            if (choice.contains("1=")) {
                const QString skill = choice.split("=").last();
                room->addPlayerMark(target, "Qingcheng_" + skill);
                target->addMark("iftianweiBanSkill_" + skill);
                target->drawCards(2, objectName());
            } else {
                room->damage(DamageStruct(objectName(), player, target, gap));
            }
        }
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
        return request.initiator && request.pattern == "@@ifxiongzheng"
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
        if (!cardSelectionFeasible(request)) return nullptr;
        Card *slash = Sanguosha->cloneCard("slash");
        if (!slash) return nullptr;
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        return slash;
    }

    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &) const override { return true; }
};

class IfXiongzheng : public TriggerSkillV2
{
public:
    IfXiongzheng() : TriggerSkillV2("ifxiongzheng")
    {
        events << RoundStart << CardFinished << Damaged;
        view_as_skill = new IfXiongzhengViewAs;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event != CardFinished) return false;
        if (!player || !player->isAlive()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() <= 0) return true;
        bool marked = false;
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            if (other->getMark(player->objectName() + "ifxiongzhengBan_lun") > 0) {
                marked = true;
                break;
            }
        }
        if (!marked) return true;
        return watchOnce(event, room, player, data, objectName(), out);
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == CardFinished || !player) return result;
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            if (player->getMark(other->objectName() + "ifxiongzhengBan_lun") < 1) {
                addPrimary(result, player, objectName());
                break;
            }
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == CardFinished || !ctx.owner) return event == CardFinished;
        QList<ServerPlayer *> targets;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner)) {
            if (ctx.owner->getMark(other->objectName() + "ifxiongzhengBan_lun") < 1) targets << other;
        }
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "ifxiongzheng0", true, true);
        if (!target) return false;
        ctx.targets = QList<ServerPlayer *>{target};
        Q_UNUSED(player);
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != CardFinished) {
            ServerPlayer *owner = ctx.owner;
            ServerPlayer *target = ctx.targets.isEmpty() ? nullptr : ctx.targets.first();
            if (!owner || !target) return false;
            owner->addMark(target->objectName() + "ifxiongzhengBan_lun");
            if (target->getHandcardNum() <= 0) return false;
            const int id = room->askForCardChosen(owner, target, "h", objectName());
            if (id < 0) return false;
            room->showCard(target, id);
            const QString type = Sanguosha->getCard(id)->getType();
            room->setPlayerMark(target, "&ifxiongzheng+:+" + type + "+#" + owner->objectName() + "_lun", 1);
            return false;
        }
        if (!player || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            if (other->getMark(player->objectName() + "ifxiongzhengBan_lun") <= 0) continue;
            const QString typeMark = "&ifxiongzheng+:+" + use.card->getType() + "+#" + other->objectName() + "_lun";
            if (player->getMark(typeMark) > 0) {
                room->sendCompulsoryTriggerLog(other, objectName());
                other->drawCards(1, objectName());
            } else {
                room->setPlayerProperty(other, "ifxiongzhengSlash", player->objectName());
                room->askForUseCard(other, "@@ifxiongzheng", "ifxiongzheng1:" + player->objectName());
            }
        }
        return false;
    }
};

class IfXiongzhengBf : public ProhibitSkill
{
public:
    IfXiongzhengBf() : ProhibitSkill("#IfXiongzhengBf") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return card && from && to && card->getSkillName() == "ifxiongzheng"
            && from->property("ifxiongzhengSlash").toString() != to->objectName();
    }
};

IfEjiangCard::IfEjiangCard() {}

bool IfEjiangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.isEmpty() && Self->property("ifejiangTo").toString() != to_select->objectName()) return false;
    Card *chain = Sanguosha->cloneCard("iron_chain");
    chain->deleteLater();
    return chain->targetFilter(targets, to_select, Self);
}

const Card *IfEjiangCard::validate(CardUseStruct &use) const
{
    use.from->getRoom()->addPlayerMark(use.from, "ifejiangBan-Clear");
    Card *chain = Sanguosha->cloneCard("iron_chain");
    chain->setSkillName("ifejiang");
    chain->deleteLater();
    return chain;
}

class IfEjiangViewAs : public ViewAsSkillV2
{
public:
    IfEjiangViewAs() : ViewAsSkillV2("ifejiang") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@ifejiang"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("ifejiangBan-Clear") < 1;
    }

    const Card *createCard(const ActiveSkillRequest &) const override
    {
        IfEjiangCard *card = new IfEjiangCard;
        card->setSkillName(objectName());
        return card;
    }

    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &) const override { return true; }
};

class IfEjiang : public TriggerSkillV2
{
public:
    IfEjiang() : TriggerSkillV2("ifejiang")
    {
        events << Damaged;
        view_as_skill = new IfEjiangViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        for (ServerPlayer *holder : room->getAllPlayers()) {
            if (holder->getMark("ifejiangBan-Clear") < 1) addPrimary(result, holder, objectName());
        }
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner || !player) return false;
        room->setPlayerProperty(ctx.owner, "ifejiangTo", player->objectName());
        room->askForUseCard(ctx.owner, "@@ifejiang", "ifejiang0:" + player->objectName());
        return false;
    }
};

class IfPini : public TriggerSkillV2
{
public:
    IfPini() : TriggerSkillV2("ifpini") { events << ChainStateChanged << Dying; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        if (event == ChainStateChanged && player->isChained()) return result;
        if (event == Dying && data.value<DyingStruct>().who != player) return result;
        for (ServerPlayer *holder : room->getAllPlayers())
            addPrimary(result, holder, objectName());
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        return ctx.owner && player && ctx.owner->askForSkillInvoke(objectName(), player);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !player || !player->isAlive()) return false;
        QList<int> banned;
        for (const Card *card : player->getCards("ej")) {
            bool blocked = true;
            for (ServerPlayer *other : room->getOtherPlayers(player)) {
                if (other->isChained() != player->isChained() || owner->isProhibited(other, card)) continue;
                if (card->isKindOf("EquipCard")
                    && other->getEquip(static_cast<const EquipCard *>(card->getRealCard())->location())) continue;
                blocked = false;
                break;
            }
            if (blocked) banned << card->getId();
        }
        for (const Card *card : player->getCards("h")) {
            bool blocked = true;
            for (ServerPlayer *other : room->getOtherPlayers(player)) {
                if (other->isChained() != player->isChained()) {
                    blocked = false;
                    break;
                }
            }
            if (blocked) banned << card->getId();
        }
        if (banned.length() >= player->getCardCount(true, true)) return false;
        room->doAnimate(1, owner->objectName(), player->objectName());
        const int id = room->askForCardChosen(owner, player, "hej", objectName(), false, Card::MethodNone, banned);
        if (id < 0) return false;
        Card *card = Sanguosha->getCard(id);
        QList<ServerPlayer *> targets;
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            if (other->isChained() != player->isChained()) continue;
            if (room->getCardPlace(id) != Player::PlaceHand) {
                if (owner->isProhibited(other, card)) continue;
                if (card->isKindOf("EquipCard")
                    && other->getEquip(static_cast<const EquipCard *>(card->getRealCard())->location())) continue;
            }
            targets << other;
        }
        owner->setMark("ifpiniId", id);
        ServerPlayer *target = room->askForPlayerChosen(owner, targets, objectName(), "ifpini0");
        if (!target) return false;
        room->doAnimate(1, owner->objectName(), target->objectName());
        room->moveCardTo(card, target, room->getCardPlace(id),
            CardMoveReason(CardMoveReason::S_REASON_TRANSFER, owner->objectName(), objectName(), ""), false);
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
            if (lord->hasLordSkill(objectName())) addPrimary(result, lord, objectName());
        }
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        return player && ctx.owner && player->askForSkillInvoke(objectName(), "0:" + ctx.owner->objectName(), false);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !ctx.owner) return false;
        player->skillInvoked(objectName(), -1, ctx.owner);
        room->damage(DamageStruct(objectName(), player, player, 1, DamageStruct::Fire));
        QList<ServerPlayer *> drawers;
        drawers << player << ctx.owner;
        room->drawCards(drawers, 1, objectName());
        return false;
    }
};

class IfMoran : public TriggerSkillV2
{
public:
    IfMoran() : TriggerSkillV2("ifmoran") { events << EventPhaseChanging << CardUsed << CardFinished; }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event != CardFinished) return false;
        if (!player) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->hasFlag("ifmoranBf") || !use.card->hasFlag("DamageDone")) return true;
        return watchOnce(event, room, player, data, objectName(), out);
    }

    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventPhaseChanging || !ctx.original_data
            || ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive
            || !sponsorOnce(room, ctx, objectName())) return;
        for (ServerPlayer *alive : room->getAlivePlayers()) {
            if (alive->getMark("ifmoranBan-Clear") > 0)
                room->removePlayerCardLimitation(alive, "use", ".");
        }
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardUsed || !player || !player->isWounded()) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("NatureSlash")) return result;
        for (ServerPlayer *holder : room->getAllPlayers())
            addPrimary(result, holder, objectName());
        return result;
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != CardUsed) return true;
        return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        if (event == CardUsed) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.card || !ctx.owner) return false;
            room->setCardFlag(use.card, "ifmoranBf");
            room->setCardFlag(use.card, "ifmoranBfFrom" + ctx.owner->objectName());
            ctx.owner->addMark("ifmoranBan-Clear");
            if (ctx.owner->getMark("ifmoranBan-Clear") == 1)
                room->setPlayerCardLimitation(ctx.owner, "use", ".", false);
            return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.card->hasFlag("ifmoranBf") || !use.card->hasFlag("DamageDone")) return false;
        for (ServerPlayer *holder : room->getAllPlayers()) {
            if (use.card->hasFlag("ifmoranBfFrom" + holder->objectName()))
                holder->drawCards(2, objectName());
        }
        return false;
    }
};

class IfJilveViewAs : public ViewAsSkillV2
{
public:
    IfJilveViewAs() : ViewAsSkillV2("ifjilve", 1) {}

    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "IfJilveCard"; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->usedTimes("IfJilveCard") < 2;
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
        if (!ctx.use_card) return ContinueEffects;
        target->addToPile("ifji_bing", ctx.use_card->getSubcards(), false);
        return ContinueEffects;
    }
};

class IfJilve : public TriggerSkillV2
{
public:
    IfJilve() : TriggerSkillV2("ifjilve")
    {
        events << DamageInflicted;
        view_as_skill = new IfJilveViewAs;
    }

    bool collectTriggerContexts(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (!player || player->getPile("ifji_bing").isEmpty()) return true;
        return watchOnce(DamageInflicted, room, player, data, objectName(), out);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !ctx.original_data) return false;
        room->sendCompulsoryTriggerLog(player, objectName());
        const QList<int> ids = player->getPile("ifji_bing");
        room->throwCard(ids, objectName(), nullptr);
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.from && damage.card && !ids.isEmpty()
            && damage.card->getColor() != Sanguosha->getEngineCard(ids.last())->getColor()) {
            damage.to = damage.from;
            damage.transfer = true;
            damage.transfer_reason = objectName();
            ctx.original_data->setValue(damage);
            player->setTag("TransferDamage", *ctx.original_data);
            return true;
        }
        return false;
    }
};

class IfBujia : public TriggerSkillV2
{
public:
    IfBujia() : TriggerSkillV2("ifbujia")
    {
        events << EventPhaseChanging << CardsMoveOneTime;
        frequency = Compulsory;
    }

    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != CardsMoveOneTime || !firstInstance(ctx, objectName()) || ctx.owner != player
            || !player->hasFlag("CurrentPlayer") || !ctx.original_data) return;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.to == player && move.to_place == Player::PlaceHand) {
            player->addMark("ifbujiaNum-Clear", move.card_ids.length());
            if (player->hasSkill(objectName(), true))
                room->addPlayerMark(player, "&ifbujia+#num-Clear", move.card_ids.length());
        } else if (move.from == player && move.from_places.contains(Player::PlaceHand)) {
            int lost = 0;
            for (Player::Place place : move.from_places) {
                if (place == Player::PlaceHand) lost--;
            }
            player->addMark("ifbujiaNum-Clear", lost);
            if (player->hasSkill(objectName(), true))
                room->addPlayerMark(player, "&ifbujia+#num-Clear", lost);
        }
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseChanging && player && player->hasFlag("CurrentPlayer")
            && data.value<PhaseChangeStruct>().to == Player::NotActive)
            addPrimary(result, player, objectName());
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (!player || !player->isAlive()) return false;
        room->sendCompulsoryTriggerLog(player, this);
        if (player->getMark("ifbujiaNum-Clear") < 0)
            room->loseHp(player, 1, true, player, objectName());
        else
            room->recover(player, RecoverStruct(objectName(), player));
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
    if_caopi->addSkill(new IfXiongzhengBf);

    General *if_caocao = new General(this, "if_caocao$", "wei", 4);
    if_caocao->addSkill(new IfEjiang);
    if_caocao->addSkill(new IfPini);
    if_caocao->addSkill(new IfLianque);
    addMetaObject<IfEjiangCard>();

    General *if_fazheng = new General(this, "if_fazheng", "shu", 4, true, false, false, 2);
    if_fazheng->addSkill(new IfMoran);
    if_fazheng->addSkill(new IfJilve);
    if_fazheng->addSkill(new IfBujia);
}

ADD_PACKAGE(Dream)

