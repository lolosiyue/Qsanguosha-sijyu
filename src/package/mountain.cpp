#include "mountain.h"
//#include "general.h"
//#include "settings.h"
#include "engine.h"
#include "qt-collection-utils.h"
#include "standard.h"
#include "clientplayer.h"
//#include "client.h"
//#include "ai.h"
//#include "json.h"
//#include "util.h"
#include "room.h"
#include "roomthread.h"
#include "maneuvering.h"
#include "thicket.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>

QiaobianCard::QiaobianCard()
{
    mute = true;
    m_skillName = "qiaobian";
}

bool QiaobianCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Player::Phase phase = (Player::Phase)Self->getMark(m_skillName + "Phase");
    if (phase == Player::Draw)
        return targets.length() <= 2 && !targets.isEmpty();
    else if (phase == Player::Play)
        return targets.length() == 1;
    return false;
}

bool QiaobianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Player::Phase phase = (Player::Phase)Self->getMark(m_skillName + "Phase");
    if (phase == Player::Draw)
        return targets.length() < 2 && to_select != Self && !to_select->isKongcheng();
    else if (phase == Player::Play)
        return targets.isEmpty() && (!to_select->getJudgingArea().isEmpty() || !to_select->getEquips().isEmpty());
    return false;
}

static void moveQiaobianFieldCard(Room *room, ServerPlayer *zhanghe, ServerPlayer *from, const QString &skillName)
{
        if (!from->hasEquip() && from->getJudgingArea().isEmpty()) return;
        int card_id = room->askForCardChosen(zhanghe, from, "ej", skillName);
        const Card *card = Sanguosha->getCard(card_id);
        Player::Place place = room->getCardPlace(card_id);

        int equip_index = -1;
        if (place == Player::PlaceEquip) {
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            equip_index = static_cast<int>(equip->location());
        }

        QList<ServerPlayer *> tos;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (equip_index > -1) {
                if (p->getEquip(equip_index) == nullptr)
                    tos << p;
            } else {
                if (!zhanghe->isProhibited(p, card) && !p->containsTrick(card->objectName()))
                    tos << p;
            }
        }

        QString tag = "QiaobianTarget";
        if (skillName == "olqiaobian")
            tag = "OLQiaobianTarget";

        const QVariant previous = room->getTag(tag);
        const auto restore = qScopeGuard([&] {
            if (previous.isValid()) room->setTag(tag, previous); else room->removeTag(tag);
        });
        room->setTag(tag, QVariant::fromValue(from));
        ServerPlayer *to = room->askForPlayerChosen(zhanghe, tos, skillName, "@qiaobian-to:::" + card->objectName());
        if (to){
			room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, from->objectName(), to->objectName());
            room->moveCardTo(card, from, to, place,
			CardMoveReason(CardMoveReason::S_REASON_TRANSFER, zhanghe->objectName(), skillName, ""), true);
		}
}

void QiaobianCard::use(Room *room, ServerPlayer *zhanghe, QList<ServerPlayer *> &targets) const
{
    Player::Phase phase = (Player::Phase)zhanghe->getMark(m_skillName + "Phase");
    if (phase == Player::Draw) {
        foreach (ServerPlayer *target, targets) {
            if (zhanghe->isAlive() && target->isAlive())
                room->cardEffect(this, zhanghe, target);
        }
    } else if (phase == Player::Play) {
        if (!targets.isEmpty()) moveQiaobianFieldCard(room, zhanghe, targets.first(), m_skillName);
    }
}

void QiaobianCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (!effect.to->isKongcheng()) {
        int card_id = room->askForCardChosen(effect.from, effect.to, "h", m_skillName);
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
        room->obtainCard(effect.from, Sanguosha->getCard(card_id), reason, false);
    }
}

class QiaobianViewAsSkill : public ViewAsSkillV2
{
public:
    QiaobianViewAsSkill() : ViewAsSkillV2("qiaobian") {}
    int pendingPhase(const ActiveSkillRequest &request) const
    {
        return request.initiator && request.activationRef.isValid()
            ? request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "pending_phase", -1).toInt() : -1;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.pattern == "@@qiaobian" && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && (pendingPhase(request) == Player::Draw || pendingPhase(request) == Player::Play);
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        const Card *card = ViewAsSkillV2::createCard(request);
        if (card) card->tag["QiaobianPhase"] = pendingPhase(request);
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "QiaobianCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *candidate) const override
    {
        if (!candidate || !candidate->isAlive() || targets.contains(candidate)) return false;
        if (pendingPhase(request) == Player::Draw)
            return targets.size() < 2 && candidate != request.initiator && request.initiator->canGet(candidate, "h");
        return pendingPhase(request) == Player::Play && targets.isEmpty()
            && (!candidate->getJudgingArea().isEmpty() || !candidate->getEquips().isEmpty());
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        if (targets.isEmpty()) return false;
        QList<const Player *> prefix;
        for (const Player *target : targets) { if (!canSelectTarget(request, prefix, target)) return false; prefix << target; }
        return true;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { ctx.choice = QString::number(pendingPhase(request)); return canActivate(request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !actor->isAlive()) return ContinueEffects;
        Room *room = actor->getRoom();
        if (ctx.choice == "obtain" || ctx.choice == "move") {
            const QVariantMap selected = ctx.extra_data.toMap();
            const int id = selected.value("id", -1).toInt();
            ServerPlayer *from = room->findPlayerByObjectName(selected.value("from").toString());
            const Player::Place place = Player::Place(selected.value("place").toInt());
            if (!from || room->getCardOwner(id) != from || room->getCardPlace(id) != place) return ContinueEffects;
            const Card *card = Sanguosha->getCard(id);
            if (ctx.choice == "obtain") {
                if (getEffectiveAmount(ctx) > 0 && target->canGet(from, id)) room->obtainCard(target, id, false);
            } else if (from != target && !from->isCardLimited(card, Card::MethodMove)) {
                const auto *equip = qobject_cast<const EquipCard *>(card->getRealCard());
                const bool legal = place == Player::PlaceEquip ? equip && target->hasEquipArea(int(equip->location()))
                    && target->getEquip(int(equip->location())) == nullptr
                    : place == Player::PlaceDelayedTrick && target->hasJudgeArea() && !target->containsTrick(card->objectName())
                        && !room->isProhibited(actor, target, card);
                if (legal) room->moveCardTo(card, from, target, place,
                    CardMoveReason(CardMoveReason::S_REASON_TRANSFER, actor->objectName(), objectName(), QString()), true);
            }
            return ContinueEffects;
        }
        // The admitted phase survives bypass_cost and retirement of the prompt leaf.
        const int phase = ctx.use_card ? ctx.use_card->tag.value("QiaobianPhase", -1).toInt() : -1;
        if (phase == Player::Draw) {
            const int count = getEffectiveAmount(ctx);
            for (int i = 0; i < count && actor->isAlive() && target->isAlive() && actor->canGet(target, "h"); ++i) {
                const int id = room->askForCardChosen(actor, target, "h", objectName(), false, Card::MethodGet);
                if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
                    || Sanguosha->getCard(id)->hasFlag("using") || !actor->canGet(target, id)) continue;
                SkillContext obtain = ctx; obtain.choice = "obtain";
                obtain.extra_data = QVariantMap{{"id", id}, {"from", target->objectName()}, {"place", int(Player::PlaceHand)}};
                skillEffect(obtain, actor);
            }
        } else if (phase == Player::Play && !target->getCards("ej").isEmpty()) {
            const int id = room->askForCardChosen(actor, target, "ej", objectName(), false, Card::MethodNone);
            if (id < 0 || room->getCardOwner(id) != target) return ContinueEffects;
            const Player::Place place = room->getCardPlace(id);
            const Card *card = Sanguosha->getCard(id);
            if (target->isCardLimited(card, Card::MethodMove)) return ContinueEffects;
            const auto *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            QList<ServerPlayer *> destinations;
            for (ServerPlayer *other : room->getOtherPlayers(target)) {
                if (place == Player::PlaceEquip && equip && other->hasEquipArea(int(equip->location()))
                    && other->getEquip(int(equip->location())) == nullptr) destinations << other;
                else if (place == Player::PlaceDelayedTrick && other->hasJudgeArea()
                    && !other->containsTrick(card->objectName()) && !room->isProhibited(actor, other, card)) destinations << other;
            }
            if (destinations.isEmpty()) return ContinueEffects;
            const QVariant previous = room->getTag("QiaobianTarget");
            auto restore = qScopeGuard([&] { if (previous.isValid()) room->setTag("QiaobianTarget", previous); else room->removeTag("QiaobianTarget"); });
            room->setTag("QiaobianTarget", QVariant::fromValue(target)); // Scoped legacy AI hint, not authority.
            ServerPlayer *to = room->askForPlayerChosen(actor, destinations, objectName(), "@qiaobian-to:::" + card->objectName());
            SkillContext move = ctx; move.choice = "move";
            move.extra_data = QVariantMap{{"id", id}, {"from", target->objectName()}, {"place", int(place)}};
            skillEffect(move, to);
        }
        return ContinueEffects;
    }
};

class Qiaobian : public TriggerSkillV2
{
public:
    Qiaobian() : TriggerSkillV2("qiaobian") { events << EventPhaseChanging; view_as_skill = new QiaobianViewAsSkill; }
    static int phaseIndex(Player::Phase phase)
    {
        switch (phase) { case Player::Judge: return 1; case Player::Draw: return 2;
        case Player::Play: return 3; case Player::Discard: return 4; default: return 0; }
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Player::Phase phase = data.value<PhaseChangeStruct>().to;
        return player && player->isAlive() && player->hasSkill(objectName()) && player->canDiscard(player, "h")
            && phaseIndex(phase) > 0 && !player->isSkipped(phase) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const Player::Phase phase = ctx.original_data->value<PhaseChangeStruct>().to;
        const int previous = owner->getMark("qiaobianPhase");
        auto restore = qScopeGuard([&] { room->setPlayerMark(owner, "qiaobianPhase", previous); });
        room->setPlayerMark(owner, "qiaobianPhase", int(phase));
        const Card *selection = room->askForExchange(owner, objectName(), 1, 1, false,
            QString("#qiaobian-%1:::1").arg(phaseIndex(phase)), true);
        if (!selection || selection->getSubcards().size() != 1) return false;
        const int id = selection->getSubcards().first();
        if (!owner->handCards().contains(id) || !owner->canDiscard(owner, id)) return false;
        ctx.extra_data = QVariantMap{{"id", id}, {"phase", int(phase)}};
        ctx.targets << owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toMap().value("id", -1).toInt();
        if (!owner->handCards().contains(id) || !owner->canDiscard(owner, id)) return false;
        room->throwCard(id, objectName(), owner); return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != owner) return false;
        const Player::Phase phase = Player::Phase(ctx.extra_data.toMap().value("phase").toInt());
        const int index = phaseIndex(phase);
        room->broadcastSkillInvoke(objectName(), index);
        if (!owner->isSkipped(phase) && (index == 2 || index == 3)) {
            // This prompt is part of the paid effect, even if the original grant retired.
            Room::AcceptedViewAsEffectScope prompt(room, owner, objectName(), ctx);
            if (prompt.isValid()) {
                const int previousPhase = owner->getMark("qiaobianPhase");
                const auto restore = qScopeGuard([&] { room->setPlayerMark(owner, "qiaobianPhase", previousPhase); });
                room->setPlayerMark(owner, "qiaobianPhase", int(phase));
                owner->setSkillInstanceStateValue(objectName(), prompt.activationRef().key.instanceID, "pending_phase", int(phase));
                room->askForUseCard(owner, "@@qiaobian", QString("@qiaobian-%1").arg(index), index, Card::MethodNone);
            }
        }
        owner->skip(phase, true);
        return false;
    }
};

