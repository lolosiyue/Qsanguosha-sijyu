#include "special1v1.h"
//#include "general.h"
//#include "standard.h"
#include "standard-cards.h"
//#include "skill.h"
#include "engine.h"
//#include "client.h"
#include "serverplayer.h"
#include "room.h"
//#include "ai.h"
#include "settings.h"
#include "maneuvering.h"
//#include "util.h"
#include "roomthread.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>

class KOFTuxi : public TriggerSkillV2
{
public:
    KOFTuxi() : TriggerSkillV2("koftuxi")
    {
        events << DrawNCards;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *zhangliao = ctx.owner;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(zhangliao)) {
            if (p->getHandcardNum() > zhangliao->getHandcardNum())
                targets << p;
        }

		ServerPlayer *target = room->askForPlayerChosen(zhangliao, targets, objectName(), "koftuxi-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num -= getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        room->broadcastSkillInvoke("nostuxi");
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->isKongcheng()) return false;
        int id = room->askForCardChosen(ctx.owner, target, "h", objectName());
        if (id >= 0) {
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, ctx.owner->objectName(), target->objectName(), objectName(), "");
            room->obtainCard(ctx.owner, Sanguosha->getCard(id), reason, false);
        }
        return false;
    }
};

XiechanCard::XiechanCard()
{
    setSkillName("xiechan");
}

bool XiechanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void XiechanCard::use(Room *room, ServerPlayer *xuchu, QList<ServerPlayer *> &targets) const
{
    room->removePlayerMark(xuchu, "@twine");
    //room->doLightbox("$XiechanAnimate");
    room->doSuperLightbox(xuchu, "xiechan");

    bool success = xuchu->pindian(targets.first(), "xiechan", nullptr);
    Duel *duel = new Duel(Card::NoSuit, 0);
    duel->setSkillName("_xiechan");
    ServerPlayer *from = nullptr, *to = nullptr;
    if (success) {
        from = xuchu;
        to = targets.first();
    } else {
        from = targets.first();
        to = xuchu;
    }
    if (!from->isLocked(duel) && !from->isProhibited(to, duel))
        room->useCard(CardUseStruct(duel, from, to));
}

class Xiechan : public ViewAsSkillV2
{
public:
    Xiechan() : ViewAsSkillV2("xiechan")
    {
        frequency = Limited;
        limit_mark = "@twine";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canPindian();
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "XiechanCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        return targets.isEmpty() && request.initiator->canPindian(target);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        // The public token is presentation; the exact instance owns the quota.
        if (ctx.invoker->getMark(limit_mark) > 0) room->removePlayerMark(ctx.invoker, limit_mark);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (!ctx.invoker->canPindian(target)) return ContinueEffects;
        room->doSuperLightbox(ctx.invoker, objectName());
        bool won = ctx.invoker->pindian(target, objectName(), nullptr);
        ServerPlayer *from = won ? ctx.invoker : target;
        ServerPlayer *to = won ? target : ctx.invoker;
        Duel *duel = new Duel(Card::NoSuit, 0);
        duel->setSkillName("_xiechan");
        duel->deleteLater();
        if (from->isAlive() && to->isAlive() && !from->isLocked(duel) && !from->isProhibited(to, duel))
            room->useCardFromSkillEffect(CardUseStruct(duel, from, to), ctx);
        return ContinueEffects;
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        if (card->isKindOf("Duel"))
            return -2;
        else
            return -1;
    }
};

class KOFQingguo : public ViewAsSkillV2
{
public:
    KOFQingguo() : ViewAsSkillV2("kofqingguo", 1) {}
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card
            && request.initiator->getEquips().contains(card) && !card->hasFlag("using");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
        jink->setSkillName(objectName());
        jink->addSubcard(originalCard->getId());
        return jink;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Jink"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.pattern == "jink" && !request.initiator->getEquips().isEmpty();
    }
};

class Suzi : public TriggerSkillV2
{
public:
    Suzi() : TriggerSkillV2("suzi") { events << BuryVictim; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || player->isNude() || data.value<DeathStruct>().who != player) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != player) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        return !player->isNude() && room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        DummyCard cards(player->handCards());
        for (const Card *card : player->getEquips()) cards.addSubcard(card);
        if (cards.subcardsLength() > 0) {
            CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, ctx.owner->objectName());
            room->obtainCard(ctx.owner, &cards, reason, false);
        }
        return false;
    }
};

class Huwei : public TriggerSkillV2
{
public:
    Huwei() : TriggerSkillV2("huwei") { events << Debut; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        Drowning card(Card::NoSuit, 0);
        card.setSkillName("_huwei");
        ServerPlayer *opponent = player->getNext();
        return opponent && opponent->isAlive() && card.isAvailable(player) && !player->isProhibited(opponent, &card)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner->getNext()};
        return room->askForSkillInvoke(ctx.owner, objectName());
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        Drowning *card = new Drowning(Card::NoSuit, 0);
        card->setSkillName("_huwei");
        card->deleteLater();
        if (card->isAvailable(ctx.owner) && !ctx.owner->isProhibited(target, card)) {
            ctx.owner->peiyin(this);
            room->useCardFromSkillEffect(CardUseStruct(card, ctx.owner, target), ctx);
        }
        return false;
    }
};

