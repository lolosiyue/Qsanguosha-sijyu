#include "yin.h"
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
#include "skill-instance-utils.h"
#include "roomthread.h"
#include <QScopeGuard>

class YinAllowances : public TriggerSkillV2
{
public:
    YinAllowances() : TriggerSkillV2("#yin-allowances")
    { global = true; events << TurnStart << EventPhaseStart << EventPhaseEnd << EventPhaseChanging << TurnBroken << TurnedOver; }
    static void project(Room *room, qint64 turn, qint64 phase)
    {
        // Property writes can re-enter prohibit / target-mod checks. Those checks call projectCurrent.
        static bool projecting = false;
        if (!room || projecting) return;
        projecting = true;
        const auto stop = qScopeGuard([&]() { projecting = false; });
        const QVariantList receipts = room->getTag("YinAllowances").toList();
        for (ServerPlayer *recipient : room->getAllPlayers(true)) {
            QStringList prohibited, suits; int residue = 0;
            for (const QVariant &entry : receipts) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("recipient").toString() != recipient->objectName() || receipt.value("turn").toLongLong() != turn) continue;
                const QString kind = receipt.value("kind").toString();
                if (kind == "juzhan") { const QString other = receipt.value("value").toString(); if (!prohibited.contains(other)) prohibited << other; }
                else if (kind == "chenglve") { const QString suit = receipt.value("value").toString(); if (!suits.contains(suit)) suits << suit; }
                else if (kind == "ollijun" && receipt.value("phase").toLongLong() == phase) residue += receipt.value("value").toInt();
            }
            // Only public effect values are projected; frozen attribution remains server-side.
            if (recipient->property("juzhan_prohibited").toStringList() != prohibited) room->setPlayerProperty(recipient, "juzhan_prohibited", prohibited);
            if (recipient->property("chenglve_suits").toStringList() != suits) room->setPlayerProperty(recipient, "chenglve_suits", suits);
            if (recipient->property("ollijun_residue").toInt() != residue) room->setPlayerProperty(recipient, "ollijun_residue", residue);
        }
    }
    static void apply(Room *room, const SkillContext &ctx, ServerPlayer *recipient, const QString &kind, const QVariant &value)
    {
        const QVariantMap scope = room->historyScopes();
        const qint64 turn = scope.value("turn_id").toLongLong(), phase = scope.value("phase_id").toLongLong();
        if (turn <= 0 || (kind == "ollijun" && phase <= 0)) return;
        const int serial = room->getTag("YinAllowanceSerial").toInt() + 1; room->setTag("YinAllowanceSerial", serial);
        QVariantList receipts = room->getTag("YinAllowances").toList();
        receipts << QVariantMap{{"serial", serial}, {"turn", turn}, {"phase", phase}, {"kind", kind}, {"value", value}, {"recipient", recipient->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        room->setTag("YinAllowances", receipts); project(room, turn, phase);
    }
    static void projectCurrent(Room *room)
    {
        if (!room) return;
        const QVariantMap scope = room->historyScopes();
        project(room, scope.value("turn_id").toLongLong(), scope.value("phase_id").toLongLong());
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const QVariantMap scope = room->historyScopes();
        qint64 turn = scope.value("turn_id").toLongLong(), phase = scope.value("phase_id").toLongLong();
        const bool endingTurn = event == TurnBroken
            || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            || (event == EventPhaseStart && player && player->getPhase() == Player::NotActive);
        // A face-down extra turn never opens a phase, so NotActive cleanup does not run.
        // Restore the caller before TurnStart returns; a face-down player is turned face up and skips play.
        const bool skippedExtraTurn = event == TurnedOver && room->isCurrentExtraTurn() && phase <= 0 && turn > 0
            && player && player == room->getCurrent() && player->faceUp();
        const bool endingPlay = event == EventPhaseEnd && player && player->getPhase() == Player::Play;
        if (endingTurn || skippedExtraTurn || endingPlay) {
            QVariantList kept;
            for (const QVariant &entry : room->getTag("YinAllowances").toList()) {
                const QVariantMap receipt = entry.toMap();
                const bool expires = ((endingTurn || skippedExtraTurn) && receipt.value("turn").toLongLong() == turn)
                    || (endingPlay && !endingTurn && !skippedExtraTurn && receipt.value("kind").toString() == "ollijun"
                        && receipt.value("phase").toLongLong() == phase);
                if (!expires) kept << entry;
            }
            room->setTag("YinAllowances", kept);
            // TurnBroken is still scoped to the interrupted turn. Do not publish the outer turn until that scope ends.
            if ((endingTurn && event != TurnBroken) || skippedExtraTurn) {
                const qint64 ended = turn;
                turn = room->historyParent(ended, "turn", false).value("id").toLongLong();
                phase = room->historyParent(ended, "phase", false).value("id").toLongLong();
            }
        }
        if (!endingTurn && !skippedExtraTurn && event == EventPhaseEnd && phase > 0) {
            const QVariantMap current = room->historyEvent(phase);
            const QVariantMap parent = room->historyEvent(current.value("parent_id").toLongLong());
            // Ending a nested phase resumes the still-active outer phase. Its play-phase slash allowance must stay.
            if (parent.value("kind").toString() == "phase" && parent.value("status").toString() == "active") {
                const qint64 parentTurn = parent.value("turn_id").toLongLong();
                if (parentTurn > 0) turn = parentTurn;
                phase = parent.value("id").toLongLong();
            }
        }
        project(room, turn, phase); return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class Juzhan : public TriggerSkillV2
{
public:
    Juzhan() : TriggerSkillV2("juzhan") { events << TargetSpecified << TargetConfirmed; change_skill = true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card || !use.card->isKindOf("Slash")) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const int state = player->getSkillInstanceStateValue(objectName(), id, "conversion_state", 1).toInt();
            if ((event == TargetConfirmed && state == 1 && use.from && use.from != player && use.to.contains(player))
                || (event == TargetSpecified && state == 2 && use.from == player)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetConfirmed) {
            if (!use.from || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(use.from))) return false;
            ctx.targets = {use.from}; ctx.choice = "defend";
        } else {
            for (ServerPlayer *target : use.to)
                if (target->isAlive() && !target->isNude() && ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) { ctx.targets = {target}; break; }
            if (ctx.targets.isEmpty()) return false;
            ctx.choice = "attack";
        }
        ctx.manual_effect = true; return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const bool defend = ctx.choice == "defend";
        ServerPlayer *other = ctx.targets.value(0);
        if (!other) return false;
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", defend ? 2 : 1);
        room->setChangeSkillState(ctx.owner, objectName(), defend ? 2 : 1); room->broadcastSkillInvoke(objectName());
        if (defend) {
            QList<ServerPlayer *> recipients{ctx.owner, other}; room->sortByActionOrder(recipients);
            for (ServerPlayer *target : recipients) { SkillContext draw = ctx; draw.choice = "draw"; draw.targets = {target}; skillEffect(event, room, player, draw, target); }
        } else {
            SkillContext take = ctx; take.choice = "take"; take.targets = {other}; skillEffect(event, room, player, take, other);
        }
        if (room->hasCurrent() && room->getCurrent()->getPhase() != Player::NotActive) {
            SkillContext restriction = ctx; restriction.choice = "restriction"; restriction.extra_data = defend ? ctx.owner->objectName() : other->objectName();
            ServerPlayer *from = defend ? other : ctx.owner; restriction.targets = {from}; skillEffect(event, room, player, restriction, from);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else if (ctx.choice == "restriction") {
            // Each accepted restriction names its exact counterpart; unrelated pairs never form a cross-product.
            YinAllowances::apply(room, ctx, target, "juzhan", ctx.extra_data.toString());
        } else if (ctx.choice == "receive") {
            const QVariantMap material = ctx.extra_data.toMap(); const int id = material.value("id", -1).toInt();
            ServerPlayer *victim = room->findPlayerByObjectName(material.value("victim").toString(), true);
            if (id >= 0 && victim && room->getCardOwner(id) == victim
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip) && !Sanguosha->getCard(id)->hasFlag("using"))
                room->obtainCard(target, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, ctx.owner->objectName()), room->getCardPlace(id) != Player::PlaceHand);
        } else if (ctx.choice == "take") {
            for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive() && !target->isNude(); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "he", objectName());
                SkillContext receive = ctx; receive.choice = "receive"; receive.extra_data = QVariantMap{{"id", id}, {"victim", target->objectName()}}; receive.targets = {ctx.owner};
                skillEffect(event, room, player, receive, ctx.owner);
            }
        }
        return false;
    }
};
class JuzhanPro : public ProhibitSkill
{
public:
    JuzhanPro() : ProhibitSkill("#juzhanpro")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(from)) YinAllowances::projectCurrent(server->getRoom());
        return from && to && card && !card->isKindOf("SkillCard") && from->property("juzhan_prohibited").toStringList().contains(to->objectName());
    }
};

