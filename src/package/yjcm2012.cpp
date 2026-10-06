#include "yjcm2012.h"
#include <QJsonDocument>
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
#include "exppattern.h"
#include "room.h"
#include "roomthread.h"
#include <QScopeGuard>
#include "skill-instance-utils.h"

class Zhenlie : public TriggerSkillV2
{
public:
    Zhenlie() : TriggerSkillV2("zhenlie") { events << TargetConfirmed; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(this) && use.card && use.from != player && use.to.contains(player)
            && (use.card->isKindOf("Slash") || use.card->isNDTrick()) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        room->loseHp(HpLostStruct(ctx.owner, 1, objectName(), ctx.owner));
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.original_data) return false;
        if (ctx.choice == "discard") {
            for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive()
                 && ctx.owner->canDiscard(target, "he"); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
                if (id >= 0 && room->getCardOwner(id) == target && ctx.owner->canDiscard(target, id)) room->throwCard(id, target, ctx.owner);
            }
            return false;
        }
        // Read the live use after HP-loss callbacks; never overwrite nested modifications with a stale copy.
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
        ctx.original_data->setValue(use);
        if (use.from && use.from->isAlive()) {
            SkillContext discard = ctx;
            discard.choice = "discard";
            skillEffect(event, room, player, discard, use.from);
        }
        return false;
    }
};
class Miji : public TriggerSkillV2
{
public:
    Miji(const QString &name = "miji", bool optional = false, bool fixed = false)
        : TriggerSkillV2(name), optionalDistribution(optional), fixedDraw(fixed)
    {
        events << EventPhaseStart;
        if (fixed) frequency = Frequent;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish
            && player->isWounded() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isWounded() || !room->askForSkillInvoke(ctx.owner, objectName())) return false;
        const int lost = ctx.owner->getLostHp();
        if (fixedDraw) ctx.extra_data = lost;
        else {
            QStringList numbers;
            for (int i = 1; i <= lost; ++i) numbers << QString::number(i);
            if (numbers.isEmpty()) return false;
            ctx.extra_data = qBound(1, room->askForChoice(ctx.owner, objectName() + "_draw", numbers.join("+")).toInt(), lost);
        }
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        SkillContext draw = ctx;
        draw.choice = "draw";
        skillEffect(event, room, player, draw, ctx.owner);
        const int count = draw.extra_data.toMap().value("drawn").toInt();
        if (count <= 0 || !ctx.owner->isAlive()) return false;
        QList<int> available = ctx.owner->handCards();
        QList<CardsMoveStruct> choices;
        int assigned = 0;
        while (ctx.owner->isAlive() && assigned < count && !available.isEmpty()) {
            for (int id : QList<int>(available))
                if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
                    || Sanguosha->getCard(id)->hasFlag("using")) available.removeOne(id);
            const QList<ServerPlayer *> recipients = room->getOtherPlayers(ctx.owner);
            if (available.isEmpty() || recipients.isEmpty()) break;
            QList<int> offered = available;
            CardsMoveStruct move = room->askForYijiStruct(ctx.owner, offered, objectName(), false, false,
                optionalDistribution && assigned == 0, count - assigned, recipients, CardMoveReason(), "", false, false);
            QList<int> selected;
            if (recipients.contains(qobject_cast<ServerPlayer *>(move.to)))
                for (int id : move.card_ids)
                    if (available.contains(id) && !selected.contains(id) && selected.size() < count - assigned) selected << id;
            if (selected.isEmpty()) {
                if (optionalDistribution && assigned == 0) break;
                // Required distributions retain the native random fallback on an empty reply.
                selected << available.at(qsanRandomBounded(available.size()));
                move.to = recipients.at(qsanRandomBounded(recipients.size()));
            }
            move.card_ids = selected;
            choices << move;
            for (int id : selected) available.removeOne(id);
            assigned += selected.size();
        }
        QList<CardsMoveStruct> accepted;
        for (const CardsMoveStruct &move : choices) {
            SkillContext give = ctx;
            give.choice = "give";
            give.extra_data = QVariantMap{{"ids", ListI2V(move.card_ids)}};
            skillEffect(event, room, player, give, qobject_cast<ServerPlayer *>(move.to));
            const QVariantMap result = give.extra_data.toMap();
            if (!result.contains("recipient")) continue;
            ServerPlayer *recipient = room->findChild<ServerPlayer *>(result.value("recipient").toString());
            if (!recipient) continue;
            accepted << CardsMoveStruct(move.card_ids, ctx.owner, recipient, Player::PlaceHand, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), recipient->objectName(), objectName(), ""));
        }
        // All choices precede the atomic transfer; target callbacks can still invalidate a material.
        QList<CardsMoveStruct> moves;
        for (CardsMoveStruct move : accepted) {
            if (!move.to || !move.to->isAlive()) continue;
            for (int id : QList<int>(move.card_ids))
                if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
                    || Sanguosha->getCard(id)->hasFlag("using")) move.card_ids.removeOne(id);
            if (!move.card_ids.isEmpty()) moves << move;
        }
        if (!moves.isEmpty()) room->moveCardsAtomic(moves, false);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "draw") {
            const int count = ctx.extra_data.toInt() * getEffectiveAmount(ctx);
            ctx.extra_data = QVariantMap{{"drawn", count}};
            target->drawCards(count, objectName());
        } else {
            QVariantMap result = ctx.extra_data.toMap();
            result.insert("recipient", target->objectName());
            ctx.extra_data = result;
        }
        return false;
    }
private:
    const bool optionalDistribution;
    const bool fixedDraw;
};
QiceCard::QiceCard()
{
    setSkillName("qice");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool QiceCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *use_card = Sanguosha->cloneCard(user_string);
    if (use_card) {
		use_card->setSkillName("qice");
        use_card->addSubcards(subcards);
        use_card->setCanRecast(false);
        use_card->deleteLater();
    }
    return use_card && use_card->targetFilter(targets, to_select, Self);
}

bool QiceCard::targetFixed() const
{
    Card *use_card = Sanguosha->cloneCard(user_string);
    if (use_card) {
		use_card->setSkillName("qice");
        use_card->addSubcards(subcards);
        use_card->setCanRecast(false);
        use_card->deleteLater();
    }
    return use_card && use_card->targetFixed();
}

bool QiceCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card *use_card = Sanguosha->cloneCard(user_string);
    if (use_card) {
		use_card->setSkillName("qice");
        use_card->addSubcards(subcards);
        use_card->setCanRecast(false);
        use_card->deleteLater();
    }
    return use_card && use_card->targetsFeasible(targets, Self);
}

const Card *QiceCard::validate(CardUseStruct &) const
{
    Card *use_card = Sanguosha->cloneCard(user_string);
    use_card->addSubcards(subcards);
    use_card->setSkillName("qice");
    use_card->deleteLater();
    return use_card;
}