CangjiCard::CangjiCard()
{
    setSkillName("cangji");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool CangjiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self)
        return false;
    QList<int> equip_loc;
    foreach (int id, subcards) {
        const Card *card = Sanguosha->getCard(id);
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        if (equip)
            equip_loc << equip->location();
    }
    foreach (int loc, equip_loc) {
        if (to_select->getEquip(loc))
            return false;
    }
    return true;
}

void CangjiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();

    CardsMoveStruct move(subcards, effect.from, effect.to, Player::PlaceUnknown, Player::PlaceEquip, CardMoveReason());
    room->moveCardsAtomic(move, true);

    if (effect.from->getEquips().isEmpty())
        return;
    bool loop = false;
    for (int i = 0; i <= 3; i++) {
        if (effect.from->getEquip(i)) {
            foreach (ServerPlayer *p, room->getOtherPlayers(effect.from)) {
                if (!p->getEquip(i)) {
                    loop = true;
                    break;
                }
            }
            if (loop) break;
        }
    }
    if (loop)
        room->askForUseCard(effect.from, "@@cangji", "@cangji-install", -1, Card::MethodNone);
}

class CangjiViewAsSkill : public ViewAsSkillV2
{
public:
    CangjiViewAsSkill() : ViewAsSkillV2("cangji", -1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@cangji"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }
    bool willThrowSelectedCards() const override { return false; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && request.initiator->getEquips().contains(card) && !request.selectedCardIds.contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return !request.selectedCardIds.isEmpty(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "CangjiCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!selected.isEmpty() || !target || target == request.initiator) return false;
        for (int id : request.selectedCardIds) {
            const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
            if (!equip || target->getEquip(equip->location())) return false;
        }
        return true;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        QList<int> ids;
        for (int id : ctx.use_card->getSubcards()) {
            const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
            if (equip && room->getCardOwner(id) == ctx.invoker && room->getCardPlace(id) == Player::PlaceEquip
                && !target->getEquip(equip->location())) ids << id;
        }
        if (!ids.isEmpty()) {
            CardsMoveStruct move(ids, ctx.invoker, target, Player::PlaceEquip, Player::PlaceEquip, CardMoveReason());
            room->moveCardsAtomic(move, true);
        }
        return ContinueEffects;
    }
};

class Cangji : public TriggerSkillV2
{
public:
    Cangji() : TriggerSkillV2("cangji") { events << Death; view_as_skill = new CangjiViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && data.value<DeathStruct>().who == player && player->hasSkill(objectName())
            && !player->getEquips().isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->getMode() != "02_1v1" || room->askForSkillInvoke(ctx.owner, objectName());
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (room->getMode() == "02_1v1") {
            CardsMoveStruct move;
            move.from = ctx.owner;
            move.to_place = Player::PlaceTable;
            for (const Card *equip : ctx.owner->getEquips()) move.card_ids << equip->getEffectiveId();
            if (move.card_ids.isEmpty()) return false;
            // Applied equipment receipt survives general replacement and the original skill instance.
            QVariantList pending = ctx.owner->getTag("cangji").toList();
            for (int id : move.card_ids) pending << id;
            ctx.owner->setTag("cangji", pending);
            const qulonglong serial = room->getTag("CangjiReceiptSerial").toULongLong() + 1;
            room->setTag("CangjiReceiptSerial", serial);
            QVariantList ids; for (int id : move.card_ids) ids << id;
            QVariantMap receipts = ctx.owner->property("cangji_receipts").toMap();
            receipts.insert(QString::number(serial), QVariantMap{{"cards", ids}, {"owner", ctx.sourceRef.ownerObjectName},
                {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
                {"activation", ctx.activationRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}});
            room->setPlayerProperty(ctx.owner, "cangji_receipts", receipts);
            room->moveCardsAtomic(move, true);
        } else {
            Room::BorrowedSkillScope scope(room, ctx.owner, objectName(), ctx.sourceRef);
            while (!ctx.owner->getEquips().isEmpty()) {
                bool available = false;
                for (const Card *card : ctx.owner->getEquips()) {
                    const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
                    if (!equip || equip->location() > 3) continue;
                    for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
                        if (!other->getEquip(equip->location())) available = true;
                }
                if (!available || !room->askForUseCard(ctx.owner, "@@cangji", "@cangji-install", -1, Card::MethodNone)) break;
            }
        }
        return false;
    }
};

class CangjiInstall : public TriggerSkillV2
{
public:
    CangjiInstall() : TriggerSkillV2("#cangji-install") { events << EventSkillInvoking << Debut; global = true; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 5; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.skill_name == objectName()) {
                // Retiring an accepted receipt is bookkeeping, never a bypassable resource cost.
                if (!pay(accepted.current_event, room, accepted.invoker, accepted)) accepted.is_canceled = true;
                data.setValue(accepted);
            }
            return true;
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event == EventSkillInvoking) return true;
        if (!player || !player->isAlive()) return true;
        const QVariantMap receipts = player->property("cangji_receipts").toMap();
        for (auto it = receipts.cbegin(); it != receipts.cend(); ++it) {
            const QVariantMap receipt = it.value().toMap();
            SkillContext ctx; ctx.skill_name = objectName(); ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.owner = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true);
            if (!ctx.owner) continue;
            ctx.targets = {player}; ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = QVariantMap{{"key", it.key()}, {"receipt", receipt}}; ctx.current_event = event; ctx.original_data = &data;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.sourceRef.isValid() && !ctx.extra_data.toMap().value("receipt").toMap().isEmpty(); }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantMap receipts = ctx.invoker->property("cangji_receipts").toMap();
        const QVariant receipt = receipts.take(ctx.extra_data.toMap().value("key").toString());
        if (!receipt.isValid()) return false;
        ctx.extra_data = receipt;
        room->setPlayerProperty(ctx.invoker, "cangji_receipts", receipts);
        if (receipts.isEmpty()) ctx.invoker->removeTag("cangji");
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toMap().value("cards").toList()) {
            const int id = value.toInt();
            if (room->getCardPlace(id) == Player::PlaceTable && Sanguosha->getCard(id)->getTypeId() == Card::TypeEquip) ids << id;
        }
        // The accepted equipment settlement has its own recipient interception at debut.
        if (!ids.isEmpty()) room->moveCardsAtomic(CardsMoveStruct(ids, target, Player::PlaceEquip, CardMoveReason()), true);
        return false;
    }
};

