#include "mobileshiji.h"
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
#include "wind.h"
#include "skill-instance-utils.h"
#include <memory>
#include <climits>
#include <QScopeGuard>

// PostCardResponded is a completion point only for the consuming response.
// A supplied card keeps its original response identity through the native paid-provision receipt.
static QList<QVariantMap> mobileShijiCompletedResponses(Room *room, const CardResponseStruct &response)
{
    if (!response.m_card || response.nullified) return {};
    const QVariantMap event = room->historyParent(room->currentHistoryEventId(), "respond_card", true);
    const QVariantMap data = event.value("data").toMap();
    if (!data.contains("is_provision") || data.value("is_provision").toBool()) return {};
    QList<qint64> ids{event.value("id").toLongLong()};
    const qint64 provided = data.value("provenance").toMap().value("paid_provision").toMap()
        .value("response_event_id").toLongLong();
    if (provided > 0 && !ids.contains(provided)) ids << provided;
    QList<QVariantMap> result;
    for (qint64 id : ids) {
        if (id <= 0) continue;
        const QVariantMap page = room->queryHistoryFacts({{"kind", "respond_card"}, {"event_id", id}, {"limit", 1}});
        if (page.contains("error") || !page.value("complete").toBool() || page.value("has_more").toBool()
            || page.value("items").toList().size() != 1) continue;
        result << page.value("items").toList().first().toMap();
    }
    return result;
}

class MobileShijiActiveQuota : public TriggerSkillV2
{
public:
    explicit MobileShijiActiveQuota(const QString &skillName)
        : TriggerSkillV2("#" + skillName + "-quota"), activeName(skillName)
    {
        events << EventSkillInvoking;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const SkillContext accepted = data.value<SkillContext>();
        if (!accepted.bypass_cost || accepted.skill_name != activeName || accepted.executionID == 0
            || !accepted.use_card || !accepted.sourceRef.isValid() || !accepted.activationRef.isValid()) return true;
        const ViewAsSkillV2 *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(activeName));
        if (!skill) return true;
        const SkillInstanceRef ref = skill->getUsageRef(accepted);
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!ref.isValid() || !holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return true;
        // These custom quotas insert into an exact-instance set, so repeated acceptance cannot double-charge.
        // A borrowed or granted activation may have a different name from its frozen source.
        skill->addUsage(accepted);
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
private:
    QString activeName;
};

MobileZhiQiaiCard::MobileZhiQiaiCard()
{
    setSkillName("mobilezhiqiai");
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MobileZhiQiaiCard::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.from->getRoom();
    room->giveCard(effect.from, effect.to, this, "mobilezhiqiai");
    if (effect.from->isDead() || effect.to->isDead()) return;
    QStringList choices;
    if (effect.from->getLostHp() > 0)
        choices << "recover";
    choices << "draw";
    if (room->askForChoice(effect.to, "mobilezhiqiai", choices.join("+"), QVariant::fromValue(effect.from)) == "recover")
        room->recover(effect.from, RecoverStruct("mobilezhiqiai", effect.to));
    else
        effect.from->drawCards(2, "mobilezhiqiai");
}

class MobileZhiQiai : public ViewAsSkillV2
{
public:
    MobileZhiQiai() : ViewAsSkillV2("mobilezhiqiai", 1)
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileZhiQiaiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && !card->isVirtualCard() && !card->hasFlag("using") && !card->isKindOf("BasicCard")
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
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
        Room *room = ctx.invoker->getRoom();
        // Giving is the recipient's effect; an intercepted target receives no material.
        foreach (int id, ctx.use_card->getSubcards()) {
            if (!ctx.initiator || room->getCardOwner(id) != ctx.initiator
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip))
                return ContinueEffects;
        }
        DummyCard gift(ctx.use_card->getSubcards());
        room->giveCard(ctx.initiator, target, &gift, objectName());
        if (ctx.invoker->isDead() || target->isDead()) return ContinueEffects;
        QStringList choices;
        if (ctx.invoker->isWounded()) choices << "recover";
        choices << "draw";
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant::fromValue(ctx.invoker)) == "recover")
            room->recover(ctx.invoker, RecoverStruct(objectName(), target, getEffectiveAmount(ctx)));
        else
            ctx.invoker->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class MobileZhiShanxi : public TriggerSkillV2
{
public:
    MobileZhiShanxi() : TriggerSkillV2("mobilezhishanxi") { events << EventPhaseStart << HpRecover; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.is_forced = ctx.current_event == HpRecover;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->isDead()) return result;
        if (event == EventPhaseStart) {
            if (player->getPhase() == Player::Play && player->hasSkill(objectName()))
                result[player] << objectName();
        } else if (player->getMark("&mobilezhixi") > 0 && !player->hasFlag("Global_Dying")) {
            for (ServerPlayer *owner : room->getAlivePlayers())
                if (owner->hasSkill(objectName())) result[owner] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == HpRecover) {
            if (ctx.invoker->getMark("&mobilezhixi") <= 0 || ctx.invoker->hasFlag("Global_Dying")) return false;
            ctx.targets = {ctx.invoker};
            return true;
        }
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner))
            if (target->getMark("&mobilezhixi") == 0) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(),
            "@mobilezhishanxi-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "give") {
            const QVariantMap state = ctx.extra_data.toMap();
            ServerPlayer *donor = room->findPlayerByObjectName(state.value("donor").toString());
            if (!donor || donor->isDead()) return false;
            DummyCard gift;
            for (int id : ListV2I(state.value("ids").toList()))
                if (room->getCardOwner(id) == donor
                    && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) gift.addSubcard(id);
            if (gift.subcardsLength() > 0) room->giveCard(donor, target, &gift, objectName());
            return false;
        }
        if (event == EventPhaseStart) {
            room->broadcastSkillInvoke(objectName());
            int transferred = 0;
            // Xi is a public, transferable game resource, not an activation counter.
            for (ServerPlayer *previous : room->getOtherPlayers(target)) {
                const int count = previous->getMark("&mobilezhixi");
                if (count <= 0) continue;
                previous->loseAllMarks("&mobilezhixi");
                transferred += count;
            }
            if (target->isAlive()) target->gainMark("&mobilezhixi", transferred > 0 ? transferred : getEffectiveAmount(ctx));
            return false;
        }
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (target != ctx.owner && target->getCardCount() >= 2) {
            std::unique_ptr<const Card> gift(room->askForExchange(target, objectName(), 2, 2, true,
                "mobilezhishanxi-give:" + ctx.owner->objectName(), true));
            if (gift && gift->subcardsLength() == 2) {
                SkillContext transfer = ctx;
                transfer.choice = "give";
                transfer.extra_data = QVariantMap{{"donor", target->objectName()}, {"ids", ListI2V(gift->getSubcards())}};
                skillEffect(event, room, player, transfer, ctx.owner);
                return false;
            }
        }
        room->loseHp(HpLostStruct(target, amount, objectName(), ctx.owner));
        return false;
    }
};

MobileZhiShamengCard::MobileZhiShamengCard()
{
    setSkillName("mobilezhishameng");
}

void MobileZhiShamengCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->drawCards(2, "mobilezhishameng");
    effect.from->drawCards(3, "mobilezhishameng");
}

class MobileZhiShameng : public ViewAsSkillV2
{
public:
    MobileZhiShameng() : ViewAsSkillV2("mobilezhishameng", 2)
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileZhiShamengCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->isVirtualCard() || card->hasFlag("using")
            || request.selectedCardIds.size() >= 2 || request.selectedCardIds.contains(card->getEffectiveId())
            || !request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->isJilei(card))
            return false;
        return request.selectedCardIds.isEmpty()
            || card->sameColorWith(Sanguosha->getCard(request.selectedCardIds.first()));
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
        // Selected cards are paid atomically by the V2 proxy before target effects.
        target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        if (ctx.invoker->isAlive()) ctx.invoker->drawCards(3 * getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

MobileZhiDuojiCard::MobileZhiDuojiCard()
{
    setSkillName("mobilezhiduoji");
}

bool MobileZhiDuojiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->getEquips().isEmpty();
}

void MobileZhiDuojiCard::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.from->getRoom();
    room->removePlayerMark(effect.from, "@mobilezhiduojiMark");
    room->doSuperLightbox(effect.from, "mobilezhiduoji");
    QList<int> equiplist = effect.to->getEquipsId();
    if (equiplist.isEmpty()) return;
    DummyCard equips(equiplist);
    room->obtainCard(effect.from, &equips);
}

class MobileZhiDuoji : public ViewAsSkillV2
{
public:
    MobileZhiDuoji() : ViewAsSkillV2("mobilezhiduoji", 2)
    {
        frequency = Limited;
        limit_mark = "@mobilezhiduojiMark";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileZhiDuojiCard"; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.size() < 2 && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.initiator->handCards().contains(card->getEffectiveId()) && !request.initiator->isJilei(card);
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target != request.initiator && selected.isEmpty() && !target->getEquips().isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        // The mark is presentation only; the game quota belongs to activationRef.
        room->removePlayerMark(ctx.invoker, "@mobilezhiduojiMark");
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        room->doSuperLightbox(ctx.invoker, objectName());
        const QList<int> ids = target->getEquipsId();
        if (!ids.isEmpty()) {
            DummyCard equips(ids);
            room->obtainCard(ctx.invoker, &equips);
        }
        return ContinueEffects;
    }
};

MobileZhiJianzhanCard::MobileZhiJianzhanCard()
{
    setSkillName("mobilezhijianzhan");
}

void MobileZhiJianzhanCard::onEffect(CardEffectStruct &effect) const
{
    Slash*slash = new Slash(Card::NoSuit, 0);
    slash->deleteLater();
    slash->setSkillName("_mobilezhijianzhan");

    Room*room = effect.from->getRoom();
    QStringList choices;
    QList<ServerPlayer*> can_slash;
    foreach (ServerPlayer*p, room->getOtherPlayers(effect.to)) {
        if (!effect.to->canSlash(p, slash) || p->getHandcardNum() >= effect.to->getHandcardNum()) continue;
        can_slash << p;
    }
    if (!can_slash.isEmpty())
        choices << "slash";
    choices << "draw";

    QString choice = room->askForChoice(effect.to, "mobilezhijianzhan", choices.join("+"), QVariant::fromValue(effect.from));
    if (choice == "slash") {
        foreach (ServerPlayer*p, can_slash) {
            if (!effect.to->canSlash(p, slash) || p->getHandcardNum() >= effect.to->getHandcardNum())
                can_slash.removeOne(p);
        }
        if (can_slash.isEmpty()) return;
        ServerPlayer*to = room->askForPlayerChosen(effect.from, can_slash, "mobilezhijianzhan", "@mobilezhijianzhan-slash:" + effect.to->objectName());
        room->useCard(CardUseStruct(slash, effect.to, to));
    } else
        effect.from->drawCards(1, "mobilezhijianzhan");
}

class MobileZhiJianzhan : public ViewAsSkillV2
{
public:
    MobileZhiJianzhan() : ViewAsSkillV2("mobilezhijianzhan")
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileZhiJianzhanCard"; }
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
        Room *room = ctx.invoker->getRoom();
        std::unique_ptr<Slash> slash(new Slash(Card::NoSuit, 0));
        slash->setSkillName("_mobilezhijianzhan");
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(target))
            if (target->canSlash(other, slash.get()) && other->getHandcardNum() < target->getHandcardNum())
                candidates << other;
        QStringList choices;
        if (!candidates.isEmpty()) choices << "slash";
        choices << "draw";
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant::fromValue(ctx.invoker)) == "slash") {
            for (ServerPlayer *other : QList<ServerPlayer *>(candidates))
                if (other->isDead() || !target->canSlash(other, slash.get())
                    || other->getHandcardNum() >= target->getHandcardNum()) candidates.removeOne(other);
            if (target->isAlive() && ctx.invoker->isAlive() && !candidates.isEmpty()) {
                ServerPlayer *victim = room->askForPlayerChosen(ctx.invoker, candidates, objectName(),
                    "@mobilezhijianzhan-slash:" + target->objectName());
                if (victim) {
                    CardUseStruct use(slash.get(), target, victim);
                    use.sourceRef = ctx.sourceRef;
                    use.activationRef = ctx.activationRef;
                    room->useCardFromSkillEffect(use, ctx, true);
                }
            }
        } else if (ctx.invoker->isAlive()) {
            ctx.invoker->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return ContinueEffects;
    }
};

SecondMobileZhiDuojiCard::SecondMobileZhiDuojiCard()
{
    setSkillName("secondmobilezhiduoji");
    will_throw = false;
    handling_method = Card::MethodNone;
}

void SecondMobileZhiDuojiCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->addToPile("smzdjji", subcards);
}

SecondMobileZhiDuojiRemove::SecondMobileZhiDuojiRemove()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void SecondMobileZhiDuojiRemove::onUse(Room*room, CardUseStruct &card_use) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "secondmobilezhiduoji", "");
    room->throwCard(this, reason, nullptr);
    card_use.from->drawCards(1, "secondmobilezhiduoji");
}

class SecondMobileZhiDuojiVS : public ViewAsSkillV2
{
public:
    SecondMobileZhiDuojiVS() : ViewAsSkillV2("secondmobilezhiduoji", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "SecondMobileZhiDuojiCard"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && card && request.selectedCardIds.isEmpty() && request.initiator->getCards("he").contains(card); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        const int id = ctx.use_card->getSubcards().value(0, -1);
        if (id < 0 || room->getCardOwner(id) != ctx.initiator
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
        const qint64 previous = room->getTag("SecondMobileZhiDuojiSequence").toLongLong();
        if (previous < 0 || previous >= INT_MAX) return ContinueEffects;
        const int serial = int(previous + 1); room->setTag("SecondMobileZhiDuojiSequence", serial);
        QVariantList receipts = target->getTag("SecondMobileZhiDuojiReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"card_id", id}, {"owner", ctx.owner->objectName()},
            {"actor", ctx.invoker->objectName()}, {"holder", target->objectName()}, {"amount", getEffectiveAmount(ctx)},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("SecondMobileZhiDuojiReceipts", receipts);
        target->addToPile("smzdjji", id);
        if (!target->getPile("smzdjji").contains(id)) {
            receipts = target->getTag("SecondMobileZhiDuojiReceipts").toList();
            for (int i = receipts.size() - 1; i >= 0; --i)
                if (receipts.at(i).toMap().value("serial").toInt() == serial) receipts.removeAt(i);
            target->setTag("SecondMobileZhiDuojiReceipts", receipts);
        }
        return ContinueEffects;
    }
};

class SecondMobileZhiDuoji : public TriggerSkillV2
{
public:
    SecondMobileZhiDuoji() : TriggerSkillV2("secondmobilezhiduoji")
    {
        global = true; frequency = Compulsory; events << CardFinished << EventPhaseChanging << CardsMoveOneTime << Death;
        view_as_skill = new SecondMobileZhiDuojiVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!player || move.from != player) return true;
            QVariantList receipts;
            for (const QVariant &value : player->getTag("SecondMobileZhiDuojiReceipts").toList())
                if (player->getPile("smzdjji").contains(value.toMap().value("card_id").toInt())) receipts << value;
            player->setTag("SecondMobileZhiDuojiReceipts", receipts);
        } else if (event == Death && player && data.value<DeathStruct>().who == player)
            player->removeTag("SecondMobileZhiDuojiReceipts");
        Q_UNUSED(room);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (!player || player->isDead()) return true;
        const bool ending = event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive;
        if (!ending && event != CardFinished) return true;
        const CardUseStruct use = ending ? CardUseStruct() : data.value<CardUseStruct>();
        if (!ending && (!use.card || !use.card->isKindOf("EquipCard") || use.from != player
            || room->getCardOwner(use.card->getEffectiveId()) != player || room->getCardPlace(use.card->getEffectiveId()) != Player::PlaceEquip)) return true;
        for (const QVariant &value : player->getTag("SecondMobileZhiDuojiReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (!player->getPile("smzdjji").contains(receipt.value("card_id").toInt())) continue;
            ServerPlayer *issuer = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            if (!ending && (!issuer || issuer->isDead())) continue;
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = player; ctx.invoker = issuer ? issuer : player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt(); ctx.amount = receipt.value("amount").toInt();
            ctx.choice = ending ? "return" : "equip"; ctx.is_forced = true; ctx.extra_data = receipt;
            ctx.targets = {player}; ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.initiator->getTag("SecondMobileZhiDuojiReceipts").toList().contains(ctx.extra_data)
            || !ctx.initiator->getPile("smzdjji").contains(ctx.extra_data.toMap().value("card_id").toInt())) return false;
        if (ctx.choice == "return") return true;
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        return card && room->getCardOwner(card->getEffectiveId()) == ctx.initiator
            && room->getCardPlace(card->getEffectiveId()) == Player::PlaceEquip;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice != "equip") return true;
        const QVariantMap original = ctx.extra_data.toMap();
        QMap<int, QVariant> choices;
        for (const QVariant &value : ctx.initiator->getTag("SecondMobileZhiDuojiReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("source_owner") == original.value("source_owner") && receipt.value("source_skill") == original.value("source_skill")
                && receipt.value("source_id") == original.value("source_id") && receipt.value("activation_owner") == original.value("activation_owner")
                && receipt.value("activation_skill") == original.value("activation_skill") && receipt.value("activation_id") == original.value("activation_id")
                && ctx.initiator->getPile("smzdjji").contains(receipt.value("card_id").toInt()))
                choices[receipt.value("card_id").toInt()] = value;
        }
        if (choices.isEmpty()) return false;
        int id = choices.firstKey();
        if (choices.size() > 1) {
            room->fillAG(choices.keys(), ctx.initiator);
            const auto clear = qScopeGuard([&] { room->clearAG(ctx.initiator); });
            id = room->askForAG(ctx.initiator, choices.keys(), false, objectName());
        }
        if (!choices.contains(id)) return false;
        ctx.extra_data = choices.value(id);
        ctx.amount = ctx.extra_data.toMap().value("amount").toInt();
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        const int id = receipt.value("card_id").toInt();
        if (ctx.choice == "obtain_equip") {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            if (card && room->getCardOwner(card->getEffectiveId()) == ctx.initiator
                && room->getCardPlace(card->getEffectiveId()) == Player::PlaceEquip) room->obtainCard(target, card);
            return false;
        }
        if (ctx.choice == "obtain_return") {
            if (room->getCardPlace(id) == Player::DiscardPile) room->obtainCard(target, id);
            return false;
        }
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        // The pile owner is the actual affected recipient; redirecting cannot spend another player's receipt.
        if (target != ctx.initiator) return false;
        QVariantList receipts = target->getTag("SecondMobileZhiDuojiReceipts").toList();
        receipts.removeAll(ctx.extra_data); target->setTag("SecondMobileZhiDuojiReceipts", receipts);
        ServerPlayer *issuer = room->findPlayerByObjectName(receipt.value("actor").toString());
        if (ctx.choice == "equip" && issuer && issuer->isAlive()) {
            SkillContext obtain = ctx; obtain.choice = "obtain_equip";
            skillEffect(event, room, ctx.owner, obtain, issuer);
        }
        if (target->getPile("smzdjji").contains(id)) {
            CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", objectName(), "");
            room->throwCard(Sanguosha->getCard(id), reason, nullptr);
        }
        if (ctx.choice == "return") {
            if (issuer && issuer->isAlive()) {
                SkillContext obtain = ctx; obtain.choice = "obtain_return";
                skillEffect(event, room, ctx.owner, obtain, issuer);
            }
        } else if (target->isAlive()) {
            SkillContext draw = ctx; draw.choice = "draw";
            skillEffect(event, room, ctx.owner, draw, target);
        }
        return false;
    }
};

MobileZhiWanweiCard::MobileZhiWanweiCard()
{
    setSkillName("mobilezhiwanwei");
}

void MobileZhiWanweiCard::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.from->getRoom();
    room->addPlayerMark(effect.from, "mobilezhiwanwei_lun");
    int hp = effect.from->getHp();
    if (hp + 1 > 0)
        room->recover(effect.to, RecoverStruct(effect.from, nullptr, qMin(hp + 1, effect.to->getMaxHp() - effect.to->getHp()), "mobilezhiwanwei"));
    if (hp > 0)
        room->loseHp(HpLostStruct(effect.from, hp, "mobilezhiwanwei", effect.from));
}

class MobileZhiWanweiVS : public ViewAsSkillV2
{
public:
    MobileZhiWanweiVS() : ViewAsSkillV2("mobilezhiwanwei")
    {
    }

    LimitScope getLimitScope() const override { return Limit_Round; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileZhiWanweiCard"; }
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
        Room *room = ctx.invoker->getRoom();
        const int hp = ctx.invoker->getHp();
        const int recovery = qMin(hp + getEffectiveAmount(ctx), target->getLostHp());
        if (recovery > 0)
            room->recover(target, RecoverStruct(ctx.invoker, nullptr, recovery, objectName()));
        // Losing HP is the accepted effect's consequence, after the target recovers.
        if (hp > 0 && ctx.invoker->isAlive())
            room->loseHp(HpLostStruct(ctx.invoker, hp, objectName(), ctx.invoker));
        return ContinueEffects;
    }
};

class MobileZhiWanwei : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileZhiWanwei() : TriggerSkillV2("mobilezhiwanwei")
    {
        events << Dying << EventSkillInvoking;
        view_as_skill = new MobileZhiWanweiVS;
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
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        const DyingStruct dying = data.value<DyingStruct>();
        // Dying is dispatched once per potential helper; do not loop all holders again.
        return player && player->isAlive() && player->hasSkill(objectName()) && dying.who
            && dying.who != player && dying.who->isAlive() && dying.who->getHp() <= 0
            && player->getHp() + 1 > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DyingStruct>().who;
        if (!target || target->isDead() || target->getHp() > 0
            || !ctx.owner->askForSkillInvoke(this, target)) return false;
        ctx.targets = {target};
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Both the active and dying paths charge the same activation instance's round quota.
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int hp = ctx.owner->getHp();
        room->broadcastSkillInvoke(objectName());
        const int recovery = qMin(hp + getEffectiveAmount(ctx), target->getLostHp());
        if (recovery > 0)
            room->recover(target, RecoverStruct(ctx.owner, nullptr, recovery, objectName()));
        if (hp > 0 && ctx.owner->isAlive())
            room->loseHp(HpLostStruct(ctx.owner, hp, objectName(), ctx.owner));
        return false;
    }
};

class MobileZhiYuejian : public TriggerSkillV2
{
public:
    MobileZhiYuejian() : TriggerSkillV2("mobilezhiyuejian")
    {
        events << Dying;
    }

    QStringList payable(ServerPlayer *player) const
    {
        QStringList result;
        foreach (int id, player->handCards() + player->getEquipsId()) {
            if (player->canDiscard(player, id)) result << QString::number(id);
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DyingStruct>().who == player && player->getHp() <= 0 && payable(player).size() >= 2
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QStringList choices = payable(ctx.owner);
        if (choices.size() < 2) return false;
        const Card *cards = room->askForExchange(ctx.owner, objectName(), 2, 2, true,
            "@mobilezhiyuejian", true, choices.join(","));
        if (!cards || cards->subcardsLength() != 2) return false;
        QVariantList selected;
        foreach (int id, cards->getSubcards()) selected << id;
        ctx.extra_data = selected;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<int> selected;
        const QStringList choices = payable(ctx.owner);
        foreach (const QVariant &value, ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (selected.contains(id) || !choices.contains(QString::number(id))) return false;
            selected << id;
        }
        if (selected.size() != 2) return false;
        // Validate the whole cost before moving either card.
        DummyCard cards(selected);
        room->throwCard(&cards, ctx.owner, nullptr);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        return false;
    }
};

class MobileZhiYuejianMax : public MaxCardsSkillV2
{
public:
    MobileZhiYuejianMax() : MaxCardsSkillV2("#mobilezhiyuejian-max")
    {
    }

    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        return ctx.holder ? CorrectSkillResult::useAmount(ctx.holder->getMaxHp() * ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};

MobileZhiJianyuCard::MobileZhiJianyuCard()
{
    setSkillName("mobilezhijianyu");
}

bool MobileZhiJianyuCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
    return targets.length() < 2;
}

bool MobileZhiJianyuCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
    return targets.length() == 2;
}

void MobileZhiJianyuCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
    room->addPlayerMark(source, "mobilezhijianyu_lun");
    room->addPlayerMark(targets.first(), "&mobilezhijianyu+#" + source->objectName() + "#" + targets.last()->objectName());
    room->addPlayerMark(targets.last(), "&mobilezhijianyu+#" + source->objectName() + "#" + targets.first()->objectName());
}

class MobileZhiJianyuVS : public ViewAsSkillV2
{
public:
    MobileZhiJianyuVS() : ViewAsSkillV2("mobilezhijianyu") { setPhaseName("Play"); }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return effectOnTargetGroup(ctx, ctx.targets);
    }
    LimitScope getLimitScope() const override { return Limit_Round; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileZhiJianyuCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return target && selected.size() < 2 && !selected.contains(target); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 2 && selected.first() != selected.last(); }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (targets.size() != 2 || targets.first() == targets.last() || targets.first()->isDead() || targets.last()->isDead())
            return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        QList<ServerPlayer *> recipients;
        QList<int> amounts;
        for (ServerPlayer *target : targets) {
            SkillContext part = ctx;
            part.extra_data = QVariant();
            skillEffect(part, target);
            const QVariantMap accepted = part.extra_data.toMap();
            ServerPlayer *recipient = room->findPlayerByObjectName(accepted.value("jianyu_target").toString());
            if (!recipient || recipient->isDead() || recipients.contains(recipient)) return ContinueEffects;
            recipients << recipient; amounts << accepted.value("amount").toInt();
        }
        if (recipients.first()->isDead() || recipients.last()->isDead()) return ContinueEffects;
        const qint64 previous = room->getTag("MobileZhiJianyuSequence").toLongLong();
        if (previous < 0 || previous >= INT_MAX) return ContinueEffects;
        const int serial = int(previous + 1); room->setTag("MobileZhiJianyuSequence", serial);
        QVariantList receipts = room->getTag("MobileZhiJianyuReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"owner", ctx.owner->objectName()}, {"actor", ctx.invoker->objectName()},
            {"first", recipients.first()->objectName()}, {"second", recipients.last()->objectName()},
            {"first_amount", amounts.first()}, {"second_amount", amounts.last()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        room->setTag("MobileZhiJianyuReceipts", receipts);
        room->addPlayerMark(recipients.first(), "&mobilezhijianyu+#" + ctx.invoker->objectName() + "#" + recipients.last()->objectName());
        room->addPlayerMark(recipients.last(), "&mobilezhijianyu+#" + ctx.invoker->objectName() + "#" + recipients.first()->objectName());
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.extra_data = QVariantMap{{"jianyu_target", target->objectName()}, {"amount", getEffectiveAmount(ctx)}};
        return ContinueEffects;
    }
};

