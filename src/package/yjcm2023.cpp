#include "yjcm2023.h"
#include "engine.h"
#include "maneuvering.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "clientplayer.h"
#include <memory>
#include <QScopeGuard>

static auto yjcm2023RequestScope()
{
    RoomState *state = Sanguosha->currentRoomState();
    const auto reason = state->getCurrentCardUseReason();
    const QString pattern = state->getCurrentCardUsePattern();
    return qScopeGuard([=] {
        state->setCurrentCardUseReason(reason);
        state->setCurrentCardUsePattern(pattern);
    });
}

static QString zhizheEquipObjectNameByArea(int area)
{
	if (area == 0) return "_zhizhe_weapon";
	if (area == 1) return "_zhizhe_armor";
	if (area == 2) return "_zhizhe_defensivehorse";
	if (area == 3) return "_zhizhe_offensivehorse";
	if (area == 4) return "_zhizhe_treasure";
	return QString();
}

static bool safeTurnCardToEquip(Room *room, ServerPlayer *source, int cardId, const QString &equipObjectName, const QString &skillName, ServerPlayer *materialOwner = nullptr)
{
	if (!room || !source || equipObjectName.isEmpty())
		return false;
	if (!materialOwner) materialOwner = source;
	if (room->getCardPlace(cardId) != Player::PlaceHand || room->getCardOwner(cardId) != materialOwner)
		return false;

	const Card *rawCard = Sanguosha->getCard(cardId);
	if (!rawCard)
		return false;

	std::unique_ptr<Card> equipCard(Sanguosha->cloneCard(equipObjectName, rawCard->getSuit(), rawCard->getNumber()));
	if (!equipCard || !equipCard->isKindOf("EquipCard"))
		return false;

	const EquipCard *equip = qobject_cast<const EquipCard *>(equipCard->getRealCard());
	if (!equip)
		return false;

	int location = equip->location();
	if (!source->hasEquipArea(location))
		return false;

	WrappedCard *wrapped = Sanguosha->getWrappedCard(cardId);
	if (!wrapped)
		return false;

	// 移入装备区时 updateCardsChange 会 refilter，把牌重置回引擎原牌；
	// #zhizhe 的 tag 与技能必须在移动前就位，否则非装备牌进装备区
	QStringList info;
	info << equipObjectName << rawCard->getSuitString() << QString::number(rawCard->getNumber()) << skillName;
	room->setTag("ZhizheFilter_" + QString::number(cardId), info.join("+"));
	foreach (ServerPlayer *p, room->getPlayers())
		if (!p->ownsSkill("#zhizhe")) room->acquireSkill(p, "#zhizhe");

	wrapped->takeOver(equipCard.release());
	room->notifyUpdateCard(source, cardId, wrapped);

	QList<CardsMoveStruct> exchangeMove;
	if (source->getEquips(location).length() >= source->getEquipArea(location)) {
		const Card *oldEquip = source->getEquip(location);
		if (oldEquip) {
			CardsMoveStruct moveOld(oldEquip->getEffectiveId(), nullptr, Player::DiscardPile,
				CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, source->objectName(), skillName, "change equip"));
			exchangeMove.append(moveOld);
		}
	}
	CardsMoveStruct moveNew(cardId, source, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_USE, source->objectName(), skillName, ""));
	exchangeMove.append(moveNew);
	room->moveCardsAtomic(exchangeMove, true);
	return true;
}




class Zhitu : public TriggerSkillV2
{
public:
    Zhitu() : TriggerSkillV2("zhitu")
    { events << PreCardUsed << CardFinished; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || use.targetModReveal.useHistoryEventId <= 0) return true;
        const QString key = QString::number(use.targetModReveal.useHistoryEventId);
        QVariantMap pending = room->getTag("ZhituOriginalTargets").toMap();
        if (event == CardFinished) pending.remove(key);
        else if (event == PreCardUsed && use.card && use.to.size() == 1
            && (use.card->isKindOf("BasicCard") || (use.card->isNDTrick() && use.card->isSingleTargetCard())))
            pending.insert(key, use.to.first()->objectName());
        room->setTag("ZhituOriginalTargets", pending);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return event == PreCardUsed && player && player->isAlive() && player->hasSkill(this)
            && use.from == player && originalTarget(room, use)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *original = originalTarget(room, use);
        if (!use.card || !original) return false;
        // All instances share the original single target, even after an earlier copy adds targets.
        ctx.extra_data = original->objectName();
        QList<ServerPlayer *> candidates;
        const int distance = ctx.owner->distanceTo(original);
        for (ServerPlayer *target : room->getCardTargets(ctx.owner, use.card, use.to))
            if (ctx.owner->distanceTo(target) == distance) candidates << target;
        ctx.targets = room->askForPlayersChosen(ctx.owner, candidates, objectName(), 0, 9,
            "zhitu0:" + use.card->objectName(), false, true);
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->peiyin(this); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data || !target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *original = originalTarget(room, use);
        if (!original || !use.card || !ctx.owner || !ctx.owner->isAlive() || use.to.contains(target)
            || ctx.owner->distanceTo(target) != ctx.owner->distanceTo(original)
            || !room->getCardTargets(ctx.owner, use.card, use.to).contains(target)) return false;
        use.to << target;
        room->sortByActionOrder(use.to);
        ctx.original_data->setValue(use);
        return false;
    }
private:
    static ServerPlayer *originalTarget(Room *room, const CardUseStruct &use)
    {
        if (use.targetModReveal.useHistoryEventId <= 0) return nullptr;
        const QString name = room->getTag("ZhituOriginalTargets").toMap()
            .value(QString::number(use.targetModReveal.useHistoryEventId)).toString();
        return name.isEmpty() ? nullptr : room->findChild<ServerPlayer *>(name);
    }
};
FujueCard::FujueCard()
{
	setSkillName("fujue");
}

void FujueCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int n = source->getCardCount();
	room->moveField(source,"fujue",true,"ej");
	int x = source->getCardCount();
	int h = 5-source->getHandcardNum();
	if(h>0) source->drawCards(h,"fujue");
	else if(h<0) room->askForDiscard(source,"fujue",-h,-h);
	if((x<n&&h>0)||(x>n&&h<0)){
		room->addDistance(source,-1);
	}
}

class Fujue : public ViewAsSkillV2
{
public:
    Fujue() : ViewAsSkillV2("fujue") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "FujueCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive()) return FinishSkill;
        Room *room = source->getRoom();
        const int before = source->getCardCount();
        QList<ServerPlayer *> sources;
        for (ServerPlayer *from : room->getAlivePlayers()) {
            bool movable = false;
            for (const Card *card : from->getCards("ej")) {
                for (ServerPlayer *to : room->getOtherPlayers(from))
                    if (canTransfer(source, from, to, card)) { movable = true; break; }
                if (movable) break;
            }
            if (movable) sources << from;
        }
        if (!sources.isEmpty()) {
            ServerPlayer *from = room->askForPlayerChosen(source, sources, objectName() + "_from", "@movefield-from-optional", true);
            if (from && from->isAlive() && source->isAlive()) {
                QList<int> disabled;
                for (const Card *card : from->getCards("ej")) {
                    bool movable = false;
                    for (ServerPlayer *to : room->getOtherPlayers(from))
                        if (canTransfer(source, from, to, card)) { movable = true; break; }
                    if (!movable) disabled << card->getEffectiveId();
                }
                if (disabled.size() < from->getCards("ej").size()) {
                    const int id = room->askForCardChosen(source, from, "ej", objectName(), false, Card::MethodNone, disabled);
                    const Card *card = id < 0 ? nullptr : Sanguosha->getCard(id);
                    QList<ServerPlayer *> destinations;
                    for (ServerPlayer *to : room->getOtherPlayers(from)) if (canTransfer(source, from, to, card)) destinations << to;
                    if (!destinations.isEmpty()) {
                        ServerPlayer *to = room->askForPlayerChosen(source, destinations, objectName() + "_to", "@movefield-to:" + card->objectName());
                        QStringList accepted;
                        for (ServerPlayer *participant : QList<ServerPlayer *>{from, to}) {
                            SkillContext move = ctx;
                            move.choice = "field";
                            move.extra_data = QStringList();
                            skillEffect(move, participant);
                            accepted << move.extra_data.toStringList();
                        }
                        if (accepted.size() == 2 && accepted[0] != accepted[1]) {
                            ServerPlayer *actualFrom = room->findPlayerByObjectName(accepted[0]);
                            ServerPlayer *actualTo = room->findPlayerByObjectName(accepted[1]);
                            // Recheck the selected material and both zones after recipient callbacks.
                            if (canTransfer(source, actualFrom, actualTo, card))
                                room->moveCardTo(card, actualFrom, actualTo, room->getCardPlace(id),
                                    CardMoveReason(CardMoveReason::S_REASON_TRANSFER, source->objectName(), objectName(), ""), true);
                        }
                    }
                }
            }
        }
        SkillContext settle = ctx;
        settle.choice = "settle";
        settle.extra_data = QVariantMap{{"before", before}, {"after", source->getCardCount()}};
        skillEffect(settle, source);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (ctx.choice == "field") {
            QStringList accepted = ctx.extra_data.toStringList();
            accepted << target->objectName();
            ctx.extra_data = accepted;
            return ContinueEffects;
        }
        Room *room = target->getRoom();
        const QVariantMap counts = ctx.extra_data.toMap();
        const int amount = getEffectiveAmount(ctx);
        const int delta = 5 * amount - target->getHandcardNum();
        if (delta > 0) target->drawCards(delta, objectName());
        else if (delta < 0) room->askForDiscard(target, objectName(), -delta, -delta);
        if ((counts.value("after").toInt() < counts.value("before").toInt() && delta > 0)
            || (counts.value("after").toInt() > counts.value("before").toInt() && delta < 0)) room->addDistance(target, -amount);
        return ContinueEffects;
    }
private:
    static bool canTransfer(ServerPlayer *actor, ServerPlayer *from, ServerPlayer *to, const Card *card)
    {
        if (!actor || !actor->isAlive() || !from || !from->isAlive() || !to || !to->isAlive() || from == to || !card) return false;
        Room *room = actor->getRoom();
        const int id = card->getEffectiveId();
        if (id < 0 || room->getCardOwner(id) != from || !from->canMove(from, id) || !actor->canMove(from, id)
            || actor->isProhibited(to, card)) return false;
        if (room->getCardPlace(id) == Player::PlaceEquip) {
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            if (!equip) return false;
            for (int slot : equip->getOccupyLocations()) if (!to->hasEquipArea(slot) || to->getEquip(slot)) return false;
            return true;
        }
        return room->getCardPlace(id) == Player::PlaceDelayedTrick && to->hasJudgeArea() && !to->containsTrick(card->objectName());
    }
};
GongqiaoCard::GongqiaoCard()
{
	setSkillName("gongqiao");
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void GongqiaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QStringList choices;
	for (int i = 0; i < 5; i++){
		if(source->hasEquipArea(i))
			choices << QString("EquipArea%1").arg(i);
	}
	if(choices.isEmpty()) return;
	QString choice = room->askForChoice(source,"gongqiao",choices.join("+"));
	choice.remove("EquipArea");
	int n = choice.toInt();
	QString equipObjectName = zhizheEquipObjectNameByArea(n);
	if (equipObjectName.isEmpty())
		return;
	if (!safeTurnCardToEquip(room, source, getEffectiveId(), equipObjectName, "gongqiao"))
		return;
	QStringList info = room->getTag("gongqiaoEquip").toStringList();
	info << QString::number(getEffectiveId());
	room->setTag("gongqiaoEquip", info);
}