class KOFLiegong : public TriggerSkillV2
{
public:
    KOFLiegong() : TriggerSkillV2("kofliegong") { events << TargetSpecified; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            && use.card && use.card->isKindOf("Slash") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : ctx.original_data->value<CardUseStruct>().to)
            if (target->getHandcardNum() >= ctx.owner->getHp()
                && ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target)))
                ctx.targets << target;
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const int index = use.to.indexOf(target);
        QVariantList jinks = ctx.owner->getTag("Jink_" + use.card->toString()).toList();
        if (index < 0 || index >= jinks.size()) return false;
        room->broadcastSkillInvoke("liegong");
        LogMessage log;
        log.type = "#NoJink";
        log.from = target;
        room->sendLog(log);
        jinks[index] = 0;
        ctx.owner->setTag("Jink_" + use.card->toString(), jinks);
        return false;
    }
};

class Manyi : public TriggerSkillV2
{
public:
    Manyi() : TriggerSkillV2("manyi") { events << Debut; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        SavageAssault card(Card::NoSuit, 0);
        card.setSkillName("_manyi");
        return card.isAvailable(player) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName());
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        SavageAssault *card = new SavageAssault(Card::NoSuit, 0);
        card->setSkillName("_manyi");
        card->deleteLater();
        if (card->isAvailable(ctx.owner))
            room->useCardFromSkillEffect(CardUseStruct(card, ctx.owner, QList<ServerPlayer *>()), ctx);
        return false;
    }
};

class ManyiAvoid : public TriggerSkillV2
{
public:
    ManyiAvoid() : TriggerSkillV2("#manyi-avoid") { events << CardEffected; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && effect.card
            && effect.card->isKindOf("SavageAssault") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(ctx.owner->isFemale() ? "juxiang" : "huoshou");
        LogMessage log;
        log.type = "#SkillNullify";
        log.from = ctx.owner;
        log.arg = "manyi";
        log.arg2 = "savage_assault";
        room->sendLog(log);
        return true;
    }
};