class MobileZhiJianyu : public TriggerSkillV2
{
public:
    MobileZhiJianyu() : TriggerSkillV2("mobilezhijianyu")
    {
        global = true; frequency = Compulsory; events << EventPhaseStart << TargetSpecifying;
        view_as_skill = new MobileZhiJianyuVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::RoundStart) return true;
        QVariantList kept, expired;
        for (const QVariant &value : room->getTag("MobileZhiJianyuReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("actor").toString() != player->objectName()) { kept << value; continue; }
            expired << value;
        }
        room->setTag("MobileZhiJianyuReceipts", kept);
        for (const QVariant &value : expired) {
            const QVariantMap receipt = value.toMap();
            // Clear only this issuer's expired public contribution; primitive receipts are authoritative.
            ServerPlayer *first = room->findPlayerByObjectName(receipt.value("first").toString(), true);
            ServerPlayer *second = room->findPlayerByObjectName(receipt.value("second").toString(), true);
            if (first) room->removePlayerMark(first, "&mobilezhijianyu+#" + player->objectName() + "#" + receipt.value("second").toString());
            if (second) room->removePlayerMark(second, "&mobilezhijianyu+#" + player->objectName() + "#" + receipt.value("first").toString());
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != TargetSpecifying || !player || player->getPhase() != Player::Play) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->isKindOf("SkillCard")) return true;
        for (const QVariant &value : room->getTag("MobileZhiJianyuReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            const QString first = receipt.value("first").toString(), second = receipt.value("second").toString();
            const QString partner = first == player->objectName() ? second : second == player->objectName() ? first : QString();
            ServerPlayer *target = room->findPlayerByObjectName(partner);
            if (!target || target->isDead() || !use.to.contains(target)) continue;
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            ctx.invoker = ctx.owner;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.amount = receipt.value(partner == first ? "first_amount" : "second_amount").toInt(); ctx.is_forced = true;
            ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event;
            ctx.targets = {target}; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return room->getTag("MobileZhiJianyuReceipts").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};

class MobileZhiShengxi : public TriggerSkillV2
{
public:
    MobileZhiShengxi() : TriggerSkillV2("mobilezhishengxi")
    {
        events << EventPhaseStart;
        frequency = Frequent;
        m_baseAmount = 2;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish)
            return {};
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toString().isEmpty() || turn.toString() == "0") return {};
        const QVariantMap history = room->queryActualDamage({{"turn_id", turn},
            {"from", player->objectName()}, {"limit", 1}});
        // An incomplete journal cannot prove the no-damage condition.
        if (history.contains("error") || !history.value("complete").toBool() || !history.value("items").toList().isEmpty()) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner->askForSkillInvoke(this); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(this);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MobileZhiQinzheng : public TriggerSkillV2
{
public:
    MobileZhiQinzheng() : TriggerSkillV2("mobilezhiqinzheng")
    {
        events << GameStart << EventAcquireSkill << CardUsed << CardResponded;
        frequency = Compulsory;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || (event != GameStart && event != EventAcquireSkill)) return true;
        if (event == GameStart) {
            for (const SkillInstance &instance : player->getSkillInstances())
                if (instance.skillName == objectName() && !player->getSkillInstanceStateValue(objectName(),
                    instance.instanceID, "qinzheng_history_after").isValid())
                    player->setSkillInstanceStateValue(objectName(), instance.instanceID, "qinzheng_history_after", qint64(0));
        } else {
            SkillChangeStruct change;
            if (!change.tryParse(data) || change.skillName != objectName()
                || !player->hasSkillInstance(change.skillName, change.instanceID)) return true;
            const QVariantMap page = room->queryHistoryFacts({{"limit", 1}});
            if (!page.contains("error") && page.value("complete").toBool())
                player->setSkillInstanceStateValue(change.skillName, change.instanceID, "qinzheng_history_after",
                    page.value("watermark"));
        }
        return true;
    }
    int count(TriggerEvent event, Room *room, const SkillContext &ctx) const
    {
        const SkillInstanceRef ref = ctx.activationRef;
        if (!ctx.owner || !ref.isValid()) return -1;
        const QVariant baseline = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
            "qinzheng_history_after");
        if (!baseline.isValid()) return -1;
        const QString currentKind = event == CardUsed ? "use_card" : "respond_card";
        const qint64 eventId = room->historyParent(room->currentHistoryEventId(), currentKind, true).value("id").toLongLong();
        if (eventId <= 0) return -1;
        const QVariantMap current = room->queryHistoryFacts({{"kind", currentKind}, {"event_id", eventId}, {"limit", 1}});
        if (current.contains("error") || !current.value("complete").toBool()
            || current.value("items").toList().size() != 1 || current.value("has_more").toBool()) return -1;
        const QVariant watermark = current.value("items").toList().first().toMap().value("sequence");
        int total = 0;
        // Freeze at this use/response, so a nested later card cannot change the outer card's ordinal.
        for (const QString &kind : QStringList{"use_card", "respond_card"}) {
            QVariantMap filter{{"kind", kind}, {kind == "use_card" ? "from" : "player", ctx.owner->objectName()},
                {"after", baseline}, {"watermark", watermark}, {"limit", 128}};
            for (;;) {
                const QVariantMap page = room->queryHistoryFacts(filter);
                if (page.contains("error") || !page.value("complete").toBool()) return -1;
                for (const QVariant &entry : page.value("items").toList()) {
                    const QVariantMap fact = entry.toMap().value("data").toMap();
                    // MethodUse responses consume a card without another use_card event; both response kinds count.
                    const QVariantMap card = fact.value("card").toMap();
                    if (!card.contains("type")) return -1;
                    if (card.value("type").toInt() != Card::TypeSkill) ++total;
                }
                if (!page.value("has_more").toBool()) break;
                filter["after"] = page.value("next_after");
            }
        }
        return total;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())
            || (event != CardUsed && event != CardResponded)) return {};
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        if (!card || card->isKindOf("SkillCard")) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int ordinal = count(event, room, ctx);
        if (ordinal <= 0 || (ordinal % 3 && ordinal % 5 && ordinal % 8)) return false;
        ctx.extra_data = ordinal;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        const int ordinal = ctx.extra_data.toInt();
        for (int divisor : {3, 5, 8}) {
            if (ordinal % divisor || target->isDead()) continue;
            QList<int> ids;
            for (int id : room->getDrawPile()) {
                const Card *card = Sanguosha->getCard(id);
                if ((divisor == 3 && (card->isKindOf("Slash") || card->isKindOf("Jink")))
                    || (divisor == 5 && (card->isKindOf("Peach") || card->isKindOf("Analeptic")))
                    || (divisor == 8 && (card->isKindOf("ExNihilo") || card->isKindOf("Duel")))) ids << id;
            }
            qsanShuffle(ids);
            while (ids.size() > qMax(0, getEffectiveAmount(ctx))) ids.removeLast();
            if (!ids.isEmpty()) {
                DummyCard reward(ids);
                room->obtainCard(target, &reward, true);
            }
        }
        return false;
    }
};

class MobileZhiWuku : public TriggerSkillV2
{
public:
    MobileZhiWuku() : TriggerSkillV2("mobilezhiwuku")
    {
        events << CardUsed;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        TriggerList result;
        if (!use.card || !use.card->isKindOf("EquipCard")) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->getMark("&mobilezhiwuku") < 3) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int gain = qMin(getEffectiveAmount(ctx), 3 - target->getMark("&mobilezhiwuku"));
        if (gain <= 0) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        target->gainMark("&mobilezhiwuku", gain);
        return false;
    }
};

class MobileZhiSanchen : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileZhiSanchen() : TriggerSkillV2("mobilezhisanchen")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "mobilezhimiewu";
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
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        return player && player->isAlive() && player->getPhase() == Player::Finish && player->hasSkill(objectName())
            && (player->getMark("&mobilezhiwuku") > 2 || player->canWake(objectName()))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return ctx.owner->getMark("&mobilezhiwuku") > 2 || ctx.owner->canWake(objectName());
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!(ctx.owner->getMark("&mobilezhiwuku") > 2 || ctx.owner->canWake(objectName()))) return false;
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        room->setPlayerMark(ctx.owner, objectName(), 1);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = target;

        if (ctx.owner->getMark("&mobilezhiwuku")>2){
			LogMessage log;
			log.type = "#MobileZhiSanchenWake";
			log.from = ctx.owner;
			log.arg = QString::number(ctx.owner->getMark("&mobilezhiwuku"));
			log.arg2 = objectName();
			room->sendLog(log);
		}
        ctx.owner->peiyin(this);
        room->notifySkillInvoked(ctx.owner, objectName());
        room->doSuperLightbox(ctx.owner, objectName());

        if (room->changeMaxHpForAwakenSkill(player, getEffectiveAmount(ctx), objectName())) {
            room->recover(player, RecoverStruct("mobilezhisanchen", ctx.owner, getEffectiveAmount(ctx)));
            room->acquireSkillFromEffect(player, "mobilezhimiewu", ctx);
        }
        return false;
    }
};

MobileZhiMiewuCard::MobileZhiMiewuCard()
{
    setSkillName("mobilezhimiewu");
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileZhiMiewuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->addSubcards(subcards);
		card->setSkillName("mobilezhimiewu");
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}

    const Card*_card = Self->getTag("mobilezhimiewu").value<const Card*>();
    if (_card == nullptr)
        return false;

    card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->addSubcards(subcards);
    card->setSkillName("mobilezhimiewu");
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool MobileZhiMiewuCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->deleteLater();
		return card->targetFixed();
	}

	const Card*_card = Self ? Self->getTag("mobilezhimiewu").value<const Card*>() : nullptr;
	if (_card == nullptr)
		return false;

	card = Sanguosha->cloneCard(_card);
	card->deleteLater();
	return card->targetFixed();
}

bool MobileZhiMiewuCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->addSubcards(subcards);
		card->setSkillName("mobilezhimiewu");
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}

    const Card*_card = Self->getTag("mobilezhimiewu").value<const Card*>();
    if (_card == nullptr)
        return false;

    card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->addSubcards(subcards);
    card->setSkillName("mobilezhimiewu");
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card*MobileZhiMiewuCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer*player = card_use.from;
    player->loseMark("&mobilezhiwuku");
    Room*room = player->getRoom();
    room->addPlayerMark(player, "mobilezhimiewu-Clear");

    QString to_yizan = user_string;

    if ((user_string.contains("slash") || user_string.contains("Slash")) &&
            Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list = Sanguosha->getSlashNames();
        if (guhuo_list.isEmpty())
            guhuo_list << "slash";
        to_yizan = room->askForChoice(player, "mobilezhimiewu_slash", guhuo_list.join("+"));
    }

    const Card*card = Sanguosha->getCard(subcards.first());
    if (to_yizan == "normal_slash")
        to_yizan = "slash";

    Card*use_card = Sanguosha->cloneCard(to_yizan, card->getSuit(), card->getNumber());
    use_card->setSkillName("mobilezhimiewu");
    use_card->addSubcards(getSubcards());
    room->setCardFlag(use_card, "mobilezhimiewu");
	use_card->deleteLater();
    return use_card;
}

const Card*MobileZhiMiewuCard::validateInResponse(ServerPlayer*player) const
{
    player->loseMark("&mobilezhiwuku");
    Room*room = player->getRoom();
    room->addPlayerMark(player, "mobilezhimiewu-Clear");

    QString to_yizan;
    if (user_string == "peach+analeptic") {
        QStringList guhuo_list;
        guhuo_list << "peach";
        if (Sanguosha->hasCard("analeptic"))
            guhuo_list << "analeptic";
        to_yizan = room->askForChoice(player, "mobilezhimiewu_saveself", guhuo_list.join("+"));
    } else if (user_string == "slash") {
        QStringList guhuo_list = Sanguosha->getSlashNames();
        if (guhuo_list.isEmpty())
            guhuo_list << "slash";
        to_yizan = room->askForChoice(player, "mobilezhimiewu_slash", guhuo_list.join("+"));
    } else
        to_yizan = user_string;

    const Card*card = Sanguosha->getCard(subcards.first());
    if (to_yizan == "normal_slash")
        to_yizan = "slash";

    Card*use_card = Sanguosha->cloneCard(to_yizan, card->getSuit(), card->getNumber());
    use_card->setSkillName("mobilezhimiewu");
    use_card->addSubcards(getSubcards());
    room->setCardFlag(use_card, "mobilezhimiewu");
	use_card->deleteLater();
    return use_card;
}

class MobileZhiMiewuVS : public ViewAsSkillV2
{
public:
    MobileZhiMiewuVS() : ViewAsSkillV2("mobilezhimiewu", 1) { response_or_use = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    SkillDialogInfo getDialogInfo() const override
    { return SkillDialogInfo::guhuo(objectName(), true, true, true, false, true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.initiator->hasTurn() && request.initiator->getMark("&mobilezhiwuku") > 0
            && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
                || (!request.pattern.startsWith(".") && !request.pattern.startsWith("@")));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || !request.selectedCardIds.isEmpty()) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            ? !request.initiator->isCardLimited(card, Card::MethodResponse) : !request.initiator->isLocked(card);
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || ctx.initiator->getMark("&mobilezhiwuku") <= 0) return false;
        ctx.initiator->loseMark("&mobilezhiwuku");
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || ctx.executionID <= 0 || !ctx.invoker || ctx.use_card->getSubcards().size() != 1) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        QVariantMap receipts = room->getTag("MobileZhiMiewuReceipts").toMap();
        // This accepted conversion owns its completion reward even if its grant is removed.
        receipts[QString::number(ctx.executionID)] = QVariantMap{{"execution", ctx.executionID},
            {"actor", ctx.invoker->objectName()}, {"owner", ctx.owner->objectName()}, {"amount", getEffectiveAmount(ctx)},
            {"material", ctx.use_card->getSubcards().first()}, {"turn_id", room->historyScopes().value("turn_id")},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        room->setTag("MobileZhiMiewuReceipts", receipts);
        return ContinueEffects;
    }
protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        const std::unique_ptr<Card> card(Sanguosha->cloneCard(name));
        return card && (card->isKindOf("BasicCard") || card->isNDTrick());
    }
};

class MobileZhiMiewu : public TriggerSkillV2
{
public:
    MobileZhiMiewu() : TriggerSkillV2("mobilezhimiewu")
    {
        global = true; frequency = Compulsory;
        events << CardFinished << PostCardResponded << EventSkillEffectFinished << EventPhaseChanging << Death;
        view_as_skill = new MobileZhiMiewuVS;
    }
    SkillDialogInfo getDialogInfo() const override
    { return SkillDialogInfo::guhuo(objectName(), true, true, true, false, true); }
    int getPriority(TriggerEvent) const override { return 0; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        QVariantMap receipts = room->getTag("MobileZhiMiewuReceipts").toMap();
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            // Ordinary use completes at CardFinished. Responses publish an explicit native completion receipt.
            if (finished.skill_name == objectName() && finished.executionID > 0
                && !finished.interceptor_data.value("native_response_completion").value("completed").toBool())
                receipts.remove(QString::number(finished.executionID));
        } else if ((event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) || event == Death) {
            const QVariant turn = room->historyScopes().value("turn_id");
            for (auto it = receipts.begin(); it != receipts.end(); ) {
                const QVariantMap receipt = it.value().toMap();
                if ((event == EventPhaseChanging && receipt.value("turn_id") == turn)
                    || (event == Death && player && receipt.value("actor").toString() == player->objectName())) it = receipts.erase(it);
                else ++it;
            }
        }
        room->setTag("MobileZhiMiewuReceipts", receipts);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        QList<QVariantMap> completions;
        if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || use.card->isKindOf("SkillCard")) return true;
            completions << QVariantMap{{"execution_id", use.skillExecutionID},
                {"skill_owner", use.sourceRef.ownerObjectName}, {"skill_name", use.sourceRef.key.skillName},
                {"instance_id", use.sourceRef.key.instanceID}, {"activation_owner", use.activationRef.ownerObjectName},
                {"activation_skill", use.activationRef.key.skillName}, {"activation_instance_id", use.activationRef.key.instanceID},
                {"card", room->historyCardSnapshot(use.card)}};
        } else if (event == PostCardResponded) {
            for (const QVariantMap &fact : mobileShijiCompletedResponses(room, data.value<CardResponseStruct>()))
                completions << fact.value("data").toMap();
        } else return true;
        for (const QVariantMap &completion : completions) {
            const qint64 execution = completion.value("execution_id").toLongLong();
            if (execution <= 0) continue;
            const QVariantMap receipt = room->getTag("MobileZhiMiewuReceipts").toMap().value(QString::number(execution)).toMap();
            if (receipt.isEmpty() || receipt.value("source_owner") != completion.value("skill_owner")
                || receipt.value("source_skill") != completion.value("skill_name")
                || receipt.value("source_id").toInt() != completion.value("instance_id").toInt()
                || receipt.value("activation_owner") != completion.value("activation_owner")
                || receipt.value("activation_skill") != completion.value("activation_skill")
                || receipt.value("activation_id").toInt() != completion.value("activation_instance_id").toInt()
                || !completion.value("card").toMap().value("subcards").toList().contains(receipt.value("material"))) continue;
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true); ctx.invoker = ctx.initiator;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.initiator || ctx.initiator->isDead() || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = int(execution);
            ctx.amount = receipt.value("amount").toInt(); ctx.is_forced = true; ctx.targets = {ctx.initiator};
            ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return room->getTag("MobileZhiMiewuReceipts").toMap().value(QString::number(ctx.extra_data.toMap().value("execution").toLongLong())) == ctx.extra_data;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantMap receipts = room->getTag("MobileZhiMiewuReceipts").toMap();
        receipts.remove(QString::number(ctx.extra_data.toMap().value("execution").toLongLong()));
        room->setTag("MobileZhiMiewuReceipts", receipts);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

MobileZhiPackage::MobileZhiPackage()
    : Package("mobilezhi")
{
    General*mobilezhi_wangcan = new General(this, "mobilezhi_wangcan", "wei", 3);
    mobilezhi_wangcan->addSkill(new MobileZhiQiai);
    mobilezhi_wangcan->addSkill(new MobileZhiShanxi);

    General*mobilezhi_chenzhen = new General(this, "mobilezhi_chenzhen", "shu", 3);
    mobilezhi_chenzhen->addSkill(new MobileZhiShameng);

    General*mobilezhi_xunchen = new General(this, "mobilezhi_xunchen", "qun", 3);
    mobilezhi_xunchen->addSkill(new MobileZhiDuoji);
    mobilezhi_xunchen->addSkill(new MobileZhiJianzhan);

    General*second_mobilezhi_xunchen = new General(this, "second_mobilezhi_xunchen", "qun", 3);
    second_mobilezhi_xunchen->addSkill(new SecondMobileZhiDuoji);
    second_mobilezhi_xunchen->addSkill("mobilezhijianzhan");

    General*mobilezhi_bianfuren = new General(this, "mobilezhi_bianfuren", "wei", 3, false);
    mobilezhi_bianfuren->addSkill(new MobileZhiWanwei);
    mobilezhi_bianfuren->addSkill(new MobileZhiYuejian);
    mobilezhi_bianfuren->addSkill(new MobileZhiYuejianMax);
    related_skills.insert("mobilezhiyuejian", "#mobilezhiyuejian-max");

    General*mobilezhi_feiyi = new General(this, "mobilezhi_feiyi", "shu", 3);
    mobilezhi_feiyi->addSkill(new MobileZhiJianyu);
    mobilezhi_feiyi->addSkill(new MobileZhiShengxi);

    General*mobilezhi_luotong = new General(this, "mobilezhi_luotong", "wu", 4);
    mobilezhi_luotong->addSkill(new MobileZhiQinzheng);

    General*mobilezhi_duyu = new General(this, "mobilezhi_duyu", "qun", 4);
    mobilezhi_duyu->addSkill(new MobileZhiWuku);
    mobilezhi_duyu->addSkill(new MobileZhiSanchen);
    mobilezhi_duyu->addRelateSkill("mobilezhimiewu");

    skills << new MobileZhiMiewu;

    addMetaObject<MobileZhiQiaiCard>();
    addMetaObject<MobileZhiShamengCard>();
    addMetaObject<MobileZhiDuojiCard>();
    addMetaObject<MobileZhiJianzhanCard>();
    addMetaObject<SecondMobileZhiDuojiCard>();
    addMetaObject<SecondMobileZhiDuojiRemove>();
    addMetaObject<MobileZhiWanweiCard>();
    addMetaObject<MobileZhiJianyuCard>();
    addMetaObject<MobileZhiMiewuCard>();
}

ADD_PACKAGE(MobileZhi)



MobileXinYinjuCard::MobileXinYinjuCard()
{
    setSkillName("mobilexinyinju");
}

void MobileXinYinjuCard::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.from->getRoom();
    if (!effect.to->canSlash(effect.from, false) ||
            !room->askForUseSlashTo(effect.to, effect.from, "@mobilexinyinju-slash:" + effect.from->objectName(), false))
        room->addPlayerMark(effect.to, "&mobilexinyinju");
}

class MobileXinYinjuVS : public ViewAsSkillV2
{
public:
    MobileXinYinjuVS() : ViewAsSkillV2("mobilexinyinju") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileXinYinjuCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    { return target && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (target->canSlash(ctx.invoker, false)
            && room->askForUseSlashTo(target, ctx.invoker, "@mobilexinyinju-slash:" + ctx.invoker->objectName(), false))
            return ContinueEffects;
        if (target->isDead()) return ContinueEffects;
        const qint64 previous = room->getTag("MobileXinYinjuSequence").toLongLong();
        if (previous < 0 || previous >= INT_MAX) return ContinueEffects;
        const int serial = int(previous + 1);
        room->setTag("MobileXinYinjuSequence", serial);
        QVariantList receipts = target->getTag("MobileXinYinjuReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"owner", ctx.owner->objectName()}, {"actor", ctx.invoker->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("MobileXinYinjuReceipts", receipts);
        room->setPlayerMark(target, "&mobilexinyinju", receipts.size());
        return ContinueEffects;
    }
};

class MobileXinYinju : public TriggerSkillV2
{
public:
    MobileXinYinju() : TriggerSkillV2("mobilexinyinju")
    {
        events << EventPhaseStart << EventPhaseEnd << EventPhaseChanging << Death;
        global = true; frequency = Compulsory;
        view_as_skill = new MobileXinYinjuVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == EventPhaseStart && player->getPhase() == Player::Start) {
            // Consume the pending batch at the boundary. Nested Starts have independent frames.
            QVariantList frames = player->getTag("MobileXinYinjuDue").toList();
            frames << QVariant(player->getTag("MobileXinYinjuReceipts").toList());
            player->setTag("MobileXinYinjuDue", frames);
            player->removeTag("MobileXinYinjuReceipts");
            room->setPlayerMark(player, "&mobilexinyinju", 0);
        } else if (event == EventPhaseEnd && player->getPhase() == Player::Start) {
            QVariantList frames = player->getTag("MobileXinYinjuDue").toList();
            if (!frames.isEmpty()) frames.removeLast();
            player->setTag("MobileXinYinjuDue", frames);
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            // Turn interruption may bypass EventPhaseEnd; already-due effects never carry into another turn.
            player->removeTag("MobileXinYinjuDue");
        } else if (event == Death && data.value<DeathStruct>().who == player) {
            player->removeTag("MobileXinYinjuReceipts"); player->removeTag("MobileXinYinjuDue");
            room->setPlayerMark(player, "&mobilexinyinju", 0);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->isDead() || player->getPhase() != Player::Start) return true;
        const QVariantList frames = player->getTag("MobileXinYinjuDue").toList();
        if (frames.isEmpty()) return true;
        for (const QVariant &value : frames.last().toList()) {
            const QVariantMap receipt = value.toMap();
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.invoker = player;
            // Runtime identity is the receipt serial; immutable sourceRef retains the true skill instance.
            ctx.instanceID = receipt.value("serial").toInt(); ctx.is_forced = true;
            ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.targets = {player};
            contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.invoker) return false;
        for (const QVariant &frame : ctx.invoker->getTag("MobileXinYinjuDue").toList())
            if (frame.toList().contains(ctx.extra_data)) return true;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Phase replacement is only meaningful for the actor at this pending Start boundary.
        if (target != ctx.invoker || target->getPhase() != Player::Start) return false;
        QVariantList frames = target->getTag("MobileXinYinjuDue").toList();
        for (QVariant &frame : frames) {
            QVariantList receipts = frame.toList();
            receipts.removeAll(ctx.extra_data); frame = receipts;
        }
        target->setTag("MobileXinYinjuDue", frames);
        if (!target->isSkipped(Player::Play)) target->skip(Player::Play);
        if (!target->isSkipped(Player::Discard)) target->skip(Player::Discard);
        return false;
    }
};

class MobileXinChijie : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileXinChijie() : TriggerSkillV2("mobilexinchijie")
    {
        events << TargetConfirming << EventSkillInvoking;
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
    LimitScope getLimitScope() const override { return Limit_Turn; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasTurn() && player->hasSkill(objectName())
            && use.card && !use.card->isKindOf("SkillCard") && use.to.size() == 1
            && use.to.contains(player) && use.from != player ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
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
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.to.contains(target)) return false;
        room->broadcastSkillInvoke(objectName());

        JudgeStruct judge;
        judge.who = target;
        judge.reason = objectName();
        judge.pattern = ".|.|7~99";
        judge.good = true;
        room->judge(judge);

        if (judge.isGood()) {
            // Judgement may resolve nested effects; preserve their changes to the pending use.
            use = ctx.original_data->value<CardUseStruct>();
            use.to.removeOne(target);
            *ctx.original_data = QVariant::fromValue(use);
        }
        return false;
    }
};

MobileXinCunsiCard::MobileXinCunsiCard()
{
    setSkillName("mobilexincunsi");
}

bool MobileXinCunsiCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
    return targets.isEmpty();
}

void MobileXinCunsiCard::onEffect(CardEffectStruct &effect) const
{
    effect.from->turnOver();
    if (effect.to->isDead()) return;
    Room*room = effect.from->getRoom();
    QList<int> slashs, ids = room->getDrawPile() + room->getDiscardPile();
    foreach (int id, ids) {
        if (Sanguosha->getCard(id)->isKindOf("Slash"))
            slashs << id;
    }
    if (!slashs.isEmpty())
        room->obtainCard(effect.to, slashs.at(qsanRandomBounded(slashs.length())));
    room->addPlayerMark(effect.to, "&mobilexincunsi");
}

class MobileXinCunsiVS : public ViewAsSkillV2
{
public:
    MobileXinCunsiVS() : ViewAsSkillV2("mobilexincunsi") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileXinCunsiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->faceUp(); }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return target && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || !ctx.initiator->faceUp()) return false;
        ctx.initiator->turnOver(); return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        const int amount = getEffectiveAmount(ctx);
        QList<int> ids;
        for (int id : room->getDrawPile() + room->getDiscardPile())
            if (Sanguosha->getCard(id)->isKindOf("Slash")) ids << id;
        qsanShuffle(ids);
        while (ids.size() > amount) ids.removeLast();
        if (!ids.isEmpty()) { DummyCard reward(ids); room->obtainCard(target, &reward); }
        if (target->isDead() || amount <= 0) return ContinueEffects;
        const qint64 previous = room->getTag("MobileXinCunsiSequence").toLongLong();
        if (previous < 0 || previous >= INT_MAX) return ContinueEffects;
        const int serial = int(previous + 1); room->setTag("MobileXinCunsiSequence", serial);
        QVariantList receipts = target->getTag("MobileXinCunsiReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"amount", amount}, {"owner", ctx.owner->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("MobileXinCunsiReceipts", receipts);
        room->addPlayerMark(target, "&mobilexincunsi", amount);
        return ContinueEffects;
    }
};

class MobileXinCunsi : public TriggerSkillV2
{
public:
    MobileXinCunsi() : TriggerSkillV2("mobilexincunsi")
    {
        global = true; frequency = Compulsory; view_as_skill = new MobileXinCunsiVS;
        events << PreCardUsed << DamageCaused << DamageComplete << CardFinished << EventPhaseChanging << Death;
    }
    QString eventKey(Room *room, const QString &kind) const
    {
        const qint64 id = room->historyParent(room->currentHistoryEventId(), kind, true).value("id").toLongLong();
        return id > 0 ? QString::number(id) : QString();
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        QVariantMap uses = room->getTag("MobileXinCunsiUses").toMap();
        QVariantMap damageFrames = room->getTag("MobileXinCunsiDamage").toMap();
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !use.card || !use.card->isKindOf("Slash")) return true;
            const QString key = eventKey(room, "use_card");
            if (key.isEmpty()) return true; // No exact correlation is available with history disabled.
            const QVariantList receipts = player->getTag("MobileXinCunsiReceipts").toList();
            if (receipts.isEmpty()) return true;
            uses[key] = receipts; player->removeTag("MobileXinCunsiReceipts");
            room->setTag("MobileXinCunsiUses", uses);
            room->setPlayerMark(player, "&mobilexincunsi", 0);
            return true;
        } else if (event == DamageCaused) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from != player || !damage.card || !damage.card->isKindOf("Slash")) return true;
            const QString useKey = eventKey(room, "use_card"), damageKey = eventKey(room, "damage");
            if (useKey.isEmpty() || damageKey.isEmpty() || !uses.contains(useKey)) return true;
            // The next Slash spends the benefit on its first damage attempt, not a card-string alias.
            damageFrames[damageKey] = uses.take(useKey);
        } else if (event == CardFinished) uses.remove(eventKey(room, "use_card"));
        else if (event == DamageComplete) damageFrames.remove(eventKey(room, "damage"));
        else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            for (auto it = uses.begin(); it != uses.end(); ) {
                if (room->historyEvent(it.key().toLongLong()).value("turn_id").toLongLong() == turn) it = uses.erase(it);
                else ++it;
            }
            for (auto it = damageFrames.begin(); it != damageFrames.end(); ) {
                if (room->historyEvent(it.key().toLongLong()).value("turn_id").toLongLong() == turn) it = damageFrames.erase(it);
                else ++it;
            }
        } else if (event == Death && data.value<DeathStruct>().who == player) {
            player->removeTag("MobileXinCunsiReceipts"); room->setPlayerMark(player, "&mobilexincunsi", 0);
            return true;
        }
        room->setTag("MobileXinCunsiUses", uses); room->setTag("MobileXinCunsiDamage", damageFrames);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != DamageCaused || !player) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from != player || !damage.to) return true;
        const QString key = eventKey(room, "damage");
        for (const QVariant &value : room->getTag("MobileXinCunsiDamage").toMap().value(key).toList()) {
            const QVariantMap receipt = value.toMap();
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = player; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.extra_data = QVariantMap{{"receipt", receipt}, {"damage_event", key}};
            ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true;
            ctx.amount = receipt.value("amount").toInt(); ctx.targets = {damage.to}; contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap values = ctx.extra_data.toMap();
        return room->getTag("MobileXinCunsiDamage").toMap().value(values.value("damage_event").toString())
            .toList().contains(values.value("receipt"));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data || ctx.original_data->value<DamageStruct>().to != target) return false;
        const QVariantMap values = ctx.extra_data.toMap();
        QVariantMap frames = room->getTag("MobileXinCunsiDamage").toMap();
        QVariantList receipts = frames.value(values.value("damage_event").toString()).toList();
        receipts.removeAll(values.value("receipt")); frames[values.value("damage_event").toString()] = receipts;
        room->setTag("MobileXinCunsiDamage", frames);
        target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        return false;
    }
};