FeijunCard::FeijunCard()
{
    setSkillName("feijun");
    target_fixed = true;
}

void FeijunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->isDead()) return;
    int handcardnum = source->getHandcardNum();
    int equipnum = source->getEquips().length();
    QList<ServerPlayer *> handp;
    QList<ServerPlayer *> equipp;
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (p->getHandcardNum() > handcardnum)
            handp << p;
        if (p->getEquips().length() > equipnum)
            equipp << p;
    }
    QStringList choices;
    if (!handp.isEmpty()) choices << "givehand";
    if (!equipp.isEmpty()) choices << "discardequip";
    if (choices.isEmpty()) return;
    QString choice = room->askForChoice(source, "feijun", choices.join("+"), QVariant());
    if (choice == "givehand") {
        ServerPlayer *target = room->askForPlayerChosen(source, handp, "feijun", "@feijun-choosehand");
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, source->objectName(), target->objectName());
        room->addPlayerMark(target, "feijun_hasused_" + source->objectName());
        if (target->isKongcheng()) return;
        const Card *card = room->askForExchange(target, "feijun", 1, 1, true, "feijun-givehand:" + source->objectName());
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "feijun", "");
        room->obtainCard(source, card, reason, false);
    } else {
        ServerPlayer *target = room->askForPlayerChosen(source, equipp, "feijun", "@feijun-chooseequip");
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, source->objectName(), target->objectName());
        room->addPlayerMark(target, "feijun_hasused_" + source->objectName());
        if (!target->canDiscard(target, "e")) return;
        room->askForDiscard(target, "feijun_discardequip", 1, 1, false, true, "feijun-discardequip", ".|.|.|equipped");
    }
}

class Feijun : public ViewAsSkillV2
{
public:
    Feijun() : ViewAsSkillV2("feijun", 1) { setPhaseName("Play"); }
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canDiscard(request.initiator, "he"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using") && !request.initiator->isJilei(card) && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId())); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { ActiveSkillRequest empty = request; empty.selectedCardIds.clear(); return request.selectedCardIds.size() == 1 && canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first())); }
    QString historyKey(const ActiveSkillRequest &) const override { return "FeijunCard"; }
    static bool newTarget(Room *room, const SkillContext &ctx, ServerPlayer *target)
    {
        if (ctx.executionID <= 0) return false;
        QVariantMap query{{"kind", "skill"}, {"limit", 64}};
        while (true) {
            const QVariantMap page = room->queryHistoryEvents(query);
            if (!page.value("complete").toBool()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap use = entry.toMap().value("data").toMap();
                if (use.value("executing_skill").toString().isEmpty() && use.value("skill_name").toString() == "feijun") return false;
                if (use.value("executing_skill").toString() != "feijun" || use.value("invoker").toString() != ctx.invoker->objectName()
                    || use.value("execution_id").toLongLong() == ctx.executionID) continue;
                if (!use.contains("targets")) return false;
                for (const QVariant &name : use.value("targets").toList()) if (name.toString() == target->objectName()) return false;
            }
            if (!page.value("has_more").toBool()) return true;
            query.insert("after", page.value("next_after"));
        }
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom(); QList<ServerPlayer *> hand, equip;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.invoker)) {
            if (other->getHandcardNum() > ctx.invoker->getHandcardNum()) hand << other;
            if (other->getEquips().size() > ctx.invoker->getEquips().size()) equip << other;
        }
        QStringList choices; if (!hand.isEmpty()) choices << "givehand"; if (!equip.isEmpty()) choices << "discardequip";
        if (choices.isEmpty()) return FinishSkill;
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"));
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, ctx.choice == "givehand" ? hand : equip, objectName(), ctx.choice == "givehand" ? "@feijun-choosehand" : "@feijun-chooseequip");
        if (!target) return FinishSkill;
        ctx.targets = {target}; ctx.extra_data = QVariantMap{{"new_target", newTarget(room, ctx, target)}};
        // Commit the chosen recipient before any nested effect. Both normal and interrupted executions preserve it.
        room->setSkillExecutionContext(ctx.executionID, ctx);
        const QVariantMap parent = room->historyParent(room->currentHistoryEventId(), "skill", true);
        if (parent.value("id").toLongLong() > 0 && parent.value("data").toMap().value("execution_id").toLongLong() == ctx.executionID)
            room->resolutionHistory().updateEvent(parent.value("id").toLongLong(), room->historySkillContext(ctx));
        skillEffect(ctx, target); return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "receive") {
            const QVariantMap gift = ctx.extra_data.toMap(); ServerPlayer *donor = room->findPlayerByObjectName(gift.value("donor").toString());
            QList<int> ids; for (const QVariant &value : gift.value("ids").toList()) { const int id = value.toInt(); if (donor && room->getCardOwner(id) == donor && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip) && !Sanguosha->getCard(id)->hasFlag("using")) ids << id; }
            if (!ids.isEmpty()) room->giveCard(donor, target, ids, objectName());
        } else if (ctx.choice == "givehand") {
            const int count = getEffectiveAmount(ctx); if (count <= 0 || target->isNude()) return ContinueEffects;
            const Card *gift = room->askForExchange(target, objectName(), count, count, true, "feijun-givehand:" + ctx.invoker->objectName());
            if (gift && ctx.invoker->isAlive()) {
                QVariantList ids; for (int id : gift->getSubcards()) ids << id;
                SkillContext receive = ctx; receive.choice = "receive"; receive.targets = {ctx.invoker}; receive.extra_data = QVariantMap{{"donor", target->objectName()}, {"ids", ids}}; skillEffect(receive, ctx.invoker);
            }
        } else if (target->canDiscard(target, "e")) room->askForDiscard(target, "feijun_discardequip", getEffectiveAmount(ctx), getEffectiveAmount(ctx), false, true, "feijun-discardequip", ".|.|.|equipped");
        return ContinueEffects;
    }
};

class Binglve : public TriggerSkillV2
{
public:
    Binglve() : TriggerSkillV2("binglve") { events << EventSkillEffectTarget; frequency = Compulsory; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const SkillContext feijun = data.value<SkillContext>();
        return player && feijun.activationRef.key.skillName == "feijun" && feijun.invoker && feijun.invoker->isAlive()
            && feijun.invoker->hasSkill(objectName()) && feijun.targets.contains(player)
            && (feijun.choice == "givehand" || feijun.choice == "discardequip") && feijun.extra_data.toMap().value("new_target").toBool()
            ? TriggerList{{feijun.invoker, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};
class Huaiju : public TriggerSkillV2
{
public:
    Huaiju() : TriggerSkillV2("huaiju") { events << GameStart << DamageInflicted << DrawNCards; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive()) return {};
        if (event == GameStart) return player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (player->getMark("&orange") <= 0) return {};
        if (event == DrawNCards && (data.value<DrawStruct>().who != player || data.value<DrawStruct>().reason != "draw_phase")) return {};
        if (event == DamageInflicted && data.value<DamageStruct>().to != player) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    { ctx.targets = {player}; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == GameStart) {
            room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
            target->gainMark("&orange", 3 * getEffectiveAmount(ctx)); return false;
        }
        if (target->getMark("&orange") <= 0) return false;
        room->broadcastSkillInvoke(objectName()); room->notifySkillInvoked(ctx.owner, objectName());
        LogMessage log; log.from = target; log.arg = objectName();
        if (event == DamageInflicted) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to != target) return false;
            // Orange is a shared game resource; consuming it is this prevention's effect.
            target->loseMark("&orange");
            log.type = "#HuaijuPrevent"; log.arg2 = QString::number(damage.damage); room->sendLog(log);
            return true;
        }
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        if (draw.who != target) return false;
        log.type = "#HuaijuDraw"; log.arg2 = QString::number(getEffectiveAmount(ctx)); room->sendLog(log);
        draw.num += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(draw); return false;
    }
};