class KOFXiaoji : public TriggerSkillV2
{
public:
    KOFXiaoji() : TriggerSkillV2("kofxiaoji") { events << CardsMoveOneTime; frequency = Frequent; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        int count = move.from_places.count(Player::PlaceEquip);
        return player && player->isAlive() && player->hasSkill(objectName()) && move.from == player && count > 0
            ? TriggerList{{player, {objectName() + "*" + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QStringList choices{"draw", "cancel"};
        if (ctx.owner->isWounded()) choices.prepend("recover");
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
        return ctx.choice != "cancel";
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName(), ctx.choice == "draw" ? 1 : 2);
        room->notifySkillInvoked(ctx.owner, objectName());
        if (ctx.choice == "draw") ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        else room->recover(ctx.owner, RecoverStruct(objectName(), ctx.owner));
        return false;
    }
};

class Yinli : public TriggerSkillV2
{
public:
    Yinli() : TriggerSkillV2("yinli") { events << CardsMoveOneTime; frequency = Frequent; }
    int getPriority(TriggerEvent) const override { return 3; }
    QList<int> available(Room *room, const CardsMoveOneTimeStruct &move) const
    {
        QList<int> ids;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if ((move.from_places.value(i) == Player::PlaceHand || move.from_places.value(i) == Player::PlaceEquip)
                && Sanguosha->getCard(move.card_ids[i])->getTypeId() == Card::TypeEquip
                && room->getCardPlace(move.card_ids[i]) == Player::DiscardPile)
                ids << move.card_ids[i];
        return ids;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        TriggerList result;
        if (!move.from || !move.from->hasFlag("CurrentPlayer") || move.to_place != Player::DiscardPile
            || available(room, move).isEmpty()) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != move.from) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<int> ids = available(room, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (ids.isEmpty() || !ctx.owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
        room->fillAG(ids, ctx.owner);
        const auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
        QList<int> chosen;
        while (!ids.isEmpty()) {
            int id = room->askForAG(ctx.owner, ids, !chosen.isEmpty(), objectName());
            if (id < 0) break;
            ids.removeOne(id);
            chosen << id;
            room->takeAG(ctx.owner, id, false, {ctx.owner});
        }
        ctx.extra_data = ListI2V(chosen);
        return !chosen.isEmpty();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DummyCard cards;
        for (int id : ListV2I(ctx.extra_data.toList()))
            if (room->getCardPlace(id) == Player::DiscardPile) cards.addSubcard(id);
        if (cards.subcardsLength() > 0) ctx.owner->obtainCard(&cards);
        return false;
    }
};

class Pianyi : public TriggerSkillV2
{
public:
    Pianyi() : TriggerSkillV2("pianyi") { events << Debut; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        const auto others = player ? room->getOtherPlayers(player) : QList<ServerPlayer *>();
        return player && player->isAlive() && player->hasSkill(objectName()) && !others.isEmpty()
            && others.first()->getPhase() != Player::NotActive ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const auto others = room->getOtherPlayers(ctx.owner);
        if (others.isEmpty() || others.first()->getPhase() == Player::NotActive) return false;
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        LogMessage log;
        log.type = "#TurnBroken";
        log.from = others.first();
        room->sendLog(log);
        throw TurnBroken;
    }
};

MouzhuCard::MouzhuCard()
{
    setSkillName("mouzhu");
}

bool MouzhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void MouzhuCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    ServerPlayer *hejin = effect.from, *target = effect.to;
    if (target->isKongcheng()) return;

    const Card *card = nullptr;
    if (target->getHandcardNum() > 1) {
        card = room->askForCard(target, ".!", "@mouzhu-give:" + hejin->objectName(), QVariant(), Card::MethodNone);
        if (!card)
            card = target->getHandcards().at(qsanRandomBounded(target->getHandcardNum()));
    } else {
        card = target->getHandcards().first();
    }
    Q_ASSERT(card != nullptr);
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), hejin->objectName(), "mouzhu", "");
    room->obtainCard(hejin, card, reason, false);
    if (!hejin->isAlive() || !target->isAlive()) return;
    if (hejin->getHandcardNum() > target->getHandcardNum()) {
        QStringList choicelist;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_mouzhu");
        Duel *duel = new Duel(Card::NoSuit, 0);
        duel->setSkillName("_mouzhu");
        if (!target->isLocked(slash) && target->canSlash(hejin, slash, false))
            choicelist.append("slash");
        if (!target->isLocked(duel) && !target->isProhibited(hejin, duel))
            choicelist.append("duel");
        if (choicelist.isEmpty()) {
            delete slash;
            delete duel;
            return;
        }
        QString choice = room->askForChoice(target, "mouzhu", choicelist.join("+"));
        CardUseStruct use;
        use.from = target;
        use.to << hejin;
        if (choice == "slash") {
            delete duel;
            use.changeCard(slash);
        } else {
            delete slash;
            use.changeCard(duel);
        }
        room->useCard(use);
    }
}

class Mouzhu : public ViewAsSkillV2
{
public:
    Mouzhu() : ViewAsSkillV2("mouzhu") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "MouzhuCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return selected.isEmpty() && target && target != request.initiator && !target->isKongcheng();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *owner = ctx.invoker;
        Room *room = owner->getRoom();
        if (target->isKongcheng()) return ContinueEffects;
        const Card *given = target->getHandcardNum() > 1
            ? room->askForCard(target, ".!", "@mouzhu-give:" + owner->objectName(), QVariant(), Card::MethodNone) : nullptr;
        if (!given) given = target->getHandcards().at(qsanRandomBounded(target->getHandcardNum()));
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), owner->objectName(), objectName(), "");
        room->obtainCard(owner, given, reason, false);
        if (!owner->isAlive() || !target->isAlive() || owner->getHandcardNum() <= target->getHandcardNum()) return ContinueEffects;
        Slash slash(Card::NoSuit, 0);
        Duel duel(Card::NoSuit, 0);
        slash.setSkillName("_mouzhu");
        duel.setSkillName("_mouzhu");
        QStringList choices;
        if (!target->isLocked(&slash) && target->canSlash(owner, &slash, false)) choices << "slash";
        if (!target->isLocked(&duel) && !target->isProhibited(owner, &duel)) choices << "duel";
        if (choices.isEmpty()) return ContinueEffects;
        const QString choice = room->askForChoice(target, objectName(), choices.join("+"));
        Card *card = choice == "slash" ? static_cast<Card *>(new Slash(Card::NoSuit, 0)) : new Duel(Card::NoSuit, 0);
        card->setSkillName("_mouzhu");
        card->deleteLater();
        room->useCardFromSkillEffect(CardUseStruct(card, target, owner), ctx);
        return ContinueEffects;
    }
};