class Qice : public ViewAsSkillV2
{
public:
    Qice() : ViewAsSkillV2("qice") { setPhaseName("Play"); }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), false); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override { return qMax(1, getEffectiveAmount(ctx)); }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->isKongcheng()) return false;
        const QList<int> hand = request.initiator->handCards();
        if (!request.selectedCardIds.isEmpty()) {
            if (request.selectedCardIds.size() != hand.size()) return false;
            for (int id : hand) if (!request.selectedCardIds.contains(id)) return false;
        }
        for (int id : hand) if (Sanguosha->getCard(id)->hasFlag("using")) return false;
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        ActiveSkillRequest wholeHand = request;
        wholeHand.selectedCardIds = request.initiator->handCards();
        // Shared declaration admission enforces enabled packages, trick kind and live legality.
        return ViewAsSkillV2::createCard(wholeHand);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.use_card) return FinishSkill;
        Room *room = ctx.initiator->getRoom();
        if (!room) return FinishSkill;
        const QList<int> hand = ctx.initiator->handCards();
        // Empty or locked material cannot be rebuilt into the trick. The phase use is already committed.
        if (hand.isEmpty()) return FinishSkill;
        for (int id : hand) {
            const Card *card = Sanguosha->getCard(id);
            if (!card || card->hasFlag("using") || room->getCardOwner(id) != ctx.initiator
                || room->getCardPlace(id) != Player::PlaceHand) return FinishSkill;
        }
        const QList<int> ids = ctx.use_card->getSubcards();
        bool match = ids.size() == hand.size();
        if (match) {
            QList<int> seen;
            for (int id : ids) {
                if (seen.contains(id) || !hand.contains(id)) { match = false; break; }
                seen << id;
            }
        }
        if (match) return ContinueEffects;
        Card *card = Sanguosha->cloneCard(ctx.use_card->objectName(), Card::SuitToBeDecided, -1);
        if (!card) return FinishSkill;
        card->addSubcards(hand);
        card->setSkillName(objectName());
        card->setCanRecast(false);
        card->deleteLater();
        ctx.updated_card = card;
        return ContinueEffects;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !ctx.use_card || !cardSelectionFeasible(request)) return false;
        const QList<int> ids = ctx.use_card->getSubcards(), hand = ctx.initiator->handCards();
        if (ids.size() != hand.size()) return false;
        for (int id : hand)
            if (!ids.contains(id) || room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
        return true; // The ordinary trick pipeline consumes the entire hand once.
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "QiceCard"; }
};
class Zhiyu : public TriggerSkillV2
{
public:
    Zhiyu() : TriggerSkillV2("zhiyu") { events << Damaged; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(this) ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.original_data && room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data, false); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        SkillContext draw = ctx;
        draw.choice = "draw";
        draw.extra_data = false;
        skillEffect(event, room, player, draw, ctx.owner);
        if (!draw.extra_data.toBool()) return false;
        ServerPlayer *source = ctx.original_data->value<DamageStruct>().from;
        if (source && source->isAlive()) {
            SkillContext discard = ctx;
            discard.choice = "discard";
            skillEffect(event, room, player, discard, source);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "draw") {
            target->drawCards(amount, objectName());
            if (!target->isAlive() || target->isKongcheng()) return false;
            room->showAllCards(target);
            const QList<const Card *> cards = target->getHandcards();
            if (cards.isEmpty()) return false;
            for (const Card *card : cards) if (card->getColor() != cards.first()->getColor()) return false;
            ctx.extra_data = true;
        } else if (amount > 0 && target->canDiscard(target, "h")) room->askForDiscard(target, objectName(), amount, amount);
        return false;
    }
};
class Jiangchi : public TriggerSkillV2
{
public:
    Jiangchi() : TriggerSkillV2("jiangchi") { events << DrawNCards << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging) return false;
        if (data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *holder : room->getAllPlayers(true))
                if (!QJsonDocument::fromJson(holder->property("JiangchiEffects").toByteArray()).toVariant().toList().isEmpty()) room->setPlayerProperty(holder, "JiangchiEffects", QString::fromUtf8(QJsonDocument::fromVariant(QVariantList()).toJson(QJsonDocument::Compact)));
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == DrawNCards && player && player->isAlive() && player->hasSkill(this) && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "jiang+chi+cancel");
        if (ctx.choice != "jiang" && ctx.choice != "chi") return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.original_data || getEffectiveAmount(ctx) <= 0) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "jiang") {
            draw.num += amount;
            // This applied restriction expires with the turn, independently of the source grant.
            const QString reason = objectName() + ":" + ctx.activationRef.ownerObjectName + ":" + QString::number(ctx.activationRef.key.instanceID);
            room->setPlayerCardLimitation(target, "use,response", "Slash", true, reason);
        } else {
            draw.num = qMax(0, draw.num - amount);
            QVariantList effects = QJsonDocument::fromJson(target->property("JiangchiEffects").toByteArray()).toVariant().toList();
            effects << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"amount", amount}};
            room->setPlayerProperty(target, "JiangchiEffects", QString::fromUtf8(QJsonDocument::fromVariant(effects).toJson(QJsonDocument::Compact)));
        }
        ctx.original_data->setValue(draw);
        return false;
    }
};

class JiangchiTargetMod : public TargetModSkillV2
{
public:
    JiangchiTargetMod() : TargetModSkillV2("#jiangchi-target") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.card || !ctx.card->isKindOf("Slash")) return CorrectSkillResult::noEffect();
        const QVariantList effects = QJsonDocument::fromJson(ctx.primary->property("JiangchiEffects").toByteArray()).toVariant().toList();
        int amount = 0;
        for (const QVariant &entry : effects) amount += qMax(0, entry.toMap().value("amount").toInt());
        if (amount <= 0) return CorrectSkillResult::noEffect();
        if (ctx.modType == DistanceLimit) return CorrectSkillResult::useAmount(1000 * ctx.currentAmount);
        if (ctx.modType != Residue) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(amount * ctx.currentAmount);
    }
};
class Qianxi : public TriggerSkillV2
{
public:
    Qianxi(const QString &name = "qianxi", bool drawDiscard = false) : TriggerSkillV2(name), discardVersion(drawDiscard)
    { events << EventPhaseStart << FinishJudge; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge && judge->reason == objectName() && judge->card)
                judge->pattern = judge->card->isRed() ? "red" : "black";
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(this)
            && player->getPhase() == Player::Start ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && room->askForSkillInvoke(ctx.owner, objectName()); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        SkillContext prepare = ctx;
        prepare.choice = "prepare";
        prepare.extra_data = QString();
        skillEffect(event, room, player, prepare, ctx.owner);
        const QString color = prepare.extra_data.toString();
        if ((color != "red" && color != "black") || !ctx.owner->isAlive()) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner)) if (ctx.owner->distanceTo(other) == 1) candidates << other;
        if (candidates.isEmpty()) return false;
        const QVariant previousTag = ctx.owner->getTag(objectName());
        ctx.owner->setTag(objectName(), color);
        const auto cleanup = qScopeGuard([&] {
            if (previousTag.isValid()) ctx.owner->setTag(objectName(), previousTag);
            else ctx.owner->removeTag(objectName());
        });
        ServerPlayer *victim = room->askForPlayerChosen(ctx.owner, candidates, objectName());
        if (!victim || !victim->isAlive() || !ctx.owner->isAlive()) return false;
        SkillContext restrict = ctx;
        restrict.choice = "restrict";
        restrict.extra_data = color;
        skillEffect(event, room, player, restrict, victim);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "prepare") {
            if (!discardVersion) {
                JudgeStruct judge;
                judge.reason = objectName();
                judge.play_animation = false;
                judge.who = target;
                room->judge(judge);
                ctx.extra_data = judge.pattern;
            } else {
                target->drawCards(getEffectiveAmount(ctx), objectName());
                if (!target->isAlive() || !target->canDiscard(target, "he")) return false;
                const Card *card = room->askForCard(target, "..!", "@olqianxi", QVariant(), Card::MethodNone, nullptr, false, objectName());
                int id = card ? card->getEffectiveId() : -1;
                QList<int> available;
                for (const Card *candidate : target->getCards("he"))
                    if (target->canDiscard(target, candidate->getEffectiveId()) && !candidate->hasFlag("using")) available << candidate->getEffectiveId();
                if (!available.contains(id)) {
                    if (available.isEmpty()) return false;
                    id = available.at(qsanRandomBounded(available.size()));
                }
                card = Sanguosha->getCard(id);
                const QString color = card->isRed() ? "red" : (card->isBlack() ? "black" : "");
                room->throwCard(id, objectName(), target);
                ctx.extra_data = color;
            }
            return false;
        }
        const QString color = ctx.extra_data.toString();
        if (color != "red" && color != "black") return false;
        const QString ledger = objectName() + "_effects";
        QVariantList pending = ctx.owner->getTag(ledger).toList();
        const int serial = room->getTag("QianxiReceiptSerial").toInt() + 1;
        room->setTag("QianxiReceiptSerial", serial);
        const QString reason = objectName() + ":" + QString::number(serial);
        pending << QVariantMap{{"target", target->objectName()}, {"color", color}, {"reason", reason},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}};
        ctx.owner->setTag(ledger, pending);
        // Each applied restriction adds one pattern occurrence; cleanup removes only its occurrence.
        room->setPlayerCardLimitation(target, "use,response", QString(".|%1|.|hand$0").arg(color), false, reason);
        room->addPlayerMark(target, "@qianxi_" + color);
        LogMessage log;
        log.type = "#Qianxi";
        log.from = target;
        log.arg = "no_suit_" + color;
        room->sendLog(log);
        return false;
    }
private:
    const bool discardVersion;
};

class QianxiClear : public TriggerSkillV2
{
public:
    QianxiClear(const QString &skill = "qianxi") : TriggerSkillV2("#" + skill + "-clear"), parentSkill(skill)
    { events << EventPhaseChanging << Death; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || (event == Death && data.value<DeathStruct>().who != player)
            || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)) return true;
        const QString ledger = parentSkill + "_effects";
        const QVariantList pending = player->getTag(ledger).toList();
        player->removeTag(ledger);
        for (const QVariant &value : pending) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            const QString color = receipt.value("color").toString();
            if (!target || (color != "red" && color != "black")) continue;
            room->removePlayerCardLimitation(target, "use,response", QString(".|%1|.|hand$0").arg(color), receipt.value("reason").toString());
            if (target->getMark("@qianxi_" + color) > 0) room->removePlayerMark(target, "@qianxi_" + color);
        }
        return true;
    }
private:
    const QString parentSkill;
};
class Dangxian : public TriggerSkillV2
{
public:
    Dangxian() : TriggerSkillV2("dangxian") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::RoundStart ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        for (int i = 0; i < getEffectiveAmount(ctx); ++i) target->insertPhase(Player::Play);
        return false;
    }
};