class MobileXinGuixiu : public TriggerSkillV2
{
public:
    MobileXinGuixiu() : TriggerSkillV2("mobilexinguixiu")
    {
        events << Damaged << TurnedOver;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && (event == Damaged ? !player->faceUp() : player->faceUp())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = target;
        if (event == Damaged) {
            if (player->faceUp()) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            player->turnOver();
        } else {
            if (!player->faceUp()) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            player->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return false;
    }
};

class SecondMobileXinGuixiu : public TriggerSkillV2
{
public:
    SecondMobileXinGuixiu() : TriggerSkillV2("secondmobilexinguixiu")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = target;
        int hp = player->getHp();
        if (hp % 2 == 0) {
            if (player->isWounded())
                room->sendCompulsoryTriggerLog(player, this);
            room->recover(player, RecoverStruct("secondmobilexinguixiu", player, getEffectiveAmount(ctx)));
        } else {
            room->sendCompulsoryTriggerLog(player, this);
            player->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return false;
    }
};

class SecondMobileXinQingyu : public TriggerSkillV2
{
public:
    SecondMobileXinQingyu() : TriggerSkillV2("secondmobilexinqingyu")
    {
        events << EventPhaseStart << DamageInflicted << Dying;
        shiming_skill = true;
        waked_skills = "secondmobilexinxuancun";
        frequency = NotCompulsory;
    }

    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        // Mission obligations are mandatory without turning every branch into a locked skill.
        ctx.is_forced = true;
        return TriggerSkillV2::prepareSource(room, ctx);
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || player->isDead() || !player->hasSkill(objectName())) return result;
        if (event == EventPhaseStart && (player->getPhase() != Player::Start || player->getLostHp() != 0 || !player->isKongcheng())) return result;
        if (event == Dying && data.value<DyingStruct>().who != player) return result;
        if (event == DamageInflicted) {
            int count = 0;
            for (int id : player->handCards() + player->getEquipsId())
                if (player->canDiscard(player, id)) ++count;
            if (count < 2) return result;
        }
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            if (room->getShimingStatus(ref) <= 0)
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (room->getShimingStatus(ctx.activationRef) > 0) return false;
        ctx.targets = {ctx.owner};
        if (event != DamageInflicted) return true;
        QStringList payable;
        for (int id : ctx.owner->handCards() + ctx.owner->getEquipsId())
            if (ctx.owner->canDiscard(ctx.owner, id)) payable << QString::number(id);
        if (payable.size() < 2) return false;
        const Card *cards = room->askForExchange(ctx.owner, objectName(), 2, 2, true, "", false, payable.join(","));
        if (!cards || cards->subcardsLength() != 2) return false;
        QVariantList ids;
        for (int id : cards->getSubcards()) {
            if (!ctx.owner->canDiscard(ctx.owner, id)) return false;
            ids << id;
        }
        ctx.extra_data = ids;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != DamageInflicted) return true;
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (ids.contains(id) || room->getCardOwner(id) != ctx.owner
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || !ctx.owner->canDiscard(ctx.owner, id)) return false;
            ids << id;
        }
        if (ids.size() != 2) return false;
        // Validate the entire payment before any card moves or damage prevention.
        DummyCard payment(ids);
        room->throwCard(&payment, objectName(), ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DamageInflicted) {
            if (target != ctx.original_data->value<DamageStruct>().to) return false;
            room->sendCompulsoryTriggerLog(ctx.owner, this, 1);
            return true;
        } else if (event == EventPhaseStart) {
            if (room->sendShimingLog(ctx.activationRef))
                room->acquireSkillFromEffect(target, "secondmobilexinxuancun", ctx);
        } else {
            if (room->sendShimingLog(ctx.activationRef, false))
                room->loseMaxHp(target, getEffectiveAmount(ctx), objectName());
        }
        return false;
    }
};

class SecondMobileXinXuancun : public TriggerSkillV2
{
public:
    SecondMobileXinXuancun() : TriggerSkillV2("secondmobilexinxuancun")
    {
        events << EventPhaseStart;
        m_baseAmount = 2;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->isDead() || player->getPhase() != Player::NotActive) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->getHp() > owner->getHandcardNum()) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const int draw = ctx.owner->getHp() - ctx.owner->getHandcardNum();
        if (draw <= 0 || !ctx.invoker || ctx.invoker->isDead() || !ctx.owner->askForSkillInvoke(this, ctx.invoker)) return false;
        ctx.extra_data = draw;
        ctx.targets = {ctx.invoker};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(this);
        target->drawCards(qMin(ctx.extra_data.toInt(), getEffectiveAmount(ctx)), objectName());
        return false;
    }
};

class MobileXinHeji : public TriggerSkillV2
{
public:
    MobileXinHeji() : TriggerSkillV2("mobilexinheji") { events << CardFinished; }
    bool eligible(Room *room, const CardUseStruct &use) const
    {
        if (use.to.size() != 1 || use.to.first()->isDead()) return false;
        const qint64 useID = use.targetModReveal.useHistoryEventId > 0 ? use.targetModReveal.useHistoryEventId
            : room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useID <= 0) return false;
        const QVariantMap result = room->queryHistoryFacts({{"kind", "use_card"}, {"event_id", useID}, {"limit", 1}});
        const QVariantList facts = result.value("items").toList();
        if (result.contains("error") || !result.value("complete").toBool() || facts.size() != 1) return false;
        const QVariantMap card = facts.first().toMap().value("data").toMap().value("card").toMap();
        const QVariantList classes = card.value("classes").toList();
        return classes.contains(QVariant("Duel"))
            || (classes.contains(QVariant("Slash")) && card.value("red").toBool());
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        TriggerList list;
        if (!eligible(room, use)) return list;
        for (ServerPlayer *owner : room->getOtherPlayers(use.to.first()))
            if (owner->hasSkill(objectName())) list[owner] << objectName();
        return list;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!eligible(room, use)) return false;
        ctx.invoker = ctx.owner; ctx.targets = {use.to.first()};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "reward") {
            QList<int> ids = room->getDrawPile() + room->getDiscardPile(), selected;
            qsanShuffle(ids);
            for (int id : ids) {
                if (Sanguosha->getCard(id)->isRed()) selected << id;
                if (selected.size() >= getEffectiveAmount(ctx)) break;
            }
            if (!selected.isEmpty() && getEffectiveAmount(ctx) > 0) {
                DummyCard cards(selected); room->obtainCard(target, &cards);
            }
            return false;
        }
        if (!ctx.invoker || ctx.invoker->isDead() || ctx.invoker == target) return false;
        // The native retrieval prompt does not consume the card; its subsequent ordinary use pays and resolves once.
        const Card *card = room->askForCard(ctx.invoker, "Slash,Duel|.|.|hand", "@mobilexinheji-use:" + target->objectName(),
            *ctx.original_data, Card::MethodUse, target, true);
        if (!card || target->isDead() || ctx.invoker->isDead() || ctx.invoker->isProhibited(target, card)) return false;
        const bool physical = !card->isVirtualCard();
        CardUseStruct use(card, ctx.invoker, target);
        room->notifySkillInvoked(ctx.owner, objectName()); room->broadcastSkillInvoke(objectName());
        if (!room->useCard(use, false) || !physical || ctx.invoker->isDead()) return false;
        SkillContext reward = ctx; reward.choice = "reward";
        skillEffect(event, room, ctx.owner, reward, ctx.invoker);
        return false;
    }
};

MobileXinMouliCard::MobileXinMouliCard()
{
    setSkillName("mobilexinmouli");
    handling_method = Card::MethodNone;
    will_throw = false;
}

void MobileXinMouliCard::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.from->getRoom();
    room->giveCard(effect.from, effect.to, this, "mobilexinmouli");
    if (effect.to->isDead()) return;
    //effect.to->gainMark("&mobilexinli+#" + effect.from->objectName());
    LogMessage log;
    log.type = "#GetMark";
    log.from = effect.to;
    log.arg = "mobilexinli";
    log.arg2 = QString::number(1);
    room->sendLog(log);
    room->addPlayerMark(effect.to, "&mobilexinli+#" + effect.from->objectName());
}

class MobileXinMouliVS : public ViewAsSkillV2
{
public:
    MobileXinMouliVS() : ViewAsSkillV2("mobilexinmouli", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileXinMouliCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->isVirtualCard()
            && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.use_card || !target || target->isDead()) return ContinueEffects;
        Room *room = target->getRoom();
        room->giveCard(ctx.invoker, target, ctx.use_card, objectName());
        if (target->isDead()) return ContinueEffects;
        const int previous = room->getTag("MobileXinMouliSerial").toInt();
        if (previous == INT_MAX) return ContinueEffects;
        room->setTag("MobileXinMouliSerial", previous + 1);
        const QVariantMap baseline = room->queryHistoryFacts({{"kind", "use_card"}, {"limit", 1}});
        QVariantMap receipt{{"serial", previous + 1}, {"owner", ctx.owner->objectName()},
            {"actor", ctx.invoker->objectName()}, {"amount", getEffectiveAmount(ctx)}, {"used", false},
            {"after", baseline.value("watermark")}, {"known", !baseline.contains("error") && baseline.value("complete").toBool()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        // Publish the exact effect grant before acquisition hooks can use the granted conversion.
        room->acquireSkillFromEffect(target, "mobilexinmouli_effect", ctx, [room, target, receipt](int id) mutable {
            receipt["grant_id"] = id;
            QVariantList receipts = target->getTag("MobileXinMouliReceipts").toList();
            receipts << receipt; target->setTag("MobileXinMouliReceipts", receipts);
            room->addPlayerMark(target, "&mobilexinli+#" + receipt.value("actor").toString());
        });
        return ContinueEffects;
    }
};

class MobileXinMouli : public TriggerSkillV2
{
public:
    MobileXinMouli() : TriggerSkillV2("mobilexinmouli")
    {
        global = true; frequency = Compulsory;
        events << EventPhaseStart << CardFinished << PostCardResponded;
        view_as_skill = new MobileXinMouliVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::RoundStart) return true;
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            QVariantList kept, expired;
            for (const QVariant &value : holder->getTag("MobileXinMouliReceipts").toList())
                (value.toMap().value("actor").toString() == player->objectName() ? expired : kept) << value;
            holder->setTag("MobileXinMouliReceipts", kept);
            if (expired.isEmpty()) continue;
            room->setPlayerMark(holder, "&mobilexinli+#" + player->objectName(), 0);
            for (const QVariant &value : expired) {
                const int id = value.toMap().value("grant_id").toInt();
                if (id > 0 && holder->hasSkillInstance("mobilexinmouli_effect", id))
                    room->detachSkillFromPlayer(holder, SkillInstanceUtils::formatName("mobilexinmouli_effect", id), false, true);
            }
        }
        return true;
    }
    static qint64 firstUse(Room *room, ServerPlayer *actor, const QVariantMap &receipt, const QVariant &watermark)
    {
        if (!receipt.value("known").toBool()) return 0;
        qint64 firstSequence = 0, firstEvent = 0;
        for (const QString &kind : QStringList{"use_card", "respond_card"}) {
            QVariantMap filter{{"kind", kind}, {kind == "use_card" ? "from" : "player", actor->objectName()},
                {"after", receipt.value("after")}, {"watermark", watermark}, {"limit", 128}};
            for (;;) {
                const QVariantMap page = room->queryHistoryFacts(filter);
                if (page.contains("error") || !page.value("complete").toBool()) return 0;
                for (const QVariant &value : page.value("items").toList()) {
                    const QVariantMap fact = value.toMap(), data = fact.value("data").toMap();
                    if (kind == "respond_card" && !data.value("is_use").toBool()) continue;
                    const QVariantMap card = data.value("card").toMap();
                    if (!card.contains("classes")) return 0;
                    const QVariantList classes = card.value("classes").toList();
                    if (!classes.contains("Slash") && !classes.contains("Jink")) continue;
                    const qint64 sequence = fact.value("sequence").toLongLong();
                    if (sequence > 0 && (!firstSequence || sequence < firstSequence)) {
                        firstSequence = sequence; firstEvent = fact.value("event_id").toLongLong();
                    }
                }
                if (!page.value("has_more").toBool()) break;
                filter["after"] = page.value("next_after");
            }
        }
        return firstEvent;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        QList<QVariantMap> completed;
        if (event == PostCardResponded) completed = mobileShijiCompletedResponses(room, data.value<CardResponseStruct>());
        else if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.from) return true;
            const qint64 id = use.targetModReveal.useHistoryEventId > 0 ? use.targetModReveal.useHistoryEventId
                : room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            const QVariantMap page = room->queryHistoryFacts({{"kind", "use_card"}, {"event_id", id}, {"limit", 1}});
            if (id > 0 && !page.contains("error") && page.value("complete").toBool()
                && !page.value("has_more").toBool() && page.value("items").toList().size() == 1)
                completed << page.value("items").toList().first().toMap();
        } else return true;
        for (const QVariantMap &fact : completed) {
            const QVariantMap finished = fact.value("data").toMap();
            const QString actorName = fact.value("kind").toString() == "use_card"
                ? finished.value("from").toString() : finished.value("player").toString();
            ServerPlayer *actor = room->findPlayerByObjectName(actorName, true);
            if (!actor) continue;
            for (const QVariant &value : actor->getTag("MobileXinMouliReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                if (receipt.value("used").toBool() || firstUse(room, actor, receipt, fact.value("sequence"))
                    != fact.value("event_id").toLongLong()) continue;
                SkillContext ctx;
                ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
                ctx.initiator = actor;
                ctx.invoker = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
                ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                    SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
                if (!ctx.owner || !ctx.invoker || ctx.invoker->isDead() || !ctx.sourceRef.isValid()) continue;
                ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
                ctx.amount = 3 * receipt.value("amount").toInt(); ctx.is_forced = true;
                ctx.targets = {ctx.invoker}; ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event;
                contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.initiator && ctx.initiator->getTag("MobileXinMouliReceipts").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList receipts = ctx.initiator->getTag("MobileXinMouliReceipts").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return false;
        QVariantMap receipt = receipts.at(index).toMap(); receipt["used"] = true; receipts[index] = receipt;
        ctx.initiator->setTag("MobileXinMouliReceipts", receipts);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MobileXinMouliEffect : public ViewAsSkillV2
{
public:
    MobileXinMouliEffect() : ViewAsSkillV2("mobilexinmouli_effect", 1) { attached_lord_skill = true; response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? Slash::IsAvailable(request.initiator)
            : request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                && (request.pattern.contains("slash", Qt::CaseInsensitive) || request.pattern.contains("jink", Qt::CaseInsensitive)));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using")
            || request.initiator->isLocked(card)) return false;
        return (card->isBlack() && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
                || request.pattern.contains("slash", Qt::CaseInsensitive)))
            || (card->isRed() && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                && request.pattern.contains("jink", Qt::CaseInsensitive));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        if (!material || (!material->isRed() && !material->isBlack())) return nullptr;
        Card *card = material->isBlack() ? static_cast<Card *>(new Slash(material->getSuit(), material->getNumber()))
            : static_cast<Card *>(new Jink(material->getSuit(), material->getNumber()));
        card->addSubcard(material); card->setSkillName(objectName());
        return card;
    }
};

class MobileXinZifu : public TriggerSkillV2
{
public:
    MobileXinZifu() : TriggerSkillV2("mobilexinzifu")
    {
        events << Death;
        frequency = Compulsory;
        m_baseAmount = 2;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && death.who && death.who != player
            && death.who->getMark("&mobilexinli+#" + player->objectName()) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // The granted Li relationship is an applied public effect; each Zifu instance resolves separately.
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        room->loseMaxHp(target, getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MobileXinXunyi : public TriggerSkillV2
{
public:
    MobileXinXunyi() : TriggerSkillV2("mobilexinxunyi")
    { events << GameStart << Death << Damage << Damaged << EventLoseSkill; }
    QString partner(Room *room, const SkillContext &ctx) const
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        const ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        return holder ? holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "xunyi_target").toString() : QString();
    }
    void refreshMarks(Room *room) const
    {
        for (ServerPlayer *owner : room->getAllPlayers(true)) {
            QMap<QString, int> counts;
            for (const SkillInstance &instance : owner->getSkillInstances()) {
                const QString name = owner->getSkillInstanceStateValue(instance.skillName, instance.instanceID, "xunyi_target").toString();
                if (!name.isEmpty()) ++counts[name];
            }
            for (ServerPlayer *target : room->getAllPlayers(true))
                room->setPlayerMark(target, "&mobilexinyi+#" + owner->objectName(), counts.value(target->objectName()));
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event == EventLoseSkill) refreshMarks(room);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return {};
        if (event == GameStart)
            return player->isAlive() && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (event == EventLoseSkill || (event == Death && data.value<DeathStruct>().who != player)) return {};
        if ((event == Damage && data.value<DamageStruct>().from != player)
            || (event == Damaged && data.value<DamageStruct>().to != player)) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (!TriggerSkillV2::prepareSource(room, ctx)) return false;
        ctx.is_forced = ctx.current_event == Damage || ctx.current_event == Damaged;
        if (ctx.current_event == GameStart) return true;
        const QString name = partner(room, ctx);
        if (name.isEmpty()) return false;
        if (ctx.current_event == Death) return ctx.original_data->value<DeathStruct>().who->objectName() == name;
        return ctx.invoker == ctx.owner || (ctx.invoker && ctx.invoker->objectName() == name);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == GameStart || event == Death) {
            QList<ServerPlayer *> candidates = room->getOtherPlayers(ctx.owner);
            if (event == Death) candidates.removeOne(ctx.original_data->value<DeathStruct>().who);
            if (candidates.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(),
                event == GameStart ? "@mobilexinxunyi-invoke" : "@mobilexinxunyi-transfer", true, true);
            if (!target) return false;
            ctx.targets = {target}; ctx.choice = "relation";
            return true;
        }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *linked = room->findPlayerByObjectName(partner(room, ctx));
        ServerPlayer *recipient = ctx.invoker == ctx.owner ? linked : ctx.owner;
        if (!recipient || recipient->isDead() || recipient == (event == Damaged ? damage.from : damage.to)
            || (event == Damaged && !recipient->canDiscard(recipient, "he"))) return false;
        ctx.choice = event == Damaged ? "discard" : "draw";
        ctx.extra_data = damage.damage; ctx.targets = {recipient};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "relation") {
            const SkillInstanceRef ref = getUsageRef(ctx);
            ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
            if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID) || target == ctx.owner) return false;
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "xunyi_target", target->objectName());
            room->broadcastSkillInvoke(this);
            refreshMarks(room);
            return false;
        }
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        const int amount = getEffectiveAmount(ctx);
        for (int i = 0; i < ctx.extra_data.toInt() && target->isAlive() && amount > 0; ++i) {
            if (ctx.choice == "draw") target->drawCards(amount, objectName());
            else if (target->canDiscard(target, "he")) room->askForDiscard(target, objectName(), amount, amount, false, true);
        }
        return false;
    }
};

class MobileXinXianghai : public FilterSkill
{
public:
    MobileXinXianghai() : FilterSkill("mobilexinxianghai")
    {
    }

    bool viewFilter(const Card*to_select) const
    {
        return to_select->isKindOf("EquipCard")
		&& Sanguosha->getCardPlace(to_select->getEffectiveId()) == Player::PlaceHand;
    }

    const Card*viewAs(const Card*originalCard) const
    {
        Card*ana = new Analeptic(originalCard->getSuit(), originalCard->getNumber());
        ana->setSkillName(objectName());/*
        WrappedCard*card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(ana);*/
        return ana;
    }
};

class MobileXinXianghaiMax : public MaxCardsSkillV2
{
public:
    MobileXinXianghaiMax() : MaxCardsSkillV2("#mobilexinxianghai")
    {
        setHolderSelector(CorrectSkill_AllHolders);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // Each exact Xianghai helper contributes once to other players only.
        return ctx.holder && ctx.primary && ctx.holder != ctx.primary
            ? CorrectSkillResult::useAmount(-ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

MobileXinChuhaiCard::MobileXinChuhaiCard()
{
    setSkillName("mobilexinchuhai");
    target_fixed = true;
}

void MobileXinChuhaiCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
    source->drawCards(1, "mobilexinchuhai");
    if (source->isDead()) return;

    QList<ServerPlayer*> pindian_targets;
    foreach (ServerPlayer*p, room->getAlivePlayers()) {
        if (!source->canPindian(p)) continue;
        pindian_targets << p;
    }
    if (pindian_targets.isEmpty()) return;

    ServerPlayer*pindian_target = room->askForPlayerChosen(source, pindian_targets, "mobilexinchuhai", "@mobilexinchuhai-invoke", false);
    room->doAnimate(1, source->objectName(), pindian_target->objectName());
    if (!source->canPindian(pindian_target, false)) return;

    if (source->pindian(pindian_target, "mobilexinchuhai")) {
        if (source->isDead() || pindian_target->isDead()) return;

        room->addPlayerMark(source, "mobilexinchuhai_from-PlayClear");
        room->addPlayerMark(pindian_target, "mobilexinchuhai_to-PlayClear");

        if (!pindian_target->isKongcheng()) {
            room->doGongxin(source, pindian_target, QList<int>(), "mobilexinchuhai");

            QList<int> type_ids, get_ids;
            foreach (const Card*c, pindian_target->getHandcards()) {
                int type_id = c->getTypeId();
                if (!type_ids.contains(type_id))
                    type_ids << type_id;
            }

            foreach (int type_id, type_ids) {
                QList<int> cards = room->getDiscardPile() + room->getDrawPile(), list;
                foreach (int id, cards) {
                    if (Sanguosha->getCard(id)->getTypeId() == type_id)
                        list << id;
                }
                if (!list.isEmpty()) {
                    int id = list.at(qsanRandomBounded(list.length()));
                    get_ids << id;
                }
            }

            if (!get_ids.isEmpty()) {
                DummyCard get(get_ids);
                room->obtainCard(source, &get, true);
            }
        }
    }
}

class MobileXinChuhaiVS : public ViewAsSkillV2
{
public:
    MobileXinChuhaiVS() : ViewAsSkillV2("mobilexinchuhai") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileXinChuhaiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        SkillContext draw = ctx; draw.choice = "draw"; skillEffect(draw, ctx.invoker);
        if (ctx.invoker->isDead()) return ContinueEffects;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.invoker))
            if (ctx.invoker->canPindian(other)) candidates << other;
        if (candidates.isEmpty()) return ContinueEffects;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "@mobilexinchuhai-invoke");
        if (target) { SkillContext duel = ctx; duel.choice = "pindian"; skillEffect(duel, target); }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return ContinueEffects; }
        if (ctx.choice == "reward") {
            const QVariantList types = ctx.extra_data.toList();
            QList<int> ids;
            for (const QVariant &type : types) {
                QList<int> candidates;
                for (int id : room->getDiscardPile() + room->getDrawPile())
                    if (Sanguosha->getCard(id)->getTypeId() == type.toInt()) candidates << id;
                qsanShuffle(candidates);
                for (int i = 0; i < getEffectiveAmount(ctx) && i < candidates.size(); ++i) ids << candidates.at(i);
            }
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, true); }
            return ContinueEffects;
        }
        if (!ctx.invoker->canPindian(target, false) || !ctx.invoker->pindian(target, objectName())
            || ctx.invoker->isDead() || target->isDead()) return ContinueEffects;
        const QVariantMap scopes = room->historyScopes();
        const qint64 phase = scopes.value("phase_id").toLongLong();
        const qint64 previous = room->getTag("MobileXinChuhaiSequence").toLongLong();
        if (phase > 0 && previous >= 0 && previous < INT_MAX) {
            const int serial = int(previous + 1); room->setTag("MobileXinChuhaiSequence", serial);
            QVariantList receipts = room->getTag("MobileXinChuhaiReceipts").toList();
            receipts << QVariantMap{{"serial", serial}, {"owner", ctx.owner->objectName()}, {"actor", ctx.invoker->objectName()},
                {"victim", target->objectName()}, {"amount", getEffectiveAmount(ctx)}, {"phase_id", phase}, {"turn_id", scopes.value("turn_id")},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            room->setTag("MobileXinChuhaiReceipts", receipts);
        }
        if (!target->isKongcheng()) {
            room->doGongxin(ctx.invoker, target, QList<int>(), objectName());
            QVariantList types;
            for (const Card *card : target->getHandcards()) if (!types.contains(int(card->getTypeId()))) types << int(card->getTypeId());
            if (ctx.invoker->isAlive()) {
                SkillContext reward = ctx; reward.choice = "reward"; reward.extra_data = types;
                skillEffect(reward, ctx.invoker);
            }
        }
        return ContinueEffects;
    }
};

class MobileXinChuhai : public TriggerSkillV2
{
public:
    MobileXinChuhai() : TriggerSkillV2("mobilexinchuhai")
    {
        global = true; frequency = Compulsory;
        events << Damage << EventPhaseEnd << EventPhaseChanging << Death;
        view_as_skill = new MobileXinChuhaiVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const bool ending = event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive;
        if (event != EventPhaseEnd && event != Death && !ending) return true;
        const QVariantMap scopes = room->historyScopes();
        QVariantList receipts;
        for (const QVariant &value : room->getTag("MobileXinChuhaiReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            const bool expired = event == EventPhaseEnd
                ? receipt.value("phase_id").toLongLong() == scopes.value("phase_id").toLongLong()
                : ending ? receipt.value("turn_id") == scopes.value("turn_id")
                : player && (receipt.value("actor").toString() == player->objectName() || receipt.value("victim").toString() == player->objectName());
            if (!expired) receipts << value;
        }
        room->setTag("MobileXinChuhaiReceipts", receipts);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != Damage || !player || player->isDead()) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from != player || !damage.to) return true;
        const qint64 phase = room->historyScopes().value("phase_id").toLongLong();
        for (const QVariant &value : room->getTag("MobileXinChuhaiReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("actor").toString() != player->objectName() || receipt.value("victim").toString() != damage.to->objectName()
                || receipt.value("phase_id").toLongLong() != phase) continue;
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = player; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt(); ctx.is_forced = true;
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt; ctx.targets = {player};
            ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return room->getTag("MobileXinChuhaiReceipts").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            QList<int> candidates;
            for (int id : room->getDiscardPile() + room->getDrawPile()) {
                const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
                if (equip && !target->getEquip(equip->location()) && target->hasEquipArea(equip->location())) candidates << id;
            }
            if (candidates.isEmpty()) break;
            const int id = candidates.at(qsanRandomBounded(candidates.size()));
            room->moveCardTo(Sanguosha->getCard(id), nullptr, target, Player::PlaceEquip,
                CardMoveReason(CardMoveReason::S_REASON_PUT, target->objectName(), objectName(), ""));
        }
        return false;
    }
};

