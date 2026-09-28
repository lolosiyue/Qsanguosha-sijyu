#include "yingbian.h"
#include <memory>
#include <QScopeGuard>
//#include "standard.h"
#include "standard-cards.h"
//#include "maneuvering.h"
//#include "general.h"
#include "engine.h"
//#include "client.h"
#include "room.h"
//#include "ai.h"
//#include "settings.h"
#include "clientplayer.h"
#if !defined(QSAN_ENGINE_BUILD)
#include "clientstruct.h"
#endif
//#include "util.h"
#include "wrapped-card.h"
#include "roomthread.h"
#include "wind.h"
#include "special1v1.h"

IceSlash::IceSlash(Suit suit, int number)
    : NatureSlash(suit, number, DamageStruct::Ice)
{
    setObjectName("ice_slash");
    nature = DamageStruct::Ice;
    damage_card = true;
    single_target = true;
}

Chuqibuyi::Chuqibuyi(Card::Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("chuqibuyi");
    damage_card = true;
}

bool Chuqibuyi::isAvailable(const Player *player) const
{
	foreach (const Player *p, player->getAliveSiblings(true)) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool Chuqibuyi::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return !to_select->isKongcheng() && (to_select != Self || !Self->isLastHandCard(this, true))
	&& targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this, targets);
}

void Chuqibuyi::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isKongcheng()) return;
    Room *room = effect.from->getRoom();

    int hand_id = room->askForCardChosen(effect.from, effect.to, "h", objectName());
    room->showCard(effect.to, hand_id);

    if (Sanguosha->getCard(hand_id)->getSuit() != getSuit())
        room->damage(DamageStruct(this, effect.from, effect.to));
}

Dongzhuxianji::Dongzhuxianji(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("dongzhuxianji");
    target_fixed = true;
}

bool Dongzhuxianji::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !Self->isProhibited(to_select, this, targets);
}

void Dongzhuxianji::onUse(Room *room, CardUseStruct &use) const
{
    if (use.to.isEmpty()) use.to << use.from;
    SingleTargetTrick::onUse(room, use);
}

bool Dongzhuxianji::isAvailable(const Player *player) const
{
    return !player->isProhibited(player, this) && TrickCard::isAvailable(player);
}

void Dongzhuxianji::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    QList<int> ids = room->getNCards(2, false);
    room->askForGuanxing(effect.to, ids, Room::GuanxingBothSides);
    effect.to->drawCards(2, "dongzhuxianji");
}

Zhujinqiyuan::Zhujinqiyuan(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("zhujinqiyuan");
}

bool Zhujinqiyuan::isAvailable(const Player *player) const
{
	foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool Zhujinqiyuan::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select!=Self&&!to_select->isAllNude()&&targets.length()<=Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget,Self,this)) {
		if (Self->isProhibited(to_select, this, targets)) return false;
        if (Self->distanceTo(to_select) <= 1) return true;
        else return Self->canDiscard(to_select, "hej");
    }
    return false;
}

void Zhujinqiyuan::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isAllNude()) return;
    Room *room = effect.from->getRoom();

    if (hasFlag("yb_zhuzhan1_buff")) {
        if (effect.from->canDiscard(effect.to, "hje")) {
            int id2 = room->askForCardChosen(effect.from, effect.to, "hej", "zhujinqiyuan", false, Card::MethodDiscard);
            room->throwCard(id2, room->getCardPlace(id2) == Player::PlaceDelayedTrick ? nullptr : effect.to, effect.from);
        }
        if (effect.from->isAlive() && effect.to->isAlive() && !effect.to->isAllNude()) {
            int id1 = room->askForCardChosen(effect.from, effect.to, "hej", "zhujinqiyuan");
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
            room->obtainCard(effect.from, Sanguosha->getCard(id1), reason, room->getCardPlace(id1) != Player::PlaceHand);
        }
    } else {
        int distance = effect.from->distanceTo(effect.to);
        if (distance < 1) return;

        Card::HandlingMethod method = Card::MethodDiscard;
        if (distance == 1)
            method = Card::MethodNone;
        if (method == Card::MethodDiscard && !effect.from->canDiscard(effect.to, "hje")) return;

        int id = room->askForCardChosen(effect.from, effect.to, "hej", "zhujinqiyuan", false, method);

        if (method == Card::MethodDiscard)
            room->throwCard(id, room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : effect.to, effect.from);
        else {
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
            room->obtainCard(effect.from, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
        }
    }
}