class Fuli : public TriggerSkillV2
{
public:
    Fuli() : TriggerSkillV2("fuli") { events << EventSkillInvoking << AskForPeaches; global = true; frequency = Limited; limit_mark = "@laoji"; }
    QStringList usableEntries(ServerPlayer *player, QVariant &data) const
    {
        QStringList result;
        if (!player) return result;
        Room *room = player->getRoom();
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext candidate;
            candidate.owner = candidate.invoker = candidate.initiator = player;
            candidate.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            candidate.sourceRef = room->resolveSkillInstanceRootRef(candidate.activationRef);
            candidate.instanceID = id;
            candidate.skill_name = objectName() + "#" + QString::number(id);
            candidate.amount = room->getSkillInstanceAmount(candidate.activationRef);
            candidate.original_data = &data;
            if (candidate.sourceRef.isValid() && isUsable(candidate)) result << candidate.skill_name;
        }
        return result;
    }
    void commitAccepted(Room *room, SkillContext &ctx) const
    {
        if (!ctx.owner || !ctx.activationRef.isValid() || ctx.extra_data.toMap().value("quota_committed").toBool()) return;
        QVariantMap receipt = ctx.extra_data.toMap();
        receipt["quota_committed"] = true;
        ctx.extra_data = receipt;
        addUsage(ctx);
        if (ctx.owner->getMark("@laoji") > 0) room->removePlayerMark(ctx.owner, "@laoji");
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; commitAccepted(room, ctx); return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return true;
        SkillContext accepted = data.value<SkillContext>();
        if (accepted.activationRef.key.skillName == objectName() && TriggerSkillV2::parseSkillName(accepted.skill_name) == objectName()) {
            commitAccepted(room, accepted);
            data = QVariant::fromValue(accepted);
        }
        return true;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != AskForPeaches) return {};
        const DyingStruct dying = data.value<DyingStruct>();
        return player && player == dying.who && player->isAlive() && player->getHp() <= 0 && player->hasSkill(this)
            ? TriggerList{{player, usableEntries(player, data)}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        if (!ctx.owner || !ctx.original_data || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The selected instance owns its game quota; the old visible token is only a projection.

        room->doSuperLightbox(ctx.owner, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        QSet<QString> kingdoms;
        for (ServerPlayer *player : room->getAlivePlayers()) kingdoms << player->getKingdom();
        const int recovery = qMax(0, int(kingdoms.size()) * getEffectiveAmount(ctx) - target->getHp());
        room->recover(target, RecoverStruct(ctx.owner, nullptr, recovery, objectName()));
        if (target->isAlive()) target->turnOver();
        return false;
    }
};

class NosZishou : public TriggerSkillV2
{
public:
    NosZishou() : TriggerSkillV2("noszishou") { events << DrawNCards; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->isWounded()
            && data.value<DrawStruct>().reason == "draw_phase" ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !room->askForSkillInvoke(ctx.owner, objectName())) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.original_data) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += target->getLostHp() * getEffectiveAmount(ctx);
        ctx.original_data->setValue(draw);
        target->clearHistory();
        target->skip(Player::Play);
        return false;
    }
};
class Zongshi : public MaxCardsSkillV2
{
public:
    Zongshi() : MaxCardsSkillV2("zongshi")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder || !ctx.holder->parent()) return CorrectSkillResult::noEffect();
        QSet<QString> kingdoms;
        for (const Player *player : ctx.holder->parent()->findChildren<const Player *>())
            if (player->isAlive()) kingdoms << player->getKingdom();
        // The engine has already selected this exact valid source instance.
        return CorrectSkillResult::useAmount(kingdoms.size() * ctx.currentAmount);
    }
};

class Zishou : public TriggerSkillV2
{
public:
    Zishou() : TriggerSkillV2("zishou") { events << DrawNCards << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging) return false;
        if (data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *holder : room->getAllPlayers(true))
                if (!QJsonDocument::fromJson(holder->property("ZishouEffects").toByteArray()).toVariant().toList().isEmpty()) room->setPlayerProperty(holder, "ZishouEffects", QString::fromUtf8(QJsonDocument::fromVariant(QVariantList()).toJson(QJsonDocument::Compact)));
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == DrawNCards && player && player->isAlive() && player->hasSkill(this) && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !room->askForSkillInvoke(ctx.owner, objectName())) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.original_data) return false;
        QSet<QString> kingdoms;
        for (ServerPlayer *player : room->getAlivePlayers()) kingdoms << player->getKingdom();
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += int(kingdoms.size()) * getEffectiveAmount(ctx);
        ctx.original_data->setValue(draw);
        // The prohibition is a retained consequence of this accepted draw modification.
        QVariantList effects = QJsonDocument::fromJson(target->property("ZishouEffects").toByteArray()).toVariant().toList();
        effects << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}};
        room->setPlayerProperty(target, "ZishouEffects", QString::fromUtf8(QJsonDocument::fromVariant(effects).toJson(QJsonDocument::Compact)));
        return false;
    }
};

// ProhibitSkill has no V2 replacement; it reads retained applied effects, not current ownership.
class ZishouProhibit : public ProhibitSkill
{
public:
    ZishouProhibit() : ProhibitSkill("#zishou") { }
    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return from && to && from != to && card && card->getTypeId() != Card::TypeSkill && !QJsonDocument::fromJson(from->property("ZishouEffects").toByteArray()).toVariant().toList().isEmpty();
    }
};
class Shiyong : public TriggerSkillV2
{
public:
    Shiyong() : TriggerSkillV2("shiyong") { events << Damaged; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this) && damage.card && damage.card->isKindOf("Slash")
            && (damage.card->isRed() || damage.card->hasFlag("drank")) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) {
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            room->loseMaxHp(target, getEffectiveAmount(ctx), objectName());
        }
        return false;
    }
};
static void expireFuhunGrants(Room *room, const QString &name)
{
    for (ServerPlayer *holder : room->getAllPlayers(true)) {
        const QVariantList entries = holder->getTag(name + "_grants").toList();
        holder->removeTag(name + "_grants");
        for (const QVariant &entry : entries)
            for (const QVariant &grant : entry.toMap().value("grants").toList())
                room->detachSkillFromPlayer(holder, grant.toString(), false, true);
    }
}

static void applyFuhunGrants(Room *room, ServerPlayer *target, const QString &name, const SkillContext &ctx)
{
    if (!target || !target->isAlive() || !ctx.activationRef.isValid()) return;
    const QString ledger = name + "_grants";
    QVariantList entries = target->getTag(ledger).toList();
    for (const QVariant &entry : entries) {
        const QVariantMap record = entry.toMap();
        if (record.value("owner").toString() == ctx.activationRef.ownerObjectName
            && record.value("instance").toInt() == ctx.activationRef.key.instanceID) return;
    }
    const int serial = room->getTag("FuhunGrantSerial").toInt() + 1;
    room->setTag("FuhunGrantSerial", serial);
    entries << QVariantMap{{"serial", serial}, {"owner", ctx.activationRef.ownerObjectName},
        {"instance", ctx.activationRef.key.instanceID}, {"grants", QVariantList()}};
    target->setTag(ledger, entries);
    for (const QString &skill : {QString("wusheng"), QString("paoxiao")}) {
        const int id = room->acquireSkillFromEffect(target, skill, ctx);
        if (id <= 0) continue;
        const QString exact = SkillInstanceUtils::formatName(skill, id);
        entries = target->getTag(ledger).toList();
        bool retained = false;
        for (int i = 0; i < entries.size(); ++i) {
            QVariantMap record = entries[i].toMap();
            if (record.value("serial").toInt() != serial) continue;
            QVariantList grants = record.value("grants").toList();
            grants << exact;
            record["grants"] = grants;
            entries[i] = record;
            retained = true;
            break;
        }
        // Acquire callbacks may have already ended the turn and expired this receipt.
        if (!retained) { room->detachSkillFromPlayer(target, exact, false, true); return; }
        target->setTag(ledger, entries);
        if (!target->isAlive()) return;
    }
}

class FuhunViewAsSkill : public ViewAsSkillV2
{
public:
    FuhunViewAsSkill() : ViewAsSkillV2("fuhun", 2) { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->getHandcardNum() < 2) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return Slash::IsAvailable(request.initiator);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && request.pattern.contains("slash", Qt::CaseInsensitive);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.size() < 2 && !request.selectedCardIds.contains(card->getEffectiveId())
            && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId());
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::SuitToBeDecided, 0);
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        return slash;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.use_card || !cardSelectionFeasible(request)) return false;
        ActiveSkillRequest check = request;
        check.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(check, Sanguosha->getCard(id))) return false;
            check.selectedCardIds << id;
        }
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || !ctx.initiator) return ContinueEffects;
        Room *room = ctx.initiator->getRoom();
        const int serial = room->getTag("FuhunCardSerial").toInt() + 1;
        room->setTag("FuhunCardSerial", serial);
        ctx.use_card->setTag("FuhunEffect", QVariantMap{{"serial", serial}, {"actor", ctx.initiator->objectName()},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
            {"activation_instance", ctx.activationRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}});
        return ContinueEffects;
    }
};

class Fuhun : public TriggerSkillV2
{
public:
    Fuhun() : TriggerSkillV2("fuhun")
    { events << Damage << EventPhaseChanging; global = true; view_as_skill = new FuhunViewAsSkill; waked_skills = "wusheng,paoxiao"; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) expireFuhunGrants(room, objectName());
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != Damage) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || player != damage.from || player->getPhase() != Player::Play || !damage.card) return true;
        const QVariantMap receipt = damage.card->getTag("FuhunEffect").toMap();
        if (receipt.value("actor").toString() != player->objectName() || receipt.value("amount").toInt() <= 0) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        if (!ctx.sourceRef.isValid()) return true;
        ctx.instanceID = receipt.value("serial").toInt();
        ctx.current_event = event;
        ctx.original_data = &data;
        ctx.extra_data = receipt;
        ctx.setModifiedAmount(receipt.value("amount").toInt());
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const QVariantMap receipt = damage.card ? damage.card->getTag("FuhunEffect").toMap() : QVariantMap();
        return receipt == ctx.extra_data.toMap() && receipt.value("serial").toInt() == ctx.instanceID
            && receipt.value("actor").toString() == ctx.owner->objectName();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        const QVariantMap receipt = ctx.extra_data.toMap();
        SkillContext accepted = ctx;
        // Restore only the API copy's frozen activation; dispatcher admission remains receipt-based.
        accepted.activationRef = SkillInstanceRef(receipt.value("activation_owner").toString(),
            SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_instance").toInt()));
        applyFuhunGrants(room, target, objectName(), accepted);
        return false;
    }
};
GongqiCard::GongqiCard()
{
    setSkillName("gongqi");
    mute = true;
    target_fixed = true;
}

void GongqiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->setPlayerFlag(source, "InfinityAttackRange");
    const Card *cd = Sanguosha->getCard(subcards.first());
    if (cd->isKindOf("EquipCard")) {
        room->broadcastSkillInvoke("gongqi", 2);
        QList<ServerPlayer *> targets;
        foreach(ServerPlayer *p, room->getOtherPlayers(source))
            if (source->canDiscard(p, "he")) targets << p;
        if (!targets.isEmpty()) {
            ServerPlayer *to_discard = room->askForPlayerChosen(source, targets, "gongqi", "@gongqi-discard", true);
            if (to_discard)
                room->throwCard(room->askForCardChosen(source, to_discard, "he", "gongqi", false, Card::MethodDiscard), to_discard, source);
        }
    } else {
        room->broadcastSkillInvoke("gongqi", 1);
    }
}

class Gongqi : public ViewAsSkillV2
{
public:
    Gongqi() : ViewAsSkillV2("gongqi", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "GongqiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canDiscard(request.initiator, "he"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using") && !request.initiator->isJilei(card)
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (card && request.selectedCardIds.size() == 1)
            card->setTag("GongqiEquip", Sanguosha->getCard(request.selectedCardIds.first())->isKindOf("EquipCard"));
        return card;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ctx.extra_data = Sanguosha->getCard(request.selectedCardIds.first())->isKindOf("EquipCard");
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest empty = request;
        empty.selectedCardIds.clear();
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        if (!canSelectCard(empty, card)) return false;
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        // Cost freezes the equip bit. Bypass skips cost, so recover it from the still-addressable card.
        if (ctx.extra_data.userType() != QMetaType::Bool) {
            bool equip = false;
            if (ctx.use_card && ctx.use_card->getSubcards().size() == 1) {
                const Card *card = Sanguosha->getCard(ctx.use_card->getSubcards().first());
                if (card) equip = card->isKindOf("EquipCard");
            }
            ctx.extra_data = equip;
        }
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "discard") {
            for (int i = 0; i < getEffectiveAmount(ctx) && ctx.invoker->isAlive() && target->isAlive()
                && ctx.invoker->canDiscard(target, "he"); ++i) {
                const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
                if (room->getCardOwner(id) == target && ctx.invoker->canDiscard(target, id)
                    && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) room->throwCard(id, target, ctx.invoker);
            }
            return ContinueEffects;
        }
        QVariantList receipts = QJsonDocument::fromJson(target->property("GongqiEffects").toByteArray()).toVariant().toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)},
            {"turn", room->historyScopes().value("turn_id").toLongLong()}};
        room->setPlayerProperty(target, "GongqiEffects", QString::fromUtf8(QJsonDocument::fromVariant(receipts).toJson(QJsonDocument::Compact)));
        if (ctx.extra_data.toBool() && ctx.invoker->isAlive()) {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *other : room->getOtherPlayers(ctx.invoker))
                if (ctx.invoker->canDiscard(other, "he")) candidates << other;
            if (!candidates.isEmpty()) {
                ServerPlayer *victim = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "@gongqi-discard", true);
                if (victim) {
                    SkillContext discard = ctx;
                    discard.choice = "discard";
                    skillEffect(discard, victim);
                }
            }
        }
        return ContinueEffects;
    }
};

class GongqiRange : public AttackRangeSkillV2
{
public:
    GongqiRange() : AttackRangeSkillV2("#gongqi-range") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &) const override
    {
        // A fixed range effect must not inherit the default additive bonus.
        return CorrectSkillResult::noEffect();
    }
    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        if (ctx.primary)
            for (const QVariant &entry : QJsonDocument::fromJson(ctx.primary->property("GongqiEffects").toByteArray()).toVariant().toList())
                if (entry.toMap().value("amount").toInt() > 0) return CorrectSkillResult::useAmount(1000 * ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class GongqiRecord : public TriggerSkillV2
{
public:
    GongqiRecord() : TriggerSkillV2("#gongqi-record") { events << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        // A missing turn id must not wipe an outer turn's range when a nested extra turn ends.
        if (turn <= 0) return true;
        QVariantList kept;
        for (const QVariant &entry : QJsonDocument::fromJson(player->property("GongqiEffects").toByteArray()).toVariant().toList())
            if (entry.toMap().value("turn").toLongLong() != turn) kept << entry;
        room->setPlayerProperty(player, "GongqiEffects", QString::fromUtf8(QJsonDocument::fromVariant(kept).toJson(QJsonDocument::Compact)));
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};
JiefanCard::JiefanCard()
{
    setSkillName("jiefan");
    mute = true;
}

bool JiefanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void JiefanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->removePlayerMark(source, "@rescue");
    ServerPlayer *target = targets.first();
    source->setTag("JiefanTarget", QVariant::fromValue(target));
    room->broadcastSkillInvoke("jiefan");
    //room->doLightbox("$JiefanAnimate", 2500);
    room->doSuperLightbox(source, "jiefan");

    foreach (ServerPlayer *player, room->getAllPlayers()) {
        if (player->isAlive() && player->inMyAttackRange(target))
            room->cardEffect(this, source, player);
    }
    source->removeTag("JiefanTarget");
}

void JiefanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    ServerPlayer *target = effect.from->getTag("JiefanTarget").value<ServerPlayer *>();
    QVariant data = effect.from->getTag("JiefanTarget");
    if (target && !room->askForCard(effect.to, ".Weapon", "@jiefan-discard::" + target->objectName(), data))
        target->drawCards(1, "jiefan");
}

class Jiefan : public ViewAsSkillV2
{
public:
    Jiefan() : ViewAsSkillV2("jiefan") { frequency = Limited; limit_mark = "@rescue"; setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JiefanCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.initiator->getMark("@rescue") > 0) ctx.initiator->getRoom()->removePlayerMark(ctx.initiator, "@rescue");
        ctx.initiator->getRoom()->doSuperLightbox(ctx.initiator, objectName());
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
        } else if (ctx.choice == "weapon") {
            const QString name = ctx.extra_data.toString();
            if (name.isEmpty()) return ContinueEffects;
            ServerPlayer *beneficiary = room->findChild<ServerPlayer *>(name);
            if (!beneficiary || !beneficiary->isAlive()) return ContinueEffects;
            if (!room->askForCard(target, ".Weapon", "@jiefan-discard::" + name, QVariant::fromValue(beneficiary))) {
                SkillContext draw = ctx;
                draw.choice = "draw";
                skillEffect(draw, beneficiary);
            }
        } else {
            const QList<ServerPlayer *> players = room->getAlivePlayers();
            for (ServerPlayer *other : players) {
                if (!target->isAlive()) break;
                if (!other->isAlive() || !other->inMyAttackRange(target)) continue;
                SkillContext request = ctx;
                request.choice = "weapon";
                request.extra_data = target->objectName();
                skillEffect(request, other);
            }
        }
        return ContinueEffects;
    }
};
AnxuCard::AnxuCard()
{
    setSkillName("anxu");
    mute = true;
}

bool AnxuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self)
        return false;
    if (targets.isEmpty())
        return true;
    else if (targets.length() == 1)
        return to_select->getHandcardNum() != targets.first()->getHandcardNum();
    else
        return false;
}

bool AnxuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void AnxuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    QList<ServerPlayer *> selecteds = targets;
    ServerPlayer *from = selecteds.first()->getHandcardNum() < selecteds.last()->getHandcardNum() ? selecteds.takeFirst() : selecteds.takeLast();
    ServerPlayer *to = selecteds.takeFirst();
    if (from->getGeneralName().contains("sunquan"))
        room->broadcastSkillInvoke("anxu", 2);
    else
        room->broadcastSkillInvoke("anxu", 1);
    int id = room->askForCardChosen(from, to, "h", "anxu");
    const Card *cd = Sanguosha->getCard(id);
    from->obtainCard(cd);
    room->showCard(from, id);
    if (cd->getSuit() != Card::Spade)
        source->drawCards(1, "anxu");
}