class HuaijuDeath : public TriggerSkillV2
{
public:
    HuaijuDeath() : TriggerSkillV2("#huaijudeath") { events << Death; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DeathStruct>().who != player || !player->hasSkill(objectName())) return {};
        for (ServerPlayer *other : room->getAlivePlayers()) if (other->getMark("&orange") > 0) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *other : room->getAlivePlayers()) if (other->getMark("&orange") > 0) ctx.targets << other;
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    { target->loseAllMarks("&orange"); return false; }
};
class Weili : public TriggerSkillV2
{
public:
    Weili() : TriggerSkillV2("weili") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "@weili-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        ctx.choice = room->askForChoice(ctx.owner, objectName(),
            ctx.owner->getMark("&orange") > 0 ? "losehp+losemark" : "losehp");
        return ctx.choice == "losehp" || ctx.choice == "losemark";
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        // Orange is a shared gameplay resource; losing HP still allows the already-selected gift after death.
        if (ctx.choice == "losehp") room->loseHp(HpLostStruct(ctx.owner, 1, objectName(), ctx.owner));
        else if (ctx.owner->getMark("&orange") > 0) ctx.owner->loseMark("&orange");
        else ctx.targets.clear();
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->gainMark("&orange", getEffectiveAmount(ctx));
        return false;
    }
};

class Zhenglun : public TriggerSkillV2
{
public:
    Zhenglun() : TriggerSkillV2("zhenglun") { events << EventPhaseChanging; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<PhaseChangeStruct>().to == Player::Draw && !player->isSkipped(Player::Draw)
            && player->getMark("&orange") == 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->skip(Player::Draw);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // The phase skip belongs to the invoker, while the orange recipient remains interceptable.
        target->gainMark("&orange", getEffectiveAmount(ctx));
        return false;
    }
};
KuizhuCard::KuizhuCard(QString kuizhu) : kuizhu(kuizhu)
{
    setSkillName(kuizhu);
    mute = true;
}

bool KuizhuCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    int n = 0;
    for (int i = 0; i < targets.length(); i++) {
        n = n + targets.at(i)->getHp();
    }
    return (targets.length() > 0 && targets.length() <= Self->getMark(kuizhu + "-Clear")) || (n == Self->getMark(kuizhu + "-Clear"));
}

bool KuizhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int n = Self->getMark(kuizhu + "-Clear");
    for (int i = 0; i < targets.length(); i++) {
        n = n - targets.at(i)->getHp();
    }
    return (targets.length() < Self->getMark(kuizhu + "-Clear")) || to_select->getHp() <= n;
}

void KuizhuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->broadcastSkillInvoke(kuizhu);
    int n = 0;
    foreach (ServerPlayer *p, targets) {
        n = n + p->getHp();
    }
    QStringList choices;
    if (targets.length() <= source->getMark(kuizhu + "-Clear")) choices << "draw";
    if (n == source->getMark(kuizhu + "-Clear")) choices << "damage";
    QString choice = room->askForChoice(source, kuizhu, choices.join("+"), QVariant());
    if (choice == "draw")
        room->setPlayerFlag(source, kuizhu + "_draw");
    else
        room->setPlayerFlag(source, kuizhu + "_damage");
    try {
        foreach (ServerPlayer *p, targets) {
            if (p->isAlive()) {
                room->cardEffect(this, source, p);
            }
        }
        if (source->hasFlag(kuizhu + "_draw"))
            room->setPlayerFlag(source,"-" + kuizhu + "_draw");
        if (source->hasFlag(kuizhu + "_damage")) {
            room->setPlayerFlag(source,"-" + kuizhu + "_damage");
            if (targets.length() >= 2 && source->isAlive() && kuizhu == "kuizhu")
                room->damage(DamageStruct(kuizhu, nullptr, source));
        }
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
            if (source->hasFlag(kuizhu + "_draw"))
                room->setPlayerFlag(source,"-" + kuizhu + "_draw");
            if (source->hasFlag(kuizhu + "_damage"))
                room->setPlayerFlag(source,"-" + kuizhu + "_damage");
        }
        throw triggerEvent;
    }
}

void KuizhuCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.from->hasFlag(kuizhu + "_draw"))
        effect.to->drawCards(1, kuizhu);
    if (effect.from->hasFlag(kuizhu + "_damage"))
        effect.from->getRoom()->damage(DamageStruct(kuizhu, effect.from, effect.to));
}

OLKuizhuCard::OLKuizhuCard() : KuizhuCard("olkuizhu")
{
    mute = true;
}

class KuizhuViewAsSkill : public ViewAsSkillV2
{
public:
    KuizhuViewAsSkill(const QString &name) : ViewAsSkillV2(name) { response_pattern = "@@" + name; }
    int allowance(const ActiveSkillRequest &request) const
    { return request.initiator ? request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "discard_count", 0).toInt() : 0; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.pattern == "@@" + objectName() && allowance(request) > 0; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!target || target->isDead() || selected.contains(target)) return false;
        int hp = target->getHp(); for (const Player *p : selected) hp += p->getHp();
        return selected.size() < allowance(request) || hp <= allowance(request);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        int hp = 0; for (const Player *p : selected) hp += p->getHp();
        return !selected.isEmpty() && (selected.size() <= allowance(request) || hp == allowance(request));
    }
    QString historyKey(const ActiveSkillRequest &) const override { return objectName() == "kuizhu" ? "KuizhuCard" : "OLKuizhuCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        const int count = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "discard_count", 0).toInt();
        int hp = 0; for (ServerPlayer *p : ctx.targets) hp += p->getHp();
        QStringList choices;
        if (!ctx.targets.isEmpty() && ctx.targets.size() <= count) choices << "draw";
        if (!ctx.targets.isEmpty() && hp == count) choices << "damage";
        if (choices.isEmpty()) return FinishSkill;
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"));
        const QString choice = ctx.choice;
        for (ServerPlayer *target : ctx.targets) { SkillContext recipient = ctx; skillEffect(recipient, target); }
        if (choice == "damage" && objectName() == "kuizhu" && ctx.targets.size() >= 2 && ctx.invoker->isAlive()) {
            SkillContext backlash = ctx; backlash.choice = "backlash"; skillEffect(backlash, ctx.invoker);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else target->getRoom()->damage(DamageStruct(objectName(), ctx.choice == "backlash" ? nullptr : ctx.invoker, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class Kuizhu : public TriggerSkillV2
{
public:
    Kuizhu(const QString &name) : TriggerSkillV2(name) { events << EventPhaseEnd; view_as_skill = new KuizhuViewAsSkill(name); }
    static int discarded(Room *room, ServerPlayer *player)
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return -1;
        QVariantMap query{{"phase_id", phase}, {"from", player->objectName()}, {"limit", 64}};
        int count = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap();
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
                    && move.value("to_place").toInt() == Player::DiscardPile
                    && (move.value("from_place").toInt() == Player::PlaceHand || move.value("from_place").toInt() == Player::PlaceEquip)) ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            query.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Discard && discarded(room, player) > 0 ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.manual_effect = true; return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = discarded(room, ctx.owner);
        if (count <= 0) return false;
        Room::AcceptedViewAsEffectScope response(room, ctx.owner, objectName(), ctx);
        if (!response.isValid()) return false;
        // Bind this completed discard phase to the exact temporary response source.
        ctx.owner->setSkillInstanceStateValue(objectName(), response.activationRef().key.instanceID, "discard_count", count);
        room->askForUseCard(ctx.owner, "@@" + objectName(), "@" + objectName(), -1, Card::MethodNone);
        return false;
    }
};
class Chezheng : public TriggerSkillV2
{
public:
    Chezheng(const QString &name) : TriggerSkillV2(name)
    {
        events << EventPhaseEnd << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
        if (event == DamageCaused) {
            const DamageStruct damage = data.value<DamageStruct>();
            return objectName() == "olchezheng" && damage.to && damage.to->isAlive() && !damage.to->inMyAttackRange(player)
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        // Preserve the old turn-wide Play counter across extra Play phases, using committed history.
        const int count = room->countHistoryCards(player, "turn", QString(), false, true);
        if (count < 0) return {};
        int outside = 0;
        bool discardable = false;
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            if (other->inMyAttackRange(player)) continue;
            ++outside;
            discardable = discardable || player->canDiscard(other, "he");
        }
        return count < outside && discardable ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageCaused) {
            ctx.targets = {ctx.original_data->value<DamageStruct>().to};
            return true;
        }
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if (!other->inMyAttackRange(ctx.owner) && ctx.owner->canDiscard(other, "he")) candidates << other;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@" + objectName() + "-discard", false, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DamageCaused) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            LogMessage log;
            log.type = "#OLzishouPrevent";
            log.from = ctx.invoker;
            log.to << target;
            log.arg = objectName();
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(ctx.invoker, objectName());
            return true;
        }
        if (!ctx.invoker->canDiscard(target, "he")) return false;
        room->broadcastSkillInvoke(this);
        const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (card && !card->hasFlag("using") && room->getCardOwner(id) == target
            && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
            && ctx.invoker->canDiscard(target, id)) room->throwCard(id, target, ctx.invoker);
        return false;
    }
};