class WuxinghelingshanVSSkill : public ViewAsSkillV2
{
public:
    WuxinghelingshanVSSkill() : ViewAsSkillV2("wuxinghelingshan", 1) { response_or_use = true; }
    bool isEquipSkill() const override { return true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.initiator->hasWeapon(objectName())
            && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && !usableNames(request).isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using")
            && card->isKindOf("NatureSlash") && (request.userString.isEmpty() || card->objectName() != request.userString)
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getHandPile().contains(card->getEffectiveId()));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        const std::unique_ptr<Card> card(Sanguosha->cloneCard(name));
        return card && card->isKindOf("NatureSlash");
    }
    bool allowDeclaration(const ActiveSkillRequest &request, const QString &name) const override
    {
        if (!allowDeclaration(request.initiator, name)) return false;
        return request.selectedCardIds.isEmpty()
            || (request.selectedCardIds.size() == 1 && request.selectedCardIds.first() >= 0
                && Sanguosha->getCard(request.selectedCardIds.first())->objectName() != name);
    }
};

class WuxinghelingshanSkill : public WeaponSkillV2
{
public:
    WuxinghelingshanSkill() : WeaponSkillV2("wuxinghelingshan")
    {
        events << ChangeSlash;
        view_as_skill = new WuxinghelingshanVSSkill;
    }
    SkillDialogInfo getDialogInfo() const override
    { return SkillDialogInfo::guhuo("wuxinghelingshan", true, false); }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return WeaponSkillV2::triggerable(player) && use.card && use.card->isKindOf("NatureSlash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QStringList choices;
        for (const NatureSlash *slash : Sanguosha->findChildren<const NatureSlash *>()) {
            const QString name = slash->objectName();
            if (name.startsWith("_") || name == use.card->objectName() || ServerInfo.BanPackages.contains(slash->getPackage())
                || choices.contains(name)) continue;
            const std::unique_ptr<Card> candidate(makeSlash(use, name));
            if (!candidate) continue;
            bool legal = true;
            for (ServerPlayer *target : use.to)
                if (!ctx.owner->canSlash(target, candidate.get(), false)) { legal = false; break; }
            if (legal) choices << name;
        }
        if (choices.isEmpty() || !ctx.owner->askForSkillInvoke(this, *ctx.original_data, false)) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), *ctx.original_data);
        return choices.contains(ctx.choice);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        std::unique_ptr<Card> replacement(makeSlash(use, ctx.choice));
        if (!replacement) return false;
        for (ServerPlayer *target : use.to)
            if (!ctx.invoker->canSlash(target, replacement.get(), false)) return false;
        room->setEmotion(ctx.invoker, "weapon/wuxinghelingshan");
        Card *card = replacement.release();
        use.changeCard(card);
        // The use payload, including all nested copies, owns the generated conversion.
        use.setOwnedCard(card);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }

private:
    Card *makeSlash(const CardUseStruct &use, const QString &name) const
    {
        if (!use.card) return nullptr;
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return nullptr;
        if (use.card->isVirtualCard()) card->addSubcards(use.card->getSubcards());
        else card->addSubcard(use.card);
        card->setSkillName(objectName());
        return card;
    }
};

Wuxinghelingshan::Wuxinghelingshan(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("wuxinghelingshan");
}