class MobileXinMingshi : public TriggerSkillV2
{
public:
    MobileXinMingshi() : TriggerSkillV2("mobilexinmingshi")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && damage.damage > 0
            && damage.from && damage.from->isAlive() && damage.from->canDiscard(damage.from, "he")
            ? TriggerList{{player, {objectName() + "*" + QString::number(damage.damage)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *attacker = ctx.original_data->value<DamageStruct>().from;
        if (!attacker || attacker->isDead() || !attacker->canDiscard(attacker, "he")) return false;
        ctx.targets = {attacker};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        const int count = getEffectiveAmount(ctx);
        if (count > 0) room->askForDiscard(target, objectName(), count, count, false, true);
        return false;
    }
};

MobileXinLirangCard::MobileXinLirangCard()
{
    setSkillName("mobilexinlirang");
    target_fixed = true;
}

void MobileXinLirangCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
    QList<int> hands = source->handCards(), handcards;
    source->throwAllHandCards(getSkillName());
    if (source->isDead()) return;

    foreach (int id, hands) {
        if (room->getCardPlace(id) == Player::DiscardPile)
            handcards << id;
    }
    if (handcards.isEmpty()) return;

    int hp = source->getHp();
    if (hp < 1) return;

    room->setPlayerFlag(source, "mobilexinlirang_InTempMoving");

    CardMoveReason r(CardMoveReason::S_REASON_UNKNOWN, source->objectName());
    CardsMoveStruct fake_move(handcards, nullptr, source, Player::DiscardPile, Player::PlaceHand, r);
    QList<CardsMoveStruct> moves;
    moves << fake_move;
    QList<ServerPlayer*> _source;
    _source << source;
    room->notifyMoveCards(true, moves, true, _source);
    room->notifyMoveCards(false, moves, true, _source);

    int num = qMin(hp, handcards.length());
    QList<int> ids = room->askForyiji(source, handcards, "mobilexinlirang", false, true, true, num,
                                      room->getOtherPlayers(source), CardMoveReason(), "@mobilexinlirang-give:" + QString::number(num));

    foreach (int id, ids)
        handcards.removeOne(id);
    if (!ids.isEmpty()) {
        CardsMoveStruct move(ids, source, nullptr, Player::PlaceHand, Player::DiscardPile,
            CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, source->objectName(), "mobilexinlirang", ""));
        QList<CardsMoveStruct> moves;
        moves.append(move);
        room->notifyMoveCards(true, moves, false, _source);
        room->notifyMoveCards(false, moves, false, _source);
    }

    if (!handcards.isEmpty()) {
        CardsMoveStruct fake_move2(handcards, source, nullptr, Player::PlaceHand, Player::DiscardPile, r);
        QList<CardsMoveStruct> moves2;
        moves2 << fake_move2;
        room->notifyMoveCards(true, moves2, true, _source);
        room->notifyMoveCards(false, moves2, true, _source);
    }

    room->setPlayerFlag(source, "-mobilexinlirang_InTempMoving");

    if (ids.isEmpty()) return;
    source->drawCards(1, "mobilexinlirang");
}

class MobileXinLirang : public ViewAsSkillV2
{
public:
    MobileXinLirang() : ViewAsSkillV2("mobilexinlirang") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileXinLirangCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h");
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || !ctx.initiator->canDiscard(ctx.initiator, "h")) return false;
        ctx.extra_data = ListI2V(ctx.initiator->handCards());
        ctx.initiator->throwAllHandCards(objectName());
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (!ctx.invoker || ctx.invoker->isDead()) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        QList<int> cards;
        for (int id : ListV2I(ctx.extra_data.toList()))
            if (room->getCardPlace(id) == Player::DiscardPile) cards << id;
        const int maximum = qMin(qMax(0, ctx.invoker->getHp()), cards.size());
        bool given = false;
        for (int i = 0; i < maximum && !cards.isEmpty() && ctx.invoker->isAlive(); ++i) {
            const QList<int> remaining = cards;
            for (int id : remaining)
                if (room->getCardPlace(id) != Player::DiscardPile) cards.removeAll(id);
            if (cards.isEmpty()) break;
            int id = -1;
            {
                room->fillAG(cards, ctx.invoker);
                auto clear = qScopeGuard([&] { room->clearAG(ctx.invoker); });
                id = room->askForAG(ctx.invoker, cards, true, objectName());
            }
            if (!cards.contains(id)) break;
            ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getOtherPlayers(ctx.invoker),
                objectName(), "@mobilexinlirang-give:" + QString::number(maximum - i), true, true);
            if (!target) break;
            cards.removeAll(id);
            SkillContext transfer = ctx;
            transfer.choice = "give"; transfer.extra_data = QVariantMap{{"card_id", id}};
            skillEffect(transfer, target);
            given = given || transfer.extra_data.toMap().value("given").toBool();
        }
        if (given && ctx.invoker->isAlive()) {
            SkillContext reward = ctx;
            reward.choice = "draw";
            skillEffect(reward, ctx.invoker);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return ContinueEffects;
        }
        Room *room = ctx.invoker->getRoom();
        QVariantMap state = ctx.extra_data.toMap();
        const int id = state.value("card_id", -1).toInt();
        if (id < 0 || room->getCardPlace(id) != Player::DiscardPile) return ContinueEffects;
        room->obtainCard(target, Sanguosha->getCard(id),
            CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), target->objectName(), objectName(), ""), true);
        state.insert("given", true); ctx.extra_data = state;
        return ContinueEffects;
    }
};

class MobileXinMingfa : public TriggerSkillV2
{
public:
    MobileXinMingfa() : TriggerSkillV2("mobilexinmingfa") { events << EventPhaseStart << PindianVerifying; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { ctx.is_forced = ctx.current_event == PindianVerifying; return TriggerSkillV2::prepareSource(room, ctx); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseStart)
            return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish && !player->isNude()
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        const PindianStruct *pindian = data.value<PindianStruct *>();
        TriggerList list;
        if (!pindian || player != pindian->from) return list;
        for (ServerPlayer *owner : QList<ServerPlayer *>{pindian->from, pindian->to})
            if (owner && owner->isAlive() && owner->hasSkill(objectName())) list[owner] << objectName();
        return list;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        if (event == PindianVerifying) return true;
        const Card *card = room->askForCard(ctx.owner, "..", "@mobilexinmingfa-show", QVariant(), Card::MethodNone);
        if (!card) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == PindianVerifying) {
            PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
            if (!pindian) return false;
            int *number = target == pindian->from ? &pindian->from_number : target == pindian->to ? &pindian->to_number : nullptr;
            if (!number) return false;
            *number = qMin(13, *number + 2 * getEffectiveAmount(ctx));
            ctx.original_data->setValue(pindian);
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            return false;
        }
        const int id = ctx.extra_data.toInt();
        if (target != ctx.owner || room->getCardOwner(id) != target) return false;
        const qint64 previous = room->getTag("MobileXinMingfaSequence").toLongLong();
        if (previous < 0 || previous >= INT_MAX) return false;
        const int serial = int(previous + 1); room->setTag("MobileXinMingfaSequence", serial);
        QVariantList receipts;
        for (const QVariant &value : target->getTag("MobileXinMingfaReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("activation_owner").toString() != ctx.activationRef.ownerObjectName
                || receipt.value("activation_skill").toString() != ctx.activationRef.key.skillName
                || receipt.value("activation_id").toInt() != ctx.activationRef.key.instanceID) receipts << value;
        }
        receipts << QVariantMap{{"serial", serial}, {"card_id", id}, {"owner", ctx.owner->objectName()}, {"amount", getEffectiveAmount(ctx)},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("MobileXinMingfaReceipts", receipts);
        room->broadcastSkillInvoke(this); room->showCard(target, id);
        return false;
    }
};

class MobileXinMingfaPindian : public TriggerSkillV2
{
public:
    MobileXinMingfaPindian() : TriggerSkillV2("#mobilexinmingfa-pindian")
    { global = true; frequency = Compulsory; events << EventPhaseStart << EventPhaseEnd << EventPhaseChanging << Death; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        QVariantMap due = player->getTag("MobileXinMingfaDue").toMap();
        const qint64 phase = room->historyScopes().value("phase_id").toLongLong();
        if (event == EventPhaseStart && player->getPhase() == Player::Play && phase > 0) {
            due[QString::number(phase)] = player->getTag("MobileXinMingfaReceipts").toList();
            player->removeTag("MobileXinMingfaReceipts");
        } else if (event == EventPhaseEnd && player->getPhase() == Player::Play) due.remove(QString::number(phase));
        else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            for (auto it = due.begin(); it != due.end(); ) {
                if (room->historyEvent(it.key().toLongLong()).value("turn_id").toLongLong() == turn) it = due.erase(it);
                else ++it;
            }
        } else if (event == Death && data.value<DeathStruct>().who == player) {
            due.clear(); player->removeTag("MobileXinMingfaReceipts");
        }
        player->setTag("MobileXinMingfaDue", due);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->isDead() || player->getPhase() != Player::Play) return true;
        const QString phase = room->historyScopes().value("phase_id").toString();
        for (const QVariant &value : player->getTag("MobileXinMingfaDue").toMap().value(phase).toList()) {
            const QVariantMap receipt = value.toMap();
            const int id = receipt.value("card_id", -1).toInt();
            if (id < 0 || !player->hasCard(id)) continue;
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = player; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt(); ctx.is_forced = true;
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = QVariantMap{{"receipt", receipt}, {"phase", phase}};
            ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.initiator && ctx.initiator->getTag("MobileXinMingfaDue").toMap()
            .value(ctx.extra_data.toMap().value("phase").toString()).toList().contains(ctx.extra_data.toMap().value("receipt"));
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.invoker))
            if (ctx.invoker->canPindian(other)) candidates << other;
        if (candidates.isEmpty()) return false;
        room->fillAG({ctx.extra_data.toMap().value("receipt").toMap().value("card_id").toInt()}, ctx.invoker);
        const auto clear = qScopeGuard([&] { room->clearAG(ctx.invoker); });
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, candidates, "mobilexinmingfa", "@mobilexinmingfa-pindian", false, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap state = ctx.extra_data.toMap();
        if (ctx.choice == "reward") {
            ServerPlayer *victim = room->findPlayerByObjectName(state.value("victim").toString());
            for (int i = 0; i < getEffectiveAmount(ctx) && victim && victim->isAlive() && !victim->isNude() && target->isAlive(); ++i) {
                const int id = room->askForCardChosen(ctx.invoker, victim, "he", "mobilexinmingfa");
                if (id >= 0 && room->getCardOwner(id) == victim) room->obtainCard(target, id, false);
            }
            QList<int> cards;
            for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->getNumber() == state.value("number").toInt()) cards << id;
            qsanShuffle(cards);
            QList<int> ids;
            for (int i = 0; i < getEffectiveAmount(ctx) && i < cards.size(); ++i) ids << cards.at(i);
            if (target->isAlive() && !ids.isEmpty()) { DummyCard gain(ids); room->obtainCard(target, &gain); }
            return false;
        }
        const QVariantMap receipt = state.value("receipt").toMap();
        const int id = receipt.value("card_id").toInt();
        QVariantMap due = ctx.initiator->getTag("MobileXinMingfaDue").toMap();
        QVariantList receipts = due.value(state.value("phase").toString()).toList(); receipts.removeAll(receipt);
        due[state.value("phase").toString()] = receipts; ctx.initiator->setTag("MobileXinMingfaDue", due);
        if (!ctx.invoker->hasCard(id) || !ctx.invoker->canPindian(target)) return false;
        const PindianStruct *pindian = ctx.invoker->PinDian(target, "mobilexinmingfa", Sanguosha->getCard(id));
        if (!pindian || ctx.invoker->isDead()) return false;
        if (pindian->success) {
            SkillContext reward = ctx; reward.choice = "reward";
            QVariantMap rewardState = state; rewardState["victim"] = target->objectName();
            rewardState["number"] = Sanguosha->getEngineCard(pindian->from_card->getEffectiveId())->getNumber() - 1;
            reward.extra_data = rewardState;
            skillEffect(event, room, ctx.owner, reward, ctx.invoker);
        } else room->addPlayerMark(ctx.invoker, "mobilexinmingfa-Clear");
        return false;
    }
};

class MobileXinMingfaPro : public ProhibitSkill
{
public:
    MobileXinMingfaPro() : ProhibitSkill("#mobilexinmingfa-pro")
    {
    }

    bool isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*> &) const
    {
        return from->getMark("mobilexinmingfa-Clear") > 0 && from != to && !card->isKindOf("SkillCard");
    }
};

MobileXinRongbeiCard::MobileXinRongbeiCard()
{
    setSkillName("mobilexinrongbei");
}

bool MobileXinRongbeiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
    return targets.isEmpty() && to_select->getEquips().length() < S_EQUIP_AREA_LENGTH;
}

void MobileXinRongbeiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer*to = effect.to;
    Room*room = to->getRoom();

    room->removePlayerMark(effect.from, "@mobilexinrongbeiMark");
    room->doSuperLightbox(effect.from, "mobilexinrongbei");

    QList<int> areas;

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (to->getEquip(i)) continue;
        areas << i;
    }
    if (areas.isEmpty()) return;

    QList<const EquipCard*> equips;
    foreach (int id, room->getDrawPile()) {
        const Card*card = Sanguosha->getCard(id);
        if (!card->isKindOf("EquipCard")) continue;
        const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
        int equip_index = static_cast<int>(equip->location());
        if (to->getEquip(equip_index)) continue;
        equips << equip;
    }
    if (equips.isEmpty()) return;

    for (int i = 0; i < areas.length(); i++) {
        if (to->isDead()) return;
        int area = areas.at(i);

        QList<const Card*> equip_cards;
        foreach (const EquipCard*ec, equips) {
            int equip_index = static_cast<int>(ec->location());
            if (equip_index == area)
                equip_cards << ec;
        }
        if (equip_cards.isEmpty()) continue;
        const Card*equip = equip_cards.at(qsanRandomBounded(equip_cards.length()));
        room->obtainCard(to, equip);
        if (to->isAlive() && to->hasEquipArea(area) && !to->isLocked(equip, true) && !to->isProhibited(to, equip))
            room->useCard(CardUseStruct(equip, to, to));
    }
}

class MobileXinRongbei : public ViewAsSkillV2
{
public:
    MobileXinRongbei() : ViewAsSkillV2("mobilexinrongbei")
    {
        frequency = Limited;
        limit_mark = "@mobilexinrongbeiMark";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileXinRongbeiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    {
        return target && selected.isEmpty() && target->getEquips().size() < S_EQUIP_AREA_LENGTH;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        room->removePlayerMark(ctx.invoker, "@mobilexinrongbeiMark");
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        room->doSuperLightbox(ctx.invoker, objectName());
        QList<int> emptyAreas;
        for (int area = 0; area < S_EQUIP_AREA_LENGTH; ++area)
            if (!target->getEquip(area)) emptyAreas << area;
        for (int area : emptyAreas) {
            if (target->isDead()) break;
            QList<int> candidates;
            // Earlier equipment use can move the deck; never reuse stale card pointers.
            for (int id : room->getDrawPile()) {
                const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
                if (equip && int(equip->location()) == area) candidates << id;
            }
            if (candidates.isEmpty()) continue;
            const int id = candidates.at(qsanRandomBounded(candidates.size()));
            room->obtainCard(target, id);
            const Card *equip = Sanguosha->getCard(id);
            if (target->isAlive() && target->hasEquipArea(area) && room->getCardOwner(id) == target
                && room->getCardPlace(id) == Player::PlaceHand && !target->isLocked(equip, true)
                && !target->isProhibited(target, equip))
                room->useCardFromSkillEffect(CardUseStruct(equip, target, target), ctx, true);
        }
        return ContinueEffects;
    }
};

class SecondMobileXinXingqi : public TriggerSkillV2
{
public:
    SecondMobileXinXingqi() : TriggerSkillV2("secondmobilexinxingqi") { events << CardUsed << EventPhaseStart; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { ctx.is_forced = ctx.current_event == CardUsed; return TriggerSkillV2::prepareSource(room, ctx); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseStart)
            return player->getPhase() == Player::Finish && !player->property("second_mobilexin_wangling_bei").toString().isEmpty()
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->isKindOf("DelayedTrick") || use.card->isKindOf("SkillCard")) return {};
        const QString name = use.card->isKindOf("Slash") ? "slash" : use.card->objectName();
        return !player->property("second_mobilexin_wangling_bei").toString().split("+").contains(name)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        if (event == CardUsed) {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            if (!card) return false;
            ctx.choice = "record"; ctx.extra_data = card->isKindOf("Slash") ? "slash" : card->objectName();
            return true;
        }
        const QString bei = ctx.owner->property("second_mobilexin_wangling_bei").toString();
        if (bei.isEmpty() || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.choice = "obtain"; ctx.extra_data = room->askForChoice(ctx.owner, objectName(), bei);
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "record") return true;
        QStringList names = ctx.owner->property("second_mobilexin_wangling_bei").toString().split("+", Qt::SkipEmptyParts);
        if (!names.removeOne(ctx.extra_data.toString())) return false;
        room->setPlayerProperty(ctx.owner, "second_mobilexin_wangling_bei", names.join("+"));
        LogMessage log; log.type = "#SecondMobileXinXingqiRemove"; log.from = ctx.owner;
        log.arg = ctx.extra_data.toString(); room->sendLog(log);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QString name = ctx.extra_data.toString();
        if (ctx.choice == "record") {
            QStringList names = target->property("second_mobilexin_wangling_bei").toString().split("+", Qt::SkipEmptyParts);
            if (names.contains(name)) return false;
            names << name; room->setPlayerProperty(target, "second_mobilexin_wangling_bei", names.join("+"));
            LogMessage log; log.type = "#SecondMobileXinXingqiLog"; log.from = target; log.arg = name; room->sendLog(log);
        } else {
            QList<int> ids;
            for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->sameNameWith(name)) ids << id;
            qsanShuffle(ids);
            while (ids.size() > qMax(0, getEffectiveAmount(ctx))) ids.removeLast();
            if (!ids.isEmpty()) { DummyCard reward(ids); room->obtainCard(target, &reward); }
        }
        return false;
    }
};

class SecondMobileXinZifu : public TriggerSkillV2
{
public:
    SecondMobileXinZifu() : TriggerSkillV2("secondmobilexinzifu")
    { events << EventPhaseEnd; frequency = Compulsory; }
    bool unused(Room *room, ServerPlayer *owner) const
    {
        const QVariantMap history = room->queryCardHistory(owner, "phase");
        return !history.contains("error") && history.value("complete").toBool()
            && history.value("items").toList().isEmpty();
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Play && unused(room, player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!unused(room, ctx.owner)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        room->addMaxCards(target, -getEffectiveAmount(ctx));
        // Bei names remain this character's public game resource shared with Xingqi/Mibei.
        room->setPlayerProperty(target, "second_mobilexin_wangling_bei", "");
        return false;
    }
};

class SecondMobileXinMibei : public TriggerSkillV2
{
public:
    SecondMobileXinMibei() : TriggerSkillV2("secondmobilexinmibei")
    {
        events << EventPhaseStart << EventPhaseEnd << CardFinished << EventSkillInvoking;
        shiming_skill = true; waked_skills = "secondmobilexinmouli"; frequency = NotCompulsory;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { ctx.is_forced = true; return TriggerSkillV2::prepareSource(room, ctx); }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (event == EventSkillInvoking && ctx.original_data) {
            const SkillContext accepted = ctx.original_data->value<SkillContext>();
            if (accepted.executionID == 0 && accepted.bypass_cost && accepted.sourceRef == ctx.sourceRef
                && accepted.activationRef == ctx.activationRef && parseSkillName(accepted.skill_name) == objectName())
                room->sendShimingLog(getUsageRef(accepted), accepted.choice == "success", accepted.choice == "success" ? 1 : 2);
            return;
        }
        if (event != EventPhaseStart || player != ctx.owner || !holder
            || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        const QString turn = room->historyScopes().value("turn_id").toString();
        if (turn.isEmpty() || turn == "0") return;
        QVariantMap starts = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mibei_starts").toMap();
        if (player->getPhase() == Player::Start)
            starts[turn] = player->property("second_mobilexin_wangling_bei").toString().isEmpty();
        else if (player->getPhase() == Player::NotActive) starts.remove(turn);
        else return;
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mibei_starts", starts);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        const bool eligible = event == EventPhaseEnd ? player->getPhase() == Player::Discard
            : event == CardFinished && data.value<CardUseStruct>().card
                && !data.value<CardUseStruct>().card->isKindOf("SkillCard");
        return eligible ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (room->getShimingStatus(ref) > 0) return false;
        ctx.targets = {ctx.owner};
        const QStringList names = ctx.owner->property("second_mobilexin_wangling_bei").toString().split("+", Qt::SkipEmptyParts);
        if (event == EventPhaseEnd) {
            const ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
            if (!holder || !names.isEmpty()) return false;
            const QVariantMap starts = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mibei_starts").toMap();
            const QString turn = room->historyScopes().value("turn_id").toString();
            // Missing snapshot means unknown, including an instance acquired after this turn's Start.
            if (!starts.contains(turn) || !starts.value(turn).toBool()) return false;
            ctx.choice = "failure"; return true;
        }
        QHash<QString, int> counts;
        for (const QString &name : names) {
            const std::unique_ptr<Card> card(Sanguosha->cloneCard(name));
            if (card) ++counts[card->getType()];
        }
        if (counts.value("basic") < 2 || counts.value("equip") < 2 || counts.value("trick") < 2) return false;
        ctx.choice = "success"; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->sendShimingLog(getUsageRef(ctx), ctx.choice == "success", ctx.choice == "success" ? 1 : 2);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "failure") {
            room->loseMaxHp(target, getEffectiveAmount(ctx), objectName()); return false;
        }
        QHash<QString, int> counts;
        QList<int> ids;
        for (int id : room->getDrawPile()) {
            const QString type = Sanguosha->getCard(id)->getType();
            if ((type == "basic" || type == "equip" || type == "trick") && counts.value(type) < getEffectiveAmount(ctx)) {
                ids << id; ++counts[type];
            }
        }
        if (!ids.isEmpty()) { DummyCard reward(ids); room->obtainCard(target, &reward); }
        if (target->isAlive()) room->acquireSkillFromEffect(target, "secondmobilexinmouli", ctx);
        return false;
    }
};

SecondMobileXinMouliCard::SecondMobileXinMouliCard()
{
    setSkillName("secondmobilexinmouli");
}

void SecondMobileXinMouliCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer*from = effect.from,*to = effect.to;
    if (from->isDead()) return;
    QString bei = from->property("second_mobilexin_wangling_bei").toString();
    if (bei.isEmpty()) return;

    QStringList beis = bei.split("+");
    Room*room = from->getRoom();

    QString choice = room->askForChoice(to, "secondmobilexinmouli", bei);
    beis.removeOne(choice);
    room->setPlayerProperty(from, "second_mobilexin_wangling_bei", beis.join("+"));

    LogMessage log;
    log.type = "#SecondMobileXinXingqiRemove";
    log.from = from;
    log.arg = choice;
    room->sendLog(log);

    foreach (int id, room->getDrawPile()) {
        if (Sanguosha->getCard(id)->sameNameWith(choice)){
			room->obtainCard(to, id);
			break;
		}
    }
}

class SecondMobileXinMouli : public ViewAsSkillV2
{
public:
    SecondMobileXinMouli() : ViewAsSkillV2("secondmobilexinmouli") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "SecondMobileXinMouliCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->property("second_mobilexin_wangling_bei").toString().isEmpty();
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    { return target && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        QStringList names = ctx.invoker->property("second_mobilexin_wangling_bei").toString().split("+", Qt::SkipEmptyParts);
        if (names.isEmpty()) return ContinueEffects;
        const QString choice = room->askForChoice(target, objectName(), names.join("+"));
        names = ctx.invoker->property("second_mobilexin_wangling_bei").toString().split("+", Qt::SkipEmptyParts);
        // Bei is a shared character resource; recipient choice and removal form this applied effect.
        if (!names.removeOne(choice)) return ContinueEffects;
        room->setPlayerProperty(ctx.invoker, "second_mobilexin_wangling_bei", names.join("+"));
        LogMessage log; log.type = "#SecondMobileXinXingqiRemove"; log.from = ctx.invoker; log.arg = choice; room->sendLog(log);
        QList<int> ids;
        for (int id : room->getDrawPile()) {
            if (ids.size() >= qMax(0, getEffectiveAmount(ctx))) break;
            if (Sanguosha->getCard(id)->sameNameWith(choice)) ids << id;
        }
        if (!ids.isEmpty()) { DummyCard reward(ids); room->obtainCard(target, &reward); }
        return ContinueEffects;
    }
};

class XinDulie : public TriggerSkillV2
{
public:
    XinDulie() : TriggerSkillV2("xindulie")
    {
        events << TargetConfirming;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.to.contains(player)
            && use.card && use.card->isKindOf("Slash") && use.from && use.from->getHp() > player->getHp()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data->value<CardUseStruct>().to.contains(target)) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        ctx.owner->peiyin("dulie");
        JudgeStruct judge;
        judge.who = target;
        judge.reason = objectName();
        judge.pattern = ".|heart";
        judge.good = true;
        room->judge(judge);
        if (judge.isGood()) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            use.to.removeOne(target);
            *ctx.original_data = QVariant::fromValue(use);
        }
        return false;
    }
};