class Beige : public TriggerSkillV2
{
public:
    Beige() : TriggerSkillV2("beige") { events << Damaged << FinishJudge; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge && judge->reason == objectName())
                judge->pattern = QString::number(int(judge->card->getSuit()));
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Damaged || !player || !player->isAlive()) return result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash") || damage.to != player) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->isAlive() && owner->canDiscard(owner, "he")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *card = room->askForCard(ctx.owner, "..", "@beige", *ctx.original_data,
            Card::MethodNone, nullptr, false, objectName());
        if (!card || card->isVirtualCard() || !canPay(room, ctx.owner, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        bool ok = false;
        const int id = ctx.extra_data.toInt(&ok);
        if (!ok || !canPay(room, ctx.owner, id)) return false;
        // Freeze selection in the activation; discard only after activation acceptance.
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (ctx.choice == "club") {
            const int count = 2 * getEffectiveAmount(ctx);
            room->askForDiscard(target, objectName(), count, count, false, true);
            return false;
        }
        if (ctx.choice == "spade") { target->turnOver(); return false; }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        JudgeStruct judge;
        judge.good = true;
        judge.play_animation = false;
        judge.who = target;
        judge.reason = objectName();
        room->judge(judge);
        switch (Card::Suit(judge.pattern.toInt())) {
        case Card::Heart:
            room->broadcastSkillInvoke(objectName(), 4);
            room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
            break;
        case Card::Diamond:
            room->broadcastSkillInvoke(objectName(), 3);
            target->drawCards(2 * getEffectiveAmount(ctx), objectName());
            break;
        case Card::Club:
            room->broadcastSkillInvoke(objectName(), 1);
            if (damage.from && damage.from->isAlive()) {
                SkillContext discard = ctx; discard.choice = "club";
                skillEffect(event, room, owner, discard, damage.from);
            }
            break;
        case Card::Spade:
            room->broadcastSkillInvoke(objectName(), 2);
            if (damage.from && damage.from->isAlive()) {
                SkillContext turn = ctx; turn.choice = "spade";
                skillEffect(event, room, owner, turn, damage.from);
            }
            break;
        default: break;
        }
        return false;
    }
private:
    static bool canPay(Room *room, ServerPlayer *owner, int id)
    {
        return owner && owner->isAlive() && id >= 0 && room->getCardOwner(id) == owner
            && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
            && owner->canDiscard(owner, id);
    }
};

class Duanchang : public TriggerSkillV2
{
public:
    Duanchang() : TriggerSkillV2("duanchang")
    {
        events << Death;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        return player && player == death.who && player->hasSkill(objectName()) && death.damage && death.damage->from
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *killer = ctx.original_data->value<DeathStruct>().damage->from;
        if (killer->isAlive()) ctx.targets = {killer};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        // Removing a dead killer's skills is cleanup, outside living EffectTarget dispatch.
        ServerPlayer *killer = ctx.original_data->value<DeathStruct>().damage->from;
        return killer->isDead() ? applyCurse(room, player, ctx, killer) : false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    { return applyCurse(room, player, ctx, target); }
    bool applyCurse(Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const
    {
        QVariant &data = *ctx.original_data;
        DeathStruct death = data.value<DeathStruct>();
        if (death.who != player)
            return false;

        if (death.damage && target) {
            LogMessage log;
            log.type = "#DuanchangLoseSkills";
            log.from = player;
            log.to << target;
            log.arg = objectName();
            room->sendLog(log);
            int index = qsanRandomBounded(2) + 2;
            if (player->isJieGeneral())
                index += 2;
            player->peiyin(this, index);
            room->notifySkillInvoked(player, objectName());

            QStringList detachList;
            foreach (const Skill *skill, target->getVisibleSkillList()) {
                if (!skill->inherits("SPConvertSkill") && !skill->isAttachedLordSkill())
                    detachList.append("-" + skill->objectName());
            }
            room->handleAcquireDetachSkills(target, detachList);
            if (target->isAlive())
                target->gainMark("@duanchang");
        }

        return false;
    }
};

class Tuntian : public TriggerSkillV2
{
public:
    Tuntian() : TriggerSkillV2("tuntian") { events << CardsMoveOneTime; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->hasFlag("CurrentPlayer")) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
            && !(move.to == player && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data, false)) return false;
        ctx.targets << owner; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        const QVariant previous = target->getTag("TuntianJudgeSource");
        auto restore = qScopeGuard([&] { if (previous.isValid()) target->setTag("TuntianJudgeSource", previous); else target->removeTag("TuntianJudgeSource"); });
        target->setTag("TuntianJudgeSource", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}, {"actor", owner->objectName()}});
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            JudgeStruct judge; judge.pattern = ".|heart"; judge.good = false;
            judge.reason = objectName(); judge.who = target; room->judge(judge);
        }
        return false;
    }
};

class TuntianJudge : public TriggerSkillV2
{
public:
    TuntianJudge() : TriggerSkillV2("#tuntian-judge") { events << FinishJudge; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!judge || !player || judge->who != player || judge->reason != "tuntian" || !judge->card || !judge->isGood()) return true;
        const QVariantMap receipt = player->getTag("TuntianJudgeSource").toMap();
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner) return true;
        SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = player;
        ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
        ctx.instanceID = receipt.value("instance").toInt();
        ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
        ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.targets << player;
        contexts << ctx; return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->getTag("TuntianJudgeSource").toMap() == ctx.extra_data.toMap(); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (judge && judge->who == target && judge->card && judge->isGood()
            && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
            target->addToPile("field", judge->card->getEffectiveId());
        return false;
    }
};

class TuntianDistance : public DistanceSkillV2
{
public:
    TuntianDistance() : DistanceSkillV2("#tuntian-dist")
    {
        setBaseAmount(-1);
        setHolderSelector(CorrectSkill_Primary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder || !ctx.holder->hasSkill("tuntian")) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.currentAmount * ctx.holder->getPile("field").size());
    }
};

class Zaoxian : public TriggerSkillV2
{
public:
    Zaoxian() : TriggerSkillV2("zaoxian")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "jixi";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart) return result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start
            || !player->hasSkill(objectName()) || !(player->getPile("field").size() >= 3 || player->canWake(objectName()))) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext eligibility; eligibility.owner = player; eligibility.invoker = player;
            eligibility.skill_name = objectName(); eligibility.instanceID = id;
            eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets = {owner}; return isUsable(ctx); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *dengai) const override
    {
        if (!(dengai->getPile("field").size() >= 3 || dengai->canWake(objectName()))) return false;
        if (dengai->getPile("field").length() >= 3) {
            LogMessage log;
            log.type = "#ZaoxianWake";
            log.from = dengai;
            log.arg = QString::number(dengai->getPile("field").length());
            log.arg2 = objectName();
            log.arg3 = "field";
            room->sendLog(log);
        }else if(!dengai->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(dengai, objectName());

        //room->doLightbox("$ZaoxianAnimate", 4000);

        room->doSuperLightbox(dengai, "zaoxian");

        room->setPlayerMark(dengai, "zaoxian", 1);
        if (room->changeMaxHpForAwakenSkill(dengai, -getEffectiveAmount(ctx), objectName()))
            room->acquireSkillFromEffect(dengai, "jixi", ctx);

        return false;
    }
};

class Jixi : public ViewAsSkillV2
{
public:
    Jixi() : ViewAsSkillV2("jixi", 1)
    {
        expand_pile = "field";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator
            && !request.initiator->getPile("field").isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && !candidate->isVirtualCard() && request.selectedCardIds.isEmpty()
            && candidate->getEffectiveId() >= 0 && !candidate->hasFlag("using")
            && request.initiator->getPile("field").contains(candidate->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 1) return false;
        const int id = request.selectedCardIds.first();
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        return card && !card->hasFlag("using")
            && request.initiator->getPile("field").contains(id);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        if (!original) return nullptr;
        Snatch *snatch = new Snatch(original->getSuit(), original->getNumber());
        snatch->setSkillName(objectName());
        snatch->addSubcard(original);
        return snatch;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Snatch"; }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (player->isJieGeneral())
            index += 2;
        return index;
    }
};

class Jiang : public TriggerSkillV2
{
public:
    Jiang() : TriggerSkillV2("jiang") { events << TargetSpecified << TargetConfirmed; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.card
            && (event == TargetSpecified || use.to.contains(player))
            && (use.card->isKindOf("Duel") || (use.card->isKindOf("Slash") && use.card->isRed()))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << player; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        int index = qsanRandomBounded(2) + 1;
        if (player->hasSkill("mouduan", true)) index += 2;
        if (player->isJieGeneral()) index += 4;
        room->broadcastSkillInvoke(objectName(), index, player);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Hunzi : public TriggerSkillV2
{
public:
    Hunzi() : TriggerSkillV2("hunzi")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
		waked_skills = "nosyingzi,yinghun";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart) return result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start
            || !player->hasSkill(objectName()) || !(player->getHp() == 1 || player->canWake(objectName()))) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext eligibility; eligibility.owner = player; eligibility.invoker = player;
            eligibility.skill_name = objectName(); eligibility.instanceID = id;
            eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets = {owner}; return isUsable(ctx); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *sunce) const override
    {
        if (!(sunce->getHp() == 1 || sunce->canWake(objectName()))) return false;
        if (sunce->getHp() == 1) {
            LogMessage log;
            log.type = "#HunziWake";
            log.from = sunce;
            log.arg = QString::number(sunce->getHp());
            log.arg2 = objectName();
            room->sendLog(log);
        }else if(!sunce->canWake(objectName()))
			return false;

        int index = qsanRandomBounded(2)+1;
        if (sunce->hasSkill("xiongyisy",true))
            index += 2;
        room->broadcastSkillInvoke(objectName(), index, sunce);
        room->notifySkillInvoked(sunce, objectName());
        //room->doLightbox("$HunziAnimate", 5000);

        room->doSuperLightbox(sunce, "hunzi");

        room->setPlayerMark(sunce, "hunzi", 1);
        if (room->changeMaxHpForAwakenSkill(sunce, -getEffectiveAmount(ctx), objectName())) {
            // Each applied grant retains this accepted awakening's exact provenance.
            room->acquireSkillFromEffect(sunce, "nosyingzi", ctx);
            room->acquireSkillFromEffect(sunce, "yinghun", ctx);
        }
        return false;
    }
};

ZhibaCard::ZhibaCard()
{
    setSkillName("zhiba_pindian");
	mute = true;
}

bool ZhibaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->hasLordSkill("zhiba") && Self->canPindian(to_select);
}

class ZhibaPindian : public ViewAsSkillV2
{
public:
    ZhibaPindian() : ViewAsSkillV2("zhiba_pindian") { attached_lord_skill = true; setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool shouldBeVisible(const Player *player) const override { return player && player->getKingdom() == "wu"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && shouldBeVisible(request.initiator) && request.initiator->canPindian();
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhibaCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *candidate) const override
    {
        if (!request.initiator || !candidate || !targets.isEmpty()) return false;
        const SkillInstance *entry = request.initiator->findSkillInstance(objectName(), request.activationRef.key.instanceID);
        // An attached entry challenges its exact lord provider, never another same-name owner.
        return entry && entry->parentRef.ownerObjectName == candidate->objectName()
            && candidate->hasLordSkill("zhiba") && request.initiator->canPindian(candidate);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *sunce) const override
    {
        Room *room = ctx.invoker->getRoom();
		if (sunce->isWeidi()) {
			room->broadcastSkillInvoke("weidi",-1,sunce);
			room->notifySkillInvoked(sunce, "weidi");
		}else
			room->broadcastSkillInvoke("zhiba",1,sunce);
		ctx.invoker->skillInvoked("zhiba",0,sunce);
		if ((sunce->getMark("hunzi") > 0 || sunce->getMark("mobilehunzi") > 0) && room->askForChoice(sunce, "zhiba_pindian", "accept+reject") == "reject") {
			LogMessage log;
			log.type = "#ZhibaReject";
			log.from = sunce;
			log.to << ctx.invoker;
			log.arg = "zhiba_pindian";
			room->sendLog(log);
			room->broadcastSkillInvoke("zhiba",4,sunce);
			return ContinueEffects;
		}
		PindianStruct *pindian = ctx.invoker->PinDian(sunce, "zhiba_pindian");
		if (!pindian) return ContinueEffects;
		if (pindian->from_number > pindian->to_number)
			room->broadcastSkillInvoke("zhiba",3,sunce);
		else {
			room->broadcastSkillInvoke("zhiba",2,sunce);
			if (pindian->to->isAlive()) {
				DummyCard *dummy = new DummyCard();
				int from_card_id = pindian->from_card->getEffectiveId();
				int to_card_id = pindian->to_card->getEffectiveId();
				if (room->getCardPlace(from_card_id) == Player::DiscardPile)
					dummy->addSubcard(from_card_id);
				if (room->getCardPlace(to_card_id) == Player::DiscardPile && from_card_id != to_card_id)
					dummy->addSubcard(to_card_id);
				if (dummy->subcardsLength()>0 && room->askForChoice(pindian->to, "zhiba_pindian_obtain", "obtainPindianCards+reject") != "reject")
					pindian->to->obtainCard(dummy);
				dummy->deleteLater();
			}
		}
        return ContinueEffects;
    }
};

class Zhiba : public TriggerSkillV2
{
public:
    Zhiba() : TriggerSkillV2("zhiba$") { events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive()) return true;
        if (event == EventPhaseEnd && player->getPhase() == Player::Play) {
            for (const SkillInstance &instance : player->getSkillInstances()) {
                if (instance.skillName == "zhiba_pindian" && instance.source == SourceAttached)
                    room->detachAttachedSkill(SkillInstanceRef(player->objectName(), SkillInstanceKey(instance.skillName, instance.instanceID)));
            }
            for (ServerPlayer *other : room->getOtherPlayers(player)) room->setPlayerFlag(other, "-ZhibaInvoked");
            return true;
        }
        if (event != EventAcquireSkill && (event != EventPhaseStart || player->getPhase() != Player::Play)) return true;
        // Explicit provider/receiver mapping creates one child per root instance.
        for (ServerPlayer *receiver : room->getAlivePlayers()) {
            if (receiver->getPhase() != Player::Play) continue;
            for (ServerPlayer *lord : room->getOtherPlayers(receiver)) {
                if (!lord->hasLordSkill(this, true)) continue;
                for (int id : lord->getSkillInstanceIds(objectName()))
                    room->attachSkillToPlayer(receiver, "zhiba_pindian",
                        SkillInstanceRef(lord->objectName(), SkillInstanceKey(objectName(), id)));
            }
        }
        return true;
    }
};