class Anxu : public ViewAsSkillV2
{
public:
    Anxu(const QString &name = "anxu", bool giverChooses = false) : ViewAsSkillV2(name), voluntaryGift(giverChooses) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getAliveSiblings().size() >= 2; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && candidate != request.initiator && !selected.contains(candidate)
            && (selected.isEmpty() || (selected.size() == 1 && selected.first()->getHandcardNum() != candidate->getHandcardNum()));
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 2; }
    QString historyKey(const ActiveSkillRequest &) const override { return voluntaryGift ? "OlAnxuCard" : "AnxuCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (ctx.choice == "reward") {
            Room *room = target->getRoom();
            if (voluntaryGift && target->isWounded() && room->askForChoice(target, objectName(), "draw+recover") == "recover")
                room->recover(target, RecoverStruct(ctx.invoker, nullptr, getEffectiveAmount(ctx), objectName()));
            else target->drawCards(getEffectiveAmount(ctx), objectName());
        } else {
            QStringList accepted = ctx.extra_data.toStringList();
            accepted << target->objectName();
            ctx.extra_data = accepted;
        }
        return ContinueEffects;
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (!ctx.invoker || targets.size() != 2) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        QStringList accepted;
        // Recipient interception is isolated; neither participant rewrites the other's amount.
        for (ServerPlayer *target : targets) {
            SkillContext participant = ctx;
            participant.extra_data = QStringList();
            skillEffect(participant, target);
            accepted << participant.extra_data.toStringList();
        }
        if (accepted.size() != 2 || accepted[0] == accepted[1]) return ContinueEffects;
        ServerPlayer *giver = room->findPlayerByObjectName(accepted[0]);
        ServerPlayer *recipient = room->findPlayerByObjectName(accepted[1]);
        if (!giver || !recipient || !giver->isAlive() || !recipient->isAlive()
            || giver->getHandcardNum() == recipient->getHandcardNum()) return ContinueEffects;
        if (giver->getHandcardNum() < recipient->getHandcardNum()) qSwap(giver, recipient);
        if (giver->isKongcheng()) return ContinueEffects;
        int id = -1;
        if (voluntaryGift) {
            const bool previousFlag = recipient->hasFlag("olanxu_target");
            recipient->setFlags("olanxu_target");
            const auto cleanup = qScopeGuard([=] { if (!previousFlag) recipient->setFlags("-olanxu_target"); });
            const Card *chosen = room->askForExchange(giver, objectName(), 1, 1, false,
                QString("@olanxu:%1:%2").arg(ctx.invoker->objectName()).arg(recipient->objectName()));
            if (chosen) id = chosen->getEffectiveId();
            else if (!giver->isKongcheng()) id = giver->getRandomHandCard()->getEffectiveId();
        } else id = room->askForCardChosen(recipient, giver, "h", objectName());
        if (id < 0 || !giver->isAlive() || !recipient->isAlive() || room->getCardOwner(id) != giver
            || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        const bool nonSpade = Sanguosha->getCard(id)->getSuit() != Card::Spade;
        room->obtainCard(recipient, id);
        if (!voluntaryGift && room->getCardOwner(id) == recipient && room->getCardPlace(id) == Player::PlaceHand) room->showCard(recipient, id);
        const bool reward = voluntaryGift ? giver->getHandcardNum() == recipient->getHandcardNum() : nonSpade;
        if (reward && ctx.invoker->isAlive()) {
            SkillContext draw = ctx;
            draw.choice = "reward";
            skillEffect(draw, ctx.invoker);
        }
        return ContinueEffects;
    }
private:
    const bool voluntaryGift;
};
class Zhuiyi : public TriggerSkillV2
{
public:
    Zhuiyi() : TriggerSkillV2("zhuiyi") { events << Death; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Death is broadcast; only the deceased holder's dispatch offers the skill.
        return player && data.value<DeathStruct>().who == player && player->hasSkill(this)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        QList<ServerPlayer *> candidates = room->getAlivePlayers();
        candidates.removeOne(killer);
        candidates.removeOne(ctx.owner);
        if (candidates.isEmpty()) return false;
        QString prompt = "zhuiyi-invoke";
        if (killer && killer != ctx.owner) prompt += "x:" + killer->objectName();
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), prompt, true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        const int amount = getEffectiveAmount(ctx);
        target->drawCards(3 * amount, objectName());
        if (target->isAlive()) {
            RecoverStruct recover(objectName(), ctx.owner);
            recover.recover = amount;
            room->recover(target, recover, true);
        }
        return false;
    }
};
static void stampLihuoEffect(Room *room, const Card *card, const SkillContext &ctx, int amount)
{
    const int serial = room->getTag("LihuoReceiptSerial").toInt() + 1;
    room->setTag("LihuoReceiptSerial", serial);
    card->setTag("LihuoEffect", QVariantMap{{"serial", serial}, {"actor", ctx.initiator->objectName()},
        {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
        {"amount", amount}});
}

class LihuoViewAsSkill : public ViewAsSkillV2
{
public:
    LihuoViewAsSkill() : ViewAsSkillV2("lihuo", 1) { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return Slash::IsAvailable(request.initiator);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern.contains("slash", Qt::CaseInsensitive);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || !request.selectedCardIds.isEmpty()
            || !Sanguosha->matchExpPattern("%slash", request.initiator, card)) return false;
        const int id = card->getEffectiveId();
        return request.initiator->handCards().contains(id) || request.initiator->getEquipsId().contains(id)
            || request.initiator->getHandPile().contains(id);
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        FireSlash *slash = new FireSlash(material->getSuit(), material->getNumber());
        slash->addSubcard(material);
        slash->setSkillName(objectName());
        return slash;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.use_card || !cardSelectionFeasible(request)) return false;
        ActiveSkillRequest check = request;
        check.selectedCardIds.clear();
        if (!canSelectCard(check, Sanguosha->getCard(request.selectedCardIds.first()))) return false;
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.initiator && ctx.use_card) stampLihuoEffect(ctx.initiator->getRoom(), ctx.use_card, ctx, getEffectiveAmount(ctx));
        return ContinueEffects;
    }
};

class Lihuo : public TriggerSkillV2
{
public:
    Lihuo() : TriggerSkillV2("lihuo") { events << CardFinished << ChangeSlash; global = true; view_as_skill = new LihuoViewAsSkill; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != ChangeSlash || !player || !player->isAlive() || !player->hasSkill(this)) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return convertible(use) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || player != use.from || player->hasFlag("Global_ProcessBroken") || !use.card) return true;
        const QVariantMap receipt = use.card->getTag("LihuoEffect").toMap();
        if (receipt.value("actor").toString() != player->objectName() || receipt.value("amount").toInt() <= 0
            || use.targetModReveal.useHistoryEventId <= 0) return true;
        const QVariantMap damage = room->queryCardUseDamage(use.targetModReveal.useHistoryEventId);
        if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()
            || damage.value("items").toList().isEmpty()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        if (!ctx.sourceRef.isValid()) return true;
        ctx.instanceID = receipt.value("serial").toInt();
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.extra_data = receipt;
        ctx.setModifiedAmount(receipt.value("amount").toInt());
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const QVariantMap receipt = use.card ? use.card->getTag("LihuoEffect").toMap() : QVariantMap();
        return receipt == ctx.extra_data.toMap() && receipt.value("serial").toInt() == ctx.instanceID
            && receipt.value("actor").toString() == ctx.owner->objectName();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == ChangeSlash) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!convertible(use) || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data, false)) return false;
        }
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (event == CardFinished) {
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (target != use.from || !convertible(use)) return false;
        FireSlash *slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
        slash->addSubcard(use.card);
        slash->setSkillName(objectName());
        slash->deleteLater();
        use.sourceRef = ctx.sourceRef;
        use.activationRef = ctx.activationRef;
        use.changeCard(slash);
        stampLihuoEffect(room, use.card, ctx, getEffectiveAmount(ctx));
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
private:
    static bool convertible(const CardUseStruct &use)
    {
        if (!use.from || !use.card || use.card->objectName() != "slash") return false;
        const Skill *skill = Sanguosha->getSkill(use.card->getSkillName());
        if (skill && !skill->inherits("FilterSkill") && !skill->objectName().contains("guhuo")
            && (!use.card->isVirtualCard() || use.card->subcardsLength() != 0)) return false;
        FireSlash preview(use.card->getSuit(), use.card->getNumber());
        preview.addSubcard(use.card);
        preview.setSkillName("lihuo");
        for (ServerPlayer *target : use.to) if (!use.from->canSlash(target, &preview, false)) return false;
        return true;
    }
};
class LihuoTargetMod : public TargetModSkillV2
{
public:
    LihuoTargetMod() : TargetModSkillV2("#lihuo-target")
    {
        frequency = NotFrequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == ExtraTarget && ctx.card && ctx.card->isKindOf("FireSlash")
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

ChunlaoCard::ChunlaoCard()
{
    setSkillName("chunlao");
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void ChunlaoCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->addToPile("wine", this);
}

ChunlaoWineCard::ChunlaoWineCard()
{
    m_skillName = "chunlao";
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void ChunlaoWineCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &) const
{
    ServerPlayer *who = room->getCurrentDyingPlayer();
    if (!who) return;

    if (subcards.length() != 0) {
        room->throwCard(subcards, "chunlao", nullptr);
        Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
        analeptic->setSkillName("_chunlao");
		analeptic->deleteLater();
        room->useCard(CardUseStruct(analeptic, who, who, false));
    }
}

class ChunlaoViewAsSkill : public ViewAsSkillV2
{
public:
    ChunlaoViewAsSkill() : ViewAsSkillV2("chunlao") { expand_pile = "wine"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && (request.pattern == "@@chunlao" || (request.pattern.contains("peach") && !request.initiator->getPile("wine").isEmpty()));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || request.selectedCardIds.contains(card->getEffectiveId())) return false;
        return request.pattern == "@@chunlao" ? request.initiator->handCards().contains(card->getEffectiveId()) && card->isKindOf("Slash")
            : request.selectedCardIds.isEmpty() && request.initiator->getPile("wine").contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.pattern == "@@chunlao" ? !request.selectedCardIds.isEmpty() : request.selectedCardIds.size() == 1; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &request) const override
    { return request.pattern == "@@chunlao" ? "ChunlaoCard" : "ChunlaoWineCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (!card) return nullptr;
        card->setTag("ChunlaoStore", request.pattern == "@@chunlao");
        const ServerPlayer *player = qobject_cast<const ServerPlayer *>(request.initiator);
        ServerPlayer *dying = player ? player->getRoom()->getCurrentDyingPlayer() : nullptr;
        if (dying) card->setTag("ChunlaoDying", dying->objectName());
        return card;
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ctx.choice = request.pattern == "@@chunlao" ? "store" : "rescue";
        if (ctx.choice == "store") return true;
        ServerPlayer *dying = room->getCurrentDyingPlayer();
        if (!dying || !dying->isAlive() || dying->getHp() > 0) return false;
        ctx.extra_data = dying->objectName();
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || !ctx.initiator) return false;
        ActiveSkillRequest selected = request;
        selected.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (room->getCardOwner(id) != ctx.initiator || !canSelectCard(selected, Sanguosha->getCard(id))) return false;
            selected.selectedCardIds << id;
        }
        if (ctx.choice == "rescue") {
            DummyCard payment(request.selectedCardIds);
            const CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.initiator->objectName(), objectName(), "");
            room->throwCard(&payment, reason, nullptr);
        }
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice.isEmpty()) {
            const QVariant stored = ctx.use_card ? ctx.use_card->getTag("ChunlaoStore") : QVariant();
            if (stored.isValid()) ctx.choice = stored.toBool() ? "store" : "rescue";
            else {
                // A missing tag is not "rescue". Cards still in hand are the store pile.
                bool allInHand = ctx.use_card && ctx.initiator && !ctx.use_card->getSubcards().isEmpty();
                if (allInHand) {
                    for (int id : ctx.use_card->getSubcards()) {
                        if (!room || room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand) {
                            allInHand = false;
                            break;
                        }
                    }
                }
                ctx.choice = allInHand ? "store" : "rescue";
            }
        }
        if (ctx.choice != "store" && ctx.extra_data.toString().isEmpty()) {
            if (ctx.use_card) {
                const QVariant dyingName = ctx.use_card->getTag("ChunlaoDying");
                if (dyingName.isValid() && !dyingName.toString().isEmpty()) ctx.extra_data = dyingName.toString();
            }
            if (ctx.extra_data.toString().isEmpty() && room) {
                ServerPlayer *dying = room->getCurrentDyingPlayer();
                if (dying) ctx.extra_data = dying->objectName();
            }
        }
        ServerPlayer *target = ctx.choice == "store" ? ctx.invoker
            : (room ? room->findPlayerByObjectName(ctx.extra_data.toString()) : nullptr);
        if (target) skillEffect(ctx, target);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.use_card || !ctx.initiator || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "store") {
            DummyCard cards;
            for (int id : ctx.use_card->getSubcards())
                if (room->getCardOwner(id) == ctx.initiator && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using")) cards.addSubcard(id);
            if (cards.subcardsLength() > 0) target->addToPile("wine", &cards);
        } else if (target->getHp() <= 0) {
            // The dying recipient uses an ordinary Analeptic as the paid effect's continuation.
            Analeptic *card = new Analeptic(Card::NoSuit, 0);
            card->setSkillName("_chunlao");
            card->deleteLater();
            room->useCardFromSkillEffect(CardUseStruct(card, target, target, false), ctx);
        }
        return ContinueEffects;
    }
};