class XinPowei : public TriggerSkillV2
{
public:
    XinPowei() : TriggerSkillV2("xinpowei")
    {
        shiming_skill = true;
        global = true;
        events << GameStart << Damaged << EventPhaseStart << Dying << Death << EventSkillInvoking;
        waked_skills = "xinshenzhuo";
    }
    QStringList targets(Room *room, const SkillInstanceRef &ref) const
    {
        const ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        return holder ? holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "powei_targets").toStringList()
                      : QStringList();
    }
    void refreshMarks(Room *room) const
    {
        QStringList marked;
        for (ServerPlayer *holder : room->getAllPlayers(true))
            for (const SkillInstance &instance : holder->getSkillInstances())
                marked << holder->getSkillInstanceStateValue(instance.skillName, instance.instanceID, "powei_targets").toStringList();
        for (ServerPlayer *target : room->getAllPlayers(true))
            room->setPlayerMark(target, "&stscdlwei", marked.contains(target->objectName()) ? 1 : 0);
    }
    void setTargets(Room *room, const SkillInstanceRef &ref, const QStringList &names) const
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "powei_targets", names);
        refreshMarks(room);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.executionID == 0 && accepted.bypass_cost && accepted.skill_name == objectName()
                && accepted.activationRef.isValid() && accepted.sourceRef.isValid()
                && (accepted.choice == "success" || accepted.choice == "failure")) {
                if (room->sendShimingLog(getUsageRef(accepted), accepted.choice == "success"))
                    setTargets(room, getUsageRef(accepted), {});
            }
            return true;
        }
        if (!player) return true;
        if (event == Damaged && data.value<DamageStruct>().to == player) {
            // Marks belong to each activation instance, including borrowed instances.
            for (ServerPlayer *holder : room->getAllPlayers(true))
                for (const SkillInstance &instance : holder->getSkillInstances()) {
                    QStringList names = holder->getSkillInstanceStateValue(instance.skillName, instance.instanceID, "powei_targets").toStringList();
                    if (names.removeAll(player->objectName()) > 0)
                        holder->setSkillInstanceStateValue(instance.skillName, instance.instanceID, "powei_targets", names);
                }
            refreshMarks(room);
        }
        const bool expires = event == EventPhaseStart && player->getPhase() == Player::NotActive;
        if (expires || event == Death) {
            const QVariant turn = room->historyScopes().value("turn_id");
            QVariantList kept, removed;
            for (const QVariant &value : room->getTag("XinPoweiRangeReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                if ((expires && receipt.value("turn_id") == turn)
                    || (event == Death && (receipt.value("from").toString() == player->objectName()
                        || receipt.value("to").toString() == player->objectName()))) removed << value;
                else kept << value;
            }
            // Commit before native updates: nested effects must not be overwritten.
            room->setTag("XinPoweiRangeReceipts", kept);
            for (const QVariant &value : removed) {
                const QVariantMap receipt = value.toMap();
                ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString(), true);
                ServerPlayer *to = room->findPlayerByObjectName(receipt.value("to").toString(), true);
                if (from && to) room->removeAttackRangePair(from, to);
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead()) return {};
        TriggerList result;
        if (event == GameStart || (event == Dying && data.value<DyingStruct>().who == player)) {
            if (player->hasSkill(objectName())) result[player] << objectName();
        } else if (event == EventPhaseStart && player->getPhase() == Player::RoundStart) {
            for (ServerPlayer *holder : room->getAlivePlayers())
                if (holder->hasSkill(objectName())) result[holder] << objectName();
        }
        return result;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (!TriggerSkillV2::prepareSource(room, ctx) || room->getShimingStatus(getUsageRef(ctx)) != 0) return false;
        ctx.is_forced = ctx.owner == ctx.invoker;
        if (!ctx.is_forced && !targets(room, getUsageRef(ctx)).contains(ctx.invoker->objectName())) return false;
        return true;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.invoker = ctx.owner;
        ctx.targets = {ctx.owner};
        if (event == GameStart) { ctx.choice = "initial"; return true; }
        if (event == Dying) { ctx.choice = "failure"; return true; }
        if (player != ctx.owner) {
            if (!targets(room, getUsageRef(ctx)).contains(player->objectName())
                || !ctx.owner->askForSkillInvoke(this, player)) return false;
            ctx.choice = "interact"; ctx.targets = {player};
            return true;
        }
        const QStringList names = targets(room, getUsageRef(ctx));
        QStringList moved;
        for (ServerPlayer *target : room->getAlivePlayers()) {
            if (!names.contains(target->objectName())) continue;
            ServerPlayer *next = target->getNextAlive();
            if (next == ctx.owner) next = next->getNextAlive();
            if (next != ctx.owner && !moved.contains(next->objectName())) moved << next->objectName();
        }
        ctx.choice = moved.isEmpty() ? "success" : "rotate";
        ctx.extra_data = moved;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice != "success" && ctx.choice != "failure") return true;
        if (!room->sendShimingLog(getUsageRef(ctx), ctx.choice == "success")) return false;
        setTargets(room, getUsageRef(ctx), {});
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "obtain") {
            ServerPlayer *victim = room->findPlayerByObjectName(ctx.extra_data.toMap().value("victim").toString());
            for (int i = 0; i < amount && victim && victim->isAlive() && !victim->isKongcheng()
                 && target->isAlive() && ctx.invoker->isAlive(); ++i) {
                const int id = room->askForCardChosen(ctx.invoker, victim, "h", objectName());
                if (id < 0) break;
                if (room->getCardOwner(id) == victim && room->getCardPlace(id) == Player::PlaceHand)
                    room->obtainCard(target, id, false);
            }
        } else if (ctx.choice == "success") {
            ctx.owner->peiyin("powei", 2);
            room->acquireSkillFromEffect(target, "xinshenzhuo", ctx);
        } else if (ctx.choice == "failure") {
            ctx.owner->peiyin("powei", 3);
            if (target->getHp() < amount) room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount - target->getHp()));
            if (target->isAlive()) target->throwAllEquips(objectName());
        } else if (ctx.choice == "initial" || ctx.choice == "rotate") {
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            ctx.owner->peiyin("powei", 1);
            QStringList names = ctx.extra_data.toStringList();
            if (ctx.choice == "initial")
                for (ServerPlayer *other : room->getOtherPlayers(target)) names << other->objectName();
            setTargets(room, getUsageRef(ctx), names);
        } else {
            ctx.owner->peiyin("powei", 1);
            const QVariant turn = room->historyScopes().value("turn_id");
            if (turn.toLongLong() > 0 && ctx.invoker && ctx.invoker->isAlive()) {
                QVariantList receipts = room->getTag("XinPoweiRangeReceipts").toList();
                receipts << QVariantMap{{"from", target->objectName()}, {"to", ctx.invoker->objectName()}, {"turn_id", turn},
                    {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                    {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
                    {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
                room->setTag("XinPoweiRangeReceipts", receipts);
                room->insertAttackRangePair(target, ctx.invoker);
            }
            if (!ctx.invoker || ctx.invoker->isDead()) return false;
            if (room->askForCard(ctx.invoker, ".", "xinpowei0:" + target->objectName(), QVariant::fromValue(target)))
                room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
            else if (ctx.invoker->isAlive() && target->isAlive() && ctx.invoker->getHp() >= target->getHp() && !target->isKongcheng()) {
                SkillContext reward = ctx;
                reward.choice = "obtain"; reward.extra_data = QVariantMap{{"victim", target->objectName()}};
                skillEffect(ctx.current_event, room, ctx.owner, reward, ctx.invoker);
            }
        }
        return false;
    }
};

class XinShenzhuo : public TriggerSkillV2
{
public:
    XinShenzhuo() : TriggerSkillV2("xinshenzhuo")
    {
        events << CardFinished;
        frequency = Compulsory;
    }
    bool physicalSlash(Room *room) const
    {
        const qint64 useID = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useID <= 0) return false;
        const QVariantMap result = room->queryHistoryFacts({{"kind", "use_card"}, {"event_id", useID}, {"limit", 1}});
        const QVariantList facts = result.value("items").toList();
        if (result.contains("error") || !result.value("complete").toBool() || facts.size() != 1) return false;
        const QVariantMap card = facts.first().toMap().value("data").toMap().value("card").toMap();
        return card.contains("virtual") && !card.value("virtual").toBool()
            && card.value("classes").toList().contains(QVariant("Slash"));
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<CardUseStruct>().from == player && physicalSlash(room)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!physicalSlash(room)) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "xinshenzhuo1+xinshenzhuo2", *ctx.original_data);
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "xinshenzhuo1") {
            target->drawCards(amount, objectName());
            if (target->isAlive()) room->addSlashCishu(target, amount);
        } else {
            target->drawCards(3 * amount, objectName());
            // This already-applied turn restriction survives removal of the granting skill.
            if (target->isAlive() && amount > 0)
                room->setPlayerCardLimitation(target, "use", "Slash", true, objectName());
        }
        return false;
    }
};







MobileXinPackage::MobileXinPackage()
    : Package("mobilexin")
{
    General*mobilexin_xinpi = new General(this, "mobilexin_xinpi", "wei", 3);
    mobilexin_xinpi->addSkill(new MobileXinYinju);
    mobilexin_xinpi->addSkill(new MobileXinChijie);

    General*mobilexin_mifuren = new General(this, "mobilexin_mifuren", "shu", 3, false);
    mobilexin_mifuren->addSkill(new MobileXinCunsi);
    mobilexin_mifuren->addSkill(new MobileXinGuixiu);

    General*second_mobilexin_mifuren = new General(this, "second_mobilexin_mifuren", "shu", 3, false);
    second_mobilexin_mifuren->addSkill(new SecondMobileXinGuixiu);
    second_mobilexin_mifuren->addSkill(new SecondMobileXinQingyu);
    second_mobilexin_mifuren->addRelateSkill("secondmobilexinxuancun");

    General*mobilexin_wujing = new General(this, "mobilexin_wujing", "wu", 4);
    mobilexin_wujing->addSkill(new MobileXinHeji);

    General*mobilexin_wangling = new General(this, "mobilexin_wangling", "wei", 4);
    mobilexin_wangling->addSkill(new MobileXinMouli);
    mobilexin_wangling->addSkill(new MobileXinZifu);

    General*second_mobilexin_wangling = new General(this, "second_mobilexin_wangling", "wei", 4);
    second_mobilexin_wangling->addSkill(new SecondMobileXinXingqi);
    second_mobilexin_wangling->addSkill(new SecondMobileXinZifu);
    second_mobilexin_wangling->addSkill(new SecondMobileXinMibei);
    second_mobilexin_wangling->addRelateSkill("secondmobilexinmouli");

    General*mobilexin_wangfuzhaolei = new General(this, "mobilexin_wangfuzhaolei", "shu", 4);
    mobilexin_wangfuzhaolei->addSkill(new MobileXinXunyi);

    General*mobilexin_zhouchu = new General(this, "mobilexin_zhouchu", "wu", 4);
    mobilexin_zhouchu->addSkill(new MobileXinXianghai);
    mobilexin_zhouchu->addSkill(new MobileXinXianghaiMax);
    mobilexin_zhouchu->addSkill(new MobileXinChuhai);
    related_skills.insert("mobilexinxianghai", "#mobilexinxianghai");

    General*mobilexin_kongrong = new General(this, "mobilexin_kongrong", "qun", 3);
    mobilexin_kongrong->addSkill(new MobileXinMingshi);
    mobilexin_kongrong->addSkill(new MobileXinLirang);

    General*mobilexin_yanghu = new General(this, "mobilexin_yanghu", "qun", 3);
    mobilexin_yanghu->addSkill(new MobileXinMingfa);
    mobilexin_yanghu->addSkill(new MobileXinMingfaPindian);
    mobilexin_yanghu->addSkill(new MobileXinMingfaPro);
    mobilexin_yanghu->addSkill(new MobileXinRongbei);
    related_skills.insert("mobilexinmingfa", "#mobilexinmingfa-pindian");
    related_skills.insert("mobilexinmingfa", "#mobilexinmingfa-pro");

    addMetaObject<MobileXinYinjuCard>();
    addMetaObject<MobileXinCunsiCard>();
    addMetaObject<MobileXinMouliCard>();
    addMetaObject<MobileXinChuhaiCard>();
    addMetaObject<MobileXinLirangCard>();
    addMetaObject<MobileXinRongbeiCard>();
    addMetaObject<SecondMobileXinMouliCard>();

    General*xin_shentaishici = new General(this, "xin_shentaishici", "god", 4);
    xin_shentaishici->addSkill(new XinDulie);
    xin_shentaishici->addSkill(new XinPowei);
	skills << new XinShenzhuo;

    skills << new SecondMobileXinXuancun << new MobileXinMouliEffect << new SecondMobileXinMouli;
}
ADD_PACKAGE(MobileXin)


MobileRenRenshiCard::MobileRenRenshiCard()
{
    setSkillName("mobilerenrenshi");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileRenRenshiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
    return targets.isEmpty() && to_select != Self && to_select->getMark("mobilerenrenshi-PlayClear") <= 0;
}

void MobileRenRenshiCard::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.from->getRoom();
    room->addPlayerMark(effect.to, "mobilerenrenshi-PlayClear");
    room->giveCard(effect.from, effect.to, this, "mobilerenrenshi");
}

class MobileRenRenshi : public ViewAsSkillV2
{
public:
    MobileRenRenshi() : ViewAsSkillV2("mobilerenrenshi", 1)
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileRenRenshiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && !card->isVirtualCard()
            && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || target == request.initiator || !selected.isEmpty()
            || !request.activationRef.isValid()) return false;
        for (const Player *holder : request.initiator->getSiblings(true))
            if (holder->objectName() == request.activationRef.ownerObjectName)
                return !holder->getSkillInstanceStateValue(request.activationRef.key.skillName,
                    request.activationRef.key.instanceID, "mobilerenrenshi_phase_targets").toStringList().contains(target->objectName());
        return false;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ref.isValid() || !ctx.initiator) return false;
        const ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return false;
        const QStringList used = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
            "mobilerenrenshi_phase_targets").toStringList();
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
            "mobilerenrenshi_phase_targets").toStringList();
        for (ServerPlayer *target : ctx.targets)
            if (target && !used.contains(target->objectName())) used << target->objectName();
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mobilerenrenshi_phase_targets", used);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return;
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (holder) holder->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mobilerenrenshi_phase_targets");
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || request.selectedCardIds.size() != 1 || !checkCustomUsage(ctx)) return false;
        const int id = request.selectedCardIds.first();
        if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand) return false;
        // Custom quotas commit explicitly before recipient hooks or the gift can re-enter this activation.
        addUsage(ctx);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.initiator->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand) return ContinueEffects;
        room->giveCard(ctx.initiator, target, Sanguosha->getCard(id), objectName());
        return ContinueEffects;
    }
};

class MobileRenRenshiClear : public TriggerSkillV2
{
public:
    MobileRenRenshiClear() : TriggerSkillV2("#mobilerenrenshi-clear")
    {
        events << EventPhaseChanging;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (data.value<PhaseChangeStruct>().from != Player::Play) return true;
        // A borrowed activation may charge a different holder/name: clear only this skill's state field, on all instances.
        for (ServerPlayer *holder : room->getAllPlayers(true))
            for (const SkillInstance &instance : holder->getSkillInstances())
                if (!holder->getSkillInstanceStateValue(instance.skillName, instance.instanceID,
                    "mobilerenrenshi_phase_targets").toStringList().isEmpty())
                    holder->removeSkillInstanceStateValue(instance.skillName, instance.instanceID, "mobilerenrenshi_phase_targets");
        return true;
    }
};

MobileRenBuqiCard::MobileRenBuqiCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MobileRenBuqiCard::onUse(Room*room, CardUseStruct &card_use) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, card_use.from->objectName(), "mobilerenbuqi", "");
    room->throwCard(this, reason, nullptr);
}

class MobileRenBuqi : public TriggerSkillV2
{
public:
    MobileRenBuqi() : TriggerSkillV2("mobilerenbuqi")
    {
        events << Dying << Death;
        frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Both events are delivered separately to each player; do not enumerate owners again.
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        if (event == Dying) {
            ServerPlayer *victim = data.value<DyingStruct>().who;
            if (!victim || victim->isDead() || player->getPile("mrhxren").size() < 2) return {};
        } else if (player->getPile("mrhxren").isEmpty()) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Death) {
            ctx.targets = {ctx.owner};
            return true;
        }
        ServerPlayer *victim = ctx.original_data->value<DyingStruct>().who;
        QList<int> available = ctx.owner->getPile("mrhxren");
        if (!victim || victim->isDead() || available.size() < 2) return false;
        QList<int> selected;
        while (selected.size() < 2) {
            int id = available.first();
            if (available.size() > 2 - selected.size()) {
                room->fillAG(available, ctx.owner);
                id = room->askForAG(ctx.owner, available, false, objectName());
                room->clearAG(ctx.owner);
                if (!available.contains(id)) id = available.first();
            }
            selected << id;
            available.removeOne(id);
        }
        ctx.extra_data = ListI2V(selected);
        ctx.targets = {victim};
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Death) return true;
        const QList<int> selected = ListV2I(ctx.extra_data.toList());
        const QList<int> current = ctx.owner->getPile("mrhxren");
        if (selected.size() != 2 || selected.first() == selected.last()) return false;
        for (int id : selected)
            if (!current.contains(id)) return false;
        // Validate both physical materials before the first movement callback.
        DummyCard payment(selected);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.owner->objectName(), objectName(), "");
        room->throwCard(&payment, reason, nullptr);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        if (event == Death)
            target->clearOnePrivatePile("mrhxren");
        else
            room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        return false;
    }
};

class MobileRenDebao : public TriggerSkillV2
{
public:
    MobileRenDebao() : TriggerSkillV2("mobilerendebao")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || !move.to || move.to == player
                || player->getPile("mrhxren").size() >= player->getMaxHp()
                || (!move.from_places.contains(Player::PlaceHand) && !move.from_places.contains(Player::PlaceEquip))) return {};
        } else if (player->getPhase() != Player::Start || player->getPile("mrhxren").isEmpty()) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        if (event == CardsMoveOneTime) {
            // The public ren pile is shared material; its capacity is checked after target interception.
            const int count = qMin(getEffectiveAmount(ctx), target->getMaxHp() - target->getPile("mrhxren").size());
            if (count > 0) target->addToPile("mrhxren", room->getNCards(count));
        } else if (!target->getPile("mrhxren").isEmpty()) {
            LogMessage log;
            log.type = "$KuangbiGet";
            log.from = target;
            log.arg = "mrhxren";
            log.card_str = ListI2S(target->getPile("mrhxren")).join("+");
            room->sendLog(log);
            DummyCard dummy(target->getPile("mrhxren"));
            room->obtainCard(target, &dummy, true);
        }
        return false;
    }
};

class MobileRenSheyi : public TriggerSkillV2
{
public:
    MobileRenSheyi() : TriggerSkillV2("mobilerensheyi")
    {
        events << DamageInflicted;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->isDead()) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->getHp() > player->getHp()
                && owner->getCardCount() >= qMax(1, owner->getHp())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to || damage.to->isDead() || ctx.owner->getHp() <= damage.to->getHp()) return false;
        const int count = qMax(1, ctx.owner->getHp());
        ctx.owner->setTag("mobilerensheyi_data", *ctx.original_data);
        const Card *card = room->askForExchange(ctx.owner, objectName(), 999, count, true,
            QString("@mobilerensheyi-give:%1:%2:%3").arg(damage.to->objectName()).arg(count).arg(damage.damage), true);
        ctx.owner->removeTag("mobilerensheyi_data");
        if (!card || card->subcardsLength() < count) return false;
        ctx.extra_data = QVariantMap{{"materials", ListI2V(card->getSubcards())}, {"minimum", count}};
        ctx.targets = {damage.to};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariantMap payment = ctx.extra_data.toMap();
        const QList<int> ids = ListV2I(payment.value("materials").toList());
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
        if (!victim || victim->isDead() || ids.size() < payment.value("minimum").toInt()) return false;
        QSet<int> seen;
        for (int id : ids) {
            if (seen.contains(id) || room->getCardOwner(id) != ctx.owner
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
            seen.insert(id);
        }
        DummyCard gift(ids);
        room->giveCard(ctx.owner, victim, &gift, objectName());
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Payment is committed to the original victim; never cancel a different pending damage.
        return target == ctx.original_data->value<DamageStruct>().to;
    }
};

class MobileRenTianyin : public TriggerSkillV2
{
public:
    MobileRenTianyin() : TriggerSkillV2("mobilerentianyin")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }
    bool usedTypes(Room *room, ServerPlayer *player, QSet<int> &types) const
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toString().isEmpty() || turn.toString() == "0") return false;
        QVariantMap filter{{"kind", "use_card"}, {"turn_id", turn},
            {"from", player->objectName()}, {"limit", 128}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            foreach (const QVariant &item, page.value("items").toList()) {
                const QVariantMap card = item.toMap().value("data").toMap().value("card").toMap();
                if (!card.contains("type")) return false;
                types.insert(card.value("type").toInt());
            }
            if (!page.value("has_more").toBool()) return true;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return {};
        QSet<int> types;
        return usedTypes(room, player, types) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QSet<int> used;
        if (!usedTypes(room, ctx.owner, used)) return false;
        QMap<int, QList<int>> candidates;
        foreach (int id, room->getDrawPile()) {
            const int type = Sanguosha->getCard(id)->getTypeId();
            if (type != Card::TypeSkill && !used.contains(type)) candidates[type] << id;
        }
        DummyCard cards;
        for (auto it = candidates.begin(); it != candidates.end(); ++it) {
            QList<int> &ids = it.value();
            const int count = qMin(getEffectiveAmount(ctx), ids.size());
            for (int i = 0; i < count; ++i)
                cards.addSubcard(ids.takeAt(qsanRandomBounded(ids.size())));
        }
        if (cards.subcardsLength() == 0) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        room->obtainCard(target, &cards, true);
        return false;
    }
};

MobileRenBomingCard::MobileRenBomingCard()
{
    setSkillName("mobilerenboming");
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MobileRenBomingCard::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.from->getRoom();
    room->giveCard(effect.from, effect.to, this, "mobilerenboming");
}

class MobileRenBomingVS : public ViewAsSkillV2
{
public:
    MobileRenBomingVS() : ViewAsSkillV2("mobilerenboming", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileRenBomingCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && !card->isVirtualCard()
            && !card->hasFlag("using") && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    { return target && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator || !ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        Room *room = ctx.initiator->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != ctx.initiator
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
        room->giveCard(ctx.initiator, target, Sanguosha->getCard(id), objectName());
        return ContinueEffects;
    }
};

class MobileRenBoming : public TriggerSkillV2
{
public:
    MobileRenBoming() : TriggerSkillV2("mobilerenboming")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
        view_as_skill = new MobileRenBomingVS;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    int completedPhases(Room *room, const SkillContext &ctx) const
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (turn.toLongLong() <= 0 || !ref.isValid() || !ctx.sourceRef.isValid()) return -1;
        QVariantMap filter{{"kind", "skill_invoked"}, {"turn_id", turn},
            {"skill_name", ctx.sourceRef.key.skillName}, {"skill_owner", ctx.sourceRef.ownerObjectName}, {"limit", 128}};
        QMap<QString, int> counts;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (page.contains("error") || !page.value("complete").toBool()
                || !page.value("attribution_complete").toBool()) return -1;
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap fact = item.toMap(), invoked = fact.value("data").toMap();
                if (invoked.value("invoked_skill").toString() != objectName()) continue;
                if (!invoked.contains("activation_owner") || !invoked.contains("activation_skill")
                    || !invoked.contains("activation_instance_id")) return -1;
                if (invoked.value("activation_owner").toString() != ref.ownerObjectName
                    || invoked.value("activation_skill").toString() != ref.key.skillName
                    || invoked.value("activation_instance_id").toInt() != ref.key.instanceID) continue;
                const QString phase = fact.value("phase_id").toString();
                if (phase.isEmpty() || phase == "0") return -1;
                const QVariantMap phaseData = room->historyEvent(phase.toLongLong()).value("data").toMap();
                if (!phaseData.contains("phase")) return -1;
                if (phaseData.value("phase").toInt() != Player::Play) continue;
                ++counts[phase];
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        int result = 0;
        // Each qualifying Play phase awards once, including separately inserted Play phases.
        for (int count : counts)
            if (count >= 2) ++result;
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = completedPhases(room, ctx);
        if (count <= 0) return false;
        ctx.extra_data = count;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class MobileRenEjian : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileRenEjian() : TriggerSkillV2("mobilerenejian")
    {
        events << CardsMoveOneTime << EventSkillInvoking;
        frequency = Compulsory;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QString matchingType(const CardsMoveOneTimeStruct &move) const
    {
        const ServerPlayer *recipient = qobject_cast<const ServerPlayer *>(move.to);
        if (!recipient || recipient->isDead()) return QString();
        for (int id : move.card_ids) {
            const Card *given = Sanguosha->getCard(id);
            for (const Card *held : recipient->getCards("he"))
                if (held->getEffectiveId() != id && held->getType() == given->getType()) return given->getType();
        }
        return QString();
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking || !player || player->isDead() || !player->hasSkill(objectName())) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        // Move events repeat per player. Only this giver's Boming can activate their Ejian.
        return move.from == player && move.to && move.to != player && move.to_place == Player::PlaceHand
            && move.reason.m_skillName == "mobilerenboming" && !matchingType(move).isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const Player *recipient = ctx.original_data->value<CardsMoveOneTimeStruct>().to;
        const SkillInstanceRef ref = getUsageRef(ctx);
        const ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        return recipient && ref.isValid() && holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)
            && !holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
                "ejian_game_recipients").toStringList().contains(recipient->objectName());
    }
    void addUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return;
        const Player *recipient = ctx.original_data->value<CardsMoveOneTimeStruct>().to;
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!recipient || !ref.isValid() || !holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        QStringList used = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
            "ejian_game_recipients").toStringList();
        if (!used.contains(recipient->objectName())) used << recipient->objectName();
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "ejian_game_recipients", used);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (holder) holder->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "ejian_game_recipients");
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.bypass_cost && accepted.activationRef == ctx.activationRef && accepted.sourceRef == ctx.sourceRef
            && parseSkillName(accepted.skill_name) == objectName()) addUsage(accepted);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        ctx.choice = matchingType(move);
        ServerPlayer *recipient = qobject_cast<ServerPlayer *>(move.to);
        if (ctx.choice.isEmpty() || !recipient || !checkCustomUsage(ctx)) return false;
        ctx.targets = {recipient};
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
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        room->setPlayerMark(target, "&mobilerenejian", 1);
        if (room->askForChoice(target, objectName(), "damage+discard=" + ctx.choice,
            QVariant::fromValue(ctx.owner)) == "damage") {
            room->damage(DamageStruct(objectName(), nullptr, target, getEffectiveAmount(ctx)));
        } else {
            room->showAllCards(target);
            QList<int> ids;
            for (const Card *card : target->getCards("he"))
                if (card->getType() == ctx.choice && target->canDiscard(target, card->getEffectiveId()))
                    ids << card->getEffectiveId();
            if (!ids.isEmpty()) {
                DummyCard discard(ids);
                room->throwCard(&discard, target);
            }
        }
        return false;
    }
};

class MobileRenGuying : public TriggerSkillV2
{
public:
    MobileRenGuying() : TriggerSkillV2("mobilerenguying")
    {
        global = true; frequency = Compulsory;
        events << CardsMoveOneTime << EventPhaseStart << EventPhaseEnd << EventPhaseChanging << EventSkillInvoking << Death;
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        return TriggerSkillV2::prepareSource(room, ctx)
            && (ctx.current_event != CardsMoveOneTime || isUsable(ctx));
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.skill_name == objectName()
            && accepted.activationRef.isValid() && accepted.activationRef == ctx.activationRef && accepted.sourceRef == ctx.sourceRef)
            addUsage(accepted);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return false;
        QVariantMap due = player->getTag("MobileRenGuyingDue").toMap();
        const qint64 phase = room->historyScopes().value("phase_id").toLongLong();
        if (event == EventPhaseStart && player->getPhase() == Player::Start && phase > 0) {
            due[QString::number(phase)] = player->getTag("MobileRenGuyingReceipts").toList();
            player->setTag("MobileRenGuyingDue", due); player->removeTag("MobileRenGuyingReceipts");
            room->setPlayerMark(player, "&mobilerenguying", 0);
            return false;
        }
        if (event == EventPhaseEnd && player->getPhase() == Player::Start) due.remove(QString::number(phase));
        else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            for (auto it = due.begin(); it != due.end(); ) {
                if (room->historyEvent(it.key().toLongLong()).value("turn_id").toLongLong() == turn) it = due.erase(it);
                else ++it;
            }
        } else if (event == Death && data.value<DeathStruct>().who == player) {
            due.clear(); player->removeTag("MobileRenGuyingReceipts");
            player->setTag("MobileRenGuyingDue", due); room->setPlayerMark(player, "&mobilerenguying", 0);
            return false;
        }
        player->setTag("MobileRenGuyingDue", due);
        return false; // Normal instance record still commits an accepted waived quota.
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event == CardsMoveOneTime) return false;
        if (event != EventPhaseStart || !player || player->isDead() || player->getPhase() != Player::Start) return true;
        const QString phase = room->historyScopes().value("phase_id").toString();
        for (const QVariant &value : player->getTag("MobileRenGuyingDue").toMap().value(phase).toList()) {
            const QVariantMap receipt = value.toMap();
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = player; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt(); ctx.is_forced = true;
            ctx.amount = receipt.value("amount").toInt(); ctx.choice = "discard"; ctx.targets = {player};
            ctx.extra_data = QVariantMap{{"phase", phase}, {"receipt", receipt}}; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        return ctx.initiator && ctx.initiator->getTag("MobileRenGuyingDue").toMap()
            .value(ctx.extra_data.toMap().value("phase").toString()).toList().contains(ctx.extra_data.toMap().value("receipt"));
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player || player->isDead() || player->getPhase() != Player::NotActive
            || !room->hasCurrent() || !player->hasSkill(objectName())) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        const int reason = move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON;
        return move.from == player && move.card_ids.size() == 1
            && (reason == CardMoveReason::S_REASON_DISCARD || reason == CardMoveReason::S_REASON_USE || reason == CardMoveReason::S_REASON_RESPONSE)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardsMoveOneTime) { ctx.targets = {ctx.owner}; ctx.invoker = ctx.owner; }
        return true;
    }
    bool pay(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != CardsMoveOneTime) return true;
        if (!isUsable(ctx)) return false;
        addUsage(ctx); return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "guying_give") {
            ServerPlayer *recipient = room->findPlayerByObjectName(ctx.extra_data.toString());
            if (!recipient || recipient->isDead()) return false;
            QList<const Card *> cards = target->getCards("he"); qsanShuffle(cards);
            QList<int> ids;
            for (const Card *give : cards) { if (ids.size() >= amount) break; ids << give->getEffectiveId(); }
            if (!ids.isEmpty()) { DummyCard give(ids); room->giveCard(target, recipient, &give, objectName()); }
            return false;
        }
        if (event == EventPhaseStart) {
            const QString phase = ctx.extra_data.toMap().value("phase").toString();
            QVariantMap due = ctx.initiator->getTag("MobileRenGuyingDue").toMap();
            QVariantList receipts = due.value(phase).toList(); receipts.removeAll(ctx.extra_data.toMap().value("receipt"));
            due[phase] = receipts; ctx.initiator->setTag("MobileRenGuyingDue", due);
            if (amount > 0) room->askForDiscard(target, objectName(), amount, amount, false, true);
            return false;
        }
        ServerPlayer *current = room->getCurrent();
        if (!current || current->isDead()) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.card_ids.size() != 1) return false;
        const int id = move.card_ids.first();
        const Card *card = Sanguosha->getEngineCard(id);
        const qint64 previous = room->getTag("MobileRenGuyingSequence").toLongLong();
        if (!card || previous < 0 || previous >= INT_MAX) return false;
        const int serial = int(previous + 1); room->setTag("MobileRenGuyingSequence", serial);
        QVariantList receipts = target->getTag("MobileRenGuyingReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"owner", ctx.owner->objectName()}, {"actor", target->objectName()}, {"amount", amount},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("MobileRenGuyingReceipts", receipts);
        room->addPlayerMark(target, "&mobilerenguying", amount);
        if (target->isDead() || current->isDead()) return false;
        QStringList choices;
        if (!current->isNude()) choices << "give=" + target->objectName();
        choices << "obtain=" + target->objectName() + "=" + card->objectName();
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        if (room->askForChoice(current, objectName(), choices.join("+"), QVariant::fromValue(target)).startsWith("give")) {
            SkillContext give = ctx; give.choice = "guying_give"; give.extra_data = target->objectName();
            skillEffect(event, room, ctx.owner, give, current);
        } else {
            if (target->isDead()) return false;
            room->obtainCard(target, card, true);
            const Card *equip = Sanguosha->getCard(id);
            if (target->isAlive() && room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand
                && equip->isKindOf("EquipCard") && target->canUse(equip)) {
                CardUseStruct use(equip, target, target);
                room->useCardFromSkillEffect(use, ctx);
            }
        }
        return false;
    }
};