class GongqiaoVs : public ViewAsSkillV2
{
public:
	GongqiaoVs() : ViewAsSkillV2("gongqiao", 1)
	{
		setPhaseName("Play");
	}

	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->isKongcheng() && request.initiator->hasEquipArea();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		return request.initiator && candidate && !candidate->hasFlag("using") && request.selectedCardIds.isEmpty()
			&& request.initiator->handCards().contains(candidate->getEffectiveId());
	}
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return NoTarget; }
	QString historyKey(const ActiveSkillRequest &) const override { return "GongqiaoCard"; }
	bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		if (!ctx.invoker) return false;
		QStringList choices;
		for (int area = 0; area < 5; ++area)
			if (ctx.invoker->hasEquipArea(area)) choices << QString("EquipArea%1").arg(area);
		if (choices.isEmpty()) return false;
		ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"));
		return choices.contains(ctx.choice);
	}
	bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1 || !request.initiator->hasEquipArea(ctx.choice.mid(9).toInt())) return false;
		ActiveSkillRequest empty = request;
		empty.selectedCardIds.clear();
		return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
        if (ctx.choice.isEmpty() && ctx.invoker) {
            QStringList choices;
            for (int area = 0; area < 5; ++area)
                if (ctx.invoker->hasEquipArea(area)) choices << QString("EquipArea%1").arg(area);
            if (choices.isEmpty()) return FinishSkill;
            ctx.choice = ctx.invoker->getRoom()->askForChoice(ctx.invoker, objectName(), choices.join("+"));
            if (!choices.contains(ctx.choice)) return FinishSkill;
        }
        skillEffect(ctx, ctx.invoker);
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!target || !target->isAlive() || !ctx.initiator || !ctx.use_card || getEffectiveAmount(ctx) <= 0 || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
		Room *room = ctx.invoker->getRoom();
		const int id = ctx.use_card->getSubcards().first();
		if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
		const QString face = zhizheEquipObjectNameByArea(ctx.choice.mid(9).toInt());
		// This receipt belongs to the applied card face, so root-skill loss must not erase it.
		QVariantMap receipts = room->getTag("GongqiaoFaces").toMap();
		receipts[QString::number(id)] = QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
			{"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}};
		room->setTag("GongqiaoFaces", receipts);
		if (!safeTurnCardToEquip(room, target, id, face, objectName(), ctx.initiator)) {
			receipts = room->getTag("GongqiaoFaces").toMap();
			receipts.remove(QString::number(id));
			room->setTag("GongqiaoFaces", receipts);
		}
		return FinishSkill;
	}
};