class ChezhengPro : public ProhibitSkill
{
public:
    ChezhengPro() : ProhibitSkill("#chezhengpro")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        // 攻擊範圍要逐個修正技求值，放在技能檢查之後。
        return !card->isKindOf("SkillCard") && from != to && from->getPhase() == Player::Play
		&& from->hasSkill("chezheng") && !to->inMyAttackRange(from);
    }
};

class Lijun : public TriggerSkillV2
{
public:
    Lijun(const QString &name) : TriggerSkillV2(name + "$") { events << CardFinished << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    static QList<int> materials(Room *room, const Card *card)
    {
        if (!card || !card->isKindOf("Slash")) return {};
        const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        for (int id : ids) if (id < 0 || room->getCardPlace(id) != Player::DiscardPile) return {};
        return ids;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardFinished || !player || !player->isAlive() || player->getKingdom() != "wu" || player->getPhase() != Player::Play
            || materials(room, data.value<CardUseStruct>().card).isEmpty()) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player)) {
            if (!owner->hasLordSkill(objectName())) continue;
            for (int id : owner->getValidSkillInstanceIds(objectName())) {
                SkillContext candidate; candidate.owner = owner; candidate.invoker = player; candidate.skill_name = objectName(); candidate.instanceID = id;
                candidate.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
                if (isUsable(candidate)) result[owner] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.bypass_cost && accepted.activationRef == ctx.activationRef) addUsage(accepted);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !room->askForPlayerChosen(ctx.invoker, {ctx.owner}, objectName(), "@" + objectName() + "-give", true)) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "reward") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            if (objectName() == "ollijun" && target->isAlive() && getEffectiveAmount(ctx) > 0) YinAllowances::apply(room, ctx, target, "ollijun", getEffectiveAmount(ctx));
            return false;
        }
        const QList<int> ids = materials(room, ctx.original_data->value<CardUseStruct>().card);
        if (ids.isEmpty()) return false;
        LogMessage log; log.type = "#InvokeOthersSkill"; log.from = ctx.invoker; log.to << target; log.arg = objectName(); room->sendLog(log);
        room->notifySkillInvoked(ctx.owner, ctx.owner->isWeidi() ? "weidi" : objectName());
        DummyCard cards(ids);
        room->obtainCard(target, &cards, CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), target->objectName(), objectName(), ""), true);
        if (!target->isAlive() || !ctx.invoker->isAlive() || !target->askForSkillInvoke(objectName(), ctx.invoker)) return false;
        room->broadcastSkillInvoke(ctx.owner->isWeidi() ? "weidi" : objectName());
        SkillContext reward = ctx; reward.choice = "reward"; reward.targets = {ctx.invoker};
        skillEffect(event, room, player, reward, ctx.invoker);
        return false;
    }
};

class OLLijunTargetMod : public TargetModSkillV2
{
public:
    OLLijunTargetMod() : TargetModSkillV2("#ollijun-target") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // The accepted turn allowance is a recipient resource and survives its granting lord source.
        if (const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(ctx.primary)) YinAllowances::projectCurrent(server->getRoom());
        return ctx.modType == Residue && ctx.primary && ctx.primary->getPhase() == Player::Play
            ? CorrectSkillResult::useAmount(ctx.primary->property("ollijun_residue").toInt() * ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};
class Qizhi : public TriggerSkillV2
{
public:
    Qizhi() : TriggerSkillV2("qizhi") { events << TargetSpecified; }
    static int acceptedCount(Room *room, ServerPlayer *player)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return -1;
        QVariantMap query{{"kind", "skill_invoked"}, {"turn_id", turn}, {"player", player->objectName()}, {"limit", 64}};
        int count = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool()) return -1;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap().value("data").toMap();
                // This rule counts the explicit actor, not a inferred root-skill owner.
                if (!fact.contains("invoked_skill") || !fact.contains("player")) return -1;
                if (fact.value("invoked_skill").toString() == "qizhi" && fact.value("player").toString() == player->objectName()) ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            query.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->hasFlag("CurrentPlayer")
            || use.from != player || !use.card || (!use.card->isKindOf("BasicCard") && !use.card->isKindOf("TrickCard"))) return {};
        for (ServerPlayer *target : room->getAlivePlayers())
            if (!use.to.contains(target) && player->canDiscard(target, "he")) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (!use.to.contains(target) && ctx.owner->canDiscard(target, "he")) candidates << target;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@qizhi-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = acceptedCount(room, ctx.owner);
        if (count >= 0) room->setPlayerMark(ctx.owner, "&qizhi-Clear", count);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.owner->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            const Player::Place place = room->getCardPlace(id);
            if (id < 0 || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                || (place != Player::PlaceHand && place != Player::PlaceEquip) || !ctx.owner->canDiscard(target, id)) break;
            room->throwCard(id, target, ctx.owner);
        }
        if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Jinqu : public TriggerSkillV2
{
public:
    Jinqu() : TriggerSkillV2("jinqu") { events << EventPhaseStart; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            && Qizhi::acceptedCount(room, player) >= 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (Qizhi::acceptedCount(room, ctx.owner) < 0 || !ctx.owner->askForSkillInvoke(objectName())) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        target->drawCards(getEffectiveAmount(ctx), objectName());
        const int count = Qizhi::acceptedCount(room, ctx.owner);
        if (count < 0 || !target->isAlive()) return false;
        const int discard = target->getHandcardNum() - count;
        if (discard > 0) room->askForDiscard(target, objectName(), discard, discard);
        return false;
    }
};
class Jianxiang : public TriggerSkillV2
{
public:
    Jianxiang() : TriggerSkillV2("jianxiang")
    {
        events << TargetConfirmed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.card
                && use.to.contains(player) && !use.card->isKindOf("SkillCard") && use.from && use.from != player
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        int n = player->getHandcardNum();
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getHandcardNum() < n)
                n = p->getHandcardNum();
        }

        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getHandcardNum() == n)
                targets << p;
        }

        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@jianxiang-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

ShenshiCard::ShenshiCard()
{
    setSkillName("shenshi");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ShenshiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    const QList<const Player *> as = Self->getAliveSiblings();
    int n = as.first()->getHandcardNum();
    foreach (const Player *p, as) {
        if (p->getHandcardNum() > n)
            n = p->getHandcardNum();
    }
    return targets.isEmpty() && to_select != Self && to_select->getHandcardNum() == n;
}

void ShenshiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->setChangeSkillState(effect.from, "shenshi", 2);
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "shenshi", "");
    room->obtainCard(effect.to, this, reason, false);
    room->damage(DamageStruct("shenshi", effect.from, effect.to));
}