MobileRenMuzhenCard::MobileRenMuzhenCard()
{
    setSkillName("mobilerenmuzhen");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileRenMuzhenCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
    int num = subcardsLength();
    if (num == 1) {
        const Card*card = Sanguosha->getCard(getEffectiveId());
        const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
        if (!equip) return false;
        int equip_index = static_cast<int>(equip->location());
        return to_select->getEquip(equip_index) == nullptr && !Self->isProhibited(to_select, card) && targets.isEmpty() && to_select != Self;
    } else if (num == 2)
        return !to_select->getEquips().isEmpty() && targets.isEmpty() && to_select != Self;

    return false;
}

void MobileRenMuzhenCard::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.from->getRoom();
    int num = subcardsLength();
    if (num == 1) {
        room->addPlayerMark(effect.from, "mobilerenmuzhen_put-PlayClear");

        LogMessage log;
        log.type = "$ZhijianEquip";
        log.from = effect.to;
        log.card_str = QString::number(getEffectiveId());
        room->sendLog(log);

        room->moveCardTo(this, effect.from, effect.to, Player::PlaceEquip,
            CardMoveReason(CardMoveReason::S_REASON_PUT, effect.from->objectName(), "mobilerenmuzhen", ""));

        if (effect.from->isDead() || effect.to->isDead() || effect.to->isKongcheng()) return;
        int id = room->askForCardChosen(effect.from, effect.to, "h", "mobilerenmuzhen");
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
        room->obtainCard(effect.from, Sanguosha->getCard(id), reason, false);
    } else if (num == 2) {
        room->addPlayerMark(effect.from, "mobilerenmuzhen_give-PlayClear");

        room->giveCard(effect.from, effect.to, this, "mobilerenmuzhen");
        if (effect.from->isDead() || effect.to->isDead() || effect.to->getEquips().isEmpty()) return;
        int id = room->askForCardChosen(effect.from, effect.to, "e", "mobilerenmuzhen");
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
        room->obtainCard(effect.from, Sanguosha->getCard(id), reason, false);
    }
}

class MobileRenMuzhen : public ViewAsSkillV2
{
public:
    MobileRenMuzhen() : ViewAsSkillV2("mobilerenmuzhen", 2) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileRenMuzhenCard"; }
    QStringList usedModes(const Player *actor, const SkillInstanceRef &ref) const
    {
        if (!actor || !ref.isValid()) return {};
        for (const Player *holder : actor->getSiblings(true))
            if (holder->objectName() == ref.ownerObjectName)
                return holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "muzhen_phase_modes").toStringList();
        return {};
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && usedModes(request.initiator, request.activationRef).size() < 2;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->isVirtualCard() || card->hasFlag("using")
            || request.selectedCardIds.size() >= 2 || request.selectedCardIds.contains(card->getEffectiveId())
            || (!request.initiator->handCards().contains(card->getEffectiveId())
                && !request.initiator->getEquipsId().contains(card->getEffectiveId()))) return false;
        const QStringList used = usedModes(request.initiator, request.activationRef);
        return !used.contains("give") || (request.selectedCardIds.isEmpty() && card->isKindOf("EquipCard"));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const QStringList used = usedModes(request.initiator, request.activationRef);
        if (request.selectedCardIds.size() == 2) return !used.contains("give");
        return request.selectedCardIds.size() == 1 && !used.contains("put")
            && Sanguosha->getCard(request.selectedCardIds.first())->isKindOf("EquipCard");
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || target == request.initiator || !selected.isEmpty()) return false;
        if (request.selectedCardIds.size() == 2) return !target->getEquips().isEmpty();
        if (request.selectedCardIds.size() != 1) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        return equip && target->hasEquipArea(equip->location()) && !target->getEquip(equip->location())
            && !request.initiator->isProhibited(target, card);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return false;
        const ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return false;
        const QStringList used = usedModes(ctx.initiator, ref);
        return ctx.use_card ? !used.contains(ctx.use_card->subcardsLength() == 1 ? "put" : "give") : used.size() < 2;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ctx.use_card || !ref.isValid()) return;
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        QStringList used = usedModes(ctx.initiator, ref);
        const QString mode = ctx.use_card->subcardsLength() == 1 ? "put" : "give";
        if (!used.contains(mode)) used << mode;
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "muzhen_phase_modes", used);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return;
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (holder) holder->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "muzhen_phase_modes");
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || !checkCustomUsage(ctx)) return false;
        for (int id : request.selectedCardIds)
            if (room->getCardOwner(id) != ctx.initiator
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        addUsage(ctx);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.initiator->getRoom();
        if (ctx.choice == "extract") {
            const QVariantMap state = ctx.extra_data.toMap();
            ServerPlayer *donor = room->findPlayerByObjectName(state.value("donor").toString());
            const QString area = state.value("area").toString();
            if (!donor || donor->isDead() || donor->getCards(area).isEmpty() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
            const QList<int> chosen = room->askForCardsChosen(ctx.invoker, donor, area, objectName(), 1,
                qMin(getEffectiveAmount(ctx), donor->getCards(area).size()), false, Card::MethodNone, {}, false);
            DummyCard cards;
            for (int id : chosen)
                if (room->getCardOwner(id) == donor
                    && room->getCardPlace(id) == (area == "h" ? Player::PlaceHand : Player::PlaceEquip)) cards.addSubcard(id);
            if (cards.subcardsLength() > 0)
                room->obtainCard(target, &cards, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, ctx.invoker->objectName()), false);
            return ContinueEffects;
        }
        const QList<int> ids = ctx.use_card->getSubcards();
        for (int id : ids)
            if (room->getCardOwner(id) != ctx.initiator
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
        const bool put = ids.size() == 1;
        if (put) {
            const Card *card = Sanguosha->getCard(ids.first());
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            if (!equip || !target->hasEquipArea(equip->location()) || target->getEquip(equip->location())
                || ctx.invoker->isProhibited(target, card)) return ContinueEffects;
            room->moveCardTo(card, ctx.initiator, target, Player::PlaceEquip,
                CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.invoker->objectName(), objectName(), ""));
        } else {
            DummyCard gift(ids);
            room->giveCard(ctx.initiator, target, &gift, objectName());
        }
        if (ctx.invoker->isAlive() && target->isAlive() && getEffectiveAmount(ctx) > 0) {
            SkillContext extraction = ctx;
            extraction.choice = "extract";
            extraction.extra_data = QVariantMap{{"donor", target->objectName()}, {"area", put ? "h" : "e"}};
            // Receiving the equipment/gift and receiving the extracted cards are separate recipients.
            skillEffect(extraction, ctx.invoker);
        }
        return ContinueEffects;
    }
};
class MobileRenMuzhenClear : public TriggerSkillV2
{
public:
    MobileRenMuzhenClear() : TriggerSkillV2("#mobilerenmuzhen-clear")
    { events << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (data.value<PhaseChangeStruct>().from != Player::Play) return true;
        // Charge holders can be borrowed instances under another skill name.
        for (ServerPlayer *holder : room->getAllPlayers(true))
            for (const SkillInstance &instance : holder->getSkillInstances())
                holder->removeSkillInstanceStateValue(instance.skillName, instance.instanceID, "muzhen_phase_modes");
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class MobileRenYaohu : public TriggerSkillV2
{
public:
    MobileRenYaohu() : TriggerSkillV2("mobilerenyaohu")
    {
        global = true; frequency = Compulsory;
        events << TargetSpecifying << EventPhaseStart << EventPhaseChanging << EventSkillInvoking << Death;
    }
    LimitScope getLimitScope() const override { return Limit_Round; }
    static QStringList kingdoms(const Player *player)
    {
        QStringList result;
        for (const SkillInstance &instance : player->getSkillInstances()) {
            const QString kingdom = player->getSkillInstanceStateValue(instance.skillName, instance.instanceID, "yaohu_kingdom").toString();
            if (!kingdom.isEmpty() && !result.contains(kingdom)) result << kingdom;
        }
        return result;
    }
    static int getYaohuTargetsNum(const Player *player)
    {
        if (!player->hasSkill("mobilerenyaohu", true)) return -1;
        const QStringList selected = kingdoms(player);
        if (selected.isEmpty()) return -1;
        int count = 0;
        for (const Player *other : player->getAliveSiblings(true)) if (selected.contains(other->getKingdom())) ++count;
        return count;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.skill_name == objectName() && accepted.choice == "kingdom" && accepted.bypass_cost
                && accepted.sourceRef.isValid() && accepted.activationRef.isValid()) addUsage(accepted);
        }
        if (event == EventPhaseChanging || event == Death) {
            QVariantList kept;
            const QVariant turn = room->historyScopes().value("turn_id");
            for (const QVariant &value : room->getTag("MobileRenYaohuReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                const bool dead = event == Death && player && (receipt.value("actor").toString() == player->objectName()
                    || receipt.value("protected").toString() == player->objectName());
                const bool ended = event == EventPhaseChanging
                    && (room->historyEvent(receipt.value("phase_id").toLongLong()).value("status").toString() == "finished"
                        || (data.value<PhaseChangeStruct>().to == Player::NotActive && receipt.value("turn_id") == turn));
                if (!dead && !ended) kept << value;
            }
            room->setTag("MobileRenYaohuReceipts", kept);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != TargetSpecifying) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || !use.card->isDamageCard()) return true;
        for (const QVariant &value : room->getTag("MobileRenYaohuReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("actor").toString() != player->objectName()
                || receipt.value("phase_id") != room->historyScopes().value("phase_id")) continue;
            ServerPlayer *protectedPlayer = room->findPlayerByObjectName(receipt.value("protected").toString(), true);
            if (!protectedPlayer || protectedPlayer->isDead() || !use.to.contains(protectedPlayer)) continue;
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.initiator = player; ctx.invoker = player; ctx.targets = {protectedPlayer};
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.amount = receipt.value("amount").toInt(); ctx.is_forced = true; ctx.choice = "tax";
            ctx.extra_data = receipt; ctx.current_event = event; ctx.original_data = &data; contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || player->isDead()) return result;
        if (player->getPhase() == Player::RoundStart && player->hasSkill(objectName())) result[player] << objectName();
        else if (player->getPhase() == Player::Play)
            for (ServerPlayer *owner : room->getOtherPlayers(player))
                if (owner->hasSkill(objectName()) && !owner->getPile("mrlzsheng").isEmpty()) result[owner] << objectName();
        return result;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.choice == "tax") return true;
        if (!TriggerSkillV2::prepareSource(room, ctx) || !ctx.invoker) return false;
        ctx.is_forced = true;
        if (ctx.invoker->getPhase() == Player::RoundStart) { ctx.choice = "kingdom"; return isUsable(ctx); }
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        return holder && holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "yaohu_kingdom").toString()
            == ctx.invoker->getKingdom();
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.choice == "tax" ? room->getTag("MobileRenYaohuReceipts").toList().contains(ctx.extra_data)
            : TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "tax") return true;
        if (ctx.choice == "kingdom") {
            QStringList available;
            for (ServerPlayer *player : room->getAlivePlayers()) if (!available.contains(player->getKingdom())) available << player->getKingdom();
            if (available.isEmpty()) return false;
            ctx.extra_data = room->askForKingdom(ctx.owner, objectName(), available);
            ctx.invoker = ctx.owner; ctx.targets = {ctx.owner};
        } else ctx.targets = {ctx.invoker};
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice != "kingdom") return true;
        if (!isUsable(ctx)) return false;
        addUsage(ctx); return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "kingdom") {
            const SkillInstanceRef ref = getUsageRef(ctx);
            ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
            if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return false;
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "yaohu_kingdom", ctx.extra_data);
            for (const QString &mark : holder->getMarkNames())
                if (mark.startsWith("&mobilerenyaohu+:+")) room->setPlayerMark(holder, mark, 0);
            for (const QString &kingdom : kingdoms(holder)) room->setPlayerMark(holder, "&mobilerenyaohu+:+" + kingdom, 1);
            return false;
        }
        if (ctx.choice == "tax") {
            if (!ctx.original_data || !ctx.initiator || ctx.initiator->isDead()) return false;
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.to.contains(target)) return false;
            const int count = 2 * getEffectiveAmount(ctx);
            const Card *cards = count > 0 && ctx.initiator->getCardCount() >= count
                ? room->askForExchange(ctx.initiator, objectName(), count, count, true, "@mobilerenyaohu-give:" + target->objectName(), true) : nullptr;
            if (cards) room->giveCard(ctx.initiator, target, cards, objectName());
            else if (count > 0) {
                use = ctx.original_data->value<CardUseStruct>(); use.to.removeOne(target);
                *ctx.original_data = QVariant::fromValue(use);
            }
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            const QList<int> pile = ctx.owner->getPile("mrlzsheng");
            if (pile.isEmpty()) break;
            int id = -1;
            { room->fillAG(pile, target); const auto clear = qScopeGuard([room, target] { room->clearAG(target); });
                id = room->askForAG(target, pile, false, objectName()); }
            if (id >= 0 && ctx.owner->getPile("mrlzsheng").contains(id)) room->obtainCard(target, id);
        }
        if (target->isDead()) return false;
        QList<ServerPlayer *> targets;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if (target->canSlash(other) && target->inMyAttackRange(other)) targets << other;
        QStringList choices;
        if (!targets.isEmpty()) choices << "slash=" + ctx.owner->objectName();
        choices << "damagecard=" + ctx.owner->objectName();
        if (room->askForChoice(target, objectName(), choices.join("+")).startsWith("slash=")) {
            ServerPlayer *victim = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@mobilerenyaohu-slash:" + target->objectName());
            if (victim && room->askForUseSlashTo(target, victim, "@mobilerenyaohu-use:" + victim->objectName())) return false;
        }
        const int previous = room->getTag("MobileRenYaohuSerial").toInt();
        if (previous == INT_MAX) return false;
        room->setTag("MobileRenYaohuSerial", previous + 1);
        QVariantList receipts = room->getTag("MobileRenYaohuReceipts").toList();
        receipts << QVariantMap{{"serial", previous + 1}, {"owner", ctx.owner->objectName()},
            {"actor", target->objectName()}, {"protected", ctx.owner->objectName()}, {"amount", getEffectiveAmount(ctx)},
            {"phase_id", room->historyScopes().value("phase_id")}, {"turn_id", room->historyScopes().value("turn_id")},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        room->setTag("MobileRenYaohuReceipts", receipts);
        return false;
    }
};

class MobileRenJutu : public TriggerSkillV2
{
public:
    MobileRenJutu() : TriggerSkillV2("mobilerenjutu")
    {
        frequency = Compulsory;
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {

        QList<int> sheng = player->getPile("mrlzsheng");
        bool send_log = true;
        if (!sheng.isEmpty()) {
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            send_log = false;

            LogMessage log;
            log.type = "$KuangbiGet";
            log.from = player;
            log.arg = "mrlzsheng";
            log.card_str = ListI2S(sheng).join("+");
            room->sendLog(log);
            DummyCard get(sheng);
            room->obtainCard(player, &get);
        }

        int num = MobileRenYaohu::getYaohuTargetsNum(ctx.owner);
        if (num < 0 || player->isDead()) return false;

        if (send_log)
            room->sendCompulsoryTriggerLog(ctx.owner, this);

        player->drawCards((num + 1) * getEffectiveAmount(ctx), objectName());
        if (player->isAlive() && !player->isNude() && num > 0) {
            const Card*ex = room->askForExchange(player, objectName(), num, num, true, "@mobilerenjutu-put:" + QString::number(num));
            // A selection prompt can dispatch nested moves; only cards still owned in h/e may enter the pile.
            if (ex) {
                QList<int> live;
                for (int id : ex->getSubcards())
                    if (room->getCardOwner(id) == player && (room->getCardPlace(id) == Player::PlaceHand
                        || room->getCardPlace(id) == Player::PlaceEquip)) live << id;
                if (!live.isEmpty()) player->addToPile("mrlzsheng", live);
            }
        }
        return false;
    }
};

class MobileRenHuaibi : public TriggerSkillV2
{
public:
    MobileRenHuaibi() : TriggerSkillV2("mobilerenhuaibi$")
    {
        events << EventPhaseChanging;
        waked_skills = "#mobilerenhuaibi";
        frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasLordSkill(objectName())
            && data.value<PhaseChangeStruct>().to == Player::Discard
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        // Numerical behavior belongs to the separately registered CorrectSkillV2 helper.
        room->broadcastSkillInvoke(objectName());
        return false;
    }
};

class MobileRenHuaibiMCS : public MaxCardsSkillV2
{
public:
    MobileRenHuaibiMCS() : MaxCardsSkillV2("#mobilerenhuaibi")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.holder && ctx.holder->hasLordSkill("mobilerenhuaibi")
            ? CorrectSkillResult::useAmount(qMax(MobileRenYaohu::getYaohuTargetsNum(ctx.holder), 0) * ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};

MobileRenPackage::MobileRenPackage()
    : Package("mobileren")
{
    General*mobileren_huaxin = new General(this, "mobileren_huaxin", "wei", 3);
    mobileren_huaxin->addSkill(new MobileRenRenshi);
    mobileren_huaxin->addSkill(new MobileRenRenshiClear);
    mobileren_huaxin->addSkill(new MobileShijiActiveQuota("mobilerenrenshi"));
    related_skills.insert("mobilerenrenshi", "#mobilerenrenshi-quota");
    related_skills.insert("mobilerenrenshi", "#mobilerenrenshi-clear");
    mobileren_huaxin->addSkill(new MobileRenBuqi);
    mobileren_huaxin->addSkill(new MobileRenDebao);

    General*mobileren_caizhenji = new General(this, "mobileren_caizhenji", "wei", 3, false);
    mobileren_caizhenji->addSkill(new MobileRenSheyi);
    mobileren_caizhenji->addSkill(new MobileRenTianyin);

    General*mobileren_xujing = new General(this, "mobileren_xujing", "shu", 3);
    mobileren_xujing->addSkill(new MobileRenBoming);
    mobileren_xujing->addSkill(new MobileRenEjian);

    General*mobileren_xiangchong = new General(this, "mobileren_xiangchong", "shu", 4);
    mobileren_xiangchong->addSkill(new MobileRenGuying);
    mobileren_xiangchong->addSkill(new MobileRenMuzhen);
    mobileren_xiangchong->addSkill(new MobileRenMuzhenClear);
    mobileren_xiangchong->addSkill(new MobileShijiActiveQuota("mobilerenmuzhen"));
    related_skills.insert("mobilerenmuzhen", "#mobilerenmuzhen-clear");
    related_skills.insert("mobilerenmuzhen", "#mobilerenmuzhen-quota");

    General*mobileren_liuzhang = new General(this, "mobileren_liuzhang$", "qun", 3);
    mobileren_liuzhang->addSkill(new MobileRenJutu);
    mobileren_liuzhang->addSkill(new MobileRenYaohu);
    mobileren_liuzhang->addSkill(new MobileRenHuaibi);
    mobileren_liuzhang->addSkill(new MobileRenHuaibiMCS);
    related_skills.insert("mobilerenhuaibi", "#mobilerenhuaibi");

    addMetaObject<MobileRenRenshiCard>();
    addMetaObject<MobileRenBuqiCard>();
    addMetaObject<MobileRenBomingCard>();
    addMetaObject<MobileRenMuzhenCard>();
}

ADD_PACKAGE(MobileRen)


class MobileYongXiangzhen : public TriggerSkillV2
{
public:
    MobileYongXiangzhen() : TriggerSkillV2("mobileyongxiangzhen")
    {
        events << CardFinished;
        frequency = Compulsory;
    }

    QList<ServerPlayer *> damageSources(Room *room) const
    {
        const QVariantMap history = room->queryCardUseDamage();
        if (history.contains("error") || !history.value("complete").toBool()) return {};
        QList<ServerPlayer *> result;
        for (const QVariant &item : history.value("items").toList()) {
            const QVariantMap damage = item.toMap().value("data").toMap();
            ServerPlayer *source = room->findPlayerByObjectName(damage.value("from").toString());
            if (source && source->isAlive() && !result.contains(source)) result << source;
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        TriggerList result;
        if (!use.card || !use.card->isKindOf("SavageAssault") || damageSources(room).isEmpty()) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The exact use event separates repeated physical-card uses and independent nested damage.
        ctx.targets = damageSources(room);
        if (ctx.targets.isEmpty()) return false;
        ctx.targets << ctx.owner;
        room->sortByActionOrder(ctx.targets);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MobileYongXiangzhenNullify : public TriggerSkillV2
{
public:
    MobileYongXiangzhenNullify() : TriggerSkillV2("#mobileyongxiangzhen")
    {
        events << CardEffected;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return player && player->isAlive() && player->hasSkill("mobileyongxiangzhen") && effect.to == player
            && effect.card && effect.card->isKindOf("SavageAssault") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target == ctx.original_data->value<CardEffectStruct>().to) {
            room->broadcastSkillInvoke("mobileyongxiangzhen");
            LogMessage log;
            log.type = "#SkillNullify";
            log.from = ctx.owner;
            log.arg = "mobileyongxiangzhen";
            log.arg2 = "savage_assault";
            room->sendLog(log);
            room->notifySkillInvoked(ctx.owner, "mobileyongxiangzhen");
            return true;
        }
        return false;
    }
};

class MobileYongFangzong : public ProhibitSkill
{
public:
    MobileYongFangzong() : ProhibitSkill("mobileyongfangzong")
    {
    }

    bool isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*> &) const
    {
        if (from->getMark("mobileyongxizhan-Clear") > 0 || to->getMark("mobileyongxizhan-Clear") > 0) return false;
        if (!card->isDamageCard() || card->isKindOf("DelayedTrick")) return false;
        return (from->hasSkill(objectName()) && from->inMyAttackRange(to) && from->getPhase() == Player::Play)
			|| (to->hasSkill(objectName()) && from->inMyAttackRange(to));
    }
};

class MobileYongFangzongDraw : public TriggerSkillV2
{
public:
    MobileYongFangzongDraw() : TriggerSkillV2("#mobileyongfangzong")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->getPhase() == Player::Finish && player->hasSkill("mobileyongfangzong")
            && player->getMark("mobileyongxizhan-Clear") == 0 && room->alivePlayerCount() > player->getHandcardNum()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, "mobileyongfangzong", true, true);
        const int count = (room->alivePlayerCount() - target->getHandcardNum()) * getEffectiveAmount(ctx);
        if (count > 0) target->drawCards(count, "mobileyongfangzong");
        return false;
    }
};

class MobileYongXizhan : public TriggerSkillV2
{
public:
    MobileYongXizhan() : TriggerSkillV2("mobileyongxizhan")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->isDead() || player->getPhase() != Player::RoundStart) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->isAlive() && owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.choice = "lose_hp";
        if (ctx.owner->canDiscard(ctx.owner, "he")) {
            std::unique_ptr<const Card> discard(room->askForExchange(ctx.owner, objectName(), 1, 1,
                true, "@mobileyongxizhan-discard", true));
            if (discard && discard->subcardsLength() == 1) {
                const int id = discard->getSubcards().first();
                if (ctx.owner->canDiscard(ctx.owner, id)) {
                    ctx.choice = Sanguosha->getCard(id)->getSuitString();
                    ctx.extra_data = id;
                }
            }
        }
        ctx.targets = {ctx.choice == "lose_hp" || ctx.choice == "heart" ? ctx.owner : ctx.invoker};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "lose_hp") return true;
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != ctx.owner
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        // Disabling Fangzong is an applied character effect for this turn, not a source quota.
        if (ctx.choice != "lose_hp") room->addPlayerMark(ctx.owner, "mobileyongxizhan-Clear");
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "lose_hp") {
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            std::unique_ptr<Card> card;
            ServerPlayer *actor = ctx.owner;
            if (ctx.choice == "spade") { card.reset(new Analeptic(Card::NoSuit, 0)); actor = target; }
            else if (ctx.choice == "club") card.reset(new IronChain(Card::NoSuit, 0));
            else if (ctx.choice == "heart") card.reset(new ExNihilo(Card::NoSuit, 0));
            else if (ctx.choice == "diamond") card.reset(new FireSlash(Card::NoSuit, 0));
            if (!card || !actor || actor->isDead()) break;
            card->setSkillName("_mobileyongxizhan");
            room->setCardFlag(card.get(), "YUANBEN");
            const bool legal = card->isKindOf("Slash") ? actor->canSlash(target, card.get(), false)
                : actor->canUse(card.get(), target, true);
            if (legal) room->useCardFromSkillEffect(CardUseStruct(card.get(), actor, target), ctx, true);
        }
        return false;
    }
};

class MobileYongZaoli : public TriggerSkillV2
{
public:
    MobileYongZaoli() : TriggerSkillV2("mobileyongzaoli")
    {
        events << CardUsed << CardResponded << EventPhaseStart << CardsMoveOneTime << EventPhaseChanging << EventAcquireSkill;
        frequency = Compulsory;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || player != ctx.owner) return;
        if (event == CardsMoveOneTime && ctx.original_data->value<CardsMoveOneTimeStruct>().to != player) return;
        if (event != CardsMoveOneTime && event != EventPhaseStart && event != EventPhaseChanging && event != EventAcquireSkill) return;
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        const QVariant turn = room->historyScopes().value("turn_id");
        QVariantMap state{{"complete", false}};
        if (turn.toLongLong() > 0 && player->getPhase() != Player::NotActive) {
            QVariantMap filter{{"turn_id", turn}, {"to", player->objectName()}, {"limit", 128}};
            QVariantList ids;
            bool complete = true;
            for (;;) {
                const QVariantMap page = room->queryHistoryMoves(filter);
                if (page.contains("error") || !page.value("complete").toBool()) { complete = false; break; }
                if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
                for (const QVariant &value : page.value("items").toList()) {
                    const QVariantMap move = value.toMap().value("data").toMap();
                    if (!move.contains("to_place")) { complete = false; break; }
                    if (move.value("to_place").toInt() == Player::PlaceHand && !ids.contains(move.value("card_id"))) ids << move.value("card_id");
                }
                if (!complete || !page.value("has_more").toBool()) break;
                filter["after"] = page.value("next_after");
            }
            state = QVariantMap{{"complete", complete}, {"ids", ids}};
        }
        // Owner-private projection of authoritative Room movement history; do not broadcast hidden hand identities.
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "zaoli_gained", state);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseStart)
            return player->getPhase() == Player::RoundStart && player->getMark("&myzlli") > 0
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (player->getMark("&myzlli") >= 4) return {};
        bool handcard = false;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            handcard = use.from == player && use.card && !use.card->isKindOf("SkillCard") && use.m_isHandcard;
        } else if (event == CardResponded) {
            const CardResponseStruct response = data.value<CardResponseStruct>();
            handcard = response.m_card && !response.m_card->isKindOf("SkillCard") && response.m_isHandcard;
        }
        return handcard ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        if (event != EventPhaseStart) {
            const int amount = qMin(getEffectiveAmount(ctx), qMax(0, 4 - target->getMark("&myzlli")));
            if (amount > 0) target->gainMark("&myzlli", amount);
            return false;
        }
        int count = target->getMark("&myzlli");
        if (count <= 0) return false;
        target->loseAllMarks("&myzlli");
        if (target->isAlive() && target->canDiscard(target, "he")) {
            const Card *discard = room->askForDiscard(target, objectName(), 999, 1, false, true, "@mobileyongzaoli-discard");
            if (discard) count += discard->subcardsLength();
        }
        if (target->isAlive()) target->drawCards(count * getEffectiveAmount(ctx), objectName());
        if (target->isAlive()) room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return false;
    }
};