TiaoxinCard::TiaoxinCard()
{
    setSkillName("tiaoxin");
}

bool TiaoxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->inMyAttackRange(Self);
}


class Tiaoxin : public ViewAsSkillV2
{
public:
    Tiaoxin() : ViewAsSkillV2("tiaoxin")
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate
            && candidate->inMyAttackRange(request.initiator);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "TiaoxinCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        // The native proxy follows the final invoker after V2 interception.
        ServerPlayer *source = ctx.invoker;
        if (!source || !target) return ContinueEffects;
        Room *room = source->getRoom();
        bool use_slash = false;
        if (target->canSlash(source, nullptr, false))
            use_slash = room->askForUseSlashTo(target, source,
                "@tiaoxin-slash:" + source->objectName());
        // Amount changes the failed response's discard count, not the Slash prompt count.
        for (int i = 0, count = getEffectiveAmount(ctx); !use_slash && i < count
             && source->isAlive() && target->isAlive() && source->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(source, target, "he", "tiaoxin", false, Card::MethodDiscard);
            if (id >= 0 && room->getCardOwner(id) == target
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using") && source->canDiscard(target, id))
                room->throwCard(id, target, source);
        }
        return ContinueEffects;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (!player->hasInnateSkill(this) && player->hasSkill("baobian"))
            index += 3;
        else if (!player->hasInnateSkill(this) && player->getMark("fengliang") > 0)
            index += 5;
        else if (player->hasArmorEffect("eight_diagram"))
            index = 3;
        return index;
    }
};

class Zhiji : public TriggerSkillV2
{
public:
    Zhiji() : TriggerSkillV2("zhiji")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
		waked_skills = "guanxing";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart) return result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start
            || !player->hasSkill(objectName()) || !(player->isKongcheng() || player->canWake(objectName()))) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext eligibility; eligibility.owner = player; eligibility.invoker = player;
            eligibility.skill_name = objectName(); eligibility.instanceID = id;
            eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets = {owner}; return isUsable(ctx); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *jiangwei) const override
    {
        if (!(jiangwei->isKongcheng() || jiangwei->canWake(objectName()))) return false;
        if (jiangwei->isKongcheng()) {
            LogMessage log;
            log.type = "#ZhijiWake";
            log.from = jiangwei;
            log.arg = objectName();
            room->sendLog(log);
        }else if(!jiangwei->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(jiangwei, objectName());

        //room->doLightbox("$ZhijiAnimate", 4000);

        room->doSuperLightbox(jiangwei, "zhiji");

        room->setPlayerMark(jiangwei, "zhiji", 1);
        if (room->changeMaxHpForAwakenSkill(jiangwei, -getEffectiveAmount(ctx), objectName())) {
            if (jiangwei->isWounded() && room->askForChoice(jiangwei, objectName(), "recover+draw") == "recover")
                room->recover(jiangwei, RecoverStruct("zhiji", jiangwei, getEffectiveAmount(ctx)));
            else
                room->drawCards(jiangwei, 2 * getEffectiveAmount(ctx), objectName());
            room->acquireSkillFromEffect(jiangwei, "guanxing", ctx);
        }

        return false;
    }
};

ZhijianCard::ZhijianCard()
{
    setSkillName("zhijian");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ZhijianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self)
        return false;

    const Card *card = Sanguosha->getCard(subcards.first());
    const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
    int equip_index = static_cast<int>(equip->location());
    return to_select->hasEquipArea(equip_index) && to_select->getEquip(equip_index) == nullptr
        && !Self->isProhibited(to_select, card);
}


class Zhijian : public ViewAsSkillV2
{
public:
    Zhijian() : ViewAsSkillV2("zhijian", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && ViewAsSkillV2::canSelectCard(request, candidate)
            && candidate->isKindOf("EquipCard") && !candidate->hasFlag("using")
            && request.initiator->handCards().contains(candidate->getEffectiveId())
            && !request.selectedCardIds.contains(candidate->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }


    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhijianCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *candidate) const override
    {
        if (!request.initiator || !cardSelectionFeasible(request)) return false;
        ZhijianCard card;
        card.addSubcards(request.selectedCardIds);
        return card.targetFilter(targets, candidate, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return ContinueEffects; }
        Room *room = source->getRoom();
        if (!ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        const Card *material = Sanguosha->getCard(id);
        const EquipCard *equip = material ? qobject_cast<const EquipCard *>(material->getRealCard()) : nullptr;
        if (!equip || material->hasFlag("using") || room->getCardOwner(id) != ctx.initiator
            || room->getCardPlace(id) != Player::PlaceHand || !target->hasEquipArea(int(equip->location()))
            || target->getEquip(int(equip->location())) || ctx.initiator->isProhibited(target, material)) return ContinueEffects;
        room->moveCardTo(Sanguosha->getCard(id), ctx.initiator, target, Player::PlaceEquip,
            CardMoveReason(CardMoveReason::S_REASON_PUT, source->objectName(), objectName(), ""));
        if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip) return ContinueEffects;
        LogMessage log;
        log.type = "$ZhijianEquip";
        log.from = target;
        log.card_str = QString::number(id);
        room->sendLog(log);
        SkillContext draw = ctx; draw.choice = "draw";
        skillEffect(draw, source);
        return ContinueEffects;
    }
};

GuzhengCard::GuzhengCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void GuzhengCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->setTag("guzheng_card", subcards.first());
}

class Guzheng : public TriggerSkillV2
{
public:
    Guzheng() : TriggerSkillV2("guzheng") { events << EventPhaseEnd; }

    static QList<int> availableCards(Room *room, ServerPlayer *player, bool returnable)
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        if (!phase.toULongLong()) return {};
        QVariantMap filter{{"phase_id", phase}, {"limit", 100}};
        QList<int> recorded;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool() || page.contains("error")) return {};
            if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap move = item.toMap().value("data").toMap();
                if (move.value("to_place", -1).toInt() != Player::DiscardPile
                    || (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
                    continue;
                // The returned card must be this player's hand discard; the rest
                // includes every card discarded into the pile during the phase.
                if (returnable && (move.value("from").toString() != player->objectName()
                    || move.value("from_place", -1).toInt() != Player::PlaceHand)) continue;
                const int id = move.value("card_id", -1).toInt();
                if (id >= 0 && !recorded.contains(id)) recorded << id;
            }
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after");
        }
        QList<int> cards;
        for (int id : room->getDiscardPile()) if (recorded.contains(id)) cards << id;
        return cards;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseEnd || !player || !player->isAlive()
            || player->getPhase() != Player::Discard || availableCards(room, player, true).isEmpty()) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->isAlive() && owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive()) return false;
        const QList<int> cards = availableCards(room, ctx.invoker, true);
        if (cards.isEmpty()) return false;
        const QVariant previous = ctx.owner->getTag("GuzhengToGet");
        const auto restore = qScopeGuard([&] {
            if (previous.isValid()) ctx.owner->setTag("GuzhengToGet", previous);
            else ctx.owner->removeTag("GuzhengToGet");
        });
        ctx.owner->setTag("GuzhengToGet", ListI2V(cards));
        return ctx.owner->askForSkillInvoke(objectName() + "$-1", ctx.invoker);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !player->isAlive()) return false;
        const QList<int> cards = availableCards(room, player, true);
        if (cards.isEmpty()) return false;
        const QVariant previous = owner->getTag("GuzhengToGet");
        const auto restore = qScopeGuard([&] {
            room->clearAG(owner);
            if (previous.isValid()) owner->setTag("GuzhengToGet", previous);
            else owner->removeTag("GuzhengToGet");
        });
        owner->setTag("GuzhengToGet", ListI2V(cards));
        room->fillAG(cards, owner);
        const int id = room->askForAG(owner, cards, false, objectName(), "@guzheng:" + player->objectName());
        room->clearAG(owner);
        if (!cards.contains(id)) return false;
        SkillContext returned = ctx;
        returned.choice = "return"; returned.extra_data = id;
        skillEffect(event, room, owner, returned, player);
        if (!returned.extra_data.toMap().value("returned").toBool()) return false;
        SkillContext rest = ctx; rest.choice = "rest";
        rest.extra_data = ListI2V(availableCards(room, player, false));
        skillEffect(event, room, owner, rest, owner);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "return") {
            const int id = ctx.extra_data.toInt();
            if (!availableCards(room, ctx.invoker, true).contains(id)) return false;
            const qint64 cause = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
            QVariantMap filter{{"to", target->objectName()}, {"skill_name", ctx.sourceRef.key.skillName},
                {"skill_owner", ctx.sourceRef.ownerObjectName}, {"limit", 1}};
            const QVariantMap before = room->queryHistoryMoves(filter);
            room->obtainCard(target, id, objectName());
            bool returned = false;
            if (cause > 0 && before.value("complete").toBool() && before.contains("watermark")) {
                filter["after"] = before.value("watermark"); filter["limit"] = 100;
                for (;;) {
                    const QVariantMap page = room->queryHistoryMoves(filter);
                    if (page.contains("error") || !page.value("complete").toBool()
                        || !page.value("attribution_complete").toBool()) { returned = false; break; }
                    if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
                    for (const QVariant &value : page.value("items").toList()) {
                        const QVariantMap move = value.toMap().value("data").toMap();
                        if (move.value("card_id", -1).toInt() == id && move.value("cause_event_id").toLongLong() == cause
                            && move.value("instance_id").toInt() == ctx.sourceRef.key.instanceID
                            && move.value("reason_skill").toString() == objectName()
                            && move.value("to_place", -1).toInt() == Player::PlaceHand) returned = true;
                    }
                    if (!page.value("has_more").toBool()) break;
                    filter["after"] = page.value("next_after");
                }
            }
            // Nested movement may immediately consume the returned card; its
            // committed transfer, not its final location, unlocks the remainder.
            ctx.extra_data = QVariantMap{{"returned", returned}};
        } else if (ctx.choice == "rest") {
            QList<int> remaining;
            for (const QVariant &value : ctx.extra_data.toList()) {
                const int id = value.toInt();
                if (room->getCardPlace(id) == Player::DiscardPile) remaining << id;
            }
            if (!remaining.isEmpty()) target->obtainCard(dummyCard(remaining));
        }
        return false;
    }
};

class Xiangle : public TriggerSkillV2
{
public:
    Xiangle() : TriggerSkillV2("xiangle")
    {
        events << TargetConfirming;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName())
            && use.card && use.card->isKindOf("Slash") && use.from && use.to.contains(player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || !use.from->isAlive()) return false;
        ctx.targets = {use.from}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (ctx.choice == "protect") {
            if (target != ctx.owner || !use.to.contains(target)) return false;
            if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use); return false;
        }
        if (target != use.from || !use.to.contains(ctx.owner)) return false;
        int index = qsanRandomBounded(2) + 1;
        if (ctx.owner->isJieGeneral()) index += 2;
        room->broadcastSkillInvoke(objectName(), index);
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        for (int i = 0; i < getEffectiveAmount(ctx); ++i) {
            if (room->askForCard(target, ".Basic", "@xiangle-discard:" + ctx.owner->objectName(),
                                QVariant::fromValue(ctx.owner))) continue;
            // Read the Slash again after the nested payment; protect only its
            // original Xiangle recipient through that recipient's target gate.
            SkillContext protect = ctx; protect.choice = "protect";
            skillEffect(event, room, owner, protect, ctx.owner); break;
        }
        return false;
    }
};

FangquanCard::FangquanCard()
{
    setSkillName("fangquan");
	mute = true;
}

bool FangquanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}