class Yanhuo : public TriggerSkillV2
{
public:
    Yanhuo() : TriggerSkillV2("yanhuo") { events << BeforeGameOverJudge << Death; }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == BeforeGameOverJudge && ctx.owner == player && player->isDead())
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "cards_at_death", player->getCardCount());
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Death || !player || player->isAlive() || data.value<DeathStruct>().who != player) return {};
        TriggerList result;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (player->getSkillInstanceStateValue(objectName(), id, "cards_at_death").toInt() > 0)
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = nullptr;
        if (room->getMode() == "02_1v1") {
            const auto others = room->getOtherPlayers(ctx.owner);
            if (!others.isEmpty()) target = others.first();
            if (!target || !ctx.owner->canDiscard(target, "he") || !room->askForSkillInvoke(ctx.owner, objectName())) return false;
        } else {
            QList<ServerPlayer *> targets;
            for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
                if (ctx.owner->canDiscard(other, "he")) targets << other;
            target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "yanhuo-invoke", true, true);
        }
        if (!target) return false;
        ctx.targets = {target};
        ctx.extra_data = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "cards_at_death");
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "cards_at_death");
        for (int i = 0; i < ctx.extra_data.toInt() && ctx.owner->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (id < 0) break;
            room->throwCard(id, target, ctx.owner);
        }
        return false;
    }
};

class Renwang : public TriggerSkillV2
{
public:
    Renwang() : TriggerSkillV2("renwang") { events << CardUsed; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        TriggerList result;
        if (!player || player->getPhase() != Player::Play || !use.card
            || (!use.card->isKindOf("Slash") && !use.card->isNDTrick())
            || room->historyScopes().value("turn_id").toLongLong() == 0) return result;
        QMap<QString, int> counts;
        QVariantMap filter{{"kind", "use_card"}, {"turn_id", room->historyScopes().value("turn_id")}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool()) return {};
            if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap();
                const QVariantMap item = fact.value("data").toMap();
                const QVariantMap card = item.value("card").toMap();
                if (!card.contains("classes") || !card.contains("ndtrick")) return {};
                if (!card.value("classes").toStringList().contains("Slash") && !card.value("ndtrick").toBool()) continue;
                const QVariantMap phase = room->historyEvent(fact.value("phase_id").toLongLong());
                if (phase.value("data").toMap().value("phase").toInt() != Player::Play) continue;
                for (const QVariant &target : item.value("targets").toList())
                    if (target.toString() != item.value("from").toString()) ++counts[target.toString()];
            }
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after");
        }
        // The accepted current use is already journaled before CardUsed.
        for (ServerPlayer *target : use.to)
            if (target != player && target->hasSkill(objectName()) && counts.value(target->objectName()) > 1
                && target->canDiscard(player, "he")) result[target] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.targets = {player};
        return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->canDiscard(target, "he")) return false;
        room->broadcastSkillInvoke("rende");
        const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
        if (id >= 0) room->throwCard(id, target, ctx.owner);
        return false;
    }
};

class KOFKuanggu : public TriggerSkillV2
{
public:
    KOFKuanggu() : TriggerSkillV2("kofkuanggu") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && data.value<DamageStruct>().from == player
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        JudgeStruct judge;
        judge.pattern = ".|black";
        judge.who = ctx.owner;
        judge.reason = objectName();
        room->judge(judge);
        if (judge.isGood() && ctx.owner->isWounded()) {
            room->broadcastSkillInvoke("kuanggu");
            room->recover(ctx.owner, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        }
        return false;
    }
};

class Shenju : public MaxCardsSkillV2
{
public:
    Shenju() : MaxCardsSkillV2("shenju") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary != ctx.holder) return CorrectSkillResult::noEffect();
        int hp = 0;
        for (const Player *other : ctx.primary->getAliveSiblings()) hp = qMax(hp, other->getHp());
        return CorrectSkillResult::useAmount(hp * ctx.currentAmount);
    }
};