class WutiesuolianSkill : public WeaponSkillV2
{
public:
    WutiesuolianSkill() : WeaponSkillV2("wutiesuolian")
    {
        events << TargetSpecified;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return WeaponSkillV2::triggerable(player) && use.card && use.card->isKindOf("Slash") && !use.to.isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = ctx.original_data->value<CardUseStruct>().to;
        return !ctx.targets.isEmpty();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.invoker, objectName(), true);
        room->setEmotion(ctx.invoker, "weapon/wutiesuolian");
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker->isAlive() || !ctx.invoker->hasWeapon(objectName()) || !isSourceAvailable(room, ctx)) return false;
        if (target->isChained()) {
            LogMessage log;
            log.type = "#ViewAllCards";
            log.from = ctx.invoker;
            log.to << target;
            room->sendLog(log, room->getOtherPlayers(ctx.invoker, true));
            room->showAllCards(target, ctx.invoker);
        } else {
            room->setPlayerChained(target);
        }
        return false;
    }
};

Wutiesuolian::Wutiesuolian(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("wutiesuolian");
}

class HeiguangkaiSkill : public ArmorSkillV2
{
public:
    HeiguangkaiSkill() : ArmorSkillV2("heiguangkai")
    {
        events << TargetConfirmed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return ArmorSkillV2::triggerable(player) && use.card && use.to.contains(player) && use.to.size() > 1
                && (use.card->isKindOf("Slash") || use.card->isNDTrick())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        LogMessage log;
        log.type = "#ArmorNullify";
        log.from = ctx.invoker;
        log.arg = objectName();
        log.arg2 = use.card->objectName();
        room->sendLog(log);
        room->setEmotion(ctx.invoker, "armor/heiguangkai");
        room->notifySkillInvoked(ctx.invoker, objectName());
        use.nullified_list << ctx.invoker->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

Heiguangkai::Heiguangkai(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("heiguangkai");
}

class HuxinjingSkill : public ArmorSkillV2
{
public:
    HuxinjingSkill() : ArmorSkillV2("huxinjing")
    {
        events << DamageInflicted;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return ArmorSkillV2::triggerable(player) && player->getArmor()
                && player->getArmor()->objectName() == objectName()
                && data.value<DamageStruct>().damage >= player->getHp()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->getArmor() || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.extra_data = ctx.owner->getArmor()->getEffectiveId();
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *armor = ctx.owner->getArmor();
        if (!armor || armor->objectName() != objectName() || armor->getEffectiveId() != ctx.extra_data.toInt()) return false;
        const CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, ctx.owner->objectName(), objectName(), "");
        room->throwCard(armor, reason, nullptr);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Physical equipment admission survives its own successful payment.
        room->setEmotion(ctx.invoker, "armor/huxinjing");
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return ctx.invoker->damageRevises(*ctx.original_data, -damage.damage);
    }
};

Huxinjing::Huxinjing(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("huxinjing");
}

class TaigongyinfuSkill : public TreasureSkillV2
{
public:
    TaigongyinfuSkill() : TreasureSkillV2("taigongyinfu") { events << EventPhaseStart << EventPhaseEnd; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TreasureSkillV2::triggerable(player) && player->getPhase() == Player::Play
                && (event == EventPhaseStart || !player->isKongcheng())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *player : room->getAlivePlayers())
                if (!player->isChained()) candidates << player;
            if (candidates.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@taigongyinfu-chain", true, true);
            if (!target) return false;
            ctx.targets = {target};
            return true;
        }
        const Card *card = room->askForCard(ctx.owner, ".|.|.|hand", "@taigongyinfu-recast",
            *ctx.original_data, Card::MethodNone);
        if (!card || card->isVirtualCard() || ctx.owner->isCardLimited(card, Card::MethodRecast)
            || !ctx.owner->handCards().contains(card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }

    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) return true;
        const int id = ctx.extra_data.toInt();
        const Card *card = Sanguosha->getCard(id);
        if (!card || !ctx.owner->handCards().contains(id) || ctx.owner->isCardLimited(card, Card::MethodRecast)) return false;
        LogMessage log;
        log.type = "#UseCard_Recast"; log.from = ctx.owner; log.card_str = card->toString();
        room->sendLog(log);
        CardMoveReason reason(CardMoveReason::S_REASON_RECAST, ctx.owner->objectName());
        reason.m_skillName = objectName();
        room->moveCardTo(card, ctx.owner, nullptr, Player::DiscardPile, reason, true);
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->setEmotion(ctx.invoker, "treasure/taigongyinfu");
        if (event == EventPhaseEnd) {
            LogMessage log;
            log.type = "#InvokeSkill"; log.from = ctx.invoker; log.arg = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(ctx.invoker, objectName());
            ctx.invoker->broadcastSkillInvoke("@recast");
            ctx.invoker->drawCards(getEffectiveAmount(ctx), "recast");
        }
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        if (event == EventPhaseStart && !target->isChained()) room->setPlayerChained(target);
        return false;
    }
};