static QVariantMap fangquanReceipt(Room *room, const SkillContext &ctx)
{
    const int serial = room->getTag("FangquanNextReceipt").toInt() + 1;
    room->setTag("FangquanNextReceipt", serial);
    return QVariantMap{{"id", serial}, {"owner", ctx.sourceRef.ownerObjectName},
        {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
        {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
        {"activation_id", ctx.activationRef.key.instanceID}, {"actor", ctx.invoker->objectName()},
        {"amount", ctx.hasModifiedAmount() ? ctx.modified_amount : ctx.amount}};
}

class FangquanViewAsSkill : public ViewAsSkillV2
{
public:
    FangquanViewAsSkill() : ViewAsSkillV2("fangquan", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@fangquan"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && ViewAsSkillV2::canSelectCard(request, card)
            && !card->hasFlag("using") && !request.initiator->isJilei(card)
            && request.initiator->handCards().contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "FangquanCard"; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }

    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *candidate) const override
    {
        if (!request.initiator || !cardSelectionFeasible(request)) return false;
        FangquanCard card;
        card.addSubcards(request.selectedCardIds);
        return card.targetFilter(targets, candidate, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        LogMessage log;
        log.type = "#Fangquan";
        log.from = ctx.invoker;
        log.to << target;
        ctx.invoker->getRoom()->sendLog(log);
        QVariantList pending = ctx.invoker->getTag("FangquanTargets").toList();
        QVariantMap receipt = fangquanReceipt(ctx.invoker->getRoom(), ctx);
        receipt["target"] = target->objectName();
        pending << receipt;
        ctx.invoker->setTag("FangquanTargets", pending);
        return ContinueEffects;
    }
};

class Fangquan : public TriggerSkillV2
{
public:
    Fangquan() : TriggerSkillV2("fangquan")
    { events << EventPhaseChanging; view_as_skill = new FangquanViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<PhaseChangeStruct>().to == Player::Play && !player->isSkipped(Player::Play)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { ctx.targets = {player}; return !player->isSkipped(Player::Play) && player->askForSkillInvoke(this); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->isSkipped(Player::Play)) return false;
        QVariantList pending = target->getTag("FangquanSkippedPlay").toList();
        QVariantMap receipt = fangquanReceipt(room, ctx); receipt["actor"] = target->objectName();
        pending << receipt; target->setTag("FangquanSkippedPlay", pending);
        target->setFlags(objectName()); // Existing AI projection of the accepted skip.
        target->peiyin(this); target->skip(Player::Play, true); return false;
    }
};

class FangquanRecord : public TriggerSkillV2
{
public:
    FangquanRecord() : TriggerSkillV2("#fangquan-record")
    { events << EventPhaseChanging << EventPhaseStart << EventPhaseEnd << EventSkillEffectFinished << TurnBroken << Death; global = true; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 1; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && finished.invoker
                && (finished.choice == "FangquanSkippedPlay" || finished.choice == "FangquanTargets")) {
                QVariantList pending = finished.invoker->getTag(finished.choice).toList();
                pending.removeOne(finished.extra_data);
                finished.invoker->setTag(finished.choice, pending);
            }
            return false;
        }
        if (!player) return false;
        if (event == TurnBroken || (event == Death && data.value<DeathStruct>().who == player)
            || (event == EventPhaseStart && player->getPhase() == Player::RoundStart)
            || (event == EventPhaseEnd && player->getPhase() == Player::NotActive)) {
            player->removeTag("FangquanSkippedPlay");
            player->removeTag("FangquanTargets");
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (!player) return true;
        QString key;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            key = "FangquanSkippedPlay";
        else if (event == EventPhaseStart && player->getPhase() == Player::NotActive) key = "FangquanTargets";
        else return true;
        for (const QVariant &value : player->getTag(key).toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ServerPlayer *target = key == "FangquanSkippedPlay" ? player
                : room->findPlayerByObjectName(receipt.value("target").toString(), true);
            if (!owner || !target || !target->isAlive()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = owner;
            ctx.invoker = ctx.initiator = player; ctx.original_data = &data; ctx.current_event = event;
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = receipt.value("id").toInt(); ctx.amount = receipt.value("amount", 1).toInt();
            ctx.choice = key; ctx.extra_data = receipt; ctx.targets << target; contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->getTag(ctx.choice).toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Consume before target interception and nested extra turns; a cancelled
        // continuation cannot become a stale prompt in a later turn.
        QVariantList pending = ctx.invoker->getTag(ctx.choice).toList();
        pending.removeOne(ctx.extra_data); ctx.invoker->setTag(ctx.choice, pending);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "FangquanTargets") {
            for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i)
                room->executeExtraTurn(target, {}, "fangquan", ctx.sourceRef);
            return false;
        }
        if (!target->canDiscard(target, "h")) return false;
        const QVariantMap receipt = ctx.extra_data.toMap();
        SkillContext accepted = ctx;
        accepted.activationRef = SkillInstanceRef(receipt.value("activation_owner").toString(),
            SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_id").toInt()));
        Room::AcceptedViewAsEffectScope prompt(room, target, "fangquan", accepted);
        if (!prompt.activationRef().isValid()) return false;
        const auto oldReason = room->getRoomState()->getCurrentCardUseReason();
        const QString oldPattern = room->getRoomState()->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([&] {
            room->setCurrentCardUse(oldPattern, oldReason);
        });
        room->askForUseCard(target, "@@fangquan", "@fangquan-give", -1, Card::MethodDiscard);
        return false;
    }
};

class Ruoyu : public TriggerSkillV2
{
public:
    Ruoyu() : TriggerSkillV2("ruoyu$")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
		waked_skills = "jijiang";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart) return result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start
            || !player->hasLordSkill(this) || !(player->isLowestHpPlayer() || player->canWake(objectName()))) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext eligibility; eligibility.owner = player; eligibility.invoker = player;
            eligibility.skill_name = objectName(); eligibility.instanceID = id;
            eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets = {owner}; return isUsable(ctx); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *liushan) const override
    {
        if (!(liushan->isLowestHpPlayer() || liushan->canWake(objectName()))) return false;
        if (liushan->isLowestHpPlayer()) {
            LogMessage log;
            log.type = "#RuoyuWake";
            log.from = liushan;
            log.arg = QString::number(liushan->getHp());
            log.arg2 = objectName();
            room->sendLog(log);
        }else if(!liushan->canWake(objectName()))
			return false;
        if (liushan->isWeidi())
            room->broadcastSkillInvoke("weidi");
        else
            room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(liushan, objectName());
		room->doSuperLightbox(liushan, "ruoyu");

        room->setPlayerMark(liushan, "ruoyu", 1);
        if (room->changeMaxHpForAwakenSkill(liushan, getEffectiveAmount(ctx), objectName())) {
            room->recover(liushan, RecoverStruct("ruoyu", liushan, getEffectiveAmount(ctx)));
            room->acquireSkillFromEffect(liushan, "jijiang", ctx);
        }
        return false;
    }
};

class Huashen : public TriggerSkillV2
{
public:
    Huashen() : TriggerSkillV2("huashen")
    {
        events << GameStart;
        frequency = Compulsory;
        m_baseAmount = 2;
    }

    static void playAudioEffect(ServerPlayer *zuoci, const QString &skill_name)
    {
        Room *room = zuoci->getRoom();
        int index = qsanRandomBounded(2) + 1;
        if (zuoci->isJieGeneral() && skill_name == "xinsheng")
            index = qsanRandomBounded(2) + 5;
        else {
            if (zuoci->isFemale())
                index += 2;
        }
        room->broadcastSkillInvoke(skill_name, index);
    }

    static void AcquireGenerals(ServerPlayer *zuoci, int n)
    {
        Room *room = zuoci->getRoom();
        QStringList list = GetAvailableGenerals(zuoci);
        if (list.isEmpty()) return;
        qsanShuffle(list);
        n = qMin(n, list.length());
        
        QStringList huashens = zuoci->property("Huashens").toString().split("+");
        if(zuoci->property("Huashens").toString().isEmpty()) huashens.clear();

        QStringList acquired = list.mid(0, n);
        foreach (QString name, acquired) {
            huashens << name;
            const General *general = Sanguosha->getGeneral(name);
            if (general) {
                foreach (const TriggerSkill *skill, general->getTriggerSkills())
                    room->getThread()->addTriggerSkill(skill);
            }
        }
        
        QString pile_str = huashens.join("+");
        
        // General identities are private; only the count/unknown animation is public.
        zuoci->setProperty("Huashens", pile_str);
        zuoci->setProperty("huashen_general", pile_str);
        room->notifyProperty(zuoci, zuoci, "Huashens");
        room->notifyProperty(zuoci, zuoci, "huashen_general");

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

        room->setPlayerMark(zuoci, "@huashen", huashens.length());
    }

    static QStringList GetAvailableGenerals(ServerPlayer *zuoci)
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
        return (qsanToSet(all) - banned - huashen_set - room_set).values();
    }

    static void SelectSkill(ServerPlayer *zuoci, SkillInstanceRef source, const SkillContext &accepted)
    {
        Room *room = zuoci->getRoom();
        if (source.ownerObjectName != zuoci->objectName() || source.key.skillName != "huashen"
            || !zuoci->hasSkillInstance("huashen", source.key.instanceID)) return;

        QStringList huashens = zuoci->property("Huashens").toString().split("+");
		if(zuoci->property("Huashens").toString().isEmpty()) huashens.clear();
        if (huashens.isEmpty()) return;
        const QString previousSkill = zuoci->getSkillInstanceStateValue("huashen", source.key.instanceID, "chosen_skill").toString();
        const int previousId = zuoci->getSkillInstanceStateValue("huashen", source.key.instanceID, "chosen_id").toInt();
        const auto selectionUnchanged = [&] {
            return zuoci->hasSkillInstance("huashen", source.key.instanceID)
                && zuoci->getSkillInstanceStateValue("huashen", source.key.instanceID, "chosen_id").toInt() == previousId
                && zuoci->getSkillInstanceStateValue("huashen", source.key.instanceID, "chosen_skill").toString() == previousSkill;
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
            skill_name = ai->askForChoice("huashen", skill_names.join("+"), QVariant());
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
            skill_name = room->askForChoice(zuoci, "huashen", skill_names.join("+"));
        }

        // Prompts can run nested effects: a replaced selection must not retire
        // another activation's grant or dereference an invalid AI reply.
        if (!general || !skill_names.contains(skill_name)
            || !zuoci->hasSkillInstance("huashen", source.key.instanceID)
            || !zuoci->property("Huashens").toString().split("+").contains(general->objectName())
            || zuoci->getSkillInstanceStateValue("huashen", source.key.instanceID, "chosen_id").toInt() != previousId
            || zuoci->getSkillInstanceStateValue("huashen", source.key.instanceID, "chosen_skill").toString() != previousSkill) return;

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

        // A nested kingdom change can select another avatar; never publish stale gender/UI.
        if (!selectionUnchanged()) return;
        if (zuoci->getGender() != general->getGender()) zuoci->setGender(general->getGender());
        if (!selectionUnchanged()) return;
        JsonArray arg;
        arg << QSanProtocol::S_GAME_EVENT_HUASHEN << zuoci->objectName() << general->objectName() << skill_name;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

        // The shared general pool does not merge independently granted skill instances.
        if (!selectionUnchanged()) return;
        if (!previousSkill.isEmpty() && previousId > 0)
            room->detachSkillFromPlayer(zuoci, SkillInstanceUtils::formatName(previousSkill, previousId), false, true);
        if (!selectionUnchanged()) return;
        int committedGrant = 0;
        bool selectionPublished = false;
        const auto releaseUnpublished = qScopeGuard([&] {
            if (selectionPublished || committedGrant <= 0) return;
            QVariantList grants = zuoci->getTag("HuashenGrants").toList();
            grants.removeOne(QVariantMap{{"root", source.key.instanceID}, {"skill", skill_name}, {"id", committedGrant}});
            zuoci->setTag("HuashenGrants", grants);
            // Preserve the original interruption if a cleanup observer also throws.
            try {
                if (zuoci->hasSkillInstance(skill_name, committedGrant))
                    room->detachSkillFromPlayer(zuoci, SkillInstanceUtils::formatName(skill_name, committedGrant), false, true);
            } catch (...) {}
        });
        const int acquired = room->acquireSkillFromEffect(zuoci, skill_name, accepted, [&](int committedId) {
            committedGrant = committedId;
            // Keep cleanup outside the provider state before acquisition callbacks can retire it.
            QVariantList grants = zuoci->getTag("HuashenGrants").toList();
            grants << QVariantMap{{"root", source.key.instanceID}, {"skill", skill_name}, {"id", committedId}};
            zuoci->setTag("HuashenGrants", grants);
        });
        if (acquired <= 0) return;
        if (!selectionUnchanged()) {
            QVariantList grants = zuoci->getTag("HuashenGrants").toList();
            grants.removeOne(QVariantMap{{"root", source.key.instanceID}, {"skill", skill_name}, {"id", acquired}});
            zuoci->setTag("HuashenGrants", grants);
            room->detachSkillFromPlayer(zuoci, SkillInstanceUtils::formatName(skill_name, acquired), false, true);
            return;
        }
        zuoci->setSkillInstanceStateValue("huashen", source.key.instanceID, "chosen_skill", skill_name);
        zuoci->setSkillInstanceStateValue("huashen", source.key.instanceID, "chosen_id", acquired);
        // Retain only cleanup identities outside the root: its state is destroyed before EventLoseSkill.
        QVariantList grants = zuoci->getTag("HuashenGrants").toList();
        for (int i = grants.size() - 1; i >= 0; --i)
            if (grants.at(i).toMap().value("root").toInt() == source.key.instanceID) grants.removeAt(i);
        grants << QVariantMap{{"root", source.key.instanceID}, {"skill", skill_name}, {"id", acquired}};
        zuoci->setTag("HuashenGrants", grants);
        zuoci->setTag("HuashenSkill", skill_name);
        selectionPublished = true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets = {owner}; return true; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *zuoci) const override
    {
        playAudioEffect(zuoci, objectName());
        zuoci->getRoom()->sendCompulsoryTriggerLog(zuoci, objectName());
        AcquireGenerals(zuoci, getEffectiveAmount(ctx));
        SelectSkill(zuoci, ctx.activationRef, ctx);
        return false;
    }

    SkillDialogInfo getDialogInfo() const override
    {
        SkillDialogInfo info = SkillDialogInfo::named("huashen", objectName());
        info.parameters.insert("viewOnly", true);
        return info;
    }
};