class Chunlao : public TriggerSkillV2
{
public:
    Chunlao() : TriggerSkillV2("chunlao") { events << EventPhaseStart; view_as_skill = new ChunlaoViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish
            && !player->isKongcheng() && player->getPile("wine").isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        Room::AcceptedViewAsEffectScope prompt(room, ctx.owner, objectName(), ctx);
        if (!prompt.isValid()) return false;
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
        room->askForUseCard(ctx.owner, "@@chunlao", "@chunlao", -1, Card::MethodNone);
        return false;
    }
};
class OLZishou : public TriggerSkillV2
{
public:
    OLZishou() : TriggerSkillV2("olzishou")
    {
        events << DrawNCards << DamageCaused << EventPhaseChanging;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *player : room->getAllPlayers(true))
                player->setTag("OLZishouEffects", QVariantList());
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == DrawNCards && player && player->isAlive() && player->hasSkill(this)
            && data.value<DrawStruct>().reason == "draw_phase" ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        if (event != DamageCaused) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || damage.from != player || !damage.to || !damage.to->isAlive() || damage.to == player) return true;
        for (const QVariant &entry : player->getTag("OLZishouEffects").toList()) {
            const QVariantMap receipt = entry.toMap();
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = sourceRef(receipt);
            if (!ctx.sourceRef.isValid()) continue;
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
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        for (const QVariant &entry : ctx.owner->getTag("OLZishouEffects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("serial").toInt() == ctx.instanceID && sourceRef(receipt) == ctx.sourceRef) return true;
        }
        return false;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageCaused) {
            ctx.targets = {ctx.original_data->value<DamageStruct>().to};
            ctx.manual_effect = true;
            return true;
        }
        QSet<QString> kingdoms;
        for (ServerPlayer *player : room->getAlivePlayers()) kingdoms.insert(player->getKingdom());
        ctx.extra_data = kingdoms.size();
        if (!room->askForSkillInvoke(ctx.owner, objectName(), "olzishou:" + QString::number(kingdoms.size()))) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != DamageCaused) return false;
        ctx.extra_data = false;
        if (!ctx.targets.isEmpty()) skillEffect(event, room, player, ctx, ctx.targets.first());
        return ctx.extra_data.toBool();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == DamageCaused) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            LogMessage log;
            log.type = "#OLzishouPrevent";
            log.from = ctx.owner;
            log.to << target;
            log.arg = objectName();
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);
            ctx.extra_data = true;
            return false;
        }
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += ctx.extra_data.toInt() * getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        // The accepted draw carries a turn-long restriction even if its grant is later removed.
        const int serial = room->getTag("OLZishouReceiptSerial").toInt() + 1;
        room->setTag("OLZishouReceiptSerial", serial);
        QVariantList receipts = target->getTag("OLZishouEffects").toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"serial", serial}, {"amount", getEffectiveAmount(ctx)}};
        target->setTag("OLZishouEffects", receipts);
        return false;
    }
private:
    static SkillInstanceRef sourceRef(const QVariantMap &receipt)
    {
        return SkillInstanceRef(receipt.value("owner").toString(),
            SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
    }
};
class OlMiji : public Miji
{
public:
    OlMiji() : Miji("olmiji", true, true) {}
};
class NosFuhun : public TriggerSkillV2
{
public:
    NosFuhun() : TriggerSkillV2("nosfuhun")
    { events << EventPhaseStart << EventPhaseChanging; global = true; waked_skills = "wusheng,paoxiao"; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) expireFuhunGrants(room, objectName());
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Draw
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && room->askForSkillInvoke(ctx.owner, objectName()); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        skillEffect(event, room, player, ctx, ctx.owner);
        // Accepting the replacement suppresses the normal draw even if its target effect is intercepted.
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        const QList<int> ids = room->getNCards(2 * getEffectiveAmount(ctx));
        if (ids.isEmpty()) return false;
        const auto cleanup = qScopeGuard([&] {
            QList<int> remaining;
            for (int id : ids)
                if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id;
            if (!remaining.isEmpty()) room->returnToTopDrawPile(remaining);
            QList<int> table;
            for (int id : ids) if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::PlaceTable) table << id;
            if (!table.isEmpty()) {
                DummyCard leftovers(table);
                room->throwCard(&leftovers, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, target->objectName(), objectName(), ""), nullptr);
            }
        });
        QSet<Card::Color> colors;
        for (int id : ids) colors.insert(Sanguosha->getCard(id)->getColor());
        CardsMoveStruct reveal;
        reveal.card_ids = ids;
        reveal.to_place = Player::PlaceTable;
        reveal.reason = CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), "");
        room->moveCardsAtomic(reveal, true);
        QList<int> obtainable;
        for (int id : ids) if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::PlaceTable) obtainable << id;
        if (!obtainable.isEmpty()) {
            DummyCard cards(obtainable);
            room->obtainCard(target, &cards);
        }
        if (target->isAlive() && colors.size() > 1 && getEffectiveAmount(ctx) > 0) applyFuhunGrants(room, target, objectName(), ctx);
        return false;
    }
};
class NosGongqi : public ViewAsSkillV2
{
public:
    NosGongqi() : ViewAsSkillV2("nosgongqi", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return Slash::IsAvailable(request.initiator);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && request.pattern.contains("slash", Qt::CaseInsensitive);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !candidate || !request.selectedCardIds.isEmpty()
            || candidate->getTypeId() != Card::TypeEquip || candidate->hasFlag("using")) return false;
        const int id = candidate->getEffectiveId();
        if (!request.initiator->handCards().contains(id) && !request.initiator->getEquipsId().contains(id)) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            Slash slash(candidate->getSuit(), candidate->getNumber());
            slash.addSubcard(id);
            slash.setSkillName(objectName());
            return slash.isAvailable(request.initiator);
        }
        return true;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        if (!originalCard) return nullptr;
        // Ordinary Slash owns target validation and material movement; there is no proxy pay.
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard);
        slash->setSkillName(objectName());
        return slash;
    }
};