// CardLimitSkill has no V2 context API; it consumes only the exact live instance's private history projection.
class MobileYongZaoliBf : public CardLimitSkill
{
public:
    MobileYongZaoliBf() : CardLimitSkill("#mobileyongzaolibf") {}
    QString limitList(const Player *) const override { return "use,response"; }
    QString limitPattern(const Player *target) const override
    {
        if (!target || target->getPhase() != Player::Play) return QString();
        bool found = false;
        QVariantList gained;
        for (int id : target->getValidSkillInstanceIds("mobileyongzaoli")) {
            const QVariantMap state = target->getSkillInstanceStateValue("mobileyongzaoli", id, "zaoli_gained").toMap();
            if (!state.value("complete").toBool()) continue;
            found = true; gained = state.value("ids").toList(); break;
        }
        if (!found) return QString();
        QStringList patterns;
        for (const Card *card : target->getHandcards())
            if (!gained.contains(card->getEffectiveId())) patterns << card->toString();
        return patterns.join(",");
    }
};

MobileYongJungongCard::MobileYongJungongCard()
{
    setSkillName("mobileyongjungong");
    target_fixed = true;
}

void MobileYongJungongCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
    room->addPlayerMark(source, "&mobileyongjungong-Clear");
    int mark = source->getMark("&mobileyongjungong-Clear");
    if (subcardsLength() == 0)
        room->loseHp(HpLostStruct(source, mark, "mobileyongjungong", source));
    if (source->isDead()) return;

    Slash*slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_mobileyongjungong");
    slash->deleteLater();
    if (source->isLocked(slash)) return;

    QList<ServerPlayer*> targets;
    foreach (ServerPlayer*p, room->getOtherPlayers(source)) {
        if (!source->canSlash(p, slash, false)) continue;
        targets << p;
    }
    if (targets.isEmpty()) return;

    if (targets.length() == 1) {
        ServerPlayer*t = targets.first();
        room->useCard(CardUseStruct(slash, source, t));
        return;
    }

    if (room->askForUseCard(source, "@@mobileyongjungong!", "@mobileyongjungong", -1, Card::MethodUse, false)) return;
    ServerPlayer*t = targets.at(qsanRandomBounded(targets.length()));
    room->useCard(CardUseStruct(slash, source, t));
}

class MobileYongJungongVS : public ViewAsSkillV2
{
public:
    MobileYongJungongVS() : ViewAsSkillV2("mobileyongjungong", 999) {}
    LimitScope getLimitScope() const override { return Limit_Custom; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileYongJungongCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
                request.activationRef.key.instanceID, "jungong_stopped").toBool();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !request.initiator->isJilei(card)
            && request.selectedCardIds.size() < request.initiator->getSkillInstanceStateValue(
                request.activationRef.key.skillName, request.activationRef.key.instanceID, "jungong_count").toInt() + 1;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && (request.selectedCardIds.isEmpty() || request.selectedCardIds.size()
            == request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
                request.activationRef.key.instanceID, "jungong_count").toInt() + 1);
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        const ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        return holder && !holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jungong_stopped").toBool();
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID) || ctx.executionID <= 0) return;
        QVariantList executions = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jungong_executions").toList();
        if (executions.contains(ctx.executionID)) return;
        executions << ctx.executionID;
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jungong_executions", executions);
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jungong_count",
            holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jungong_count").toInt() + 1);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jungong_count", 0);
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jungong_stopped", false);
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jungong_executions", QVariantList());
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!isUsable(ctx) || !cardSelectionFeasible(request) || !ctx.initiator) return false;
        for (int id : request.selectedCardIds)
            if (room->getCardOwner(id) != ctx.initiator || !ctx.initiator->canDiscard(ctx.initiator, id)) return false;
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        const int amount = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jungong_count").toInt() + 1;
        addUsage(ctx);
        if (request.selectedCardIds.isEmpty()) room->loseHp(HpLostStruct(ctx.initiator, amount, objectName(), ctx.initiator));
        else if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        const QString oldPattern = Sanguosha->getCurrentCardUsePattern();
        const CardUseStruct::CardUseReason oldReason = Sanguosha->getCurrentCardUseReason();
        room->setCurrentCardUse("@@mobileyongjungong!", CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
        const auto restore = qScopeGuard([&] { room->setCurrentCardUse(oldPattern, oldReason); });
        Slash preview(Card::NoSuit, 0); preview.setSkillName("_mobileyongjungong");
        if (ctx.invoker->isDead() || ctx.invoker->isLocked(&preview)) return ContinueEffects;
        QList<const Player *> selected;
        QList<ServerPlayer *> accepted;
        QMap<ServerPlayer *, int> amounts;
        for (;;) {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *target : room->getOtherPlayers(ctx.invoker))
                if (!selected.contains(target) && preview.targetFilter(selected, target, ctx.invoker)) candidates << target;
            if (candidates.isEmpty()) break;
            ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "@mobileyongjungong", !selected.isEmpty());
            if (!target) break;
            selected << target;
        }
        // Keep the native Slash target filter, including candidate-specific extra targets.
        for (const Player *selectedTarget : selected) {
            SkillContext part = ctx; part.extra_data = QVariantMap();
            skillEffect(part, const_cast<ServerPlayer *>(static_cast<const ServerPlayer *>(selectedTarget)));
            const QVariantMap result = part.extra_data.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(result.value("target").toString());
            if (target && target->isAlive() && !accepted.contains(target)) {
                accepted << target; amounts[target] = result.value("amount").toInt();
            }
        }
        int repeats = 0;
        for (int count : amounts) repeats = qMax(repeats, count);
        for (int i = 0; i < repeats && ctx.invoker->isAlive(); ++i) {
            const std::unique_ptr<Slash> slash(new Slash(Card::NoSuit, 0)); slash->setSkillName("_mobileyongjungong");
            QList<ServerPlayer *> targets; QList<const Player *> validated;
            for (ServerPlayer *target : accepted)
                if (target->isAlive() && amounts.value(target) > i && slash->targetFilter(validated, target, ctx.invoker)) {
                    validated << target; targets << target;
                }
            if (targets.isEmpty()) break;
            CardUseStruct use(slash.get(), ctx.invoker, targets);
            room->useCardFromSkillEffect(use, ctx, true);
        }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.extra_data = QVariantMap{{"target", target->objectName()}, {"amount", getEffectiveAmount(ctx)}};
        return ContinueEffects;
    }
};

class MobileYongJungong : public TriggerSkillV2
{
public:
    MobileYongJungong() : TriggerSkillV2("mobileyongjungong")
    {
        global = true; events << PreCardUsed << DamageDone << CardFinished << EventPhaseChanging << EventSkillInvoking;
        view_as_skill = new MobileYongJungongVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.skill_name == objectName() && accepted.bypass_cost && accepted.executionID > 0
                && accepted.sourceRef.isValid() && accepted.activationRef.isValid())
                static_cast<const MobileYongJungongVS *>(view_as_skill)->addUsage(accepted);
            return true;
        }
        QVariantMap uses = room->getTag("MobileYongJungongUses").toMap();
        const qint64 useID = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (useID > 0 && use.card && use.card->isKindOf("Slash") && use.card->getSkillNames().contains(objectName())
                && use.activationRef.isValid() && use.sourceRef.isValid()) {
                uses[QString::number(useID)] = QVariantMap{{"owner", use.activationRef.ownerObjectName},
                    {"skill", use.activationRef.key.skillName}, {"id", use.activationRef.key.instanceID},
                    {"turn_id", room->historyScopes().value("turn_id")}};
            }
        } else if (event == DamageDone && useID > 0 && uses.contains(QString::number(useID))) {
            const DamageStruct damage = data.value<DamageStruct>();
            const QVariantMap receipt = uses.value(QString::number(useID)).toMap();
            // Confirm against committed damage for this exact use, never a reused Card flag.
            const QVariantMap history = room->queryCardUseDamage(useID);
            if (damage.card && history.value("complete").toBool() && history.value("attribution_complete").toBool()
                && !history.contains("error") && !history.value("items").toList().isEmpty()) {
                ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
                if (holder && holder->hasSkillInstance(receipt.value("skill").toString(), receipt.value("id").toInt()))
                    holder->setSkillInstanceStateValue(receipt.value("skill").toString(), receipt.value("id").toInt(), "jungong_stopped", true);
            }
        } else if (event == CardFinished) uses.remove(QString::number(useID));
        else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            // Match the native TurnClear boundary and include borrowed activation holders.
            for (ServerPlayer *holder : room->getAllPlayers(true))
                for (const SkillInstance &instance : holder->getSkillInstances()) {
                    const QVariant count = holder->getSkillInstanceStateValue(instance.skillName, instance.instanceID, "jungong_count");
                    if (!count.isValid()) continue;
                    holder->setSkillInstanceStateValue(instance.skillName, instance.instanceID, "jungong_count", 0);
                    holder->setSkillInstanceStateValue(instance.skillName, instance.instanceID, "jungong_stopped", false);
                    holder->setSkillInstanceStateValue(instance.skillName, instance.instanceID, "jungong_executions", QVariantList());
                }
            const QVariant turn = room->historyScopes().value("turn_id");
            for (auto it = uses.begin(); it != uses.end(); ) {
                if (it.value().toMap().value("turn_id") == turn) it = uses.erase(it); else ++it;
            }
        }
        room->setTag("MobileYongJungongUses", uses);
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class MobileYongJungongtMod : public TargetModSkillV2
{
public:
    MobileYongJungongtMod() : TargetModSkillV2("#mobileyongjungong-target")
    {
        // The accepted Slash keeps its range permission even if its provider is removed.
        setHolderSelector(CorrectSkill_System);
    }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == DistanceLimit && ctx.card && ctx.card->getSkillNames().contains("mobileyongjungong")
            ? CorrectSkillResult::useAmount(1000) : CorrectSkillResult::noEffect();
    }
};

class MobileYongDengli : public TriggerSkillV2
{
public:
    MobileYongDengli() : TriggerSkillV2("mobileyongdengli")
    {
        events << TargetSpecifying << TargetConfirming;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || player->isDead() || !player->hasSkill(objectName()) || !use.card || !use.card->isKindOf("Slash")) return {};
        QStringList subjects;
        if (event == TargetSpecifying && use.from == player) {
            for (ServerPlayer *target : use.to)
                if (target->isAlive() && target->getHp() == player->getHp()) subjects << target->objectName();
        } else if (event == TargetConfirming && use.to.contains(player) && use.from
            && use.from->isAlive() && use.from->getHp() == player->getHp()) subjects << use.from->objectName();
        return subjects.isEmpty() ? TriggerList() : TriggerList{{player, {objectName() + "->" + subjects.join("+")}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *subject = ctx.preferredTarget;
        if (!subject || subject->isDead() || subject->getHp() != ctx.owner->getHp()
            || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        // The matching Slash participant determines eligibility; the draw recipient is the owner.
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

static void mobileJimieProgress(ServerPlayer *player, const QString &branch)
{
    // Each spent Jimie instance waits for both subsequently applied Yuli branches.
    bool banned = false, first = false, second = false;
    for (const SkillInstance &instance : player->getSkillInstances()) {
        QVariantMap progress = player->getSkillInstanceStateValue(instance.skillName, instance.instanceID, "jimie_progress").toMap();
        if (!progress.value("banned").toBool()) continue;
        if (!branch.isEmpty()) progress[branch] = true;
        if (progress.value("first").toBool() && progress.value("second").toBool()) progress["banned"] = false;
        player->setSkillInstanceStateValue(instance.skillName, instance.instanceID, "jimie_progress", progress);
        if (progress.value("banned").toBool()) {
            banned = true; first = first || progress.value("first").toBool(); second = second || progress.value("second").toBool();
        }
    }
    player->getRoom()->setPlayerMark(player, "jimieBan", banned ? 1 : 0);
    player->getRoom()->setPlayerMark(player, "&yuli+1", first ? 1 : 0);
    player->getRoom()->setPlayerMark(player, "&yuli+2", second ? 1 : 0);
}

class Yuli : public TriggerSkillV2
{
public:
    Yuli() : TriggerSkillV2("yuli")
    {
        events << DamageCaused << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || player->isDead() || !player->hasSkill(objectName()) || !damage.to) return {};
        const bool eligible = event == DamageCaused ? damage.from == player
            : damage.to == player && damage.nature == DamageStruct::Thunder;
        return eligible ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        // A redirected skill target cannot mutate a different pending damage recipient.
        if (damage.to != target) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        if (event == DamageCaused) {
            if (damage.nature == DamageStruct::Thunder)
                ctx.owner->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
            else {
                damage.nature = DamageStruct::Thunder;
                ctx.original_data->setValue(damage);
            }
            mobileJimieProgress(ctx.owner, "first");
        } else if (damage.nature == DamageStruct::Thunder) {
            const int prevented = damage.damage;
            target->damageRevises(*ctx.original_data, -prevented);
            target->drawCards(prevented * getEffectiveAmount(ctx), objectName());
            mobileJimieProgress(ctx.owner, "second");
            return true;
        }
        return false;
    }
};

class Tingwei : public TriggerSkillV2
{
public:
    Tingwei() : TriggerSkillV2("tingwei")
    { global = true; frequency = Compulsory; events << TargetSpecified << ConfirmDamage << CardFinished << EventPhaseChanging; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        QVariantMap uses = room->getTag("TingweiDamageReceipts").toMap();
        if (event == CardFinished) {
            const qint64 id = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            uses.remove(QString::number(id));
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            for (auto it = uses.begin(); it != uses.end(); ) {
                if (room->historyEvent(it.key().toLongLong()).value("turn_id").toLongLong() == turn) it = uses.erase(it);
                else ++it;
            }
        }
        room->setTag("TingweiDamageReceipts", uses);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event == TargetSpecified) return false;
        if (event != ConfirmDamage) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash") || !damage.to) return true;
        const qint64 id = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (id <= 0) return true;
        for (const QVariant &value : room->getTag("TingweiDamageReceipts").toMap().value(QString::number(id)).toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("target").toString() != damage.to->objectName()) continue;
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true); ctx.invoker = ctx.initiator;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.initiator || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt(); ctx.is_forced = true;
            ctx.amount = receipt.value("amount").toInt(); ctx.targets = {damage.to}; ctx.choice = "damage";
            ctx.extra_data = QVariantMap{{"use_id", id}, {"receipt", receipt}}; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        return room->getTag("TingweiDamageReceipts").toMap().value(QString::number(ctx.extra_data.toMap().value("use_id").toLongLong()))
            .toList().contains(ctx.extra_data.toMap().value("receipt"));
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return event == TargetSpecified && player && player->isAlive() && player->hasSkill(objectName())
            && use.from == player && use.card && use.card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != TargetSpecified) return true;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : use.to) if (target->isAlive()) candidates << target;
        ctx.invoker = ctx.owner;
        if (!candidates.isEmpty()) {
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "tingwei0");
            if (target) ctx.targets = {target};
        }
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != TargetSpecified) return false;
        ctx.manual_effect = true;
        SkillContext gain = ctx; gain.choice = "gain"; skillEffect(event, room, ctx.owner, gain, ctx.invoker);
        for (ServerPlayer *target : ctx.targets) {
            SkillContext options = ctx; options.choice = "options";
            skillEffect(event, room, ctx.owner, options, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "damage") {
            if (ctx.original_data->value<DamageStruct>().to == target) target->damageRevises(*ctx.original_data, amount);
            return false;
        }
        if (ctx.choice == "gain") {
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            if (amount > 0) target->gainMark("&ting_wei", 4 * amount);
            return false;
        }
        if (ctx.choice == "give") {
            const QVariantMap state = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(state.value("giver").toString());
            const int id = state.value("card_id", -1).toInt();
            if (giver && giver->isAlive() && id >= 0 && room->getCardOwner(id) == giver
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                room->giveCard(giver, target, Sanguosha->getCard(id), objectName());
            return false;
        }
        QStringList choices{"1", "3", "cancel"};
        for (const Card *card : target->getCards("he"))
            if (card->isKindOf("EquipCard")) { choices << "2=" + ctx.invoker->objectName(); break; }
        if (target->canDiscard("he")) choices << "4";
        bool accepted = false;
        for (int i = 0; i < 4 && target->isAlive() && ctx.invoker->isAlive(); ++i) {
            const QString choice = room->askForChoice(target, objectName(), choices.join("+"), *ctx.original_data);
            if (choice == "cancel" || !choices.contains(choice)) break;
            choices.removeOne(choice); accepted = true;
            ctx.invoker->loseMark("&ting_wei");
            if (choice == "1") {
                // An applied suppression lasts to the recipient's native SelfClear boundary.
                room->setPlayerMark(target, "&tingwei+1-SelfClear", 1);
            } else if (choice.startsWith("2=")) {
                const Card *card = room->askForCard(target, "EquipCard!", "tingwei20" + ctx.invoker->objectName(), *ctx.original_data, Card::MethodNone);
                if (card) {
                    SkillContext give = ctx; give.choice = "give";
                    give.extra_data = QVariantMap{{"giver", target->objectName()}, {"card_id", card->getEffectiveId()}};
                    skillEffect(event, room, ctx.owner, give, ctx.invoker);
                }
            } else if (choice == "3") {
                const qint64 useID = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
                const qint64 previous = room->getTag("TingweiSequence").toLongLong();
                if (useID <= 0 || previous < 0 || previous >= INT_MAX) continue;
                const int serial = int(previous + 1); room->setTag("TingweiSequence", serial);
                QVariantMap uses = room->getTag("TingweiDamageReceipts").toMap();
                QVariantList receipts = uses.value(QString::number(useID)).toList();
                receipts << QVariantMap{{"serial", serial}, {"target", target->objectName()}, {"owner", ctx.owner->objectName()},
                    {"actor", ctx.invoker->objectName()}, {"amount", amount},
                    {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                    {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
                uses[QString::number(useID)] = receipts; room->setTag("TingweiDamageReceipts", uses);
            } else {
                QList<const Card *> cards = target->getCards("he"); qsanShuffle(cards);
                QList<int> ids;
                for (const Card *card : cards) {
                    if (ids.size() >= amount) break;
                    if (target->canDiscard(target, card->getEffectiveId())) ids << card->getEffectiveId();
                }
                if (!ids.isEmpty()) room->throwCard(ids, objectName(), target);
            }
        }
        if (!accepted && target->isAlive()) room->setPlayerChained(target, true);
        return false;
    }
};

class TingweiBf : public InvaliditySkill
{
public:
	TingweiBf() : InvaliditySkill("#TingweiBf")
	{
	}

	bool isSkillValid(const Player *player, const Skill *skill) const
	{
		return player->getMark("&tingwei+1-SelfClear")<1||skill->getFrequency(player)==Compulsory;
	}
};

class Jimie : public TriggerSkillV2
{
public:
    Jimie() : TriggerSkillV2("jimie")
    { events << EventPhaseEnd << EventSkillInvoking; setProperty("angyang_skill", true); }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        const ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        return holder && !holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jimie_progress").toMap().value("banned").toBool();
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID) || !checkCustomUsage(ctx)) return;
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jimie_progress",
            QVariantMap{{"banned", true}, {"first", false}, {"second", false}});
        mobileJimieProgress(holder, QString());
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) {
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "jimie_progress", QVariantMap());
            mobileJimieProgress(holder, QString());
        }
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.bypass_cost && accepted.executionID == 0 && accepted.skill_name == objectName()
            && accepted.activationRef == ctx.activationRef && accepted.sourceRef == ctx.sourceRef) addUsage(accepted);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseEnd && player && player->isAlive() && player->getPhase() == Player::Play
            && player->hasSkill(objectName()) && player->getMark("&ting_wei") >= 8
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName() + "$-1", "jimie", true, true);
        if (!target) return false;
        ctx.targets = {target}; ctx.invoker = ctx.owner;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || ctx.owner->getMark("&ting_wei") < 8) return false;
        addUsage(ctx);
        ctx.owner->loseMark("&ting_wei", 8);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Preserve this package version's current-HP damage rule.
        const int amount = qMax(0, target->getHp()) * getEffectiveAmount(ctx);
        if (amount > 0) room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        return false;
    }
};


MobileYongPackage::MobileYongPackage()
    : Package("mobileyong")
{
    General*mobileyong_huaman = new General(this, "mobileyong_huaman", "shu", 4, false);
    mobileyong_huaman->addSkill(new MobileYongXiangzhen);
    mobileyong_huaman->addSkill(new MobileYongXiangzhenNullify);
    mobileyong_huaman->addSkill(new MobileYongFangzong);
    mobileyong_huaman->addSkill(new MobileYongFangzongDraw);
    mobileyong_huaman->addSkill(new MobileYongXizhan);
    related_skills.insert("mobileyongxiangzhen", "#mobileyongxiangzhen");
    related_skills.insert("mobileyongfangzong", "#mobileyongfangzong");

    General*mobileyong_sunyi = new General(this, "mobileyong_sunyi", "wu", 4);
    mobileyong_sunyi->addSkill(new MobileYongZaoli);
    mobileyong_sunyi->addSkill(new MobileYongZaoliBf);
    related_skills.insert("mobileyongzaoli", "#mobileyongzaolibf");

    General*mobileyong_gaolan = new General(this, "mobileyong_gaolan", "qun", 4);
    mobileyong_gaolan->addSkill(new MobileYongJungong);
    mobileyong_gaolan->addSkill(new MobileYongJungongtMod);
    mobileyong_gaolan->addSkill(new MobileYongDengli);
    related_skills.insert("mobileyongjungong", "#mobileyongjungong-target");

    General *yong_shenmachao = new General(this, "yong_shenmachao", "god", 4);
    yong_shenmachao->addSkill(new Yuli);
    yong_shenmachao->addSkill(new Tingwei);
    yong_shenmachao->addSkill(new TingweiBf);
    yong_shenmachao->addSkill(new Jimie);

    addMetaObject<MobileYongJungongCard>();
}

ADD_PACKAGE(MobileYong)


MobileYanYajunCard::MobileYanYajunCard()
{
    will_throw = false;
    handling_method = Card::MethodPindian;
}

bool MobileYanYajunCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void MobileYanYajunCard::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.from->getRoom();
    ServerPlayer*from = effect.from,*to = effect.to;

    PindianStruct*pindian = from->PinDian(to, "mobileyanyajun", this);
    if (pindian->success) {
        QList<int> pindian_ids;
        if (room->CardInPlace(pindian->from_card, Player::DiscardPile))
            pindian_ids << pindian->from_card->getEffectiveId();
        if (room->CardInPlace(pindian->to_card, Player::DiscardPile) && !pindian_ids.contains(pindian->to_card->getEffectiveId()))
            pindian_ids << pindian->to_card->getEffectiveId();
        if (pindian_ids.isEmpty()) return;

        room->notifyMoveToPile(from, pindian_ids, "mobileyanyajun", Player::DiscardPile, true);

        try {
            room->askForUseCard(from, "@@mobileyanyajun2", "@mobileyanyajun2", 2, Card::MethodNone);
        }
        catch (TriggerEvent triggerEvent) {
            if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                room->notifyMoveToPile(from, pindian_ids, "mobileyanyajun", Player::DiscardPile, false);
            throw triggerEvent;
        }

        room->notifyMoveToPile(from, pindian_ids, "mobileyanyajun", Player::DiscardPile, false);

    } else
        room->addMaxCards(from, -1);
}

MobileYanYajunPutCard::MobileYanYajunPutCard()
{
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
    m_skillName = "mobileyanyajun";
}

void MobileYanYajunPutCard::onUse(Room*room, CardUseStruct &card_use) const
{
    LogMessage log;
    log.type = "$YinshicaiPut";
    log.from = card_use.from;
    log.card_str = ListI2S(subcards).join("+");
    room->sendLog(log);
    CardMoveReason reason(CardMoveReason::S_REASON_PUT, card_use.from->objectName(), "mobileyanyajun", "");
    room->moveCardTo(this, nullptr, Player::DrawPile, reason, true);
}

class MobileYanYajun : public TriggerSkillV2
{
public:
    MobileYanYajun() : TriggerSkillV2("mobileyanyajun") { events << DrawNCards << EventPhaseStart; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.is_forced = ctx.current_event == DrawNCards;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    QList<int> gainedCards(Room *room, ServerPlayer *owner) const
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return {};
        QVariantMap filter{{"turn_id", turn}, {"to", owner->objectName()}, {"limit", 128}};
        QList<int> result;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return {};
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap move = item.toMap().value("data").toMap();
                if (!move.contains("to_place")) return {};
                if (move.value("to_place").toInt() != Player::PlaceHand) continue;
                const int id = move.value("card_id", -1).toInt();
                if (id >= 0 && owner->handCards().contains(id) && !result.contains(id)) result << id;
            }
            if (!page.value("has_more").toBool()) return result;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        const bool eligible = event == DrawNCards ? data.value<DrawStruct>().reason == "draw_phase"
            : player->getPhase() == Player::Play && player->canPindian() && !gainedCards(room, player).isEmpty();
        return eligible ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DrawNCards) { ctx.targets = {ctx.owner}; return true; }
        const QList<int> cards = gainedCards(room, ctx.owner);
        if (cards.isEmpty()) return false;
        int id = -1;
        {
            room->fillAG(cards, ctx.owner);
            auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
            id = room->askForAG(ctx.owner, cards, true, objectName());
        }
        if (!cards.contains(id)) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner))
            if (ctx.owner->canPindian(target)) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@mobileyanyajun1", true, true);
        if (!target) return false;
        ctx.extra_data = id; ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            if (target != ctx.invoker || draw.reason != "draw_phase") return false;
            draw.num += getEffectiveAmount(ctx);
            ctx.original_data->setValue(draw);
            return false;
        }
        if (ctx.choice == "penalty") {
            room->addMaxCards(target, -getEffectiveAmount(ctx));
            return false;
        }
        if (ctx.choice == "put") {
            QList<int> cards;
            for (int id : ListV2I(ctx.extra_data.toList()))
                if (room->getCardPlace(id) == Player::DiscardPile) cards << id;
            if (cards.isEmpty()) return false;
            int id = -1;
            {
                room->fillAG(cards, target);
                auto clear = qScopeGuard([&] { room->clearAG(target); });
                id = room->askForAG(target, cards, true, objectName());
            }
            if (cards.contains(id) && room->getCardPlace(id) == Player::DiscardPile)
                room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPile,
                    CardMoveReason(CardMoveReason::S_REASON_PUT, target->objectName(), objectName(), ""), true);
            return false;
        }
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner->handCards().contains(id) || !ctx.owner->canPindian(target)) return false;
        PindianStruct *pindian = ctx.owner->PinDian(target, objectName(), Sanguosha->getCard(id));
        if (!pindian || ctx.owner->isDead()) return false;
        SkillContext result = ctx;
        if (!pindian->success) result.choice = "penalty";
        else {
            QList<int> cards;
            for (const Card *card : {pindian->from_card, pindian->to_card})
                if (card && room->CardInPlace(card, Player::DiscardPile) && !cards.contains(card->getEffectiveId()))
                    cards << card->getEffectiveId();
            if (cards.isEmpty()) return false;
            result.choice = "put";
            result.extra_data = ListI2V(cards);
        }
        // The opponent's pindian hook is distinct from the owner's penalty or pile-ordering benefit.
        skillEffect(event, room, player, result, ctx.owner);
        return false;
    }
};

MobileYanZundiCard::MobileYanZundiCard()
{
    setSkillName("mobileyanzundi");
}

bool MobileYanZundiCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
    return targets.isEmpty();
}

void MobileYanZundiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer*from = effect.from,*to = effect.to;
    if (from->isDead()) return;
    Room*room = from->getRoom();

    JudgeStruct judge;
    judge.who = from;
    judge.pattern = ".";
    judge.reason = "mobileyanzundi";
    judge.play_animation = false;
    room->judge(judge);

    if (to->isDead()) return;
    QString color = judge.pattern;
    if (color == "red")
        room->moveField(to, "mobileyanzundi", true, "ej");
    else if (color == "black")
        to->drawCards(3, "mobileyanzundi");
}