// 1. 建構子實作：接收參數並賦值給 m_propertyName


class HuashenSelect : public TriggerSkillV2
{
public:
    HuashenSelect() : TriggerSkillV2("#huashen-select") { events << EventPhaseStart; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 4; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->hasSkill("huashen")
            && (player->getPhase() == Player::RoundStart || player->getPhase() == Player::NotActive)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { ctx.targets = {player}; return player->askForSkillInvoke("huashen"); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        const SkillInstance *helper = player->findSkillInstance(objectName(), ctx.activationRef.key.instanceID);
        // Select the direct Huashen provider, not a copied root above it.
        if (helper) Huashen::SelectSkill(player, helper->parentRef, ctx);
        return false;
    }
};

class HuashenClear : public TriggerSkillV2
{
public:
    HuashenClear() : TriggerSkillV2("#huashen-clear")
    { events << EventLoseSkill; global = true; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const SkillChangeStruct change = data.value<SkillChangeStruct>();
        if (!player || change.skillName != "huashen") return true;
        QVariantList grants = player->getTag("HuashenGrants").toList();
        QStringList expired;
        for (int i = grants.size() - 1; i >= 0; --i) {
            const QVariantMap grant = grants.at(i).toMap();
            if (grant.value("root").toInt() != change.instanceID) continue;
            expired << SkillInstanceUtils::formatName(grant.value("skill").toString(), grant.value("id").toInt());
            grants.removeAt(i);
        }
        player->setTag("HuashenGrants", grants);
        for (const QString &skill : expired) room->detachSkillFromPlayer(player, skill, false, true);
        // Cleanup survives loss of the helper; remaining Huashen sources retain their shared pool.
        if (player->hasSkill("huashen", true)) return true;
        if (player->getKingdom() != player->getGeneral()->getKingdom() && player->getGeneral()->getKingdom() != "god")
            room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());
        if (player->getGender() != player->getGeneral()->getGender())
            player->setGender(player->getGeneral()->getGender());
        player->removeTag("HuashenSkill");
		player->setProperty("Huashens", ""); player->setProperty("huashen_general", "");
        room->notifyProperty(player, player, "Huashens");
        room->notifyProperty(player, player, "huashen_general");
        room->setPlayerMark(player, "@huashen", 0);
        return true;
    }
};

class Xinsheng : public TriggerSkillV2
{
public:
    Xinsheng() : TriggerSkillV2("xinsheng") { events << Damaged; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { ctx.targets = {player}; return player->askForSkillInvoke(this); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        Huashen::playAudioEffect(player, objectName());
        Huashen::AcquireGenerals(player, ctx.original_data->value<DamageStruct>().damage * getEffectiveAmount(ctx));
        return false;
    }
};

class Renjie : public TriggerSkillV2
{
public:
    Renjie() : TriggerSkillV2("renjie") { events << Damaged << CardsMoveOneTime; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (player->getPhase() != Player::Discard || move.from != player || move.card_ids.isEmpty()
                || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return {};
        }
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { ctx.targets = {player}; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        const int n = event == Damaged ? ctx.original_data->value<DamageStruct>().damage
            : ctx.original_data->value<CardsMoveOneTimeStruct>().card_ids.size();
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());
        player->gainMark("&bear", n * getEffectiveAmount(ctx));
        return false;
    }
};

class Baiyin : public TriggerSkillV2
{
public:
    Baiyin() : TriggerSkillV2("baiyin")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "jilve";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart) return result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start
            || !player->hasSkill(objectName()) || !(player->getMark("&bear") >= 4 || player->canWake(objectName()))) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext eligibility; eligibility.owner = player; eligibility.invoker = player;
            eligibility.skill_name = objectName(); eligibility.instanceID = id;
            eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets = {owner}; return isUsable(ctx); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *shensimayi) const override
    {
        if (!(shensimayi->getMark("&bear") >= 4 || shensimayi->canWake(objectName()))) return false;
        if (shensimayi->getMark("&bear") >= 4) {
            LogMessage log;
            log.type = "#BaiyinWake";
            log.from = shensimayi;
            log.arg = QString::number(shensimayi->getMark("&bear"));
            room->sendLog(log);
        }else if(!shensimayi->canWake("baiyin"))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(shensimayi, objectName());
        //room->doLightbox("$BaiyinAnimate");
        room->doSuperLightbox(shensimayi, "baiyin");

        room->setPlayerMark(shensimayi, "baiyin", 1);
        if (room->changeMaxHpForAwakenSkill(shensimayi, -getEffectiveAmount(ctx), objectName()))
            room->acquireSkillFromEffect(shensimayi, "jilve", ctx);

        return false;
    }
};

JilveCard::JilveCard()
{
    setSkillName("jilve");
    target_fixed = true;
    mute = true;
}

class JilveViewAsSkill : public ViewAsSkillV2
{
public:
    JilveViewAsSkill() : ViewAsSkillV2("jilve") { setPhaseName("Play"); }
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || request.initiator->getMark("&bear") < 1) return false;
        // Redirection changes the actor, never the grant that owns branch state.
        for (const Player *holder : request.initiator->getSiblings(true))
            if (holder->objectName() == request.activationRef.ownerObjectName)
                return !holder->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "zhiheng_used").toBool()
                    || !holder->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "wansha_used").toBool();
        return false;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "JilveCard"; }
    static QStringList choicesFor(ServerPlayer *shensimayi, const SkillInstanceRef &activation)
    {
        QStringList choices;
	if (!shensimayi) return choices;
	const Player *entryOwner = shensimayi->getRoom()->findPlayerByObjectName(activation.ownerObjectName, true);
	if (!entryOwner) return choices;
	const bool zhihengUsed = entryOwner->getSkillInstanceStateValue("jilve", activation.key.instanceID, "zhiheng_used").toBool();
	const bool wanshaUsed = entryOwner->getSkillInstanceStateValue("jilve", activation.key.instanceID, "wansha_used").toBool();
	foreach(QString s, Sanguosha->getSkillNames()){
		if(s.endsWith("zhiheng")&&!s.contains("#")&&!zhihengUsed&&shensimayi->canDiscard(shensimayi, "he")){
			const ViewAsSkill* zhiheng = Sanguosha->getViewAsSkill(s);
			if(const ViewAsSkillV2 *zhihengV2 = dynamic_cast<const ViewAsSkillV2 *>(zhiheng)){
				ActiveSkillRequest request;
				request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
				request.pattern = "@@"+s;
				request.initiator = shensimayi;
				if(zhihengV2->canActivate(request))
					choices << s;
			}else if(zhiheng&&zhiheng->isEnabledAtResponse(shensimayi,"@"+s))
				choices << s;
		}
		if(s.endsWith("wansha")&&!s.contains("#")&&!wanshaUsed){
			const TriggerSkill* wansha = Sanguosha->getTriggerSkill(s);
			if(wansha&&wansha->hasEvent(Dying))
				choices << s;
		}
	}

        return choices;
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        QStringList choices = choicesFor(ctx.invoker, ctx.activationRef);
        if (choices.isEmpty()) return false;
        choices << "cancel";
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"));
        return choices.contains(ctx.choice) && ctx.choice != "cancel";
    }
    static void commitBranch(Room *room, const SkillContext &ctx)
    {
        const QString key = ctx.choice.endsWith("zhiheng") ? "zhiheng_used"
            : ctx.choice.endsWith("wansha") ? "wansha_used" : QString();
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.activationRef.ownerObjectName, true);
        if (holder && !key.isEmpty() && holder->hasSkillInstance("jilve", ctx.activationRef.key.instanceID))
            holder->setSkillInstanceStateValue("jilve", ctx.activationRef.key.instanceID, key, true);
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || ctx.initiator->getMark("&bear") < 1
            || !choicesFor(ctx.invoker, ctx.activationRef).contains(ctx.choice)) return false;
        // Branch quota is spent even if the later whole/target effect is cancelled.
        commitBranch(room, ctx);
        ctx.initiator->loseMark("&bear");
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *player) const override
    {
        Room *room = player->getRoom();
        ServerPlayer *entryOwner = room->findPlayerByObjectName(ctx.activationRef.ownerObjectName, true);
        if (!entryOwner) return FinishSkill;
        room->notifySkillInvoked(player, objectName());
        if (ctx.choice.contains("wansha")) {
            room->setPlayerFlag(player, "JilveWansha");
            QVariantMap grant;
            grant["skill"] = ctx.choice;
            grant["source_owner"] = ctx.sourceRef.ownerObjectName;
            grant["source_skill"] = ctx.sourceRef.key.skillName;
            grant["source_instance"] = ctx.sourceRef.key.instanceID;
            grant["turn_id"] = room->historyScopes().value("turn_id");
            grant["turn_owner"] = room->getCurrent() ? room->getCurrent()->objectName() : QString();
            room->acquireSkillFromEffect(player, ctx.choice, ctx, [&](int committedId) {
                grant["id"] = committedId;
                QVariantList grants = player->getTag("JilveWanshaGrants").toList();
                grants << grant;
                player->setTag("JilveWanshaGrants", grants);
                player->setTag("JilveWansha", ctx.choice);
            });
        } else {
            room->setPlayerFlag(player, "JilveZhiheng");
            // The executing Jilve instance owns this temporary grant, even when borrowed itself.
            Room::AcceptedViewAsEffectScope borrowed(room, player, ctx.choice, ctx);
            if (borrowed.activationRef().isValid())
                room->askForUseCard(player, "@@" + ctx.choice, "@jilve-zhiheng", -1, Card::MethodDiscard);
        }
        return FinishSkill;
    }
};