class Gongqiao : public TriggerSkillV2
{
public:
    Gongqiao() : TriggerSkillV2("gongqiao")
    {
        global = true;
        events << PreHpRecover << ConfirmDamage << CardFinished << CardsMoveOneTime;
        view_as_skill = new GongqiaoVs;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime) return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || move.to_place == Player::PlaceEquip) return true;
        QVariantMap receipts = room->getTag("GongqiaoFaces").toMap();
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const QString id = QString::number(move.card_ids[i]);
            if (move.from_places.value(i) == Player::PlaceEquip && receipts.contains(id)) {
                receipts.remove(id);
                room->removeTag("ZhizheFilter_" + id);
                // Movement filtering precedes this event; immediately restore a recipient's hand face.
                ServerPlayer *holder = room->getCardOwner(move.card_ids[i]);
                if (holder) room->filterCards(holder, {Sanguosha->getCard(move.card_ids[i])}, true);
            }
        }
        // Record runs once per observer dispatch and remains available after grant removal.
        room->setTag("GongqiaoFaces", receipts);
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardsMoveOneTime) return {};
        // PreHpRecover is dispatched to the recipient, while this bonus belongs to the card user.
        if (event == PreHpRecover) player = data.value<RecoverStruct>().who;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return {};
        const Card *card = nullptr;
        if (event == PreHpRecover) card = data.value<RecoverStruct>().card;
        else if (event == ConfirmDamage) card = data.value<DamageStruct>().card;
        else card = data.value<CardUseStruct>().card;
        if (!card || card->getTypeId() == Card::TypeSkill) return {};
        const Card::CardType needed = event == CardFinished ? Card::TypeTrick : Card::TypeBasic;
        bool hasMaterial = false;
        for (int id : player->getEquipsId())
            if (Sanguosha->getEngineCard(id)->getTypeId() == needed) hasMaterial = true;
        if (!hasMaterial) return {};
        if (event != CardFinished && !card->isKindOf("BasicCard")) return {};
        if (event == ConfirmDamage && data.value<DamageStruct>().from != player) return {};
        if (event == CardFinished) {
            if (data.value<CardUseStruct>().from != player) return {};
            const QVariantMap history = room->queryCardHistory(player, "turn");
            if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return {};
            int count = 0;
            for (const QVariant &entry : history.value("items").toList())
                if (entry.toMap().value("type").toInt() == card->getTypeId()) ++count;
            if (count != 1) return {};
        }
        return {{player, {objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        if (event == PreHpRecover) {
            RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
            recover.recover += amount;
            ctx.original_data->setValue(recover);
        } else if (event == ConfirmDamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            damage.damage += amount;
            ctx.original_data->setValue(damage);
        } else {
            ctx.owner->drawCards(amount, objectName());
        }
        return false;
    }
};

class GongqiaoMax : public MaxCardsSkillV2
{
public:
    GongqiaoMax() : MaxCardsSkillV2("#gongqiao-max") { setBaseAmount(3); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder) return CorrectSkillResult::noEffect();
        for (int id : ctx.holder->getEquipsId())
            if (Sanguosha->getEngineCard(id)->isKindOf("EquipCard"))
                return CorrectSkillResult::useAmount(ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};
class Jingqiao : public TriggerSkillV2
{
public:
    Jingqiao() : TriggerSkillV2("jingqiao")
    {
        events << EventSkillInvoking << CardsMoveOneTime; global = true;
        frequency = Compulsory;
    }
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
            if (candidate.sourceRef.isValid() && isUsable(candidate)) result << candidate.skill_name + "*5";
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
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QStringList availableAreas(const SkillContext &ctx) const
    {
        if (!ctx.owner || !ctx.original_data || !ctx.activationRef.isValid()) return {};
        Room *room = ctx.owner->getRoom();
        const QString turn = room->historyScopes().value("turn_id").toString();
        if (turn.isEmpty() || turn == "0") return {};
        const auto &key = ctx.activationRef.key;
        const QVariantMap state = ctx.owner->getSkillInstanceState(key.skillName, key.instanceID);
        const QStringList used = state.value("turn").toString() == turn
            ? state.value("areas").toStringList() : QStringList();
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QStringList result;
        for (int id : move.card_ids) {
            if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceEquip) continue;
            const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
            if (!equip) continue;
            const QString area = QString::number(equip->location());
            if (!used.contains(area) && !result.contains(area)) result << area;
        }
        return result;
    }
    bool checkCustomUsage(const SkillContext &ctx) const override { return !availableAreas(ctx).isEmpty(); }
    void addUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.activationRef.isValid() || ctx.choice.isEmpty()) return;
        const auto &key = ctx.activationRef.key;
        const QString turn = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
        QVariantMap state = ctx.owner->getSkillInstanceState(key.skillName, key.instanceID);
        QStringList used = state.value("turn").toString() == turn
            ? state.value("areas").toStringList() : QStringList();
        if (!used.contains(ctx.choice)) used << ctx.choice;
        state["turn"] = turn;
        state["areas"] = used;
        ctx.owner->setSkillInstanceState(key.skillName, key.instanceID, state);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        // CardsMoveOneTime is broadcast; only the recipient dispatch activates its sources.
        return player && player->isAlive() && player->hasSkill(this) && player->hasTurn()
            && move.to == player && move.to_place == Player::PlaceEquip
            ? TriggerList{{player, usableEntries(player, data)}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const QStringList areas = availableAreas(ctx);
        if (areas.isEmpty()) return false;
        ctx.choice = areas.first();
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target) return false;
        room->sendCompulsoryTriggerLog(target, this);
        const int amount = getEffectiveAmount(ctx);
        target->drawCards(target->getEquips().size() * amount, objectName());
        if (target->isAlive() && amount > 0)
            room->askForDiscard(target, objectName(), 2 * amount, 2 * amount, false, true);
        return false;
    }
};
BeiyuCard::BeiyuCard()
{
	setSkillName("beiyu");
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void BeiyuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QList<int>ids;
	int n = source->getMaxHp()-source->getHandcardNum();
	if(n>0) source->drawCards(n,"beiyu");
	const Card *dc = room->askForCard(source,".!","beiyu0:",QVariant(),Card::MethodNone);
	foreach (const Card*h, source->getHandcards()){
		if(h->getSuit()==dc->getSuit())
			ids << h->getId();
	}
	//qsanShuffle(ids);
	room->moveCardsToEndOfDrawpile(source,ids,"beiyu");
}

class Beiyu : public ViewAsSkillV2
{
public:
	Beiyu() : ViewAsSkillV2("beiyu")
	{
		setPhaseName("Play");
	}

	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}
	TargetMode targetMode() const override { return NoTarget; }
	QString historyKey(const ActiveSkillRequest &) const override { return "BeiyuCard"; }
	EffectFlow effect(SkillContext &ctx) const override
	{
		skillEffect(ctx, ctx.invoker);
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *source) const override
	{
		if (!source) return FinishSkill;
		Room *room = source->getRoom();
		const int draw = source->getMaxHp() - source->getHandcardNum();
		if (draw > 0) source->drawCards(draw * getEffectiveAmount(ctx), objectName());
		if (!source->isAlive() || source->isKongcheng()) return FinishSkill;
		const Card *chosen = room->askForCard(source, ".!", "beiyu0:", QVariant(), Card::MethodNone);
		if (!chosen || !source->handCards().contains(chosen->getEffectiveId()) || chosen->hasFlag("using")) return FinishSkill;
		QList<int> ids;
		for (const Card *card : source->getHandcards())
			if (card->getSuit() == chosen->getSuit() && !card->hasFlag("using")) ids << card->getId();
		room->moveCardsToEndOfDrawpile(source, ids, objectName());
		return FinishSkill;
	}
};