class MobileYanZundi : public ViewAsSkillV2
{
public:
    MobileYanZundi() : ViewAsSkillV2("mobileyanzundi", 1)
    {
        m_baseAmount = 3;
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileYanZundiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && !card->isVirtualCard()
            && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId())
            && !request.initiator->isJilei(card);
    }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        JudgeStruct judge;
        judge.who = ctx.invoker;
        judge.pattern = ".";
        judge.reason = objectName();
        judge.play_animation = false;
        room->judge(judge);
        if (target->isDead() || !judge.card) return ContinueEffects;
        // The judgement's physical card remains authoritative without a global FinishJudge observer.
        if (judge.card->isRed()) room->moveField(target, objectName(), true, "ej");
        else if (judge.card->isBlack()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class MobileYanDifei : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileYanDifei() : TriggerSkillV2("mobileyandifei")
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
    LimitScope getLimitScope() const override { return Limit_Turn; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        return player && player->isAlive() && player->hasSkill(objectName()) && room->hasCurrent()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = target;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const bool suited = damage.card && damage.card->hasSuit() && !damage.card->isKindOf("SkillCard");
        const Card::Suit suit = suited ? damage.card->getSuit() : Card::NoSuit;
        room->sendCompulsoryTriggerLog(player, this);
        if (!player->canDiscard(player, "he") || !room->askForDiscard(player, objectName(), 1, 1, true, true, "@mobileyandifei-discard"))
            player->drawCards(getEffectiveAmount(ctx), objectName());
        if (player->isDead() || player->isKongcheng()) return false;
        room->showAllCards(player);
        if (!suited) return false;
        foreach (const Card*card, player->getHandcards()) {
            if (card->getSuit() == suit) return false;
        }
        room->recover(player, RecoverStruct("mobileyandifei", player, getEffectiveAmount(ctx)));
        return false;
    }
};

MobileYanYanjiaoCard::MobileYanYanjiaoCard()
{
    setSkillName("mobileyanyanjiao");
}

void MobileYanYanjiaoCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer*from = effect.from,*to = effect.to;
    Room*room = from->getRoom();
    QStringList suits;
    foreach (const Card*card, from->getHandcards()) {
        QString suit = card->getSuitString();
        if (suits.contains(suit)) continue;
        suits << suit;
    }
    if (suits.isEmpty()) return;

    QString suit = room->askForChoice(from, "mobileyanyanjiao", suits.join("+"), QVariant::fromValue(to));
    DummyCard*dummy = new DummyCard();
    dummy->deleteLater();
    foreach (const Card*card, from->getHandcards()) {
        if (card->getSuitString() == suit)
        dummy->addSubcard(card);
    }
    if (dummy->subcardsLength() <= 0) return;

    room->addPlayerMark(from, "&mobileyanyanjiao_draw", dummy->subcardsLength());
    room->giveCard(from, to, dummy, "mobileyanyanjiao");
    room->damage(DamageStruct("mobileyanyanjiao", from, to));
}

class MobileYanYanjiaoVS : public ViewAsSkillV2
{
public:
    MobileYanYanjiaoVS() : ViewAsSkillV2("mobileyanyanjiao") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileYanYanjiaoCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng(); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *giver = ctx.invoker;
        Room *room = giver->getRoom();
        QStringList suits;
        for (const Card *card : giver->getHandcards())
            if (!suits.contains(card->getSuitString())) suits << card->getSuitString();
        if (suits.isEmpty()) return ContinueEffects;
        const QString suit = room->askForChoice(giver, objectName(), suits.join("+"), QVariant::fromValue(target));
        QList<int> ids;
        for (const Card *card : giver->getHandcards()) if (card->getSuitString() == suit) ids << card->getEffectiveId();
        if (ids.isEmpty()) return ContinueEffects;
        const qint64 previous = room->getTag("MobileYanYanjiaoSequence").toLongLong();
        if (previous < 0 || previous >= INT_MAX) return ContinueEffects;
        const int serial = int(previous + 1); room->setTag("MobileYanYanjiaoSequence", serial);
        QVariantList receipts = giver->getTag("MobileYanYanjiaoReceipts").toList();
        const int amount = ids.size() * getEffectiveAmount(ctx);
        receipts << QVariantMap{{"serial", serial}, {"owner", ctx.owner->objectName()}, {"actor", giver->objectName()},
            {"amount", amount}, {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        giver->setTag("MobileYanYanjiaoReceipts", receipts);
        room->addPlayerMark(giver, "&mobileyanyanjiao_draw", amount);
        QList<int> live;
        for (int id : ids) if (giver->handCards().contains(id)) live << id;
        if (!live.isEmpty() && giver->isAlive() && target->isAlive()) {
            DummyCard cards(live); room->giveCard(giver, target, &cards, objectName());
        }
        if (target->isAlive()) room->damage(DamageStruct(objectName(), giver, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class MobileYanYanjiao : public TriggerSkillV2
{
public:
    MobileYanYanjiao() : TriggerSkillV2("mobileyanyanjiao")
    {
        global = true; frequency = Compulsory; events << EventPhaseStart << EventPhaseEnd << EventPhaseChanging << Death;
        view_as_skill = new MobileYanYanjiaoVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        QVariantMap due = player->getTag("MobileYanYanjiaoDue").toMap();
        const qint64 phase = room->historyScopes().value("phase_id").toLongLong();
        if (event == EventPhaseStart && player->getPhase() == Player::RoundStart && phase > 0) {
            due[QString::number(phase)] = player->getTag("MobileYanYanjiaoReceipts").toList();
            player->setTag("MobileYanYanjiaoDue", due);
            player->removeTag("MobileYanYanjiaoReceipts");
            room->setPlayerMark(player, "&mobileyanyanjiao_draw", 0);
            return true;
        }
        if (event == EventPhaseEnd && player->getPhase() == Player::RoundStart) due.remove(QString::number(phase));
        else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            for (auto it = due.begin(); it != due.end(); ) {
                if (room->historyEvent(it.key().toLongLong()).value("turn_id").toLongLong() == turn) it = due.erase(it);
                else ++it;
            }
        } else if (event == Death && data.value<DeathStruct>().who == player) {
            due.clear(); player->removeTag("MobileYanYanjiaoReceipts");
            player->setTag("MobileYanYanjiaoDue", due);
            room->setPlayerMark(player, "&mobileyanyanjiao_draw", 0);
            return true;
        }
        player->setTag("MobileYanYanjiaoDue", due);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->isDead() || player->getPhase() != Player::RoundStart) return true;
        const QString phase = room->historyScopes().value("phase_id").toString();
        for (const QVariant &value : player->getTag("MobileYanYanjiaoDue").toMap().value(phase).toList()) {
            const QVariantMap receipt = value.toMap();
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = player; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.extra_data = QVariantMap{{"receipt", receipt}, {"phase", phase}};
            ctx.amount = receipt.value("amount").toInt(); ctx.original_data = &data; ctx.current_event = event;
            ctx.is_forced = true; ctx.targets = {player}; contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.initiator; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        const QVariantMap values = ctx.extra_data.toMap();
        return ctx.initiator && ctx.initiator->getTag("MobileYanYanjiaoDue").toMap()
            .value(values.value("phase").toString()).toList().contains(values.value("receipt"));
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap values = ctx.extra_data.toMap();
        QVariantMap due = ctx.initiator->getTag("MobileYanYanjiaoDue").toMap();
        QVariantList receipts = due.value(values.value("phase").toString()).toList();
        receipts.removeAll(values.value("receipt")); due[values.value("phase").toString()] = receipts;
        ctx.initiator->setTag("MobileYanYanjiaoDue", due);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MobileYanZhenting : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileYanZhenting() : TriggerSkillV2("mobileyanzhenting") { events << TargetConfirming << EventSkillInvoking; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool canReplace(ServerPlayer *owner, ServerPlayer *victim, const CardUseStruct &use) const
    {
        return owner && owner->isAlive() && victim && victim->isAlive() && owner != victim
            && use.card && (use.card->isKindOf("Slash") || use.card->isKindOf("DelayedTrick"))
            && use.to.contains(victim) && !use.to.contains(owner) && use.from && use.from != owner
            && owner->inMyAttackRange(victim)
            && (!use.card->isKindOf("DelayedTrick") || !owner->containsTrick(use.card->objectName()));
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        TriggerList result;
        if (!room->hasCurrent()) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && canReplace(owner, player, use)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!canReplace(ctx.owner, ctx.invoker, use)
            || !ctx.owner->askForSkillInvoke(this, "mobileyanzhenting_replace:" + ctx.invoker->objectName()
                + "::" + use.card->objectName())) return false;
        ctx.targets = {ctx.invoker};
        ctx.choice = "replace";
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        if (ctx.choice == "discard") {
            for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive()
                && ctx.owner->canDiscard(target, "h"); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "h", objectName(), false, Card::MethodDiscard);
                if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
                    || !ctx.owner->canDiscard(target, id)) break;
                room->throwCard(id, target, ctx.owner);
            }
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!canReplace(ctx.owner, target, use)) return false;
        room->broadcastSkillInvoke(this);
        use.to.removeOne(target);
        use.to << ctx.owner;
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        QStringList choices;
        if (use.from->isAlive() && ctx.owner->canDiscard(use.from, "h")) choices << "discard=" + use.from->objectName();
        choices << "draw" << "cancel";
        const QString choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), QVariant::fromValue(use.from));
        if (choice != "cancel") {
            // Replacement and the optional follow-up have distinct recipients and target hooks.
            SkillContext followup = ctx;
            followup.choice = choice == "draw" ? "draw" : "discard";
            ServerPlayer *recipient = choice == "draw" ? ctx.owner : use.from;
            followup.targets = {recipient};
            skillEffect(event, room, ctx.owner, followup, recipient);
        }
        if (ctx.owner->isAlive() && ctx.original_data->value<CardUseStruct>().to.contains(ctx.owner))
            room->getThread()->trigger(TargetConfirming, room, ctx.owner, *ctx.original_data);
        return false;
    }
};
MobileYanJincuiCard::MobileYanJincuiCard()
{
    setSkillName("mobileyanjincui");
}

void MobileYanJincuiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer*from = effect.from,*to = effect.to;
    Room*room = from->getRoom();

    room->removePlayerMark(from, "@mobileyanjincuiMark");
    room->doSuperLightbox(from, "mobileyanjincui");

    room->swapSeat(from, to);

    if (from->isDead()) return;
    int hp = from->getHp();
    if (hp > 0)
        room->loseHp(HpLostStruct(from, hp, "mobileyanjincui", from));
}

class MobileYanJincui : public ViewAsSkillV2
{
public:
    MobileYanJincui() : ViewAsSkillV2("mobileyanjincui")
    {
        frequency = Limited;
        limit_mark = "@mobileyanjincuiMark";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileYanJincuiCard"; }
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
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        room->removePlayerMark(ctx.initiator, "@mobileyanjincuiMark");
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        room->doSuperLightbox(ctx.invoker, objectName());
        room->swapSeat(ctx.invoker, target);
        // Losing HP follows the swap; it is not a pre-effect payment.
        if (ctx.invoker->isAlive() && ctx.invoker->getHp() > 0)
            room->loseHp(HpLostStruct(ctx.invoker, ctx.invoker->getHp(), objectName(), ctx.invoker));
        return ContinueEffects;
    }
};

class MobileYanJianyi : public TriggerSkillV2
{
public:
    MobileYanJianyi() : TriggerSkillV2("mobileyanjianyi")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }
    QList<int> candidates(Room *room) const
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toString().isEmpty() || turn.toString() == "0") return {};
        QVariantMap filter{{"turn_id", turn}, {"limit", 128}};
        QList<int> result;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return {};
            foreach (const QVariant &item, page.value("items").toList()) {
                const QVariantMap move = item.toMap().value("data").toMap();
                if (!move.contains("reason") || !move.contains("to_place")) return {};
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD
                    || move.value("to_place").toInt() != Player::DiscardPile) continue;
                const QVariantMap before = move.value("card_before").toMap();
                if (!before.contains("classes")) return {};
                const int id = move.value("card_id", -1).toInt();
                if (id >= 0 && before.value("classes").toStringList().contains("Armor")
                    && room->getCardPlace(id) == Player::DiscardPile && !result.contains(id)) result << id;
            }
            if (!page.value("has_more").toBool()) return result;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        TriggerList result;
        if (!actor || actor->getPhase() != Player::NotActive || candidates(room).isEmpty()) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(actor)) {
            if (owner->hasSkill(objectName())) result.insert(owner, {objectName()});
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> ids = candidates(room);
        if (ids.isEmpty()) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        DummyCard cards;
        const int count = qMin(getEffectiveAmount(ctx), ids.size());
        for (int i = 0; i < count && ctx.owner->isAlive(); ++i) {
            room->fillAG(ids, ctx.owner);
            int id = room->askForAG(ctx.owner, ids, false, objectName());
            room->clearAG(ctx.owner);
            if (!ids.removeOne(id)) break;
            if (room->getCardPlace(id) == Player::DiscardPile) cards.addSubcard(id);
        }
        // Each instance chooses from the current remainder of the same immutable history.
        QList<int> payable;
        foreach (int id, cards.getSubcards()) {
            if (room->getCardPlace(id) == Player::DiscardPile) payable << id;
        }
        if (target->isAlive() && !payable.isEmpty()) {
            DummyCard current(payable);
            room->obtainCard(target, &current);
        }
        return false;
    }
};

MobileYanShangyiCard::MobileYanShangyiCard()
{
    setSkillName("mobileyanshangyi");
}

bool MobileYanShangyiCard::targetFilter(const QList<const Player*> &targets, const Player*to_selet, const Player*Self) const
{
    return targets.isEmpty() && to_selet != Self && !to_selet->isKongcheng();
}

void MobileYanShangyiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer*from = effect.from,*to = effect.to;
    Room*room = from->getRoom();

    if (!from->isKongcheng())
        room->doGongxin(to, from, QList<int>(), "mobileyanjincui");
    if (to->isAlive() && !to->isKongcheng()) {
        int id = room->doGongxin(from, to, to->handCards(), "mobileyanjincui");
        if (id < 0) id = to->getRandomHandCardId();
        room->obtainCard(from, id, false);
    }
}

class MobileYanShangyi : public ViewAsSkillV2
{
public:
    MobileYanShangyi() : ViewAsSkillV2("mobileyanshangyi", 1)
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileYanShangyiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.isEmpty() && !request.initiator->isJilei(card)
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target != request.initiator && selected.isEmpty() && !target->isKongcheng();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (!ctx.invoker->isKongcheng()) room->doGongxin(target, ctx.invoker, QList<int>(), objectName());
        if (target->isAlive() && !target->isKongcheng() && ctx.invoker->isAlive()) {
            int id = room->doGongxin(ctx.invoker, target, target->handCards(), objectName());
            if (id < 0) id = target->getRandomHandCardId();
            if (id >= 0 && room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand)
                room->obtainCard(ctx.invoker, id, false);
        }
        return ContinueEffects;
    }
};

class Tiantao : public TriggerSkillV2
{
public:
    Tiantao() : TriggerSkillV2("tiantao")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish && player->canDiscard("hej")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QStringList choices;
        if (ctx.owner->canDiscard("h")) choices << "tiantao0=hand_area";
        if (ctx.owner->canDiscard("e")) choices << "tiantao0=equip_area";
        if (ctx.owner->canDiscard("j")) choices << "tiantao0=judge_area";
        if (choices.isEmpty()) return false;
        const QString choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), *ctx.original_data);
        ctx.choice = choice == "tiantao0=hand_area" ? "h" : choice == "tiantao0=equip_area" ? "e" : "j";
        int count = 0;
        bool slash = false;
        for (const Card *card : ctx.owner->getCards(ctx.choice)) {
            if (!ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) continue;
            ++count;
            slash = slash || card->isKindOf("Slash");
        }
        ctx.extra_data = QVariantMap{{"count", count}, {"self_penalty", !slash}};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<int> ids;
        bool slash = false;
        for (const Card *card : ctx.owner->getCards(ctx.choice)) {
            if (!ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) continue;
            ids << card->getEffectiveId();
            slash = slash || card->isKindOf("Slash");
        }
        if (ids.isEmpty()) return false;
        // Freeze the paid area and Slash result before discarding can trigger nested effects.
        ctx.extra_data = QVariantMap{{"count", ids.size()}, {"self_penalty", !slash}};
        room->throwCard(ids, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.owner->isDead()) return false;
        const QVariantMap paid = ctx.extra_data.toMap();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner))
            if (ctx.owner->canDiscard(target, ctx.choice)) candidates << target;
        const QList<ServerPlayer *> targets = room->askForPlayersChosen(ctx.owner, candidates, objectName(),
            0, paid.value("count").toInt(), QString("tiantao1:%1").arg(paid.value("count").toInt()));
        QStringList penalties;
        if (paid.value("self_penalty").toBool()) penalties << ctx.owner->objectName();
        for (ServerPlayer *target : targets) {
            if (ctx.owner->isDead()) break;
            SkillContext discard = ctx;
            discard.extra_data = QVariantMap{{"stage", "discard"}, {"area", ctx.choice}};
            skillEffect(event, room, player, discard, target);
            const QString recipient = discard.extra_data.toMap().value("penalty").toString();
            if (!recipient.isEmpty()) penalties << recipient;
        }
        for (const QString &name : penalties) {
            ServerPlayer *target = room->findPlayerByObjectName(name);
            if (!target || target->isDead()) continue;
            SkillContext penalty = ctx;
            penalty.extra_data = QVariantMap{{"stage", "penalty"}};
            skillEffect(event, room, player, penalty, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantMap state = ctx.extra_data.toMap();
        if (state.value("stage").toString() == "penalty") {
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
            return false;
        }
        const QString area = state.value("area").toString();
        if (!ctx.owner->canDiscard(target, area)) return false;
        const int id = room->askForCardChosen(ctx.owner, target, area, objectName(), false, Card::MethodDiscard);
        if (id < 0 || room->getCardOwner(id) != target || !ctx.owner->canDiscard(target, id)) return false;
        const bool slash = Sanguosha->getCard(id)->isKindOf("Slash");
        room->throwCard(id, objectName(), target, ctx.owner);
        if (!slash) {
            state.insert("penalty", target->objectName());
            ctx.extra_data = state;
        }
        return false;
    }
};

ShenpeiCard::ShenpeiCard()
{
}

bool ShenpeiCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
    return targets.isEmpty();
}

void ShenpeiCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
    room->removePlayerMark(source, "@shenpei");
    room->doSuperLightbox(source, "shenpei");
	int n = source->getMark("shenpeiDying");
	if(n<1) return;
	room->recover(source,RecoverStruct("shenpei",source,n));
	room->damage(DamageStruct("shenpei",source,targets.last(),n,DamageStruct::Fire));
	room->acquireSkill(source,"huitian");
}

class Shenpei : public TriggerSkillV2
{
public:
    Shenpei() : TriggerSkillV2("shenpei")
    {
        events << Dying << EventSkillInvoking; frequency = Limited;
        limit_mark = "@shenpei"; waked_skills = "huitian";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    int dyingCount(Room *room, const ServerPlayer *player) const
    {
        QVariantMap filter{{"kind", "dying_start"}, {"player", player->objectName()}, {"limit", 128}};
        int count = 0;
        do {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            count += page.value("items").toList().size();
            if (!page.value("has_more").toBool()) return count;
            filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
        } while (true);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return event == Dying && player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DyingStruct>().who == player && dyingCount(room, player) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = dyingCount(room, ctx.owner);
        if (count <= 0) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "shenpei0", true);
        if (!target) return false;
        ctx.targets = {target}; ctx.extra_data = count;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx); return true;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.executionID != 0 || parseSkillName(accepted.skill_name) != objectName()
            || accepted.sourceRef != ctx.sourceRef || accepted.activationRef != ctx.activationRef) return;
        if (accepted.bypass_cost) addUsage(accepted);
        room->setPlayerMark(ctx.owner, "@shenpei", 0);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        room->doSuperLightbox(ctx.owner, objectName());
        SkillContext heal = ctx; heal.choice = "heal";
        skillEffect(event, room, player, heal, ctx.owner);
        for (ServerPlayer *target : ctx.targets) {
            SkillContext damage = ctx; damage.choice = "damage";
            skillEffect(event, room, player, damage, target);
        }
        SkillContext grant = ctx; grant.choice = "grant";
        skillEffect(event, room, player, grant, ctx.owner);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = ctx.extra_data.toInt() * getEffectiveAmount(ctx);
        if (ctx.choice == "heal") room->recover(target, RecoverStruct(objectName(), ctx.owner, amount));
        else if (ctx.choice == "damage") room->damage(DamageStruct(objectName(), ctx.owner, target, amount, DamageStruct::Fire));
        else room->acquireSkillFromEffect(target, "huitian", ctx);
        return false;
    }
};

class Huitian : public TriggerSkillV2
{
public:
    Huitian() : TriggerSkillV2("huitian") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList list;
        if (!player || player->isDead() || player->getPhase() != Player::Finish) return list;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->getHp() < player->getHp()) list[owner] << objectName();
        return list;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return ctx.owner->askForSkillInvoke(objectName() + "$-1");
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        const qint64 previous = room->getTag("HuitianReceiptSequence").toLongLong();
        if (previous < 0 || previous >= INT_MAX) return false;
        const int serial = int(previous + 1);
        room->setTag("HuitianReceiptSequence", serial);
        QVariantList receipts = target->getTag("HuitianReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"owner", ctx.owner->objectName()}, {"actor", target->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("HuitianReceipts", receipts);
        room->setPlayerMark(target, "&huitian+#num", 1);
        // The native scheduler owns the extra turn and its frozen provenance until the turn boundary.
        room->scheduleExtraTurn(target, ctx.sourceRef, QList<Player::Phase>(), getEffectiveAmount(ctx));
        return false;
    }
};

class HuitianDeath : public TriggerSkillV2
{
public:
    HuitianDeath() : TriggerSkillV2("#huitian-death")
    { global = true; frequency = Compulsory; events << RoundStart << Death; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == RoundStart && player) {
            // This global round boundary spends the liability even if its death effect is canceled.
            player->setTag("HuitianDue", QVariantMap{{"round_id", room->historyScopes().value("round_id")},
                {"receipts", player->getTag("HuitianReceipts").toList()}});
            player->removeTag("HuitianReceipts");
            room->setPlayerMark(player, "&huitian+#num", 0);
        }
        if (event == Death && player && data.value<DeathStruct>().who == player) {
            player->removeTag("HuitianReceipts");
            player->removeTag("HuitianDue");
            room->setPlayerMark(player, "&huitian+#num", 0);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != RoundStart || !player || player->isDead()) return true;
        for (const QVariant &value : player->getTag("HuitianDue").toMap().value("receipts").toList()) {
            const QVariantMap receipt = value.toMap();
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = player; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            // Retained liability is an applied effect; removing the original grant cannot erase it.
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event;
            ctx.is_forced = true; ctx.targets = {player}; contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.initiator) return false;
        const QVariantMap due = ctx.initiator->getTag("HuitianDue").toMap();
        return due.value("round_id").toLongLong() == room->historyScopes().value("round_id").toLongLong()
            && due.value("receipts").toList().contains(ctx.extra_data);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantMap due = ctx.initiator->getTag("HuitianDue").toMap();
        QVariantList receipts = due.value("receipts").toList();
        receipts.removeAll(ctx.extra_data); due["receipts"] = receipts;
        ctx.initiator->setTag("HuitianDue", due);
        room->killPlayer(target);
        return false;
    }
};

XingzhenCard::XingzhenCard()
{
	target_fixed = true;
}

void XingzhenCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
    QList<int>ids = room->getNCards(source->getMaxHp());
	room->notifyMoveToPile(source,ids,"xingzhen");
	const Card*sc = room->askForUseCard(source,"@@xingzhen","xingzhen0",-1,Card::MethodNone);
	room->notifyMoveToPile(source,ids,"xingzhen",Player::PlaceUnknown,false);
	room->returnToTopDrawPile(ids);
	if(sc){
        QList<CardsMoveStruct> moves;
		foreach(int id, sc->getSubcards()){
			if(ids.contains(id)){
				ids.removeOne(id);
				moves << CardsMoveStruct(id, source, Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_OVERRIDE, source->objectName(), "xingzhen", ""));
			}else{
				ids.append(id);
				moves << CardsMoveStruct(id, nullptr, Player::DrawPile,
				CardMoveReason(CardMoveReason::S_REASON_OVERRIDE, source->objectName(), "xingzhen", ""));
			}
		}
		room->moveCardsAtomic(moves,false);
		if(source->isDead()) return;
	}
	room->askForGuanxing(source,ids,Room::GuanxingUpOnly);
	ServerPlayer*tp = room->askForPlayerChosen(source,room->getOtherPlayers(source),"xingzhen","xingzhen1");
	if(tp){
		ids.clear();
		for (int i = 0; i < qMin(source->getMaxHp(),source->getHandcardNum()); i++) {
			int id = room->askForCardChosen(tp,source,"h","xingzhen",false,Card::MethodNone,ids,true);
			if(id<0) break;
			ids << id;
		}
		room->showCard(source,ids);
		ids << room->showDrawPile(tp,source->getMaxHp()-ids.length(),"xingzhen",false);
		foreach(int id, ids){
			sc = Sanguosha->getCard(id);
			if(sc->isKindOf("Slash")&&source->isAlive()&&source->canSlash(tp,sc,false)){
				room->useCard(CardUseStruct(sc,source,tp));
			}
		}
	}
}

// TODO(ruling): ":xingzhen" describes a permanent view of the top 7 draw-pile cards, usable as basic
// cards out of turn and as tricks in turn. This class is a different exchange / guanxing / slash flow.
// Do not migrate or rewrite the procedure until a human rules which version is authoritative.
class Xingzhen : public ViewAsSkill
{
public:
	Xingzhen() : ViewAsSkill("xingzhen")
	{
		expand_pile = "#xingzhen";
	}

    bool viewFilter(const QList<const Card*> &, const Card*to_select) const
    {
        return !to_select->isEquipped();
    }

    const Card*viewAs(const QList<const Card*> &cards) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
        if(pattern=="@@xingzhen"){
			int n = 0;
			foreach(const Card*c, cards){
				if(Self->getPileName(c->getId())==expand_pile)
					n--;
				else
					n++;
			}
			if(n!=0||cards.isEmpty()) return nullptr;
			SkillCard*sc = new XingzhenCard;
			sc->setUserString(pattern);
			sc->addSubcards(cards);
			return sc;
		}
		SkillCard*sc = new XingzhenCard;
		sc->setUserString(pattern);
		return sc;
	}

    bool isEnabledAtResponse(const Player*player, const QString &pattern) const
    {
        if(pattern=="@@xingzhen!") return true;
		if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
        foreach(QString cn, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(cn);
			if(dc){
				dc->deleteLater();
				if(player->hasFlag("CurrentPlayer"))
					return dc->isKindOf("TrickCard");
				return dc->isKindOf("BasicCard");
			}
        }
        return false;
    }

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("XingzhenCard")<1;
	}
};




MobileYanPackage::MobileYanPackage()
    : Package("mobileyan")
{
    General*mobileyan_cuiyan = new General(this, "mobileyan_cuiyan", "wei", 3);
    mobileyan_cuiyan->addSkill(new MobileYanYajun);
    mobileyan_cuiyan->addSkill(new MobileYanZundi);
    mobileyan_cuiyan->addSkill("#fulinbf");
    related_skills.insert("mobileyanyajun", "#fulinbf");

    General*mobileyan_zhangchangpu = new General(this, "mobileyan_zhangchangpu", "wei", 3, false);
    mobileyan_zhangchangpu->addSkill(new MobileYanDifei);
    mobileyan_zhangchangpu->addSkill(new MobileYanYanjiao);

    General*mobileyan_jiangwan = new General(this, "mobileyan_jiangwan", "shu", 3);
    mobileyan_jiangwan->addSkill(new MobileYanZhenting);
    mobileyan_jiangwan->addSkill(new MobileYanJincui);

    General*mobileyan_jiangqin = new General(this, "mobileyan_jiangqin", "wu", 4);
    mobileyan_jiangqin->addSkill(new MobileYanJianyi);
    mobileyan_jiangqin->addSkill(new MobileYanShangyi);

    General*yan_shenjiangwei = new General(this, "yan_shenjiangwei", "god", 4,true,false,false,1);
    yan_shenjiangwei->addSkill(new Tiantao);
    yan_shenjiangwei->addSkill(new Xingzhen);
    yan_shenjiangwei->addSkill(new Shenpei);
    addMetaObject<ShenpeiCard>();
    addMetaObject<XingzhenCard>();
    skills << new Huitian << new HuitianDeath;
    related_skills.insert("huitian", "#huitian-death");



    addMetaObject<MobileYanYajunCard>();
    addMetaObject<MobileYanYajunPutCard>();
    addMetaObject<MobileYanZundiCard>();
    addMetaObject<MobileYanYanjiaoCard>();
    addMetaObject<MobileYanJincuiCard>();
    addMetaObject<MobileYanShangyiCard>();
}
ADD_PACKAGE(MobileYan)