class Jilve : public TriggerSkillV2
{
public:
    Jilve() : TriggerSkillV2("jilve")
    {
        events << CardUsed << AskForRetrial << Damaged << EventSkillInvoking;
        view_as_skill = new JilveViewAsSkill;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef != ctx.activationRef || !accepted.bypass_cost) return;
        // Active bypass skips cost as well as payment. Select the necessary branch
        // before effect interception, and commit it even when the effect is cancelled.
        if (accepted.executionID > 0 && accepted.use_card && accepted.choice.isEmpty()) {
            const QStringList choices = JilveViewAsSkill::choicesFor(accepted.invoker, accepted.activationRef);
            if (choices.isEmpty()) accepted.is_canceled = true;
            else {
                accepted.choice = room->askForChoice(accepted.invoker, objectName(), choices.join("+"));
                if (!choices.contains(accepted.choice)
                    || !JilveViewAsSkill::choicesFor(accepted.invoker, accepted.activationRef).contains(accepted.choice))
                    accepted.is_canceled = true;
            }
            *ctx.original_data = QVariant::fromValue(accepted);
        }
        if (!accepted.is_canceled) JilveViewAsSkill::commitBranch(room, accepted);
    }
    static QString family(TriggerEvent event)
    { return event == CardUsed ? "jizhi" : event == AskForRetrial ? "guicai" : "fangzhu"; }
    static QStringList candidates(TriggerEvent event)
    {
        QStringList skills;
        for (const QString &name : Sanguosha->getSkillNames()) {
            if (!name.endsWith(family(event)) || name.contains("#")) continue;
            const TriggerSkill *skill = Sanguosha->getTriggerSkill(name);
            if (dynamic_cast<const TriggerSkillV2 *>(skill) && skill->hasEvent(event)) skills << name;
        }
        return skills;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getMark("&bear") < 1
            || candidates(event).isEmpty()) return {};
        if (event == CardUsed) {
            const Card *card = data.value<CardUseStruct>().card;
            if (!card || card->getTypeId() != Card::TypeTrick) return {};
        }
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        const int previous = player->getMark("JilveEvent");
        player->setMark("JilveEvent", int(event));
        const auto restore = qScopeGuard([&] { player->setMark("JilveEvent", previous); });
        return player->askForSkillInvoke("jilve_" + family(event), *ctx.original_data);
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (player->getMark("&bear") < 1) return false;
        room->notifySkillInvoked(player, objectName());
        player->loseMark("&bear");
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QStringList skills = candidates(event);
        if (skills.isEmpty()) return false;
        const int previous = player->getMark("JilveEvent");
        player->setMark("JilveEvent", int(event));
        const auto restore = qScopeGuard([&] { player->setMark("JilveEvent", previous); });
        // The delegated choice historically follows payment; keep its prompts inside the effect.
        const QString choice = room->askForChoice(player, "_jilve", skills.join("+"));
        if (!skills.contains(choice)) return false;
        const TriggerSkill *skill = Sanguosha->getTriggerSkill(choice);
        if (dynamic_cast<const TriggerSkillV2 *>(skill)) {
            Room::BorrowedSkillScope borrowed(room, player, choice, ctx.activationRef);
            if (borrowed.activationRef().isValid()) {
                room->getThread()->triggerSkillSources(event, room, player, *ctx.original_data, {borrowed.activationRef()});
            } else {
                // A paid delegation survives retirement of its original parent. Keep
                // the existing borrowed instance when available; otherwise bound this
                // frozen grant to the accepted callback and remove exactly that grant.
                int id = 0;
                const auto release = qScopeGuard([&] {
                    try {
                        if (id > 0 && player->hasSkillInstance(choice, id))
                            room->detachSkillFromPlayer(player, SkillInstanceUtils::formatName(choice, id), false, true, false);
                    } catch (...) {}
                });
                room->acquireSkillFromEffect(player, choice, ctx, [&](int committedId) { id = committedId; }, false, false, false);
                if (id <= 0) return false;
                const SkillInstanceRef activation(player->objectName(), SkillInstanceKey(choice, id));
                room->getThread()->triggerSkillSources(event, room, player, *ctx.original_data, {activation});
            }
        }
        return false;
    }
};

class JilveClear : public TriggerSkillV2
{
public:
    JilveClear() : TriggerSkillV2("#jilve-clear")
    { events << EventPhaseChanging << TurnBroken; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        const PhaseChangeStruct change = event == EventPhaseChanging ? data.value<PhaseChangeStruct>() : PhaseChangeStruct();
        const bool clearPhase = event == TurnBroken || change.from == Player::Play;
        const bool clearTurn = event == TurnBroken || change.to == Player::NotActive;
        if (!clearPhase && !clearTurn) return true;
        const QVariant turnId = room->historyScopes().value("turn_id");
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            if (clearPhase) {
                for (int id : holder->getSkillInstanceIds("jilve")) {
                    holder->removeSkillInstanceStateValue("jilve", id, "zhiheng_used");
                    holder->removeSkillInstanceStateValue("jilve", id, "wansha_used");
                }
                room->setPlayerFlag(holder, "-JilveZhiheng");
                room->setPlayerFlag(holder, "-JilveWansha");
            }
            if (!clearTurn) continue;
            QVariantList kept;
            QStringList expired;
            for (const QVariant &value : holder->getTag("JilveWanshaGrants").toList()) {
                const QVariantMap grant = value.toMap();
                const bool sameTurn = turnId.toULongLong() && grant.value("turn_id").toULongLong()
                    ? grant.value("turn_id").toULongLong() == turnId.toULongLong()
                    : grant.value("turn_owner").toString() == player->objectName();
                if (sameTurn) expired << SkillInstanceUtils::formatName(grant.value("skill").toString(), grant.value("id").toInt());
                else kept << value;
            }
            // Remove receipts before callbacks can re-enter cleanup or acquire another Wansha.
            holder->setTag("JilveWanshaGrants", kept);
            if (kept.isEmpty()) holder->removeTag("JilveWansha");
            for (const QString &skill : expired) room->detachSkillFromPlayer(holder, skill, false, true);
        }
        return true;
    }
};

class Lianpo : public TriggerSkillV2
{
public:
    Lianpo() : TriggerSkillV2("lianpo") { events << EventPhaseStart; frequency = Frequent; }
    static bool killedThisTurn(Room *room, ServerPlayer *owner)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (!turn.toULongLong()) return false;
        QVariantMap filter{{"kind", "death"}, {"turn_id", turn}, {"from", owner->objectName()}, {"limit", 100}};
        bool killed = false;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
            for (const QVariant &item : page.value("items").toList())
                if (item.toMap().value("data").toMap().value("killer").toString() == owner->objectName()) killed = true;
            if (!page.value("has_more").toBool()) return killed;
            filter["after"] = page.value("next_after");
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::NotActive) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && killedThisTurn(room, owner)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { ctx.targets = {player}; return player->askForSkillInvoke(this); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i)
            room->executeExtraTurn(target, {}, objectName(), ctx.sourceRef);
        return false;
    }
};