class Botu : public TriggerSkillV2
{
public:
    Botu() : TriggerSkillV2("botu") { events << EventPhaseStart; frequency = Frequent; }
    int getPriority(TriggerEvent) const override { return 1; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::NotActive
            || room->historyScopes().value("round_id").toLongLong() == 0) return {};
        int suits = 0;
        // Both uses and pure responses during Play count, across this round.
        for (bool responses : {false, true}) {
            const QVariantMap history = room->queryCardHistory(player, "round", QString(), responses, true);
            if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return {};
            for (const QVariant &entry : history.value("items").toList()) {
                const int suit = entry.toMap().value("suit", -1).toInt();
                if (suit >= 0 && suit <= 3) suits |= 1 << suit;
            }
        }
        return suits == 0xF ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->gainAnExtraTurn();
        return false;
    }
};

class Wanrong : public TriggerSkillV2
{
public:
    Wanrong() : TriggerSkillV2("wanrong") { events << TargetConfirmed; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.card
            && use.card->isKindOf("Slash") && use.to.contains(player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke("liuli", qsanRandomBounded(2) + 1);
        ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

PujiCard::PujiCard()
{
    setSkillName("puji");
}

bool PujiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canDiscard(to_select, "he") && to_select != Self;
}

void PujiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    bool spade = false;
    if (effect.from->canDiscard(effect.to, "he")) {
        int id = room->askForCardChosen(effect.from, effect.to, "he", "puji", false, Card::MethodDiscard);
        room->throwCard(id, effect.to, effect.from);
        spade = (Sanguosha->getCard(id)->getSuit() == Card::Spade);
    }

    if (effect.from->isAlive() && Sanguosha->getCard(getEffectiveId())->getSuit() == Card::Spade)
        effect.from->drawCards(1, "puji");
    if (effect.to->isAlive() && spade)
        effect.to->drawCards(1, "puji");
}

class Puji : public ViewAsSkillV2
{
public:
    Puji() : ViewAsSkillV2("puji", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "PujiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && request.selectedCardIds.isEmpty() && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquips().contains(card))
            && !request.initiator->isJilei(card);
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return selected.isEmpty() && target && target != request.initiator && request.initiator->canDiscard(target, "he");
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (card && request.selectedCardIds.size() == 1)
            card->setTag("v2_effect_input", Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == Card::Spade);
        return card;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ctx.extra_data = Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == Card::Spade;
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.extra_data.isValid() && ctx.use_card) ctx.extra_data = ctx.use_card->getTag("v2_effect_input");
        Room *room = ctx.invoker->getRoom();
        bool spade = false;
        if (ctx.invoker->canDiscard(target, "he")) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0) {
                spade = Sanguosha->getCard(id)->getSuit() == Card::Spade;
                room->throwCard(id, target, ctx.invoker);
            }
        }
        if (ctx.invoker->isAlive() && ctx.extra_data.toBool()) ctx.invoker->drawCards(getEffectiveAmount(ctx), objectName());
        if (target->isAlive() && spade) target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class Cuorui : public TriggerSkillV2
{
public:
    Cuorui() : TriggerSkillV2("cuorui") { events << DrawNCards << EventPhaseChanging; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        TriggerList result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            if ((event == DrawNCards && data.value<DrawStruct>().reason == "InitialHandCards")
                || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::Judge
                    && !player->getSkillInstanceStateValue(objectName(), id, "judge_skipped").toBool()))
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        if (event == DrawNCards) {
            int n = 3;
            if (room->getMode() == "02_1v1") {
                n = ctx.owner->getTag("1v1Arrange").toStringList().size();
                const QString rule = Config.value("1v1/Rule", "2013").toString();
                if (rule != "2013") n += 3;
                n += 2 - (rule == "Classical" ? 4 : ctx.owner->getMaxHp());
            }
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += n * getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(draw);
        } else {
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "judge_skipped", true);
            ctx.owner->skip(Player::Judge);
        }
        return false;
    }
};