class Duchi : public TriggerSkillV2
{
public:
	Duchi() :TriggerSkillV2("duchi")
	{
		events << EventSkillInvoking << TargetConfirmed; global = true;
	}
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
	LimitScope getLimitScope() const override { return Limit_Turn; }
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
        if (event != TargetConfirmed) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		return player && player->isAlive() && player->hasSkill(this) && player->hasTurn()
			&& use.card && use.card->getTypeId() > 0 && use.to.contains(player) && player != use.from
			? TriggerList{{player, usableEntries(player, data)}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
        if (!isUsable(ctx)) return false;
		if (!ctx.owner || !ctx.original_data || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data, false)) return false;
		ctx.targets = {ctx.owner};
		return true;
	}
	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		// Commit this copy before target interception or nested draw callbacks.
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *owner) const override
	{
		if (!owner || !owner->isAlive() || !ctx.original_data) return false;
		owner->drawCards(getEffectiveAmount(ctx), objectName(), false);
		room->showAllCards(owner);
		const QList<const Card *> cards = owner->getHandcards();
		if (cards.isEmpty()) return false;
		for (const Card *card : cards)
			if (card->getColor() != cards.first()->getColor()) return false;
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.nullified_list.contains(owner->objectName())) use.nullified_list << owner->objectName();
		ctx.original_data->setValue(use);
		return false;
	}
};

ThQimeiCard::ThQimeiCard()
{
	setSkillName("thqimei");
}

bool ThQimeiCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self;
}

void ThQimeiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->drawCards(2,"thqimei");
	const Card*sc = room->askForExchange(source,"thqimei",2,2,true,"thqimei0");
	if(sc) room->showCard(source,sc->getSubcards());
	foreach (ServerPlayer *p, targets){
		p->drawCards(2,"thqimei");
		const Card*tc = room->askForExchange(p,"thqimei",2,2,true,"thqimei0");
		QList<int>ids;
		if(tc){
			room->showCard(p,tc->getSubcards());
			ids = tc->getSubcards();
		}
		if(sc) ids << sc->getSubcards();
		QStringList suits;
		foreach (int id, ids){
			tc = Sanguosha->getCard(id);
			if(suits.contains(tc->getSuitString())) continue;
			suits.append(tc->getSuitString());
		}
		if(suits.length()==1){
			while(source->isAlive()&&ids.length()>0){
				room->notifyMoveToPile(source,ids,"thqimei");
				tc = room->askForUseCard(source,"@@thqimei","thqimei1");
				room->notifyMoveToPile(source,ids,"thqimei",Player::PlaceHand,false);
				if(tc) ids.removeOne(tc->getEffectiveId());
				else break;
			}
		}else if(suits.length()==2){
			if(source->isChained())
				room->setPlayerChained(source);
			if(!source->faceUp())
				source->turnOver();
			if(p->isChained())
				room->setPlayerChained(p);
			if(!p->faceUp())
				p->turnOver();
		}else if(suits.length()==3){
			room->setPlayerChained(source,true);
			room->setPlayerChained(p,true);
		}else if(suits.length()==4){
			source->drawCards(1,"thqimei");
			p->drawCards(1,"thqimei");
		}
	}
}