Taigongyinfu::Taigongyinfu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("taigongyinfu");
}

class TianjituSkill : public TreasureSkillV2
{
public:
    TianjituSkill() : TreasureSkillV2("tianjitu")
    { events << CardsMoveOneTime; frequency = Compulsory; global = true; }

    static void retainMovement(ServerPlayer *player, int cardId, bool drawing)
    {
        Room *room = player->getRoom();
        const qint64 moveEvent = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
        if (cardId < 0 || moveEvent <= 0) return;
        const int serial = player->getTag("TianjituNextReceipt").toInt() + 1;
        player->setTag("TianjituNextReceipt", serial);
        QVariantList receipts = player->getTag("TianjituReceipts").toList();
        receipts << QVariantMap{{"card", cardId}, {"draw", drawing}, {"dispatch", serial},
            {"move_event", QString::number(moveEvent)}};
        player->setTag("TianjituReceipts", receipts);
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive()) return true;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        const qint64 moveEvent = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
        if (moveEvent <= 0) return true;
        for (const QVariant &value : player->getTag("TianjituReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            const int index = move.card_ids.indexOf(receipt.value("card").toInt());
            if (index < 0 || receipt.value("move_event").toLongLong() != moveEvent) continue;
            const bool drawing = receipt.value("draw").toBool();
            if (drawing ? (move.from != player || move.from_places.value(index) != Player::PlaceEquip)
                        : (move.to != player || move.to_place != Player::PlaceEquip)) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.initiator = ctx.invoker = player;
            // Physical equipment callbacks retain movement identity, without inventing a skill-instance root.
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.amount = drawing ? 5 : 1;
            ctx.extra_data = value; ctx.targets = {player};
            ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }

    bool prepareSource(Room *room, SkillContext &ctx) const override { return isSourceAvailable(room, ctx); }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->isAlive() && !ctx.activationRef.isValid()
            && ctx.owner->getTag("TianjituReceipts").toList().contains(ctx.extra_data);
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantList receipts = ctx.owner->getTag("TianjituReceipts").toList();
        if (!receipts.removeOne(ctx.extra_data)) ctx.targets.clear();
        ctx.owner->setTag("TianjituReceipts", receipts);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        LogMessage log; log.type = "#TriggerEquipSkill"; log.from = ctx.owner; log.arg = objectName();
        room->sendLog(log); room->setEmotion(ctx.owner, "treasure/tianjitu");
        room->notifySkillInvoked(ctx.owner, objectName());
        if (receipt.value("draw").toBool()) {
            target->drawCards(qMax(0, getEffectiveAmount(ctx) - target->getHandcardNum()), objectName());
        } else {
            const int id = receipt.value("card").toInt();
            const QVariant previous = target->getTag("TianjituDiscardForAI");
            target->setTag("TianjituDiscardForAI", id);
            const auto restore = qScopeGuard([target, previous] {
                if (previous.isValid()) target->setTag("TianjituDiscardForAI", previous);
                else target->removeTag("TianjituDiscardForAI");
            });
            room->askForDiscard(target, objectName(), getEffectiveAmount(ctx), getEffectiveAmount(ctx),
                false, true, "", "^" + Sanguosha->getCard(id)->toString());
        }
        return false;
    }
};
Tianjitu::Tianjitu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("tianjitu");
}

void Tianjitu::onInstall(ServerPlayer *player) const
{
    if (player->isAlive() && player->hasTreasure(objectName()))
        TianjituSkill::retainMovement(player, getEffectiveId(), false);
    Treasure::onInstall(player);
}

void Tianjitu::onUninstall(ServerPlayer *player) const
{
    if (player->isAlive() && player->hasTreasure(objectName(), false))
        TianjituSkill::retainMovement(player, getEffectiveId(), true);
    Treasure::onUninstall(player);
}