class Liewei : public TriggerSkillV2
{
public:
    Liewei() : TriggerSkillV2("liewei") { events << Death; frequency = Frequent; m_baseAmount = 3; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        // Death is dispatched once to each living player and to the victim by the native rule.
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class NiluanViewAsSkill : public ViewAsSkillV2
{
public:
    NiluanViewAsSkill() : ViewAsSkillV2("niluan", 1) { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@niluan"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && request.selectedCardIds.isEmpty() && card->isBlack() && !card->hasFlag("using")
            && ((request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquips().contains(card)) || request.initiator->getHandPile().contains(card->getEffectiveId()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        return slash;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};

class Niluan : public TriggerSkillV2
{
public:
    Niluan() : TriggerSkillV2("niluan") { events << EventPhaseStart; view_as_skill = new NiluanViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->getPhase() != Player::Finish) return {};
        QSet<QString> slashTargets;
        bool complete = room->historyScopes().value("turn_id").toLongLong() != 0;
        QVariantMap filter{{"kind", "use_card_targets"}, {"turn_id", room->historyScopes().value("turn_id")}};
        if (complete) for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            complete = complete && page.value("complete").toBool();
            if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap item = entry.toMap().value("data").toMap();
                const QVariantMap card = item.value("card").toMap();
                if (!card.contains("classes")) { complete = false; continue; }
                if (!card.value("classes").toStringList().contains("Slash")) continue;
                for (const QVariant &target : item.value("targets").toList()) slashTargets.insert(target.toString());
            }
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after");
        }
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != player && !owner->isNude() && owner->canSlash(player, false)
                && (player->getHp() > owner->getHp() || (complete && slashTargets.contains(owner->objectName()))))
                result[owner] << objectName();
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->isAlive() || !ctx.owner->canSlash(player, false)) return false;
        const QStringList flags{"slashTargetFix", "slashNoDistanceLimit", "slashTargetFixToOne"};
        QStringList previous;
        for (const QString &flag : flags)
            if (ctx.owner->hasFlag(flag)) previous << flag;
        for (const QString &flag : flags) room->setPlayerFlag(ctx.owner, flag);
        const bool assignee = player->hasFlag("SlashAssignee");
        room->setPlayerFlag(player, "SlashAssignee");
        const auto cleanup = qScopeGuard([&] {
            // A nested Slash can consume these flags; restore the enclosing request exactly.
            for (const QString &flag : flags) room->setPlayerFlag(ctx.owner, previous.contains(flag) ? flag : "-" + flag);
            room->setPlayerFlag(player, assignee ? "SlashAssignee" : "-SlashAssignee");
        });
        Room::AcceptedViewAsEffectScope scope(room, ctx.owner, objectName(), ctx);
        if (!scope.isValid()) return false;
        room->askForUseCard(ctx.owner, "@@niluan", "@niluan-slash:" + player->objectName());
        return false;
    }
};



Drowning::Drowning(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("drowning");
    damage_card = true;
}

bool Drowning::isAvailable(const Player *player) const
{
	foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool Drowning::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return to_select!=Self&&targets.length()<1+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget,Self,this)
	&&!Self->isProhibited(to_select,this,targets);
}

void Drowning::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    if (hasFlag("yb_zhuzhan1_buff")) {
        if (!effect.to->getEquips().isEmpty())
            effect.to->throwAllEquips();
        if (effect.to->isAlive())
            room->damage(DamageStruct(this, effect.from->isAlive() ? effect.from : nullptr, effect.to, 1, DamageStruct::Thunder));
    } else {
        if (!effect.to->getEquips().isEmpty()
            && room->askForChoice(effect.to, objectName(), "throw+damage", QVariant::fromValue(effect)) == "throw")
            effect.to->throwAllEquips();
        else
            room->damage(DamageStruct(this, effect.from->isAlive() ? effect.from : nullptr, effect.to, 1, DamageStruct::Thunder));
    }
}

Special1v1Package::Special1v1Package()
    : Package("Special1v1")
{
    General *kof_nos_zhangliao = new General(this, "kof_nos_zhangliao", "wei");
    kof_nos_zhangliao->addSkill(new KOFTuxi);

    General *kof_nos_xuchu = new General(this, "kof_nos_xuchu", "wei");
    kof_nos_xuchu->addSkill("nosluoyi");
    kof_nos_xuchu->addSkill(new Xiechan);

    General *kof_zhenji = new General(this, "kof_zhenji", "wei", 3, false);
    kof_zhenji->addSkill(new KOFQingguo);
    kof_zhenji->addSkill("luoshen");

    General *kof_xiahouyuan = new General(this, "kof_xiahouyuan", "wei");
    kof_xiahouyuan->addSkill("shensu");
    kof_xiahouyuan->addSkill(new Suzi);

    General *kof_nos_liubei = new General(this, "kof_nos_liubei$", "shu");
    kof_nos_liubei->addSkill(new Renwang);
    kof_nos_liubei->addSkill("jijiang");

    General *kof_nos_guanyu = new General(this, "kof_nos_guanyu", "shu");
    kof_nos_guanyu->addSkill("wusheng");
    kof_nos_guanyu->addSkill(new Huwei);

    General *kof_nos_huangyueying = new General(this, "kof_nos_huangyueying", "shu", 3, false);
    kof_nos_huangyueying->addSkill("nosjizhi");
    kof_nos_huangyueying->addSkill(new Cangji);
    kof_nos_huangyueying->addSkill(new CangjiInstall);
    related_skills.insert("cangji", "#cangji-install");

    General *kof_huangzhong = new General(this, "kof_huangzhong", "shu");
    kof_huangzhong->addSkill(new KOFLiegong);

    General *kof_weiyan = new General(this, "kof_weiyan", "shu");
    kof_weiyan->addSkill(new KOFKuanggu);

    General *kof_jiangwei = new General(this, "kof_jiangwei", "shu");
    kof_jiangwei->addSkill("tiaoxin");

    General *kof_menghuo = new General(this, "kof_menghuo", "shu");
    kof_menghuo->addSkill(new Manyi);
    kof_menghuo->addSkill(new ManyiAvoid);
    kof_menghuo->addSkill("zaiqi");
    related_skills.insert("manyi", "#manyi-avoid");

    General *kof_zhurong = new General(this, "kof_zhurong", "shu", 4, false);
    kof_zhurong->addSkill("manyi");
    kof_zhurong->addSkill("lieren");

    General *kof_nos_lvmeng = new General(this, "kof_nos_lvmeng", "wu");
    kof_nos_lvmeng->addSkill(new Shenju);
    kof_nos_lvmeng->addSkill(new Botu);



    General *kof_nos_daqiao = new General(this, "kof_nos_daqiao", "wu", 3, false);
    kof_nos_daqiao->addSkill("nosguose");
    kof_nos_daqiao->addSkill(new Wanrong);

    General *kof_sunshangxiang = new General(this, "kof_sunshangxiang", "wu", 3, false);
    kof_sunshangxiang->addSkill(new Yinli);
    kof_sunshangxiang->addSkill(new KOFXiaoji);

    General *kof_nos_huatuo = new General(this, "kof_nos_huatuo", "qun", 3);
    kof_nos_huatuo->addSkill("jijiu");
    kof_nos_huatuo->addSkill(new Puji);

    General *kof_nos_diaochan = new General(this, "kof_nos_diaochan", "qun", 3, false);
    kof_nos_diaochan->addSkill(new Pianyi);
    kof_nos_diaochan->addSkill("biyue");

    addMetaObject<XiechanCard>();
    addMetaObject<CangjiCard>();
    addMetaObject<PujiCard>();
}