class NosGongqiTargetMod : public TargetModSkillV2
{
public:
    NosGongqiTargetMod() : TargetModSkillV2("#nosgongqi-target")
    {
        frequency = NotFrequent;
        // The converted card retains its distance rule even if its grant was removed.
        setHolderSelector(CorrectSkill_System);
        setBaseAmount(1000);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == DistanceLimit && ctx.card && ctx.card->getSkillName() == "nosgongqi"
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

NosJiefanCard::NosJiefanCard()
{
    setSkillName("nosjiefan");
    target_fixed = true;
    //mute = true;
}

void NosJiefanCard::use(Room *room, ServerPlayer *handang, QList<ServerPlayer *> &) const
{
    ServerPlayer *current = room->getCurrent();
    if (!current || current->isDead()) return;
    ServerPlayer *who = room->getCurrentDyingPlayer();
    if (!who) return;

    room->setTag("NosJiefanTarget", QVariant::fromValue(who));
    if (room->askForUseSlashTo(handang, current, "nosjiefan-slash:" + current->objectName(), false,false,false,nullptr,nullptr,"NosJiefanUsed"))
        return;
}

class NosJiefanViewAsSkill : public ViewAsSkillV2
{
public:
    NosJiefanViewAsSkill() : ViewAsSkillV2("nosjiefan") {}
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason == CardUseStruct::CARD_USE_REASON_PLAY || !request.pattern.contains("peach")) return false;
        for (const Player *other : request.initiator->getAliveSiblings()) if (other->hasFlag("CurrentPlayer")) return true;
        return false;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosJiefanCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        const ServerPlayer *player = qobject_cast<const ServerPlayer *>(request.initiator);
        if (!card || !player) return card;
        Room *room = player->getRoom();
        if (room->getCurrent()) card->setTag("NosJiefanCurrent", room->getCurrent()->objectName());
        if (room->getCurrentDyingPlayer()) card->setTag("NosJiefanDying", room->getCurrentDyingPlayer()->objectName());
        return card;
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        ServerPlayer *current = room->getCurrent(), *dying = room->getCurrentDyingPlayer();
        if (!current || !current->isAlive() || !dying || !dying->isAlive()) return false;
        ctx.extra_data = dying->objectName();
        ctx.targets = {current};
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
if (ctx.targets.isEmpty() && ctx.use_card && ctx.invoker) {
            const QString name = ctx.use_card->getTag("NosJiefanCurrent").toString();
            ServerPlayer *current = name.isEmpty() ? nullptr : ctx.invoker->getRoom()->findPlayerByObjectName(name);
            if (current) ctx.targets = {current};
            ctx.extra_data = ctx.use_card->getTag("NosJiefanDying");
        }
        if (!ctx.targets.isEmpty()) skillEffect(ctx, ctx.targets.first());
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive() || !target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        const QVariant previous = ctx.invoker->getTag("NosJiefanRequest");
        RoomState *state = Sanguosha->currentRoomState();
        const auto previousReason = state->getCurrentCardUseReason();
        const QString previousPattern = state->getCurrentCardUsePattern();
        const auto cleanup = qScopeGuard([&] {
            if (previous.isValid()) ctx.invoker->setTag("NosJiefanRequest", previous);
            else ctx.invoker->removeTag("NosJiefanRequest");
            state->setCurrentCardUseReason(previousReason);
            state->setCurrentCardUsePattern(previousPattern);
        });
        const int serial = room->getTag("NosJiefanSerial").toInt() + 1;
        room->setTag("NosJiefanSerial", serial);
        ctx.invoker->setTag("NosJiefanRequest", QVariantMap{{"serial", serial}, {"dying", ctx.extra_data.toString()},
            {"actor", ctx.invoker->objectName()}, {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
            {"amount", getEffectiveAmount(ctx)}});
        room->askForUseSlashTo(ctx.invoker, target, "nosjiefan-slash:" + target->objectName(), false, false, false, nullptr, nullptr, "NosJiefanUsed");
        return ContinueEffects;
    }
};

class NosJiefan : public TriggerSkillV2
{
public:
    NosJiefan() : TriggerSkillV2("nosjiefan")
    { events << PreCardUsed << CardFinished << DamageCaused; global = true; view_as_skill = new NosJiefanViewAsSkill; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((event != PreCardUsed && event != CardFinished) || !player) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.from != player) return true;
        // A physical Slash may be used again; its rescue receipt belongs to one exact use.
        use.card->removeTag("NosJiefanEffect");
        if (event == CardFinished || use.targetModReveal.useHistoryEventId <= 0) return true;
        QVariantMap receipt = player->getTag("NosJiefanRequest").toMap();
        if (use.card->isKindOf("Slash") && use.card->hasFlag("NosJiefanUsed") && !receipt.isEmpty()) {
            receipt.insert("use_id", use.targetModReveal.useHistoryEventId);
            use.card->setTag("NosJiefanEffect", receipt);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DamageCaused) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || damage.from != player || !damage.card || !damage.card->isKindOf("Slash")) return true;
        const QVariantMap receipt = damage.card->getTag("NosJiefanEffect").toMap();
        const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useId <= 0 || receipt.value("use_id").toLongLong() != useId
            || receipt.value("amount").toInt() <= 0 || receipt.value("actor").toString() != player->objectName()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        if (!ctx.sourceRef.isValid()) return true;
        ctx.instanceID = receipt.value("serial").toInt();
        ctx.current_event = event;
        ctx.original_data = &data;
        ctx.extra_data = receipt;
        ctx.setModifiedAmount(receipt.value("amount").toInt());
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const QVariantMap receipt = damage.card ? damage.card->getTag("NosJiefanEffect").toMap() : QVariantMap();
        const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        return useId > 0 && receipt.value("use_id").toLongLong() == useId
            && receipt == ctx.extra_data.toMap() && receipt.value("serial").toInt() == ctx.instanceID;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const QVariantMap receipt = ctx.extra_data.toMap();
        SkillContext prevention = ctx;
        prevention.choice = "prevent";
        prevention.extra_data = false;
        skillEffect(event, room, player, prevention, damage.to);
        if (!prevention.extra_data.toBool()) return false;
        const QString name = receipt.value("dying").toString();
        ServerPlayer *dying = name.isEmpty() ? nullptr : room->findPlayerByObjectName(name);
        if (dying && dying->isAlive()) {
            SkillContext rescue = ctx;
            rescue.choice = "rescue";
            skillEffect(event, room, player, rescue, dying);
        }
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "prevent") {
            LogMessage log;
            log.type = "#NosJiefanPrevent";
            log.from = ctx.owner;
            log.to << target;
            room->sendLog(log);
            ctx.extra_data = true;
            return false;
        }
        const QVariantMap receipt = ctx.extra_data.toMap();
        SkillContext accepted = ctx;
        accepted.activationRef = SkillInstanceRef(receipt.value("activation_owner").toString(),
            SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_instance").toInt()));
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive(); ++i) {
            Peach peach(Card::NoSuit, 0);
            peach.setSkillName("_nosjiefan");
            peach.setFlags("YUANBEN");
            if (ctx.owner->canUse(&peach, target)) room->useCardFromSkillEffect(CardUseStruct(&peach, ctx.owner, target), accepted, false);
        }
        return false;
    }
};
class NosQianxi : public TriggerSkillV2
{
public:
    NosQianxi() : TriggerSkillV2("nosqianxi") { events << DamageCaused; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player == damage.from && player->isAlive() && player->hasSkill(this) && damage.to
            && player->distanceTo(damage.to) == 1 && damage.card && damage.card->isKindOf("Slash")
            && damage.by_user && !damage.chain && !damage.transfer ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.original_data && room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.choice = "judge";
        ctx.extra_data = false;
        skillEffect(event, room, player, ctx, ctx.owner);
        if (!ctx.extra_data.toBool()) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ctx.choice = "replace";
        if (damage.to) skillEffect(event, room, player, ctx, damage.to);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (ctx.choice == "judge") {
            JudgeStruct judge;
            judge.pattern = ".|heart";
            judge.good = false;
            judge.who = target;
            judge.reason = objectName();
            room->judge(judge);
            ctx.extra_data = judge.isGood();
        } else room->loseMaxHp(target, getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class NosZhenlie : public TriggerSkillV2
{
public:
    NosZhenlie() : TriggerSkillV2("noszhenlie") { events << AskForRetrial; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        return player && player->isAlive() && player->hasSkill(this) && judge && judge->who == player
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        JudgeStruct *judge = ctx.original_data ? ctx.original_data->value<JudgeStruct *>() : nullptr;
        if (!target || !target->isAlive() || !judge || judge->who != target || getEffectiveAmount(ctx) <= 0) return false;
        const int id = room->drawCard();
        // A retrial can unwind through nested triggers; do not strand the fetched card.
        const auto cleanup = qScopeGuard([=] {
            if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id))
                room->returnToTopDrawPile(QList<int>{id});
        });
        room->retrial(Sanguosha->getCard(id), ctx.owner, judge, objectName(), false);
        return false;
    }
};
class NosMiji : public TriggerSkillV2
{
public:
    NosMiji() : TriggerSkillV2("nosmiji") { events << EventPhaseStart; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->isWounded()
            && (player->getPhase() == Player::Start || player->getPhase() == Player::Finish)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && room->askForSkillInvoke(ctx.owner, objectName()); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        SkillContext judge = ctx;
        judge.choice = "judge";
        judge.extra_data = false;
        skillEffect(event, room, player, judge, ctx.owner);
        if (!judge.extra_data.toBool() || !ctx.owner->isAlive()) return false;
        const QList<int> ids = room->getNCards(ctx.owner->getLostHp() * getEffectiveAmount(ctx));
        if (ids.isEmpty()) return false;
        const bool previousOperator = ctx.owner->hasFlag("Global_GongxinOperator");
        const auto cleanup = qScopeGuard([&] {
            room->clearAG(ctx.owner);
            if (!previousOperator) ctx.owner->setFlags("-Global_GongxinOperator");
            QList<int> remaining;
            for (int id : ids)
                if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id;
            if (!remaining.isEmpty()) room->returnToTopDrawPile(remaining);
        });
        room->fillAG(ids, ctx.owner);
        ServerPlayer *recipient = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName());
        room->clearAG(ctx.owner);
        if (!recipient) return false;
        SkillContext give = ctx;
        give.choice = "give";
        give.extra_data = ListI2V(ids);
        ctx.owner->setFlags("Global_GongxinOperator");
        skillEffect(event, room, player, give, recipient);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "judge") {
            JudgeStruct judge;
            judge.pattern = ".|black";
            judge.good = true;
            judge.reason = objectName();
            judge.who = target;
            room->judge(judge);
            ctx.extra_data = judge.isGood();
        } else {
            QList<int> cards;
            for (const QVariant &entry : ctx.extra_data.toList()) {
                const int id = entry.toInt();
                if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) cards << id;
            }
            if (!cards.isEmpty()) {
                DummyCard selected(cards);
                room->obtainCard(target, &selected, false);
            }
        }
        return false;
    }
};
OlAnxuCard::OlAnxuCard()
{
    setSkillName("olanxu");
    //mute = true;
    will_throw = true;
    target_fixed = false;
}