class Juejing : public TriggerSkillV2
{
public:
    Juejing() : TriggerSkillV2("#juejing-draw")
    {
        events << DrawNCards;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { ctx.targets = {player}; return true; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        if (player->isWounded() && player->hasSkill("juejing")) {
            Room *room = player->getRoom();
            room->broadcastSkillInvoke("juejing");

            LogMessage log;
            log.type = "#YongsiGood";
            log.from = player;
            log.arg = QString::number(player->getLostHp());
            log.arg2 = "juejing";
            room->sendLog(log);
            room->notifySkillInvoked(player, "juejing");
        }
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += player->getLostHp() * getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class JuejingKeep : public MaxCardsSkillV2
{
public:
    JuejingKeep() : MaxCardsSkillV2("juejing") { setBaseAmount(2); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    { return CorrectSkillResult::useAmount(ctx.currentAmount); }
};

Longhun::Longhun(const QString &name) : ViewAsSkillV2(name)
{
    response_or_use = true;
}

bool Longhun::canActivate(const ActiveSkillRequest &request) const
{
    if (!request.initiator) return false;
    if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
        return request.initiator->isWounded() || Slash::IsAvailable(request.initiator);
    return request.pattern.contains("slash", Qt::CaseInsensitive) || request.pattern == "jink"
        || request.pattern == "nullification"
        || (request.pattern.contains("peach") && request.initiator->getMark("Global_PreventPeach") == 0);
}

bool Longhun::canSelectCard(const ActiveSkillRequest &request, const Card *card) const
{
    if (!request.initiator || !card || card->hasFlag("using")
        || request.selectedCardIds.size() >= getEffHp(request.initiator)
        || request.selectedCardIds.contains(card->getEffectiveId())) return false;
    QList<int> available = request.initiator->handCards() + request.initiator->getHandPile();
    for (const Card *equip : request.initiator->getEquips()) available << equip->getEffectiveId();
    if (!available.contains(card->getEffectiveId())) return false;
    if (!request.selectedCardIds.isEmpty())
        return card->getSuit() == Sanguosha->getCard(request.selectedCardIds.first())->getSuit();
    if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
        if (card->getSuit() == Card::Heart) return request.initiator->isWounded();
        if (card->getSuit() != Card::Diamond) return false;
        FireSlash slash(Card::SuitToBeDecided, -1);
        slash.addSubcard(card);
        return slash.isAvailable(request.initiator);
    }
    if (request.pattern == "jink") return card->getSuit() == Card::Club;
    if (request.pattern == "nullification") return card->getSuit() == Card::Spade;
    if (request.pattern == "peach" || request.pattern == "peach+analeptic")
        return card->getSuit() == Card::Heart && request.initiator->getMark("Global_PreventPeach") == 0;
    return request.pattern.contains("slash", Qt::CaseInsensitive) && card->getSuit() == Card::Diamond;
}

bool Longhun::cardSelectionFeasible(const ActiveSkillRequest &request) const
{
    if (!request.initiator || request.selectedCardIds.size() != getEffHp(request.initiator)) return false;
    ActiveSkillRequest prefix = request;
    prefix.selectedCardIds.clear();
    for (int id : request.selectedCardIds) {
        if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false;
        prefix.selectedCardIds << id;
    }
    return true;
}

const Card *Longhun::createCard(const ActiveSkillRequest &request) const
{
    if (!cardSelectionFeasible(request)) return nullptr;
    Card *card = nullptr;
    switch (Sanguosha->getCard(request.selectedCardIds.first())->getSuit()) {
    case Card::Spade: card = new Nullification(Card::SuitToBeDecided, 0); break;
    case Card::Heart: card = new Peach(Card::SuitToBeDecided, 0); break;
    case Card::Club: card = new Jink(Card::SuitToBeDecided, 0); break;
    case Card::Diamond: card = new FireSlash(Card::SuitToBeDecided, 0); break;
    default: break;
    }
    if (card) {
        card->setSkillName(objectName());
        card->addSubcards(request.selectedCardIds);
    }
    return card;
}

QString Longhun::historyKey(const ActiveSkillRequest &request) const
{
    if (request.selectedCardIds.isEmpty()) return QString();
    switch (Sanguosha->getCard(request.selectedCardIds.first())->getSuit()) {
    case Card::Spade: return "Nullification";
    case Card::Heart: return "Peach";
    case Card::Club: return "Jink";
    case Card::Diamond: return "FireSlash";
    default: return QString();
    }
}

int Longhun::getEffectIndex(const ServerPlayer *, const Card *card) const
{
    if (!QFile::exists("audio/skill/"+objectName()+"1.ogg")) return -2;
	return Sanguosha->getCard(card->getSubcards().first())->getSuit() + 1;
}

int Longhun::getEffHp(const Player *zhaoyun) const
{
    return qMax(1, zhaoyun->getHp());
}

class NewJuejing : public MaxCardsSkillV2
{
public:
    NewJuejing() : MaxCardsSkillV2("newjuejing") { setBaseAmount(2); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    { return CorrectSkillResult::useAmount(ctx.currentAmount); }
};

class NewJuejingDraw : public TriggerSkillV2
{
public:
    NewJuejingDraw() : TriggerSkillV2("#newjuejing-draw")
    {
        events << EnterDying << QuitDying;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { ctx.targets = {player}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        room->sendCompulsoryTriggerLog(player, "newjuejing", true, true);
        player->drawCards(getEffectiveAmount(ctx), "newjuejing");
        return false;
    }
};

class NewLonghunVS : public Longhun
{
public:
    NewLonghunVS() : Longhun("newlonghun") {}
    int getEffectIndex(const ServerPlayer *player, const Card *card) const override
    { return Skill::getEffectIndex(player, card); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty() || request.selectedCardIds.size() > 2) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
protected:
    int getEffHp(const Player *) const override { return 2; }
};

class NewLonghun : public TriggerSkillV2
{
public:
    NewLonghun() : TriggerSkillV2("newlonghun")
    {
        events << EventSkillInvoking << EventSkillEffectFinished << PreHpRecover << ConfirmDamage << CardUsed << CardResponded;
        global = true; frequency = Compulsory;
        view_as_skill = new NewLonghunVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking && event != EventSkillEffectFinished) return false;
        const SkillContext accepted = data.value<SkillContext>();
        if (accepted.activationRef.key.skillName != objectName() || !accepted.use_card
            || accepted.use_card->subcardsLength() != 2 || !accepted.invoker) return false;
        if (event == EventSkillEffectFinished) {
            if (accepted.use_card->getTag("NewLonghunEffect").toMap().value("execution").toLongLong() == accepted.executionID)
                accepted.use_card->removeTag("NewLonghunEffect");
            return false;
        }
        const Card *first = Sanguosha->getCard(accepted.use_card->getSubcards().first());
        const Card *second = Sanguosha->getCard(accepted.use_card->getSubcards().last());
        if (!first || !second || first->isRed() != second->isRed()) return false;
        // Freeze the accepted conversion before material movement can reset its
        // filtered suits or remove the skill that produced the ordinary card.
        const QString kind = accepted.original_data && accepted.original_data->canConvert<CardResponseStruct>()
            ? "respond_card" : "use_card";
        const qint64 eventId = room->historyParent(room->currentHistoryEventId(), kind, true).value("id").toLongLong();
        accepted.use_card->setTag("NewLonghunEffect", QVariantMap{
            {"event", eventId}, {"event_kind", kind}, {"execution", accepted.executionID},
            {"owner", accepted.sourceRef.ownerObjectName}, {"skill", accepted.sourceRef.key.skillName},
            {"instance", accepted.sourceRef.key.instanceID}, {"activation", accepted.activationRef.key.instanceID},
            {"actor", accepted.invoker->objectName()}, {"red", first->isRed()}, {"amount", getEffectiveAmount(accepted)}});
        return false;
    }
    static const Card *eventCard(TriggerEvent event, const QVariant &data)
    {
        if (event == PreHpRecover) return data.value<RecoverStruct>().card;
        if (event == ConfirmDamage) return data.value<DamageStruct>().card;
        if (event == CardUsed) return data.value<CardUseStruct>().card;
        if (event == CardResponded) return data.value<CardResponseStruct>().m_card;
        return nullptr;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const Card *card = eventCard(event, data);
        if (!card) return true;
        const QVariantMap receipt = card->getTag("NewLonghunEffect").toMap();
        const qint64 eventId = receipt.value("event").toLongLong();
        if (eventId > 0 && room->historyParent(room->currentHistoryEventId(), receipt.value("event_kind").toString(), true)
                .value("id").toLongLong() != eventId) return true;
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        ServerPlayer *actor = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
        if (!owner || !actor || receipt.isEmpty()) return true;
        ServerPlayer *target = nullptr;
        if (event == PreHpRecover || event == ConfirmDamage) {
            if (!receipt.value("red").toBool()) return true;
            target = event == PreHpRecover ? player : data.value<DamageStruct>().to;
        } else {
            if (receipt.value("red").toBool() || !actor->isAlive() || !room->hasCurrent()) return true;
            target = room->getCurrent();
            if (!actor->canDiscard(target, "he")) return true;
        }
        if (!target || !target->isAlive()) return true;
        SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = ctx.initiator = actor;
        ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        ctx.instanceID = receipt.value("activation").toInt(); ctx.amount = receipt.value("amount", 1).toInt();
        ctx.original_data = &data; ctx.current_event = event; ctx.use_card = card;
        ctx.extra_data = receipt; ctx.targets << target;
        contexts << ctx; return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.use_card && ctx.use_card->getTag("NewLonghunEffect").toMap() == ctx.extra_data.toMap(); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == PreHpRecover) {
            RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
            recover.recover = qMin(recover.recover + getEffectiveAmount(ctx), target->getLostHp());
            *ctx.original_data = QVariant::fromValue(recover);
            return recover.recover <= 0;
        }
        if (event == ConfirmDamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to != target) return false;
            damage.damage += getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(damage); return false;
        }
        ServerPlayer *actor = ctx.invoker;
        for (int i = 0; i < getEffectiveAmount(ctx) && actor->isAlive() && target->isAlive()
            && actor->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(actor, target, "he", objectName(), false, Card::MethodDiscard);
            const Card *card = Sanguosha->getCard(id);
            if (!card || card->hasFlag("using") || room->getCardOwner(id) != target
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || !actor->canDiscard(target, id)) continue;
            room->throwCard(id, target, actor);
        }
        return false;
    }
};

class MobileRenjie : public TriggerSkillV2
{
public:
    MobileRenjie() : TriggerSkillV2("mobilerenjie")
    {
        events << ChoiceMade << CardAsked << CardUsed << TrickCardCanceling << CardOnEffect << CardFinished << TurnBroken << EventSkillInvoking;
        frequency = Compulsory;
    }
    LimitScope getLimitScope() const override { return Limit_Round; }
    int getMaxUsageLimit(const SkillContext &) const override { return 4; }
    static qint64 useEvent(Room *room, const Card *card)
    {
        if (!card) return 0;
        const CardUseStruct use = room->getTag("UseHistory" + card->toString()).value<CardUseStruct>();
        return use.targetModReveal.useHistoryEventId;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        ServerPlayer *actor = ctx.invoker;
        if (!owner || !actor) return;
        if (event == EventSkillInvoking) {
            const SkillContext accepted = ctx.original_data->value<SkillContext>();
            if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
            return;
        }
        QVariantMap state = owner->getSkillInstanceState(objectName(), ctx.instanceID);
        QVariantList asks = state.value("asks").toList();
        QVariantList tricks = state.value("tricks").toList();
        state["pending"] = false;
        if (event == CardAsked && actor == owner) {
            const QStringList asked = ctx.original_data->toStringList();
            if (asked.size() >= 4) {
                const CardUseStruct use = room->getTag("UseHistory" + asked[3]).value<CardUseStruct>();
                const qint64 id = use.targetModReveal.useHistoryEventId;
                if (id > 0 && use.card && (use.card->isKindOf("DelayedTrick") || use.from != owner)) {
                    const QString suffix = ":" + asked[0] + ":" + asked[1] + ":";
                    // askForCard(MethodUse) replies as cardResponded; askForUseCard replies as cardUsed.
                    QStringList prefixes{"cardResponded" + suffix};
                    if (asked[2] == "use") prefixes << "cardUsed" + suffix;
                    asks << QVariantMap{{"use_event", id}, {"prefixes", prefixes}, {"use_request", asked[2] == "use"}};
                }
            }
        } else if (event == TrickCardCanceling && actor == owner) {
            const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
            const qint64 id = useEvent(room, effect.card);
            if (id > 0 && effect.card && (effect.card->isKindOf("DelayedTrick") || effect.from != owner))
                tricks << QVariantMap{{"use_event", id}, {"target", effect.to ? effect.to->objectName() : QString()}};
        } else if (event == ChoiceMade) {
            const QString reply = ctx.original_data->toString();
            if (actor == owner && (reply.startsWith("cardResponded:") || reply.startsWith("cardUsed:"))) {
                const qint64 origin = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
                // Match the entire prompt and its current originating use, not an outer request.
                // A successful response consumes the same request without rewarding it.
                for (int i = asks.size() - 1; i >= 0; --i) {
                    const QVariantMap receipt = asks[i].toMap();
                    if (origin <= 0 || receipt.value("use_event").toLongLong() != origin) continue;
                    QString prefix;
                    for (const QString &candidate : receipt.value("prefixes").toStringList())
                        if (reply.startsWith(candidate)) { prefix = candidate; break; }
                    if (prefix.isEmpty()) continue;
                    asks.removeAt(i);
                    state["pending"] = reply == prefix;
                    state["pendingEvent"] = reply;
                    break;
                }
            } else if (reply.startsWith("Nullification:")) {
                const QStringList parts = reply.split(":");
                QList<qint64> parents;
                QVariantMap parent = room->historyParent(room->currentHistoryEventId(), "use_card", true);
                while (parent.value("id").toLongLong() > 0) {
                    const qint64 id = parent.value("id").toLongLong();
                    if (parents.contains(id)) break;
                    parents << id; parent = room->historyParent(id, "use_card", false);
                }
                for (int i = tricks.size() - 1; i >= 0; --i) {
                    const QVariantMap receipt = tricks[i].toMap();
                    if (!parents.contains(receipt.value("use_event").toLongLong())
                        || parts.size() < 3 || parts[2] != receipt.value("target").toString()) continue;
                    tricks.removeAt(i);
                    state["pending"] = actor != owner;
                    state["pendingEvent"] = reply;
                    break;
                }
            }
        } else if (event == CardUsed && actor == owner) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            const qint64 origin = useEvent(room, use.whocard);
            const qint64 current = use.targetModReveal.useHistoryEventId;
            const qint64 parent = room->historyParent(current, "use_card", false).value("id").toLongLong();
            // A successful response use closes only its immediate originating request.
            // Unrelated nested cards must not remove an outer request with the same prompt.
            if (origin > 0 && current > 0 && parent == origin) {
                for (int i = asks.size() - 1; i >= 0; --i) {
                    const QVariantMap receipt = asks[i].toMap();
                    if (!receipt.value("use_request").toBool() || receipt.value("use_event").toLongLong() != origin) continue;
                    asks.removeAt(i);
                    break;
                }
            }
        } else if (event == CardOnEffect) {
            const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
            const qint64 id = useEvent(room, effect.card);
            for (int i = tricks.size() - 1; i >= 0; --i) {
                const QVariantMap receipt = tricks[i].toMap();
                if (id <= 0 || receipt.value("use_event").toLongLong() != id
                    || receipt.value("target").toString() != (effect.to ? effect.to->objectName() : QString())) continue;
                tricks.removeAt(i);
                state["pending"] = true;
                state["pendingEvent"] = QString::number(id);
                break;
            }
        } else if (event == CardFinished) {
            const qint64 id = ctx.original_data->value<CardUseStruct>().targetModReveal.useHistoryEventId;
            for (int i = asks.size() - 1; i >= 0; --i)
                if (asks[i].toMap().value("use_event").toLongLong() == id) asks.removeAt(i);
            for (int i = tricks.size() - 1; i >= 0; --i)
                if (tricks[i].toMap().value("use_event").toLongLong() == id) tricks.removeAt(i);
        } else if (event == TurnBroken) { asks.clear(); tricks.clear(); }
        state["asks"] = asks; state["tricks"] = tricks;
        owner->setSkillInstanceState(objectName(), ctx.instanceID, state);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != ChoiceMade && event != CardOnEffect) return result;
        const Card *effectCard = event == CardOnEffect ? data.value<CardEffectStruct>().card : nullptr;
        const QString eventKey = effectCard ? QString::number(useEvent(room, effectCard)) : data.toString();
        for (ServerPlayer *owner : room->getAlivePlayers()) {
            if (!owner->hasSkill(objectName())) continue;
            for (int id : owner->getValidSkillInstanceIds(objectName()))
                if (owner->getSkillInstanceStateValue(objectName(), id, "pending").toBool()
                    && owner->getSkillInstanceStateValue(objectName(), id, "pendingEvent").toString() == eventKey)
                    result[owner] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.targets = {owner};
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "pending", false);
        return isUsable(ctx);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "pending", false); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        room->sendCompulsoryTriggerLog(player, this);
        player->gainMark("&bear", getEffectiveAmount(ctx));
        player->addMark("bear_lun");
        return false;
    }
};