static QVariantMap shenshiReceipt(Room *room, const SkillContext &ctx, ServerPlayer *target, const QString &kind)
{
    const int serial = room->getTag("ShenshiSerial").toInt() + 1; room->setTag("ShenshiSerial", serial);
    return {{"serial", serial}, {"kind", kind}, {"issuer", ctx.invoker->objectName()}, {"target", target->objectName()}, {"amount", ctx.hasModifiedAmount() ? qMax(0, ctx.modified_amount) : qMax(0, ctx.amount)},
        {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
        {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
}
class ShenshiVS : public ViewAsSkillV2
{
public:
    ShenshiVS() : ViewAsSkillV2("shenshi", 1) { change_skill = true; setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isNude() && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "conversion_state", 1).toInt() == 1; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using") && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId())); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { ActiveSkillRequest empty = request; empty.selectedCardIds.clear(); return request.selectedCardIds.size() == 1 && canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first())); }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!request.initiator || !target || target->isDead() || target == request.initiator || !selected.isEmpty()) return false;
        for (const Player *other : request.initiator->getAliveSiblings()) if (other->getHandcardNum() > target->getHandcardNum()) return false;
        return true;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ShenshiCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    { ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", 2); ctx.invoker->getRoom()->setChangeSkillState(ctx.owner, objectName(), 2); return ContinueEffects; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (!ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != ctx.initiator || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        room->giveCard(ctx.initiator, target, QList<int>{id}, objectName());
        if (target->isDead() || ctx.invoker->isDead()) return ContinueEffects;
        const QVariantMap receipt = shenshiReceipt(room, ctx, target, "damage");
        QVariantList receipts = room->getTag("ShenshiDamageReceipts").toList(); receipts << receipt; room->setTag("ShenshiDamageReceipts", receipts);
        const auto cleanup = qScopeGuard([&] { QVariantList current = room->getTag("ShenshiDamageReceipts").toList(); current.removeOne(receipt); room->setTag("ShenshiDamageReceipts", current); });
        DamageStruct damage(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)); damage.tips << "shenshi_receipt:" + QString::number(receipt.value("serial").toInt()); room->damage(damage);
        return ContinueEffects;
    }
};
class Shenshi : public TriggerSkillV2
{
public:
    Shenshi() : TriggerSkillV2("shenshi") { events << Damaged; view_as_skill = new ShenshiVS; change_skill = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>(); TriggerList result;
        if (!player || player->isDead() || !player->hasSkill(objectName()) || damage.to != player || !damage.from || damage.from == player || damage.from->isDead()) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) if (player->getSkillInstanceStateValue(objectName(), id, "conversion_state", 1).toInt() == 2) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ServerPlayer *source = ctx.original_data->value<DamageStruct>().from; if (!source || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(source))) return false; ctx.targets = {source}; return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", 1); room->setChangeSkillState(ctx.owner, objectName(), 1); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->doGongxin(ctx.owner, target, QList<int>(), objectName());
        if (ctx.owner->isDead() || target->isDead() || ctx.owner->isNude()) return false;
        const Card *gift = room->askForExchange(ctx.owner, objectName(), 1, 1, true, "shenshi-give:" + target->objectName());
        if (!gift || gift->subcardsLength() != 1) return false;
        const int id = gift->getSubcards().first();
        if (room->getCardOwner(id) != ctx.owner || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return false;
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        SkillContext issuer = ctx; issuer.invoker = ctx.owner;
        QVariantMap receipt = shenshiReceipt(room, issuer, target, "gift"); receipt.insert("card", id); receipt.insert("after", before.value("watermark")); receipt.insert("known", before.value("complete"));
        QVariantList receipts = room->getTag("ShenshiGiftReceipts").toList(); receipts << receipt; room->setTag("ShenshiGiftReceipts", receipts);
        room->giveCard(ctx.owner, target, QList<int>{id}, objectName()); return false;
    }
};
class ShenshiEffect : public TriggerSkillV2
{
public:
    ShenshiEffect() : TriggerSkillV2("#shenshi-effect") { events << EventPhaseChanging << Death; global = true; frequency = Compulsory; }
    static bool kept(Room *room, const QVariantMap &receipt)
    {
        ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("target").toString()); const int id = receipt.value("card", -1).toInt();
        if (!holder || !receipt.value("known").toBool() || room->getCardOwner(id) != holder || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        QVariantMap query{{"from", holder->objectName()}, {"after", receipt.value("after")}, {"limit", 64}};
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query); if (!page.value("complete").toBool()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) { const QVariantMap move = entry.toMap().value("data").toMap(); if (move.value("card_id", -1).toInt() == id && (move.value("to").toString() != holder->objectName() || (move.value("to_place").toInt() != Player::PlaceHand && move.value("to_place").toInt() != Player::PlaceEquip))) return false; }
            if (!page.value("has_more").toBool()) return true; query.insert("after", page.value("next_after"));
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            // Expire every due obligation before callbacks; cancelled opportunities cannot retry next turn.
            QVariantList due; for (const QVariant &entry : room->getTag("ShenshiGiftReceipts").toList()) if (kept(room, entry.toMap())) due << entry;
            room->setTag("ShenshiDue", due); room->removeTag("ShenshiGiftReceipts");
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const DeathStruct death = event == Death ? data.value<DeathStruct>() : DeathStruct();
        if (event == Death && (death.who != player || !death.damage)) return true;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        const QVariantList receipts = room->getTag(event == Death ? "ShenshiDamageReceipts" : "ShenshiDue").toList();
        for (const QVariant &entry : receipts) {
            const QVariantMap receipt = entry.toMap(); const int serial = receipt.value("serial").toInt();
            if (event == Death && (!death.damage->tips.contains("shenshi_receipt:" + QString::number(serial)) || receipt.value("target").toString() != player->objectName())) continue;
            ServerPlayer *issuer = room->findPlayerByObjectName(receipt.value("issuer").toString()); if (!issuer) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = serial; ctx.owner = ctx.initiator = ctx.invoker = issuer;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            ctx.amount = receipt.value("amount", 1).toInt(); ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event;
            if (event != Death) ctx.targets = {issuer};
            if (serial > 0 && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return !ctx.activationRef.isValid() && ctx.owner && ctx.owner->isAlive() && room->getTag(ctx.extra_data.toMap().value("kind").toString() == "damage" ? "ShenshiDamageReceipts" : "ShenshiDue").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { if (event != Death) return true; ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), "shenshi", "@shenshi-invoke", true, true); if (!target) return false; ctx.targets = {target}; return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { const QString tag = ctx.extra_data.toMap().value("kind").toString() == "damage" ? "ShenshiDamageReceipts" : "ShenshiDue"; QVariantList receipts = room->getTag(tag).toList(); receipts.removeOne(ctx.extra_data); room->setTag(tag, receipts); return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(qMax(0, 4 * getEffectiveAmount(ctx) - target->getHandcardNum()), "shenshi"); return false; }
};
ChenglveCard::ChenglveCard()
{
    setSkillName("chenglve");
    target_fixed = true;
}

void ChenglveCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    int n = source->getChangeSkillState("chenglve");
    int dis_num = 0;
    if (n <= 1) {
        room->setChangeSkillState(source, "chenglve", 2);
        dis_num = 2;
        source->drawCards(1, "chenglve");
    } else if (n == 2) {
        room->setChangeSkillState(source, "chenglve", 1);
        dis_num = 1;
        source->drawCards(2, "chenglve");
    }
    if (dis_num == 0 || !source->canDiscard(source, "h")) return;
    const Card *card = room->askForDiscard(source, "chenglve", dis_num, dis_num, false);
    if (!card) return;
    QString mark = "&chenglve";
    foreach (int id, card->getSubcards()) {
        const Card *c = Sanguosha->getCard(id);
        room->addPlayerMark(source, "chenglve_" + c->getSuitString() + "-Clear");
        QString m = c->getSuitString() + "_char";
        if (mark.contains(m)) continue;
        mark = mark + "+" + m;
    }
    mark = mark + "-Clear";
    room->addPlayerMark(source, mark);
}

class Chenglve : public ViewAsSkillV2
{
public:
    Chenglve() : ViewAsSkillV2("chenglve") { change_skill = true; setPhaseName("Play"); }
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ChenglveCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        const int state = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", 1).toInt();
        ctx.choice = state == 2 ? "yang" : "yin";
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", state == 2 ? 1 : 2);
        ctx.invoker->getRoom()->setChangeSkillState(ctx.owner, objectName(), state == 2 ? 1 : 2);
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const int amount = getEffectiveAmount(ctx);
        target->drawCards((ctx.choice == "yang" ? 2 : 1) * amount, objectName());
        const int count = (ctx.choice == "yang" ? 1 : 2) * amount;
        if (target->isDead() || count <= 0 || !target->canDiscard(target, "h")) return ContinueEffects;
        const QVariantMap before = room->queryHistoryMoves({{"from", target->objectName()}, {"limit", 1}});
        const Card *discarded = room->askForDiscard(target, objectName(), count, count, false);
        if (!discarded || ctx.executionID <= 0) return ContinueEffects;
        const QList<int> selected = discarded->getSubcards();
        const auto grant = [&](const QSet<int> &suits) {
            // Accepted recipient allowances stay after the granting instance is retired.
            for (int suit : suits) {
                const QString name = Card::Suit2String(Card::Suit(suit));
                YinAllowances::apply(room, ctx, target, "chenglve", name);
                room->setPlayerMark(target, "&chenglve+" + name + "_char-Clear", 1);
            }
        };
        const auto liveSuits = [&]() {
            QSet<int> suits;
            for (int id : selected) if (const Card *card = Sanguosha->getCard(id)) suits.insert(int(card->getSuit()));
            return suits;
        };
        // Suit is a public move fact. Incomplete history is unknown, not "no discard".
        const auto historyKnown = [](const QVariantMap &page) {
            return page.value("error").toString().isEmpty() && page.value("complete").toBool();
        };
        if (!historyKnown(before)) { grant(liveSuits()); return ContinueEffects; }
        QVariantMap query{{"from", target->objectName()}, {"after", before.value("watermark")}, {"limit", 64}};
        QSet<int> suits;
        bool unknown = false;
        while (!unknown) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!historyKnown(page)) { unknown = true; break; }
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap();
                if (!selected.contains(move.value("card_id", -1).toInt()) || move.value("execution_id").toLongLong() != ctx.executionID
                    || move.value("from_place").toInt() != Player::PlaceHand || move.value("to_place").toInt() != Player::DiscardPile
                    || (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) continue;
                QVariantMap card = move.value("card_before").toMap();
                if (!card.contains("suit")) card = move.value("card").toMap();
                if (!card.contains("suit")) { unknown = true; break; }
                suits.insert(card.value("suit").toInt());
            }
            if (unknown || !page.value("has_more").toBool()) break;
            query.insert("after", page.value("next_after"));
        }
        grant(unknown ? liveSuits() : suits);
        return ContinueEffects;
    }
};