Tongque::Tongque(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("tongque");
}

void Tongque::onInstall(ServerPlayer *player) const
{
    if (player->isAlive() && player->hasTreasure(objectName()))
        player->getRoom()->addPlayerMark(player, "YingBianDirectlyEffective");
    Treasure::onInstall(player);
}

void Tongque::onUninstall(ServerPlayer *player) const
{
    player->getRoom()->removePlayerMark(player, "YingBianDirectlyEffective");
    Treasure::onUninstall(player);
}

Suijiyingbian::Suijiyingbian(Suit suit, int number)
    : TrickCard(suit, number)
{
    //target_fixed = true;
    setObjectName("suijiyingbian");
}

QString Suijiyingbian::getSubtype() const
{
    return "suijiyingbian";
}

bool Suijiyingbian::isAvailable(const Player *player) const
{
	if(player->hasFlag("CurrentPlayer")){
		Card*dc = Sanguosha->cloneCard(player->property("Suijiyingbian").toString());
		if(dc){
			dc->setSkillName("suijiyingbian");
			dc->addSubcard(this);
			dc->deleteLater();
			return dc->isAvailable(player);
		}
	}
	return false;
}

bool Suijiyingbian::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	QString cn = Self->property("Suijiyingbian").toString();
	if(cn.isEmpty()) return false;
    Card *dc = Sanguosha->cloneCard(cn);
	dc->setSkillName("suijiyingbian");
	dc->addSubcard(this);
    dc->deleteLater();
    return dc->targetFilter(targets,to_select,Self);
}

bool Suijiyingbian::targetFixed() const
{
	if(Self){
		foreach (const Player *p, Self->getAliveSiblings(true)) {
			QString cn = p->property("Suijiyingbian").toString();
			if(!cn.isEmpty()&&p->hasFlag("CurrentPlayer")){
				Card *dc = Sanguosha->cloneCard(cn);
				dc->setSkillName("suijiyingbian");
				dc->addSubcard(this);
				dc->deleteLater();
				return dc->targetFixed();
			}
		}
	}
    return true;
}

bool Suijiyingbian::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	QString cn = Self->property("Suijiyingbian").toString();
	if(cn.isEmpty()) return false;
    Card *dc = Sanguosha->cloneCard(cn);
	dc->setSkillName("suijiyingbian");
	dc->addSubcard(this);
    dc->deleteLater();
    return dc->targetsFeasible(targets, Self);
}

const Card *Suijiyingbian::validate(CardUseStruct &use) const
{
    Card *dc = Sanguosha->cloneCard(use.from->property("Suijiyingbian").toString(),getSuit(),getNumber());
	dc->setSkillName("suijiyingbian");
	use.changeCard(dc);
	if(isVirtualCard()){
		dc->addSubcards(subcards);
		dc->deleteLater();
		return dc;
	}
	WrappedCard *w_card = Sanguosha->getWrappedCard(getEffectiveId());
	w_card->takeOver(dc);
	use.from->getRoom()->notifyUpdateCard(use.from,getEffectiveId(),w_card);
	return w_card;
}

const Card *Suijiyingbian::validateInResponse(ServerPlayer *player) const
{
	QString cn = player->property("Suijiyingbian").toString();
	if(cn.isEmpty()) return this;
    Card *dc = Sanguosha->cloneCard(cn,getSuit(),getNumber());
	dc->setSkillName("suijiyingbian");
	if(isVirtualCard()){
		dc->addSubcards(subcards);
		dc->deleteLater();
		return dc;
	}
	WrappedCard *w_card = Sanguosha->getWrappedCard(getEffectiveId());
	w_card->takeOver(dc);
	player->getRoom()->notifyUpdateCard(player,getEffectiveId(),w_card);
	return w_card;
}