class MobileLianpo : public TriggerSkillV2
{
public:
    MobileLianpo() : TriggerSkillV2("mobilelianpo") { events << Death << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
        return holder && (ctx.choice != "mobilelianpo1"
            || !holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_turn_used").toBool());
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
        if (holder && ctx.choice == "mobilelianpo1")
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_turn_used", true);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
        if (holder) holder->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_turn_used");
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    static QStringList choicesFor(ServerPlayer *player, int instanceId)
    {
        QStringList choices;
        if (!player->getSkillInstanceStateValue("mobilelianpo", instanceId, "extra_turn_used").toBool())
            choices << "mobilelianpo1";
        if (player->hasSkill("mobilejilve", true))
            for (const QString &skill : QStringList{"guicai", "fangzhu", "jizhi", "zhiheng", "wansha"})
                if (!player->hasSkill(skill, true)) choices << "mobilelianpo2=" + skill;
        return choices;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        TriggerList result;
        // Death is broadcast per observer: this is only the killer's observation.
        if (event != Death || !player || !player->isAlive() || !death.damage || death.damage->from != player) return result;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (!choicesFor(player, id).isEmpty()) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QStringList choices = choicesFor(player, ctx.instanceID);
        if (choices.isEmpty() || !player->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
        ctx.choice = room->askForChoice(player, objectName(), choices.join("+"));
        ctx.targets = {player}; return choices.contains(ctx.choice);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        if (ctx.choice != "mobilelianpo1") {
            const QString skill = ctx.choice.section('=', -1);
            if (QStringList{"guicai", "fangzhu", "jizhi", "zhiheng", "wansha"}.contains(skill))
                room->acquireSkillFromEffect(player, skill, ctx);
            return false;
        }
        const int serial = room->getTag("MobileLianpoNextReceipt").toInt() + 1;
        room->setTag("MobileLianpoNextReceipt", serial);
        QVariantList pending = player->getTag("MobileLianpoTurns").toList();
        pending << QVariantMap{{"id", serial}, {"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"amount", getEffectiveAmount(ctx)}, {"turn_id", room->historyScopes().value("turn_id")},
            {"turn_owner", room->getCurrent() ? room->getCurrent()->objectName() : QString()}};
        player->setTag("MobileLianpoTurns", pending);
        room->setPlayerMark(player, "mobilelianpo1", pending.size());
        return false;
    }
};

class MobileLianpoTurns : public TriggerSkillV2
{
public:
    MobileLianpoTurns() : TriggerSkillV2("#mobilelianpo-turns")
    { events << EventPhaseStart << EventPhaseChanging << EventPhaseEnd << EventSkillEffectFinished << TurnBroken << Death; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && finished.invoker) {
                QVariantList pending = finished.invoker->getTag("MobileLianpoTurns").toList();
                pending.removeOne(finished.extra_data);
                finished.invoker->setTag("MobileLianpoTurns", pending);
                room->setPlayerMark(finished.invoker, "mobilelianpo1", pending.size());
            }
            return false;
        }
        if (!player) return false;
        const bool turnEnd = event == TurnBroken || (event == EventPhaseChanging
            && data.value<PhaseChangeStruct>().to == Player::NotActive);
        const bool expire = event == TurnBroken || (event == EventPhaseEnd && player->getPhase() == Player::NotActive);
        const bool death = event == Death && data.value<DeathStruct>().who == player;
        if (!turnEnd && !expire && !death) return false;
        const QVariant turn = room->historyScopes().value("turn_id");
        const Skill *main = Sanguosha->getSkill("mobilelianpo");
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            if (turnEnd && main) {
                for (int id : holder->getSkillInstanceIds("mobilelianpo")) {
                    SkillContext reset; reset.owner = holder; reset.invoker = holder;
                    reset.activationRef = SkillInstanceRef(holder->objectName(), SkillInstanceKey("mobilelianpo", id));
                    main->resetUsage(reset);
                }
            }
            if (!expire && !(death && holder == player)) continue;
            QVariantList kept;
            for (const QVariant &value : holder->getTag("MobileLianpoTurns").toList()) {
                const QVariantMap receipt = value.toMap();
                const bool thisTurn = turn.toULongLong() && receipt.value("turn_id").toULongLong()
                    ? receipt.value("turn_id").toULongLong() == turn.toULongLong()
                    : receipt.value("turn_owner").toString() == player->objectName();
                if (!death && !thisTurn) kept << value;
            }
            holder->setTag("MobileLianpoTurns", kept);
            room->setPlayerMark(holder, "mobilelianpo1", kept.size());
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::NotActive) return true;
        const QVariant turn = room->historyScopes().value("turn_id");
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            for (const QVariant &value : target->getTag("MobileLianpoTurns").toList()) {
                const QVariantMap receipt = value.toMap();
                const bool thisTurn = turn.toULongLong() && receipt.value("turn_id").toULongLong()
                    ? receipt.value("turn_id").toULongLong() == turn.toULongLong()
                    : receipt.value("turn_owner").toString() == player->objectName();
                if (!thisTurn || !target->isAlive()) continue;
                ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
                if (!owner) continue;
                SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = ctx.initiator = target;
                ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
                ctx.instanceID = receipt.value("id").toInt(); ctx.amount = receipt.value("amount", 1).toInt();
                ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = receipt; ctx.targets << target;
                contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->getTag("MobileLianpoTurns").toList().contains(ctx.extra_data); }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantList pending = ctx.invoker->getTag("MobileLianpoTurns").toList();
        pending.removeOne(ctx.extra_data); ctx.invoker->setTag("MobileLianpoTurns", pending);
        room->setPlayerMark(ctx.invoker, "mobilelianpo1", pending.size());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i)
            room->executeExtraTurn(target, {}, "mobilelianpo", ctx.sourceRef);
        return false;
    }
};

class MobileBaiyin : public TriggerSkillV2
{
public:
    MobileBaiyin() : TriggerSkillV2("mobilebaiyin")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "mobilejilve";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart) return result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start
            || !player->hasSkill(objectName()) || !(player->getMark("&bear") >= 4 || player->canWake(objectName()))) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext eligibility; eligibility.owner = player; eligibility.invoker = player;
            eligibility.skill_name = objectName(); eligibility.instanceID = id;
            eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }

    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets = {owner}; return isUsable(ctx); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *shensimayi) const override
    {
        if (!(shensimayi->getMark("&bear") >= 4 || shensimayi->canWake(objectName()))) return false;
        if (shensimayi->getMark("&bear") >= 4) {
            LogMessage log;
            log.type = "#BaiyinWake";
            log.from = shensimayi;
            log.arg = QString::number(shensimayi->getMark("&bear"));
            room->sendLog(log);
        }else if(!shensimayi->canWake(objectName()))
			return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(shensimayi, objectName());
        //room->doLightbox("$BaiyinAnimate");
        room->doSuperLightbox(shensimayi, objectName());

        room->setPlayerMark(shensimayi, objectName(), 1);
        if (room->changeMaxHpForAwakenSkill(shensimayi, -getEffectiveAmount(ctx), objectName()))
            room->acquireSkillFromEffect(shensimayi, "mobilejilve", ctx);

        return false;
    }
};

class MobileJilve : public TriggerSkillV2
{
public:
    MobileJilve() : TriggerSkillV2("mobilejilve")
    { events << EventAcquireSkill << EventPhaseStart; waked_skills = "guicai,fangzhu,jizhi,zhiheng,wansha"; }
    static QStringList choicesFor(ServerPlayer *player, int instanceId)
    {
				QStringList choices;
				for (int i = 1; i <= qMin(3,player->getMark("&bear")); i++){
					choices << "mobilejilve1="+QString::number(i);
				}
				int n = player->getSkillInstanceStateValue("mobilejilve", instanceId, "purchases").toInt()+1;
				if(n<=player->getMark("&bear")){
					if(!player->hasSkill("guicai",true))
						choices << "mobilejilve2=guicai="+QString::number(n);
					if(!player->hasSkill("fangzhu",true))
						choices << "mobilejilve2=fangzhu="+QString::number(n);
					if(!player->hasSkill("jizhi",true))
						choices << "mobilejilve2=jizhi="+QString::number(n);
					if(!player->hasSkill("zhiheng",true))
						choices << "mobilejilve2=zhiheng="+QString::number(n);
					if(!player->hasSkill("wansha",true))
						choices << "mobilejilve2=wansha="+QString::number(n);
				}

        return choices;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventAcquireSkill) {
            const SkillChangeStruct change = data.value<SkillChangeStruct>();
            return change.skillName == objectName()
                ? TriggerList{{player, {SkillInstanceUtils::formatName(objectName(), change.instanceID)}}} : TriggerList();
        }
        TriggerList result;
        if (player->getPhase() != Player::Play) return result;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (!choicesFor(player, id).isEmpty()) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.targets = {player};
        if (event == EventAcquireSkill) return true;
        const QStringList choices = choicesFor(player, ctx.instanceID);
        if (choices.isEmpty() || !player->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
        ctx.choice = room->askForChoice(player, objectName(), choices.join("+"));
        return choices.contains(ctx.choice);
    }
    bool pay(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventAcquireSkill) return true;
        const int count = ctx.choice.section('=', -1).toInt();
        if (count <= 0 || player->getMark("&bear") < count || !choicesFor(player, ctx.instanceID).contains(ctx.choice)) return false;
        player->loseMark("&bear", count);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        if (event == EventAcquireSkill) {
            room->sendCompulsoryTriggerLog(player, this);
            room->acquireSkillFromEffect(player, "guicai", ctx);
            const QMap<QString, QString> gifts{{"wei", "fangzhu"}, {"shu", "jizhi"}, {"wu", "zhiheng"}, {"qun", "wansha"}};
            const QString gift = gifts.value(player->getKingdom());
            if (!gift.isEmpty()) room->acquireSkillFromEffect(player, gift, ctx);
        } else if (ctx.choice.startsWith("mobilejilve1=")) {
            player->drawCards(ctx.choice.section('=', -1).toInt() * getEffectiveAmount(ctx), objectName());
        } else {
            const int acquired = room->acquireSkillFromEffect(player, ctx.choice.section('=', 1, 1), ctx);
            // Target hooks may change the recipient; the purchase count belongs to the activating instance.
            ServerPlayer *holder = ctx.owner;
            if (acquired > 0 && holder && holder->hasSkillInstance(objectName(), ctx.instanceID)) {
                holder->setSkillInstanceStateValue(objectName(), ctx.instanceID, "purchases",
                    holder->getSkillInstanceStateValue(objectName(), ctx.instanceID, "purchases").toInt() + 1);
                holder->addMark("mobilejilveUse");
            }
        }
        return false;
    }
};






MountainPackage::MountainPackage()
    : Package("mountain")
{
    General *zhanghe = new General(this, "zhanghe", "wei"); // WEI 009
    zhanghe->addSkill(new Qiaobian);

    General *dengai = new General(this, "dengai", "wei", 4); // WEI 015
    dengai->addSkill(new Tuntian);
    dengai->addSkill(new TuntianJudge);
    related_skills.insertMulti("tuntian", "#tuntian-judge");
    dengai->addSkill(new TuntianDistance);
    dengai->addSkill(new Zaoxian);
    related_skills.insert("tuntian", "#tuntian-dist");

    General *jiangwei = new General(this, "jiangwei", "shu"); // SHU 012
    jiangwei->addSkill(new Tiaoxin);
    jiangwei->addSkill(new Zhiji);

    General *liushan = new General(this, "liushan$", "shu", 3); // SHU 013
    liushan->addSkill(new Xiangle);
    liushan->addSkill(new Fangquan);
    liushan->addSkill(new FangquanRecord);
    liushan->addSkill(new Ruoyu);

    General *sunce = new General(this, "sunce$", "wu"); // WU 010
    sunce->addSkill(new Jiang);
    sunce->addSkill(new Hunzi);
    sunce->addSkill(new Zhiba);

    General *erzhang = new General(this, "erzhang", "wu", 3); // WU 015
    erzhang->addSkill(new Zhijian);
    erzhang->addSkill(new Guzheng);

    General *zuoci = new General(this, "zuoci", "qun", 3); // QUN 009
    zuoci->addSkill(new Huashen);
    zuoci->addSkill(new HuashenSelect);
    zuoci->addSkill(new HuashenClear);
    zuoci->addSkill(new Xinsheng);
    related_skills.insert("huashen", "#huashen-select");
    related_skills.insert("huashen", "#huashen-clear");

    General *caiwenji = new General(this, "caiwenji", "qun", 3, false); // QUN 012
    caiwenji->addSkill(new Beige);
    caiwenji->addSkill(new Duanchang);

    General *shenzhaoyun = new General(this, "shenzhaoyun", "god", 2); // LE 007
    shenzhaoyun->addSkill(new JuejingKeep);
    shenzhaoyun->addSkill(new Juejing);
    shenzhaoyun->addSkill(new Longhun);
    related_skills.insert("juejing", "#juejing-draw");

    General *shensimayi = new General(this, "shensimayi", "god", 4); // LE 008
    shensimayi->addSkill(new Renjie);
    shensimayi->addSkill(new Baiyin);
    related_skills.insert("jilve", "#jilve-clear");
    shensimayi->addSkill(new Lianpo);
    addMetaObject<JilveCard>();

    addMetaObject<QiaobianCard>();
    addMetaObject<TiaoxinCard>();
    addMetaObject<ZhijianCard>();
    addMetaObject<ZhibaCard>();
    addMetaObject<FangquanCard>();
    addMetaObject<GuzhengCard>();

    skills << new ZhibaPindian << new Jixi << new Jilve << new JilveClear;
}
ADD_PACKAGE(Mountain)

NewShenPackage::NewShenPackage()
    : Package("NewShen")
{
    RegisterNewShencaocao(this);

    General *new_shenzhaoyun = new General(this, "new_shenzhaoyun", "god", 2);
    new_shenzhaoyun->addSkill(new NewJuejing);
    new_shenzhaoyun->addSkill(new NewJuejingDraw);
    new_shenzhaoyun->addSkill(new NewLonghun);
    related_skills.insert("newjuejing", "#newjuejing-draw");
}
ADD_PACKAGE(NewShen)

void MigrateToMobileStMountain(Package *pkg)
{
    General *mobile_shensimayi = new General(pkg, "mobile_shensimayi", "god", 4); // LE 008
    mobile_shensimayi->addSkill(new MobileRenjie);
    mobile_shensimayi->addSkill(new MobileLianpo);
    mobile_shensimayi->addSkill(new MobileLianpoTurns);
    related_skills.insertMulti("mobilelianpo", "#mobilelianpo-turns");
    mobile_shensimayi->addSkill(new MobileBaiyin);
    pkg->addSkills(new MobileJilve);
}