class ChenglveTargetMod : public TargetModSkillV2
{
public:
    ChenglveTargetMod() : TargetModSkillV2("#chenglve-target", ".") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(ctx.primary)) YinAllowances::projectCurrent(server->getRoom());
        if (!ctx.primary || !ctx.card || ctx.card->isKindOf("SkillCard") || ctx.currentAmount <= 0
            || !ctx.primary->property("chenglve_suits").toStringList().contains(ctx.card->getSuitString())) return CorrectSkillResult::noEffect();
        if (ctx.modType == Residue) return CorrectSkillResult::unlimitedResidue();
        if (ctx.modType == DistanceLimit) return CorrectSkillResult::useAmount(1000 * ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};
class YinShicai : public TriggerSkillV2
{
public:
    YinShicai() : TriggerSkillV2("yinshicai") { events << CardFinished; }
    static bool firstOfType(Room *room, ServerPlayer *player, const CardUseStruct &use)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (!use.card || use.targetModReveal.useHistoryEventId <= 0 || turn.toLongLong() <= 0) return false;
        QVariantMap query{{"kind", "use_card"}, {"turn_id", turn}, {"player", player->objectName()}, {"limit", 64}};
        qint64 first = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), card = fact.value("data").toMap().value("card").toMap();
                if (!card.contains("type") || fact.value("event_id").toLongLong() <= 0) return false;
                if (!first && card.value("type").toInt() == int(use.card->getTypeId())) first = fact.value("event_id").toLongLong();
            }
            if (!page.value("has_more").toBool()) return first == use.targetModReveal.useHistoryEventId;
            query.insert("after", page.value("next_after"));
        }
    }
    static QList<int> materials(Room *room, ServerPlayer *player, const Card *card)
    {
        if (!card || card->isKindOf("SkillCard")) return {};
        const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        for (int id : ids) {
            if (id < 0) return {};
            const Player::Place place = room->getCardPlace(id);
            if (room->getCardOwner(id) == player && (place == Player::PlaceHand || place == Player::PlaceEquip)) continue;
            if (!room->getCardOwner(id) && (place == Player::PlaceTable || place == Player::DiscardPile)) continue;
            return {};
        }
        return ids;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.from == player
            && !materials(room, player, use.card).isEmpty() && firstOfType(room, player, use)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!ctx.owner->askForSkillInvoke(objectName(), QString("yinshicai_invoke:%1").arg(use.card->objectName()))) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QList<int> ids = materials(room, ctx.owner, ctx.original_data->value<CardUseStruct>().card);
        if (ids.isEmpty()) return false;
        room->broadcastSkillInvoke(objectName());
        LogMessage log; log.type = "$YinshicaiPut"; log.from = target; log.card_str = ListI2S(ids).join("+"); room->sendLog(log);
        DummyCard cards(ids);
        room->moveCardTo(&cards, nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.owner->objectName(), objectName(), ""), true, true);
        if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class Cunmu : public TriggerSkillV2
{
public:
    Cunmu() : TriggerSkillV2("cunmu")
    {
        events << DrawNCards;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
                && data.value<DrawStruct>().reason != "InitialHandCards"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != ctx.invoker) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        room->sendCompulsoryTriggerLog(ctx.invoker, this, qsanRandomBounded(2) + 1);
        draw.top = false;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class Mingren : public TriggerSkillV2
{
public:
    Mingren(const QString &name = "mingren") : TriggerSkillV2(name) { events << GameStart << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && (event == GameStart || (player->getPhase() == Player::Finish && !player->isKongcheng() && !player->getPile("mrren").isEmpty()))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        if (event == GameStart) return true;
        const Card *chosen = room->askForExchange(ctx.owner, objectName(), 1, 1, false, "mingren-change", true);
        if (!chosen || chosen->subcardsLength() != 1) return false;
        ctx.extra_data = chosen->getSubcards().first(); return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        int id = ctx.extra_data.toInt();
        if (event == GameStart) {
            target->drawCards((objectName() == "olmingren" ? 2 : 1) * getEffectiveAmount(ctx), objectName());
            if (!target->isAlive() || target->isKongcheng()) return false;
            const Card *chosen = room->askForExchange(target, objectName(), 1, 1, false, "mingren-put");
            if (!chosen || chosen->subcardsLength() != 1) return false;
            id = chosen->getSubcards().first();
        }
        if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) return false;
        const QList<int> original = target->getPile("mrren");
        target->addToPile("mrren", id);
        if (event != GameStart && target->isAlive() && target->getPile("mrren").contains(id)) {
            // Exchange only the original pile cards still owned by this pile after movement callbacks.
            QList<int> remaining; for (int oldId : original) if (target->getPile("mrren").contains(oldId)) remaining << oldId;
            if (!remaining.isEmpty()) {
                DummyCard returned(remaining);
                room->obtainCard(target, &returned, CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, target->objectName()), true);
            }
        }
        return false;
    }
};
ZhenliangCard::ZhenliangCard()
{
    setSkillName("zhenliang");
}

bool ZhenliangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int n = qAbs(to_select->getHp() - Self->getHp());
    n = qMax(1, n);
    return targets.isEmpty() && n == getSubcards().length() && Self->inMyAttackRange(to_select) && to_select != Self;
}

void ZhenliangCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->setChangeSkillState(effect.from, "zhenliang", 2);
    room->damage(DamageStruct("zhenliang", effect.from, effect.to));
}

class ZhenliangVS : public ViewAsSkillV2
{
public:
    ZhenliangVS(const QString &name = "zhenliang") : ViewAsSkillV2(name) { setPhaseName("Play"); change_skill = true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetMode targetMode() const override { return SelectTargets; }
    QString historyKey(const ActiveSkillRequest &) const override { return objectName() == "olzhenliang" ? "OLZhenliangCard" : "ZhenliangCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->getPile("mrren").isEmpty()
            && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "conversion_state", 1).toInt() == 1;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || request.initiator->isJilei(card) || request.initiator->getPile("mrren").isEmpty()) return false;
        const int id = card->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id) && (objectName() != "olzhenliang" || request.selectedCardIds.isEmpty())
            && (request.initiator->handCards().contains(id) || request.initiator->getEquipsId().contains(id))
            && card->sameColorWith(Sanguosha->getCard(request.initiator->getPile("mrren").first()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest checked = request; checked.selectedCardIds.clear();
        for (int id : request.selectedCardIds) { if (id < 0 || !canSelectCard(checked, Sanguosha->getCard(id))) return false; checked.selectedCardIds << id; }
        return true;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate && candidate->isAlive() && candidate != request.initiator
            && request.initiator->inMyAttackRange(candidate)
            && request.selectedCardIds.size() == (objectName() == "olzhenliang" ? 1 : qMax(1, qAbs(candidate->getHp() - request.initiator->getHp())));
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return selected.size() == 1 && canSelectTarget(request, {}, selected.first()); }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", 2);
        ctx.invoker->getRoom()->setChangeSkillState(ctx.owner, objectName(), 2);
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    { ctx.invoker->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx))); return ContinueEffects; }
};