ADD_PACKAGE(Special1v1)

Special1v1ExtPackage::Special1v1ExtPackage()
: Package("Special1v1Ext")
{
    General *hejin = new General(this, "hejin", "qun", 4); // QUN 025
    hejin->addSkill(new Mouzhu);
    hejin->addSkill(new Yanhuo);

    General *niujin = new General(this, "niujin", "wei"); // WEI 025
    niujin->addSkill(new Cuorui);
    niujin->addSkill(new Liewei);

    General *hansui = new General(this, "hansui", "qun"); // QUN 027
    hansui->addSkill("mashu");
    hansui->addSkill(new Niluan);



    addMetaObject<MouzhuCard>();
}

ADD_PACKAGE(Special1v1Ext)

New1v1CardPackage::New1v1CardPackage()
: Package("~New1v1Card")
{
    QList<Card *> cards;
    cards << new Duel(Card::Spade, 1)
        << new EightDiagram(Card::Spade, 2)
        << new Dismantlement(Card::Spade, 3)
        << new Snatch(Card::Spade, 4)
        << new Slash(Card::Spade, 5)
        << new QinggangSword(Card::Spade, 6)
        << new Slash(Card::Spade, 7)
        << new Slash(Card::Spade, 8)
        << new IceSword(Card::Spade, 9)
        << new Slash(Card::Spade, 10)
        << new Snatch(Card::Spade, 11)
        << new Spear(Card::Spade, 12)
        << new SavageAssault(Card::Spade, 13);

    cards << new ArcheryAttack(Card::Heart, 1)
        << new Jink(Card::Heart, 2)
        << new Peach(Card::Heart, 3)
        << new Peach(Card::Heart, 4)
        << new Jink(Card::Heart, 5)
        << new Indulgence(Card::Heart, 6)
        << new ExNihilo(Card::Heart, 7)
        << new ExNihilo(Card::Heart, 8)
        << new Peach(Card::Heart, 9)
        << new Slash(Card::Heart, 10)
        << new Slash(Card::Heart, 11)
        << new Dismantlement(Card::Heart, 12)
        << new Nullification(Card::Heart, 13);

    cards << new Duel(Card::Club, 1)
        << new RenwangShield(Card::Club, 2)
        << new Dismantlement(Card::Club, 3)
        << new Slash(Card::Club, 4)
        << new Slash(Card::Club, 5)
        << new Slash(Card::Club, 6)
        << new Drowning(Card::Club, 7)
        << new Slash(Card::Club, 8)
        << new Slash(Card::Club, 9)
        << new Slash(Card::Club, 10)
        << new Slash(Card::Club, 11)
        << new SupplyShortage(Card::Club, 12)
        << new Nullification(Card::Club, 13);

    cards << new Crossbow(Card::Diamond, 1)
        << new Jink(Card::Diamond, 2)
        << new Jink(Card::Diamond, 3)
        << new Snatch(Card::Diamond, 4)
        << new Axe(Card::Diamond, 5)
        << new Slash(Card::Diamond, 6)
        << new Jink(Card::Diamond, 7)
        << new Jink(Card::Diamond, 8)
        << new Slash(Card::Diamond, 9)
        << new Jink(Card::Diamond, 10)
        << new Jink(Card::Diamond, 11)
        << new Peach(Card::Diamond, 12)
        << new Slash(Card::Diamond, 13);

    foreach(Card *card, cards)
        card->setParent(this);

    type = CardPack;
}

ADD_PACKAGE(New1v1Card)