bool OlAnxuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self)
        return false;

    if (targets.isEmpty())
        return true;

    if (targets.length() == 1)
        return to_select->getHandcardNum() != targets.first()->getHandcardNum();

    return false;
}

bool OlAnxuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void OlAnxuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *playerA = targets.first();
    ServerPlayer *playerB = targets.last();
    if (playerA->getHandcardNum() < playerB->getHandcardNum()) {
        playerA = targets.last();
        playerB = targets.first();
    }
    if (playerA->isKongcheng())
        return;

    room->setPlayerFlag(playerB, "olanxu_target"); // For AI
    const Card *card = room->askForExchange(playerA, "olanxu", 1, 1, false, QString("@olanxu:%1:%2").arg(source->objectName()).arg(playerB->objectName()));
    room->setPlayerFlag(playerB, "-olanxu_target"); // For AI
    if (!card) card = playerA->getRandomHandCard();

    room->obtainCard(playerB, card);

    if (playerA->getHandcardNum() == playerB->getHandcardNum()) {
        QString choices = source->getLostHp() > 0 ? "draw+recover" : "draw";
        QString choice = room->askForChoice(source, "olanxu", choices);
        if (choice == "draw")
            room->drawCards(source, 1, "olanxu");
        else if (choice == "recover") {
            RecoverStruct recover;
            recover.recover = 1;
            recover.who = source;
            recover.reason = "olanxu";
            room->recover(source, recover);
        }
    }
}

class OlAnxu : public Anxu
{
public:
    OlAnxu() : Anxu("olanxu", true) {}
};
class MobileMiji : public Miji
{
public:
    MobileMiji() : Miji("mobilemiji", true) {}
};
class OlQianxi : public Qianxi
{
public:
    OlQianxi() : Qianxi("olqianxi", true) {}
};

class OlQianxiClear : public QianxiClear
{
public:
    OlQianxiClear() : QianxiClear("olqianxi") {}
};
YJCM2012Package::YJCM2012Package()
    : Package("YJCM2012")
{
    General *bulianshi = new General(this, "bulianshi", "wu", 3, false); // YJ 101
    bulianshi->addSkill(new Anxu);
    bulianshi->addSkill(new Zhuiyi);

    General *caozhang = new General(this, "caozhang", "wei"); // YJ 102
    caozhang->addSkill(new Jiangchi);
    caozhang->addSkill(new JiangchiTargetMod);
    related_skills.insert("jiangchi", "#jiangchi-target");

    General *chengpu = new General(this, "chengpu", "wu"); // YJ 103
    chengpu->addSkill(new Lihuo);
    chengpu->addSkill(new LihuoTargetMod);
    chengpu->addSkill(new Chunlao);
    related_skills.insert("lihuo", "#lihuo-target");

    General *guanxingzhangbao = new General(this, "guanxingzhangbao", "shu"); // YJ 104
    guanxingzhangbao->addSkill(new Fuhun);

    General *handang = new General(this, "handang", "wu"); // YJ 105
    handang->addSkill(new Gongqi);
    handang->addSkill(new GongqiRange);
    handang->addSkill(new GongqiRecord);
    related_skills.insert("gongqi", "#gongqi-range");
    related_skills.insert("gongqi", "#gongqi-record");
    handang->addSkill(new Jiefan);

    General *huaxiong = new General(this, "huaxiong", "qun", 6); // YJ 106
    huaxiong->addSkill(new Shiyong);

    General *liaohua = new General(this, "liaohua", "shu"); // YJ 107
    liaohua->addSkill(new Dangxian);
    liaohua->addSkill(new Fuli);

    General *liubiao = new General(this, "liubiao", "qun", 3); // YJ 108
    liubiao->addSkill(new Zishou);
    liubiao->addSkill(new ZishouProhibit);
    liubiao->addSkill("zongshi");
    related_skills.insert("zishou", "#zishou");

    General *ol_liubiao = new General(this, "ol_liubiao", "qun", 3);
    ol_liubiao->addSkill(new OLZishou);
    ol_liubiao->addSkill("zongshi");

    General *madai = new General(this, "madai", "shu"); // YJ 109
    madai->addSkill("mashu");
    madai->addSkill(new Qianxi);
    madai->addSkill(new QianxiClear);
    related_skills.insert("qianxi", "#qianxi-clear");

    General *wangyi = new General(this, "wangyi", "wei", 3, false); // YJ 110
    wangyi->addSkill(new Zhenlie);
    wangyi->addSkill(new Miji);

    General *ol_wangyi = new General(this, "ol_wangyi", "wei", 3, false);
    ol_wangyi->addSkill("zhenlie");
    ol_wangyi->addSkill(new OlMiji);

    General *xunyou = new General(this, "xunyou", "wei", 3); // YJ 111
    xunyou->addSkill(new Qice);
    xunyou->addSkill(new Zhiyu);

    addMetaObject<QiceCard>();
    addMetaObject<ChunlaoCard>();
    addMetaObject<ChunlaoWineCard>();
    addMetaObject<GongqiCard>();
    addMetaObject<JiefanCard>();
    addMetaObject<AnxuCard>();
}

ADD_PACKAGE(YJCM2012)

void MigrateToNostalgiaYJCM2012(Package *pkg)
{
    General *nos_guanxingzhangbao = new General(pkg, "nos_guanxingzhangbao", "shu");
    nos_guanxingzhangbao->addSkill(new NosFuhun);

    General *nos_handang = new General(pkg, "nos_handang", "wu");
    nos_handang->addSkill(new NosGongqi);
    nos_handang->addSkill(new NosGongqiTargetMod);
    nos_handang->addSkill(new NosJiefan);
    pkg->insertRelatedSkills("nosgongqi", "#nosgongqi-target");
    pkg->addMetaObject<NosJiefanCard>();

    General *nos_liubiao = new General(pkg, "nos_liubiao", "qun", 4);
    nos_liubiao->addSkill(new NosZishou);
    nos_liubiao->addSkill(new Zongshi);

    General *nos_madai = new General(pkg, "nos_madai", "shu");
    nos_madai->addSkill("mashu");
    nos_madai->addSkill(new NosQianxi);

    General *nos_wangyi = new General(pkg, "nos_wangyi", "wei", 3, false);
    nos_wangyi->addSkill(new NosZhenlie);
    nos_wangyi->addSkill(new NosMiji);
}

void MigrateToOLStYJ2012(Package *pkg)
{
    General *ol_bulianshi = new General(pkg, "ol_bulianshi", "wu", 3, false);
    ol_bulianshi->addSkill(new OlAnxu);
    ol_bulianshi->addSkill("zhuiyi");
    pkg->addMetaObject<OlAnxuCard>();

    /*General *ol_guanxingzhangbao = new General(pkg, "ol_guanxingzhangbao", "shu", 4, true);
    ol_guanxingzhangbao->addSkill("fuhun");*/

    General *ol_madai = new General(pkg, "ol_madai", "shu");
    ol_madai->addSkill("mashu");
    ol_madai->addSkill(new OlQianxi);
    ol_madai->addSkill(new OlQianxiClear);
    pkg->insertRelatedSkills("olqianxi", "#olqianxi-clear");
}

void MigrateToMobileStYJ2012(Package *pkg)
{
    General *mobile_wangyi = new General(pkg, "mobile_wangyi", "wei", 3, false);
    mobile_wangyi->addSkill("zhenlie");
    mobile_wangyi->addSkill(new MobileMiji);
}