class ThQimei : public ViewAsSkillV2
{
public:
    ThQimei() : ViewAsSkillV2("thqimei")
    {
        setPhaseName("Play");
        expand_pile = "#thqimei";
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool isUsable(const SkillContext &ctx) const override
    {
        const auto &ref = ctx.activationRef;
        if (ctx.owner && ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "cards").isValid())
            return true; // Only the exact scoped child may use the displayed cards repeatedly.
        return Skill::isUsable(ctx);
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        const auto &key = request.activationRef.key;
        const QVariant cards = request.initiator->getSkillInstanceStateValue(key.skillName, key.instanceID, "cards");
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return !cards.isValid();
        return request.pattern == "@@thqimei" && cards.isValid();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !candidate || request.pattern != "@@thqimei" || !request.selectedCardIds.isEmpty()) return false;
        const auto &key = request.activationRef.key;
        const QVariantList cards = request.initiator->getSkillInstanceStateValue(key.skillName, key.instanceID, "cards").toList();
        return cards.contains(candidate->getEffectiveId()) && !candidate->isKindOf("SkillCard")
            && candidate->isAvailable(request.initiator);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? request.selectedCardIds.isEmpty()
            : request.pattern == "@@thqimei" && request.selectedCardIds.size() == 1;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return ViewAsSkillV2::createCard(request);
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        if (!material) return nullptr;
        Card *card = Sanguosha->cloneCard(material->objectName(), material->getSuit(), material->getNumber());
        if (!card) return nullptr;
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return "ThQimeiCard";
        const Card *card = request.selectedCardIds.isEmpty() ? nullptr : Sanguosha->getCard(request.selectedCardIds.first());
        return card ? card->getClassName() : QString();
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    void applyPersonalEffect(SkillContext &ctx, ServerPlayer *target, const QString &operation,
                             int multiplier = 1, const QList<int> &cards = {}) const
    {
        SkillContext personal = ctx;
        QVariantList ids;
        for (int id : cards) ids << id;
        personal.extra_data = QVariantMap{{"operation", operation}, {"multiplier", multiplier}, {"cards", ids}};
        skillEffect(personal, target);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap personal = ctx.extra_data.toMap();
        const QString operation = personal.value("operation").toString();
        if (!operation.isEmpty()) {
            if (!target || !target->isAlive()) return ContinueEffects;
            Room *room = target->getRoom();
            if (operation == "draw") target->drawCards(personal.value("multiplier").toInt() * getEffectiveAmount(ctx), objectName());
            else if (operation == "reset") {
                if (target->isChained()) room->setPlayerChained(target);
                if (!target->faceUp()) target->turnOver();
            } else if (operation == "recast") {
                QList<int> ids;
                for (const QVariant &id : personal.value("cards").toList())
                    if (room->getCardOwner(id.toInt()) == target && room->getCardPlace(id.toInt()) == Player::PlaceHand) ids << id.toInt();
                if (!ids.isEmpty()) room->recastCardsWithDraw(target, ids, ids.size() * getEffectiveAmount(ctx), objectName());
            }
            return ContinueEffects;
        }
        ServerPlayer *source = ctx.invoker;
        if (!source || !target) return ContinueEffects;
        Room *room = source->getRoom();
        Room::AcceptedViewAsEffectScope prompt(room, source, objectName(), ctx);
        const auto requestGuard = yjcm2023RequestScope();
        const SkillInstanceRef ref = prompt.activationRef();
        if (!prompt.isValid()) return ContinueEffects;
        source->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "cards", QVariantList());
        const int amount = getEffectiveAmount(ctx);
        applyPersonalEffect(ctx, source, "draw", 2);
        if (target->isAlive()) target->drawCards(2 * amount, objectName());
        if (!source->isAlive() || !target->isAlive()) return ContinueEffects;
        const Card *own = room->askForExchange(source, objectName(), 2, 2, false, "thqimei0");
        const QList<int> ownIds = own ? own->getSubcards() : QList<int>();
        const Card *other = room->askForExchange(target, objectName(), 2, 2, false, "thqimei0");
        const QList<int> otherIds = other ? other->getSubcards() : QList<int>();
        QList<int> ids = ownIds + otherIds;
        QSet<Card::Suit> suits;
        for (int id : ids) suits << Sanguosha->getCard(id)->getSuit();
        // Freeze both choices and suits before either reveal can open a nested effect.
        if (!ownIds.isEmpty()) room->showCard(source, ownIds);
        if (!otherIds.isEmpty()) room->showCard(target, otherIds);
        if (suits.size() == 1) {
            while (source->isAlive() && !ids.isEmpty()) {
                for (int i = ids.size() - 1; i >= 0; --i)
                    if (room->getCardPlace(ids[i]) != Player::PlaceHand
                        || (room->getCardOwner(ids[i]) != source && room->getCardOwner(ids[i]) != target)) ids.removeAt(i);
                if (ids.isEmpty()) break;
                QVariantList allowed;
                for (int id : ids) allowed << id;
                source->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "cards", allowed);
                room->notifyMoveToPile(source, ids, objectName());
                const QList<int> displayedIds = ids;
                const auto displayGuard = qScopeGuard([&]() {
                    room->notifyMoveToPile(source, displayedIds, objectName(), Player::PlaceHand, false);
                });
                const Card *used = room->askForUseCard(source, "@@thqimei", "thqimei1");
                if (!used) break;
                const QList<int> usedIds = used->getSubcards().isEmpty() ? QList<int>{used->getEffectiveId()} : used->getSubcards();
                bool progressed = false;
                for (int id : usedIds) progressed = ids.removeOne(id) || progressed;
                if (!progressed) break;
            }
        } else if (suits.size() == 2) {
            for (ServerPlayer *player : {source, target}) {
                if (!player->isAlive()) continue;
                if (player == source) { applyPersonalEffect(ctx, player, "reset"); continue; }
                if (player->isChained()) room->setPlayerChained(player);
                if (!player->faceUp()) player->turnOver();
            }
        } else if (suits.size() == 3) {
            for (ServerPlayer *player : {source, target}) {
                if (!player->isAlive()) continue;
                QList<int> materials = player == source ? ownIds : otherIds;
                for (int i = materials.size() - 1; i >= 0; --i)
                    if (room->getCardOwner(materials[i]) != player || room->getCardPlace(materials[i]) != Player::PlaceHand)
                        materials.removeAt(i);
                if (materials.isEmpty()) continue;
                if (player == source) { applyPersonalEffect(ctx, player, "recast", 1, materials); continue; }
                DummyCard recast(materials);
                CardMoveReason reason(CardMoveReason::S_REASON_RECAST, player->objectName(), objectName(), "");
                room->moveCardTo(&recast, nullptr, Player::DiscardPile, reason, true);
                player->drawCards(materials.size() * amount, "recast");
            }
        } else if (suits.size() == 4) {
            if (source->isAlive()) applyPersonalEffect(ctx, source, "draw");
            if (target->isAlive()) target->drawCards(amount, objectName());
        }
        return ContinueEffects;
    }
};
class ThZhuiji : public TriggerSkillV2
{
public:
    ThZhuiji() : TriggerSkillV2("thzhuiji")
    {
        events << Death << CardsMoveOneTime;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime) return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || (move.to == player && move.to_place == Player::PlaceEquip)) return true;
        QVariantMap receipts = player->getTag("ThZhuijiEquipment").toMap();
        QList<int> lostAreas;
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const QString id = QString::number(move.card_ids[i]);
            if (move.from_places.value(i) != Player::PlaceEquip || !receipts.contains(id)) continue;
            const int area = receipts.take(id).toMap().value("area").toInt();
            if (!lostAreas.contains(area)) lostAreas << area;
        }
        // Consume receipts before throwing slots; nested moves must never repeat a receipt.
        player->setTag("ThZhuijiEquipment", receipts);
        for (int area : lostAreas)
            if (player->hasEquipArea(area)) player->throwEquipArea(area);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == Death && player && data.value<DeathStruct>().who == player && player->hasSkill(this)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "thzhuiji0", true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !target) return false;
        ctx.owner->peiyin(this);
        for (int area = 0; area < 5 && target->isAlive(); ++area) {
            if (!target->hasEquipArea(area) || target->getEquip(area)) continue;
            QList<int> ids = room->getDrawPile() + room->getDiscardPile();
            qsanShuffle(ids);
            for (int id : ids) {
                const Card *card = Sanguosha->getCard(id);
                const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
                if (!equip || equip->location() != area || !card->isAvailable(target)) continue;
                QVariantMap receipts = target->getTag("ThZhuijiEquipment").toMap();
                receipts[QString::number(id)] = QVariantMap{{"area", area}, {"owner", ctx.sourceRef.ownerObjectName},
                    {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}};
                target->setTag("ThZhuijiEquipment", receipts);
                room->useCardFromSkillEffect(CardUseStruct(card, target), ctx);
                if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip) {
                    receipts = target->getTag("ThZhuijiEquipment").toMap();
                    receipts.remove(QString::number(id));
                    target->setTag("ThZhuijiEquipment", receipts);
                }
                break;
            }
        }
        return false;
    }
};
FazhuCard::FazhuCard()
{
	setSkillName("fazhu");
	will_throw = false;
}