YingbianPackage::YingbianPackage()
    : Package("yingbian_cards", Package::CardPack)
{
    QList<Card *> cards;

    Card *slash1 = new Slash(Card::Spade, 9);
    slash1->setProperty("YingBianEffects", "yb_canqu2");
    Card *slash2 = new Slash(Card::Spade, 9);
    slash2->setProperty("YingBianEffects", "yb_canqu2");
    Card *slash3 = new Slash(Card::Spade, 10);
    slash3->setProperty("YingBianEffects", "yb_canqu2");
    Card *slash4 = new Slash(Card::Spade, 10);
    slash4->setProperty("YingBianEffects", "yb_canqu2");
    Card *slash5 = new Slash(Card::Club, 11);
    slash5->setProperty("YingBianEffects", "yb_canqu2");
    Card *slash6 = new Slash(Card::Diamond, 8);
    slash6->setProperty("YingBianEffects", "yb_canqu2");
    Card *slash7 = new Slash(Card::Club, 2);
    slash7->setProperty("YingBianEffects", "yb_kongchao3");
    Card *slash8 = new Slash(Card::Club, 3);
    slash8->setProperty("YingBianEffects", "yb_kongchao3");
    Card *slash9 = new Slash(Card::Club, 4);
    slash9->setProperty("YingBianEffects", "yb_kongchao3");
    Card *slash10 = new Slash(Card::Club, 5);
    slash10->setProperty("YingBianEffects", "yb_kongchao3");

    cards << slash1//yb_canqu2
          << slash2//yb_canqu2
          << slash3//yb_canqu2
          << slash4//yb_canqu2
          << slash7//yb_kongchao3
          << slash8//yb_kongchao3
          << slash9//yb_kongchao3
          << slash10//yb_kongchao3
          << new Slash(Card::Club, 6)
          << new Slash(Card::Club, 7)
          << new Slash(Card::Club, 8)
          << new Slash(Card::Club, 8)
          << new Slash(Card::Club, 11)
          << slash5//yb_canqu2
          << new Slash(Card::Heart, 10)
          << new Slash(Card::Heart, 10)
          << new Slash(Card::Heart, 11)
          << new Slash(Card::Diamond, 6)
          << new Slash(Card::Diamond, 7)
          << slash6//yb_canqu2
          << new Slash(Card::Diamond, 9)
          << new Slash(Card::Diamond, 13)

          << new IceSlash(Card::Spade, 7)
          << new IceSlash(Card::Spade, 7)
          << new IceSlash(Card::Spade, 8)
          << new IceSlash(Card::Spade, 8)
          << new IceSlash(Card::Spade, 8)

          << new ThunderSlash(Card::Spade, 4)
          << new ThunderSlash(Card::Spade, 5)
          << new ThunderSlash(Card::Spade, 6)
          << new ThunderSlash(Card::Club, 5)
          << new ThunderSlash(Card::Club, 6)
          << new ThunderSlash(Card::Club, 7)
          << new ThunderSlash(Card::Club, 8)
          << new ThunderSlash(Card::Club, 9)
          << new ThunderSlash(Card::Club, 9)
          << new ThunderSlash(Card::Club, 10)
          << new ThunderSlash(Card::Club, 10);

    Card *fire_slash1 = new FireSlash(Card::Heart, 10);
    fire_slash1->setProperty("YingBianEffects", "yb_canqu1");
    Card *fire_slash2 = new FireSlash(Card::Diamond, 4);
    fire_slash2->setProperty("YingBianEffects", "yb_canqu1");

    cards << new FireSlash(Card::Heart, 4)
          << new FireSlash(Card::Heart, 7)
          << fire_slash1//yb_canqu1
          << fire_slash2//yb_canqu1
          << new FireSlash(Card::Diamond, 5)
          << new FireSlash(Card::Diamond, 10);

    Card *jink1 = new Jink(Card::Heart, 2);
    jink1->setProperty("YingBianEffects", "yb_kongchao1");
    Card *jink2 = new Jink(Card::Heart, 2);
    jink2->setProperty("YingBianEffects", "yb_kongchao1");
    Card *jink3 = new Jink(Card::Diamond, 2);
    jink3->setProperty("YingBianEffects", "yb_kongchao1");
    Card *jink4 = new Jink(Card::Diamond, 2);
    jink4->setProperty("YingBianEffects", "yb_kongchao1");

    cards << jink1//yb_kongchao1
          << jink2//yb_kongchao1
          << new Jink(Card::Heart, 8)
          << new Jink(Card::Heart, 9)
          << new Jink(Card::Heart, 11)
          << new Jink(Card::Heart, 12)
          << new Jink(Card::Heart, 13)
          << jink3//yb_kongchao1
          << jink4//yb_kongchao1
          << new Jink(Card::Diamond, 3)
          << new Jink(Card::Diamond, 4)
          << new Jink(Card::Diamond, 5)
          << new Jink(Card::Diamond, 6)
          << new Jink(Card::Diamond, 6)
          << new Jink(Card::Diamond, 7)
          << new Jink(Card::Diamond, 7)
          << new Jink(Card::Diamond, 8)
          << new Jink(Card::Diamond, 8)
          << new Jink(Card::Diamond, 9)
          << new Jink(Card::Diamond, 10)
          << new Jink(Card::Diamond, 10)
          << new Jink(Card::Diamond, 11)
          << new Jink(Card::Diamond, 11)
          << new Jink(Card::Diamond, 11)

          << new Peach(Card::Heart, 3)
          << new Peach(Card::Heart, 4)
          << new Peach(Card::Heart, 5)
          << new Peach(Card::Heart, 6)
          << new Peach(Card::Heart, 6)
          << new Peach(Card::Heart, 7)
          << new Peach(Card::Heart, 8)
          << new Peach(Card::Heart, 9)
          << new Peach(Card::Heart, 12)
          << new Peach(Card::Diamond, 2)
          << new Peach(Card::Diamond, 3)
          << new Peach(Card::Diamond, 12)

          << new Analeptic(Card::Spade, 3)
          << new Analeptic(Card::Spade, 9)
          << new Analeptic(Card::Club, 3)
          << new Analeptic(Card::Club, 9)
          << new Analeptic(Card::Diamond, 9);

    Card *sa1 = new SavageAssault(Card::Spade, 7);
    sa1->setProperty("YingBianEffects", "yb_fujia2");
    Card *sa2 = new SavageAssault(Card::Spade, 13);
    sa2->setProperty("YingBianEffects", "yb_fujia2");

    cards << new ArcheryAttack(Card::Heart, 1)
          << sa1//yb_fujia2
          << sa2//yb_fujia2
          << new SavageAssault(Card::Club, 7);

    cards << new GodSalvation(Card::Heart, 1)
          << new AmazingGrace(Card::Heart, 3)
          << new AmazingGrace(Card::Heart, 4);

    Card *duel1 = new Duel(Card::Spade, 1);
    duel1->setProperty("YingBianEffects", "yb_fujia1");
    Card *duel2 = new Duel(Card::Club, 1);
    duel2->setProperty("YingBianEffects", "yb_fujia1");
    Card *zhu1 = new Zhujinqiyuan(Card::Spade, 12);
    zhu1->setProperty("YingBianEffects", "yb_zhuzhan1");
    Card *zhu2 = new Zhujinqiyuan(Card::Club, 3);
    zhu2->setProperty("YingBianEffects", "yb_zhuzhan1");
    Card *zhu3 = new Zhujinqiyuan(Card::Club, 4);
    zhu3->setProperty("YingBianEffects", "yb_zhuzhan1");
    Card *drowning1 = new Drowning(Card::Spade, 3);
    drowning1->setProperty("YingBianEffects", "yb_zhuzhan1");
    Card *drowning2 = new Drowning(Card::Spade, 4);
    drowning2->setProperty("YingBianEffects", "yb_zhuzhan1");
    Card *null1 = new Nullification(Card::Spade, 13);
    null1->setProperty("YingBianEffects", "yb_kongchao1");
    Card *null2 = new Nullification(Card::Heart, 13);
    null2->setProperty("YingBianEffects", "yb_kongchao2");
    Card *chu = new Chuqibuyi(Card::Heart, 3);
    chu->setProperty("YingBianEffects", "yb_zhuzhan2");

    cards << duel1//yb_fujia1
          << duel2//yb_fujia1
          << new Duel(Card::Diamond, 1)
          << new Zhujinqiyuan(Card::Spade, 3)
          << zhu1//yb_zhuzhan1
          << zhu2//yb_zhuzhan1
          << zhu3//yb_zhuzhan1
          << drowning1//yb_zhuzhan1
          << drowning2//yb_zhuzhan1
          << new Dismantlement(Card::Spade, 4)
          << new Dismantlement(Card::Heart, 2)
          << new Dismantlement(Card::Heart, 12)
          << new Snatch(Card::Spade, 11)
          << new Snatch(Card::Diamond, 3)
          << new Snatch(Card::Diamond, 4)
          << new IronChain(Card::Spade, 11)
          << new IronChain(Card::Spade, 12)
          << new IronChain(Card::Club, 10)
          << new IronChain(Card::Club, 11)
          << new IronChain(Card::Club, 12)
          << new IronChain(Card::Club, 13)
          << new Nullification(Card::Spade, 11)
          << null1//yb_kongchao1
          << new Nullification(Card::Club, 12)
          << new Nullification(Card::Club, 13)
          << new Nullification(Card::Heart, 1)
          << null2//yb_kongchao2
          << new Nullification(Card::Diamond, 13)
          << chu//yb_zhuzhan2
          << new Chuqibuyi(Card::Diamond, 12)
          << new Dongzhuxianji(Card::Heart, 7)
          << new Dongzhuxianji(Card::Heart, 8)
          << new Dongzhuxianji(Card::Heart, 9)
          << new Dongzhuxianji(Card::Heart, 11)
          << new Suijiyingbian(Card::Spade, 2);

    cards << new Indulgence(Card::Spade, 6)
          << new Indulgence(Card::Club, 6)
          << new Indulgence(Card::Heart, 6)
          << new SupplyShortage(Card::Spade, 10)
          << new SupplyShortage(Card::Club, 4)
          << new Lightning(Card::Heart, 12);

    cards << new GudingBlade(Card::Spade, 1)
          << new DoubleSword(Card::Spade, 2)
          << new Blade(Card::Spade, 5)
          << new QinggangSword(Card::Spade, 6)
          << new Spear(Card::Spade, 12)
          << new Crossbow(Card::Club, 1)
          << new KylinBow(Card::Heart, 5)
          << new Crossbow(Card::Diamond, 1)
          << new Wuxinghelingshan(Card::Diamond, 1)
          << new Axe(Card::Diamond, 5)
          << new Wutiesuolian(Card::Diamond, 12);

    cards << new EightDiagram(Card::Spade, 2)
          << new Vine(Card::Spade, 2)
          << new Vine(Card::Club, 2)
          << new RenwangShield(Card::Club, 2)
          << new Heiguangkai(Card::Club, 2)
          << new Huxinjing(Card::Club, 1);

    QList<Card *> horses;
    horses << new DefensiveHorse(Card::Spade, 5)
           << new DefensiveHorse(Card::Club, 5)
           << new DefensiveHorse(Card::Heart, 13)
           << new DefensiveHorse(Card::Diamond, 13)
           << new OffensiveHorse(Card::Spade, 13)
           << new OffensiveHorse(Card::Heart, 5)
           << new OffensiveHorse(Card::Diamond, 13);

    horses.at(0)->setObjectName("jueying");
    horses.at(1)->setObjectName("dilu");
    horses.at(2)->setObjectName("zhuahuangfeidian");
    horses.at(3)->setObjectName("hualiu");
    horses.at(4)->setObjectName("dayuan");
    horses.at(5)->setObjectName("chitu");
    horses.at(6)->setObjectName("zixing");

    cards << horses;

    cards << new Taigongyinfu(Card::Spade, 1)
          << new Tianjitu(Card::Club, 12)
          << new Tongque(Card::Club, 13);
          //<< new WoodenOx(Card::Diamond, 5);

    foreach(Card *card, cards)
        card->setParent(this);

    skills << new WuxinghelingshanSkill << new WutiesuolianSkill << new HeiguangkaiSkill 
		<< new HuxinjingSkill << new TaigongyinfuSkill << new TianjituSkill;
}
ADD_PACKAGE(Yingbian)