class Zhenliang : public TriggerSkillV2
{
public:
    Zhenliang(const QString &name = "zhenliang") : TriggerSkillV2(name) { events << BeforeCardsMove; view_as_skill = new ZhenliangVS(name); change_skill = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->hasFlag("CurrentPlayer") || player->getPile("mrren").isEmpty()) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile || move.reason.m_playerId != player->objectName()) return result;
        const int basic = move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON;
        const Card *card = nullptr;
        if (basic == CardMoveReason::S_REASON_RESPONSE) card = move.reason.m_extraData.value<CardResponseStruct>().m_card;
        else if (basic == CardMoveReason::S_REASON_USE && move.from_places.contains(Player::PlaceTable)) {
            card = move.reason.m_useStruct.card;
            if (!card) card = move.reason.m_extraData.value<const Card *>();
        }
        if (!card) return result;
        const Card *ren = Sanguosha->getCard(player->getPile("mrren").first());
        if (objectName() == "olzhenliang" ? !card->sameColorWith(ren) : card->getTypeId() != ren->getTypeId()) return result;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (player->getSkillInstanceStateValue(objectName(), id, "conversion_state", 1).toInt() == 2)
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", 1).toInt() != 2) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@zhenliang-draw", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Conversion state belongs to the exact shared trigger/view-as instance; the public state is presentation only.
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", 1);
        room->setChangeSkillState(ctx.owner, objectName(), 1); room->broadcastSkillInvoke(objectName()); return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};
class OLMingren : public Mingren
{
public:
    OLMingren() : Mingren("olmingren") {}
};
OLZhenliangCard::OLZhenliangCard()
{
    setSkillName("olzhenliang");
}

bool OLZhenliangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->inMyAttackRange(to_select) && to_select != Self;
}

void OLZhenliangCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->setChangeSkillState(effect.from, "olzhenliang", 2);
    room->damage(DamageStruct("olzhenliang", effect.from, effect.to));
}

class OLZhenliang : public Zhenliang
{
public:
    OLZhenliang() : Zhenliang("olzhenliang") {}
};
class Longnu : public TriggerSkillV2
{
public:
    static void transform(Room *room, ServerPlayer *target, const QVariantMap &receipt, const QList<int> &ids)
    {
        // These transformations belong to an accepted turn effect, independent of the original skill's lifetime.
        for (int id : ids) {
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) continue;
            const Card *original = Sanguosha->getCard(id); if (original->hasFlag("using")) continue;
            Card *converted = nullptr;
            if (receipt.value("mode").toInt() == 1 && original->isRed()) converted = new FireSlash(original->getSuit(), original->getNumber());
            else if (receipt.value("mode").toInt() == 2 && original->isKindOf("TrickCard")) converted = new ThunderSlash(original->getSuit(), original->getNumber());
            if (!converted) continue;
            converted->setSkillName("longnu"); converted->setSourceSkill(receipt.value("source_skill").toString(), receipt.value("source_id").toInt());
            converted->setActivationSkill(receipt.value("activation_skill").toString(), receipt.value("activation_id").toInt());
            WrappedCard *wrapped = Sanguosha->getWrappedCard(id); wrapped->takeOver(converted); room->notifyUpdateCard(target, id, wrapped);
        }
    }
    Longnu() : TriggerSkillV2("longnu") { events << EventPhaseStart << CardsMoveOneTime << EventPhaseChanging; change_skill = true; frequency = Compulsory; global = true; }
    int getPriority(TriggerEvent event) const override { return event == CardsMoveOneTime ? 6 : TriggerSkillV2::getPriority(event); }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return true;
            for (ServerPlayer *holder : room->getAllPlayers(true)) {
                const QVariantList previous = holder->getTag("LongnuEffects").toList(); QVariantList kept;
                for (const QVariant &entry : previous) if (entry.toMap().value("turn").toLongLong() != turn) kept << entry;
                if (kept.size() == previous.size()) continue;
                holder->setTag("LongnuEffects", kept); room->setPlayerFlag(holder, "-longnu1"); room->setPlayerFlag(holder, "-longnu2");
                room->filterCards(holder, holder->getCards("he"), true);
                for (const QVariant &entry : kept) { const QVariantMap receipt = entry.toMap(); room->setPlayerFlag(holder, receipt.value("mode").toInt() == 2 ? "longnu2" : "longnu1"); transform(room, holder, receipt, holder->handCards()); }
            }
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardsMoveOneTime) return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || player->isDead() || move.to != player || move.to_place != Player::PlaceHand) return true;
        for (const QVariant &entry : player->getTag("LongnuEffects").toList()) {
            const QVariantMap receipt = entry.toMap(); SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.owner = ctx.initiator = ctx.invoker = player; ctx.targets = {player}; ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = receipt;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (ctx.instanceID > 0 && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx) : ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("LongnuEffects").toList().contains(ctx.extra_data); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    { return event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Play && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantMap receipt = ctx.extra_data.toMap();
        QList<int> ids;
        if (event == EventPhaseStart) {
            const int state = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", 1).toInt();
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "conversion_state", state == 2 ? 1 : 2);
            room->setChangeSkillState(ctx.owner, objectName(), state == 2 ? 1 : 2);
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return false;
            const int serial = room->getTag("LongnuSerial").toInt() + 1; room->setTag("LongnuSerial", serial);
            receipt = QVariantMap{{"serial", serial}, {"turn", turn}, {"mode", state}, {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            QVariantList applied = target->getTag("LongnuEffects").toList(); applied << receipt; target->setTag("LongnuEffects", applied);
            room->setPlayerFlag(target, state == 2 ? "longnu2" : "longnu1");
            if (state == 2) room->loseMaxHp(target, getEffectiveAmount(ctx), objectName());
            else room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
            if (target->isDead()) return false;
            target->drawCards(getEffectiveAmount(ctx), objectName()); ids = target->handCards();
        } else ids = ctx.original_data->value<CardsMoveOneTimeStruct>().card_ids;
        if (!target->getTag("LongnuEffects").toList().contains(receipt)) return false;
        transform(room, target, receipt, ids);
        return false;
    }
};
class LongnuTarget : public TargetModSkillV2
{
public:
    LongnuTarget() : TargetModSkillV2("#longnu-target")
    {
        setBaseAmount(1000);
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.currentAmount <= 0 || !ctx.card || ctx.card->getSkillName() != "longnu") return CorrectSkillResult::noEffect();
        if (ctx.modType == Residue && ctx.card->isKindOf("ThunderSlash"))
            return CorrectSkillResult::unlimitedResidue();
        return ctx.modType == DistanceLimit && ctx.card->isKindOf("FireSlash")
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class Jieying : public TriggerSkillV2
{
public:
    Jieying() : TriggerSkillV2("jieying")
    {
        events << EventPhaseStart << ChainStateChange << EventAcquireSkill << GameStart << Debut << Revived;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventAcquireSkill) {
            const SkillChangeStruct change = data.value<SkillChangeStruct>();
            if (change.skillName != objectName() || player->isChained()) return {};
            return {{player, {SkillInstanceUtils::formatName(objectName(), change.instanceID)}}};
        }
        if (event == EventPhaseStart)
            return player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (event == ChainStateChange)
            return player->isChained() ? TriggerList{{player, {objectName()}}} : TriggerList();
        return !player->isChained() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            const QList<ServerPlayer *> others = room->getOtherPlayers(ctx.owner);
            if (others.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, others, objectName(), "@jieying-invoke", false, true);
            if (!target) return false;
            ctx.targets = {target};
        } else ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == EventPhaseStart) room->broadcastSkillInvoke(objectName());
        else room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        // Unchaining is vetoed only for the actual protected holder; ordinary chain effects have real recipients.
        if (event == ChainStateChange) return target == ctx.owner && target->isChained();
        if (!target->isChained()) room->setPlayerChained(target);
        return false;
    }
};
class JieyingKeep : public MaxCardsSkillV2
{
public:
    JieyingKeep() : MaxCardsSkillV2("#jieying-keep")
    {
        setHolderSelector(CorrectSkill_AllHolders);
        setBaseAmount(2);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // Each live source contributes once; the engine aggregates exact instances.
        return ctx.primary && ctx.primary->isChained()
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class Junlve : public TriggerSkillV2
{
public:
    Junlve() : TriggerSkillV2("junlve") { events << Damage << Damaged; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        // This is the shared resource consumed by Cuike/Zhanhuo, not private usage state.
        target->gainMark("&junlve", damage.damage * getEffectiveAmount(ctx));
        return false;
    }
};

class Cuike : public TriggerSkillV2
{
public:
    Cuike() : TriggerSkillV2("cuike") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.choice = ctx.owner->getMark("&junlve") % 2 == 1 ? "damage" : "chain";
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(),
            ctx.choice == "damage" ? "@junlve-invoke" : "@junlve-invoke2", true, true);
        if (!target) return false;
        ctx.targets = {target}; ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        const QList<ServerPlayer *> firstTargets = ctx.targets;
        for (ServerPlayer *target : firstTargets) skillEffect(event, room, player, ctx, target);
        if (!ctx.owner->isAlive() || ctx.owner->getMark("&junlve") <= 7
            || !ctx.owner->askForSkillInvoke(this, QString("all"))) return false;
        // The shared resource is consumed once before any of the accepted area effects.
        ctx.owner->loseAllMarks("&junlve");
        room->broadcastSkillInvoke(objectName());
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner)) {
            SkillContext part = ctx;
            part.choice = "damage"; part.targets = {target};
            skillEffect(event, room, player, part, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "damage") room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        else {
            if (!target->isChained()) room->setPlayerChained(target);
            for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.owner->canDiscard(target, "hej"); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "hej", objectName(), false, Card::MethodDiscard);
                const Player::Place place = room->getCardPlace(id);
                if (id < 0 || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                    || (place != Player::PlaceHand && place != Player::PlaceEquip && place != Player::PlaceDelayedTrick)
                    || !ctx.owner->canDiscard(target, id)) break;
                room->throwCard(id, place == Player::PlaceDelayedTrick ? nullptr : target, ctx.owner);
            }
        }
        return false;
    }
};
ZhanhuoCard::ZhanhuoCard()
{
    setSkillName("zhanhuo");
}