bool FazhuCard::targetFixed() const
{
	return user_string=="@@fazhu";
}

bool FazhuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.length()<subcardsLength();
}

void FazhuCard::onUse(Room*room,CardUseStruct&use) const
{
	if(user_string=="@@fazhu")
		SkillCard::onUse(room,use);
	else{
		for(int i = 0; i < subcardsLength(); i++){
			room->giveCard(use.from,use.to[i],Sanguosha->getCard(subcards[i]),"fazhu");
		}
		foreach(ServerPlayer*tp,use.to){
			if(tp->isAlive())
				room->askForUseSlashTo(tp,room->getOtherPlayers(tp),"fazhu2",false);
		}
	}
}

void FazhuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	LogMessage log;
	log.type = "$RecastCard";
	log.from = source;
	log.card_str = ListI2S(subcards).join("+");
	room->sendLog(log);

	CardMoveReason reason(CardMoveReason::S_REASON_RECAST,source->objectName(),"fazhu","");
	room->moveCardTo(this,nullptr,Player::DiscardPile,reason,true);
	foreach(int id,source->drawCardsList(subcardsLength(),"recast")){
		room->setCardTip(id,"fazhu-Clear");
	}
	room->notifyMoveToPile(source, QList<int>(), "fazhu", Player::PlaceUnknown, true);
	room->askForUseCard(source,"@@fazhu1","fazhu1");
}

class Fazhuvs : public ViewAsSkillV2
{
public:
    Fazhuvs() : ViewAsSkillV2("fazhu") { expand_pile = "#fazhu"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return false;
        const auto &key = request.activationRef.key;
        const QString stage = request.initiator->getSkillInstanceStateValue(key.skillName, key.instanceID, "stage").toString();
        return (request.pattern == "@@fazhu" && stage == "recast")
            || (request.pattern == "@@fazhu1" && stage == "give");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || request.selectedCardIds.contains(card->getEffectiveId())) return false;
        const int id = card->getEffectiveId();
        const auto &key = request.activationRef.key;
        if (request.pattern == "@@fazhu1")
            return request.initiator->handCards().contains(id)
                && request.initiator->getSkillInstanceStateValue(key.skillName, key.instanceID, "cards").toList().contains(id);
        return request.pattern == "@@fazhu" && !card->isDamageCard()
            && (request.initiator->handCards().contains(id) || request.initiator->getEquipsId().contains(id)
                || request.initiator->getJudgingAreaID().contains(id));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return !request.selectedCardIds.isEmpty(); }
    bool willThrowSelectedCards() const override { return false; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.pattern == "@@fazhu1" && candidate && candidate->isAlive()
            && !selected.contains(candidate) && selected.size() < request.selectedCardIds.size();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return request.pattern == "@@fazhu" ? selected.isEmpty()
            : request.pattern == "@@fazhu1" && selected.size() == request.selectedCardIds.size();
    }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    QString historyKey(const ActiveSkillRequest &) const override { return "FazhuCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (card) card->setTag("FazhuPattern", request.pattern);
        return card;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ctx.choice = request.pattern;
        ctx.extra_data = request.selectedTargetNames;
        return true;
    }
    bool pay(Room *room, SkillContext &, const ActiveSkillRequest &request) const override
    {
        if (request.pattern != "@@fazhu") return true;
        if (!request.initiator || request.selectedCardIds.isEmpty()) return false;
        ServerPlayer *payer = room->findPlayerByObjectName(request.initiator->objectName());
        if (!payer) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (room->getCardOwner(id) != payer || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        DummyCard cards(request.selectedCardIds);
        LogMessage log;
        log.type = "$RecastCard";
        log.from = payer;
        log.card_str = ListI2S(request.selectedCardIds).join("+");
        room->sendLog(log);
        CardMoveReason reason(CardMoveReason::S_REASON_RECAST, payer->objectName(), objectName(), "");
        room->moveCardTo(&cards, nullptr, Player::DiscardPile, reason, true);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.choice.isEmpty() && ctx.use_card) ctx.choice = ctx.use_card->getTag("FazhuPattern").toString();
        if (ctx.choice != "@@fazhu") return ContinueEffects;
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (ctx.choice == "@@fazhu") {
            if (!target || !ctx.use_card) return FinishSkill;
            Room *room = target->getRoom();
            Room::AcceptedViewAsEffectScope prompt(room, target, objectName(), ctx);
            const auto requestGuard = yjcm2023RequestScope();
            const SkillInstanceRef ref = prompt.activationRef();
            if (!prompt.isValid()) return FinishSkill;
            target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "stage", "give");
            target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "cards", QVariantList());
            const QList<int> drawn = target->drawCardsList(ctx.use_card->subcardsLength() * getEffectiveAmount(ctx), "recast");
            QVariantList cards;
            for (int id : drawn) {
                cards << id;
                room->setCardTip(id, "fazhu-Clear"); // Presentation only; the scoped state authorizes gifting.
            }
            target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "stage", "give");
            target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "cards", cards);
            room->askForUseCard(target, "@@fazhu1", "fazhu1");
            return FinishSkill;
        }

        if (!ctx.invoker || !ctx.use_card || !target) return ContinueEffects;
        const int index = ctx.extra_data.toMap().value("index", -1).toInt();
        const QList<int> ids = ctx.use_card->getSubcards();
        if (index < 0 || index >= ids.size()) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        if (room->getCardOwner(ids[index]) != ctx.initiator || room->getCardPlace(ids[index]) != Player::PlaceHand
            || Sanguosha->getCard(ids[index])->hasFlag("using"))
            return ContinueEffects;
        const qint64 cause = room->currentHistoryEventId();
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        room->giveCard(ctx.initiator, target, Sanguosha->getCard(ids[index]), objectName());
        if (!before.contains("watermark") || !received(room, cause, before.value("watermark").toLongLong(),
            ids[index], ctx.initiator->objectName(), target->objectName())) return ContinueEffects;
        QVariantMap progress = ctx.extra_data.toMap();
        QStringList accepted = progress.value("accepted").toStringList();
        accepted << target->objectName();
        progress["accepted"] = accepted;
        ctx.extra_data = progress;
        return ContinueEffects;
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        QStringList accepted;
        // Complete every accepted gift before any Slash opens a nested resolution.
        for (int i = 0; i < targets.size(); ++i) {
            SkillContext gift = ctx;
            gift.extra_data = QVariantMap{{"index", i}};
            skillEffect(gift, targets[i]);
            accepted << gift.extra_data.toMap().value("accepted").toStringList();
        }
        if (!ctx.invoker) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        const auto requestGuard = yjcm2023RequestScope();
        for (const QString &name : accepted) {
            ServerPlayer *target = room->findPlayerByObjectName(name);
            if (target && target->isAlive())
                room->askForUseSlashTo(target, room->getOtherPlayers(target), "fazhu2", false);
        }
        return ContinueEffects;
    }