bool ZhanhuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length() < Self->getMark("&junlve") && to_select->isChained();
}

void ZhanhuoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->doSuperLightbox(source, "zhanhuo");
    room->removePlayerMark(source, "@zhanhuoMark");
    source->loseAllMarks("&junlve");
    foreach (ServerPlayer *p, targets) {
        if (p->isDead()) continue;
        p->throwAllEquips();
    }
    if (source->isDead()) return;
    QList<ServerPlayer *> alives;
    foreach (ServerPlayer *p, targets) {
        if (p->isDead()) continue;
        alives << p;
    }
    if (alives.isEmpty()) return;
    ServerPlayer *target = room->askForPlayerChosen(source, alives, "zhanhuo", "@zhanhuo-damage");
    room->doAnimate(1, source->objectName(), target->objectName());
    room->damage(DamageStruct("zhanhuo", source, target, 1, DamageStruct::Fire));
}

class Zhanhuo : public ViewAsSkillV2
{
public:
    Zhanhuo() : ViewAsSkillV2("zhanhuo") { frequency = Limited; limit_mark = "@zhanhuoMark"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhanhuoCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("&junlve") > 0;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && candidate && candidate->isAlive() && candidate->isChained()
            && !selected.contains(candidate) && selected.size() < request.initiator->getMark("&junlve");
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (selected.isEmpty()) return false;
        QList<const Player *> checked;
        for (const Player *target : selected) {
            if (!canSelectTarget(request, checked, target)) return false;
            checked << target;
        }
        return true;
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || ctx.initiator->getMark("&junlve") <= 0) return false;
        ctx.initiator->loseAllMarks("&junlve");
        return true;
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        Room *room = ctx.invoker->getRoom();
        // Limited usage is charged to the activation instance; the mark is only its legacy presentation.
        room->removePlayerMark(ctx.invoker, limit_mark);
        room->doSuperLightbox(ctx.invoker, objectName());
        for (ServerPlayer *target : targets) {
            SkillContext part = ctx; part.choice = "equip"; part.targets = {target};
            skillEffect(part, target);
        }
        if (!ctx.invoker->isAlive()) return FinishSkill;
        QList<ServerPlayer *> alive;
        for (ServerPlayer *target : targets) if (target->isAlive()) alive << target;
        if (!alive.isEmpty()) {
            ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, alive, objectName(), "@zhanhuo-damage");
            if (target) {
                SkillContext part = ctx; part.choice = "damage"; part.targets = {target};
                skillEffect(part, target);
            }
        }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "equip") target->throwAllEquips();
        else ctx.invoker->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        return ContinueEffects;
    }
};
YinPackage::YinPackage()
    : Package("Yin")
{
    General *yanyan = new General(this, "yanyan", "shu", 4);
    yanyan->addSkill(new Juzhan);
    yanyan->addSkill(new JuzhanPro);
    yanyan->addSkill(new YinAllowances);
    related_skills.insert("juzhan", "#yin-allowances");
    related_skills.insert("juzhan", "#juzhanpro");

    General *wangping = new General(this, "wangping", "shu", 4);
    wangping->addSkill(new Feijun);
    wangping->addSkill(new Binglve);

    General *luji = new General(this, "luji", "wu", 3);
    luji->addSkill(new Huaiju);
    luji->addSkill(new HuaijuDeath);
    luji->addSkill(new Weili);
    luji->addSkill(new Zhenglun);
    related_skills.insert("huaiju", "#huaijudeath");

    General *sunliang = new General(this, "sunliang$", "wu", 3);
    sunliang->addSkill(new Kuizhu("kuizhu"));
    sunliang->addSkill(new Chezheng("chezheng"));
    sunliang->addSkill(new ChezhengPro);
    sunliang->addSkill(new Lijun("lijun"));
    related_skills.insert("chezheng", "#chezhengpro");

    General *wangji = new General(this, "wangji", "wei", 3);
    wangji->addSkill(new Qizhi);
    wangji->addSkill(new Jinqu);

    General *kuailiangkuaiyue = new General(this, "kuailiangkuaiyue", "wei", 3);
    kuailiangkuaiyue->addSkill(new Jianxiang);
    kuailiangkuaiyue->addSkill(new Shenshi);
    kuailiangkuaiyue->addSkill(new ShenshiEffect);
    related_skills.insert("shenshi", "#shenshi-effect");

    General *yin_xuyou = new General(this, "yin_xuyou", "qun", 3);
    yin_xuyou->addSkill(new Chenglve);
    yin_xuyou->addSkill(new ChenglveTargetMod);
    yin_xuyou->addSkill(new YinShicai);
    yin_xuyou->addSkill(new Cunmu);
    //yin_xuyou->addSkill(new Skill("cunmu", Skill::Compulsory)); //耦合进了Room::drawCards
    related_skills.insert("chenglve", "#chenglve-target");

    General *luzhi = new General(this, "luzhi", "qun", 3);
    luzhi->addSkill(new Mingren);
    luzhi->addSkill(new Zhenliang);

    General *shenliubei = new General(this, "shenliubei", "god", 6);
    shenliubei->addSkill(new Longnu);
    shenliubei->addSkill(new LongnuTarget);
    shenliubei->addSkill(new Jieying);
    shenliubei->addSkill(new JieyingKeep);
    related_skills.insert("longnu", "#longnu-target");
    related_skills.insert("jieying", "#jieying-keep");

    General *shenluxun = new General(this, "shenluxun", "god");
    shenluxun->addSkill(new Junlve);
    shenluxun->addSkill(new Cuike);
    shenluxun->addSkill(new Zhanhuo);
    addMetaObject<ZhanhuoCard>();

    addMetaObject<FeijunCard>();
    addMetaObject<KuizhuCard>();
    addMetaObject<ShenshiCard>();
    addMetaObject<ChenglveCard>();
    addMetaObject<ZhenliangCard>();
}

ADD_PACKAGE(Yin)

OLStYinPackage::OLStYinPackage()
    : Package("OLStYin")
{
    General *ol_sunliang = new General(this, "ol_sunliang$", "wu", 3);
    ol_sunliang->addSkill(new Kuizhu("olkuizhu"));
    ol_sunliang->addSkill(new Chezheng("olchezheng"));
    ol_sunliang->addSkill(new Lijun("ollijun"));
    ol_sunliang->addSkill(new OLLijunTargetMod);
    related_skills.insert("ollijun", "#ollijun-target");

    General *ol_luzhi = new General(this, "ol_luzhi", "qun", 3);
    ol_luzhi->addSkill(new OLMingren);
    ol_luzhi->addSkill(new OLZhenliang);

    addMetaObject<OLKuizhuCard>();
    addMetaObject<OLZhenliangCard>();
}
ADD_PACKAGE(OLStYin)