private:
    static bool received(Room *room, qint64 cause, qint64 before, int id, const QString &from, const QString &to)
    {
        if (cause <= 0 || !room->historyRecordingEnabled()) return false;
        QVariantMap filter{{"from", from}, {"after", before}};
        bool found = false;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
                if (move.value("card_id", -1).toInt() == id && move.value("to").toString() == to
                    && move.value("to_place").toInt() == int(Player::PlaceHand)
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause) found = true;
            }
            if (!page.value("has_more").toBool()) return found;
            filter.insert("after", page.value("next_after"));
            filter.insert("watermark", page.value("watermark"));
        }
    }
};

class Fazhu : public TriggerSkillV2
{
public:
    Fazhu() : TriggerSkillV2("fazhu")
    {
        events << EventPhaseStart;
        view_as_skill = new Fazhuvs;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Start
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        Room::BorrowedSkillScope prompt(room, ctx.owner, objectName(), ctx.activationRef);
        const auto requestGuard = yjcm2023RequestScope();
        const SkillInstanceRef ref = prompt.activationRef();
        if (!ref.isValid()) return false;
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "stage", "recast");
        const QList<int> judging = ctx.owner->getJudgingAreaID();
        room->notifyMoveToPile(ctx.owner, judging, objectName(), Player::PlaceUnknown, true);
        const auto displayGuard = qScopeGuard([&]() {
            room->notifyMoveToPile(ctx.owner, judging, objectName(), Player::PlaceUnknown, false);
        });
        room->askForUseCard(ctx.owner, "@@fazhu", "fazhu0");
        return false;
    }
};
Wangmeizhike::Wangmeizhike(Suit suit, int number)
	: SingleTargetTrick(suit, number)
{
	setObjectName("wangmeizhike");
}

bool Wangmeizhike::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < 1+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this);
}

void Wangmeizhike::onEffect(CardEffectStruct &effect) const
{
	foreach (const Card*h, effect.to->getHandcards()) {
		if(h->getSuit()==1){
			Card*dc = Sanguosha->cloneCard("peach",h->getSuit(),h->getNumber());
			dc->setSkillName("wangmeizhike");
			WrappedCard *wc = Sanguosha->getWrappedCard(h->getId());
			wc->takeOver(dc);
			effect.to->getRoom()->notifyUpdateCard(effect.to, h->getId(), wc);
		}
	}
}

YJCM2023Package::YJCM2023Package()
	: Package("yjcm2023")
{
	General *th_peixiu = new General(this, "th_peixiu", "qun", 3);
	th_peixiu->addSkill(new Zhitu);
	th_peixiu->addSkill(new Fujue);
	addMetaObject<FujueCard>();

	General *th_simafu = new General(this, "th_simafu", "wei", 3);
	th_simafu->addSkill(new Beiyu);
	th_simafu->addSkill(new Duchi);
	addMetaObject<BeiyuCard>();

	General *th_xuangongzhu = new General(this, "th_xuangongzhu", "wei", 3,false);
	th_xuangongzhu->addSkill(new ThQimei);
	th_xuangongzhu->addSkill(new ThZhuiji);
	addMetaObject<ThQimeiCard>();

	General *th_majun = new General(this, "th_majun", "wei", 3);
	th_majun->addSkill(new Gongqiao);
	th_majun->addSkill(new GongqiaoMax);
	related_skills.insert("gongqiao", "#gongqiao-max");
	th_majun->addSkill(new Jingqiao);
	addMetaObject<GongqiaoCard>();

	General*xukun = new General(this,"xukun","wu",4);
	xukun->addSkill(new Fazhu);
	addMetaObject<FazhuCard>();




	/*QList<Card *> cards;
	cards << new Wangmeizhike(Card::NoSuit, 0);
	foreach(Card *card, cards)
		card->setParent(this);*/
}
ADD_PACKAGE(YJCM2023)
