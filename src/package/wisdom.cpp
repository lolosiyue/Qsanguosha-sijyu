#include "wisdom.h"
//#include "skill.h"
//#include "client.h"
#include "engine.h"
#include "settings.h"
#include "room.h"
#include "skill-instance-utils.h"
#include <climits>
#include "maneuvering.h"
//#include "general.h"
#include "roomthread.h"
#include <memory>
#include <QScopeGuard>

JuaoCard::JuaoCard()
{
    setSkillName("juao");
	will_throw = false;
	handling_method = MethodNone;
}

bool JuaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty() && to_select->getMark("juao") == 0;
}

void JuaoCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->addToPile("hautain", this, false);
	effect.to->addMark("juao");
}

class JuaoViewAsSkill : public ViewAsSkillV2
{
public:
    JuaoViewAsSkill() : ViewAsSkillV2("juao", 2) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getHandcardNum() >= 2; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return card && request.initiator && request.selectedCardIds.size() < 2 && !request.selectedCardIds.contains(card->getEffectiveId()) && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId()); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2) return false;
        ActiveSkillRequest checked = request; checked.selectedCardIds.clear();
        for (int id : request.selectedCardIds) { if (!canSelectCard(checked, Sanguosha->getCard(id))) return false; checked.selectedCardIds << id; }
        return true;
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return selected.isEmpty() && target && target->isAlive() && target->getMark("juao") == 0; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JuaoCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (!ctx.use_card || ctx.use_card->subcardsLength() != 2 || !target->getTag("JuaoReceipts").toList().isEmpty()) return ContinueEffects;
        const QList<int> ids = ctx.use_card->getSubcards();
        for (int id : ids) if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        const int serial = room->getTag("JuaoReceiptSerial").toInt() + 1; room->setTag("JuaoReceiptSerial", serial);
        const QVariantMap receipt{{"serial", serial}, {"issuer", ctx.invoker->objectName()}, {"recipient", target->objectName()},
            {"selected", ListI2V(ids)}, {"cards", QVariantList()}, {"committed", false},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        // Register before movement callbacks; only committed arrivals can create the later obligation.
        QVariantList receipts = target->getTag("JuaoReceipts").toList(); receipts << receipt; target->setTag("JuaoReceipts", receipts);
        const auto settle = [&] {
            QVariantList kept;
            for (const QVariant &entry : target->getTag("JuaoReceipts").toList()) {
                QVariantMap saved = entry.toMap();
                if (saved.value("serial").toInt() != serial) kept << entry;
                else if (saved.value("committed").toBool()) { saved.remove("selected"); kept << saved; }
            }
            target->setTag("JuaoReceipts", kept);
        };
        const auto settleGuard = qScopeGuard(settle);
        target->addToPile("hautain", ids, false);
        settle();
        room->setPlayerMark(target, "juao", target->getTag("JuaoReceipts").toList().isEmpty() ? 0 : 1);
        return ContinueEffects;
    }
};

class Juao : public TriggerSkillV2
{
public:
    Juao() : TriggerSkillV2("juao") { events << EventPhaseStart << CardsMoveOneTime << EventSkillEffectFinished; view_as_skill = new JuaoViewAsSkill; global = true; frequency = Compulsory; }
    static void consume(Room *room, const SkillContext &ctx)
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        if (!holder) return;
        QVariantList receipts = holder->getTag("JuaoReceipts").toList();
        if (!receipts.removeOne(ctx.extra_data)) return;
        holder->setTag("JuaoReceipts", receipts); room->setPlayerMark(holder, "juao", receipts.isEmpty() ? 0 : 1);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && !ctx.activationRef.isValid()) consume(room, ctx);
        } else if (event == CardsMoveOneTime && player) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to != player || move.to_place != Player::PlaceSpecial || move.to_pile_name != "hautain") return true;
            QVariantList receipts;
            for (const QVariant &entry : player->getTag("JuaoReceipts").toList()) {
                QVariantMap receipt = entry.toMap();
                QVariantList arrived = receipt.value("cards").toList();
                for (int id : ListV2I(receipt.value("selected").toList()))
                    if (!arrived.contains(id) && move.card_ids.contains(id) && player->getPile("hautain").contains(id)) arrived << id;
                receipt["cards"] = arrived; receipt["committed"] = !arrived.isEmpty();
                receipts << receipt;
            }
            player->setTag("JuaoReceipts", receipts);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->isDead() || player->getPhase() != Player::Start) return true;
        for (const QVariant &entry : player->getTag("JuaoReceipts").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (!receipt.value("committed").toBool()) continue;
            ServerPlayer *issuer = room->findPlayerByObjectName(receipt.value("issuer").toString(), true);
            if (!issuer) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.owner = ctx.initiator = issuer; ctx.invoker = player; ctx.targets = {player}; ctx.original_data = &data; ctx.current_event = event;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            ctx.extra_data = receipt;
            if (ctx.instanceID > 0 && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        return !ctx.activationRef.isValid() && holder && holder->isAlive() && holder->getTag("JuaoReceipts").toList().contains(ctx.extra_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The accepted obligation expires at this opportunity, including a cancelled recipient effect.
        consume(room, ctx); return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        LogMessage log; log.type = "#JuaoObtain"; log.from = target; log.arg = objectName(); room->sendLog(log);
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        QList<int> ids;
        if (holder) for (int id : ListV2I(ctx.extra_data.toMap().value("cards").toList()))
            if (holder->getPile("hautain").contains(id)) ids << id;
        if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); }
        if (target->isAlive()) target->skip(Player::Draw);
        return false;
    }
};
class Tanlan : public TriggerSkillV2
{
public:
    Tanlan() : TriggerSkillV2("tanlan") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *from = data.value<DamageStruct>().from;
        return player && player->isAlive() && player->hasSkill(objectName()) && from && player->canPindian(from)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        if (!from || !ctx.owner->canPindian(from) || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data))
            return false;
        ctx.targets = {from};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker->canPindian(target)) return false;
        room->broadcastSkillInvoke(objectName(), 1);
        room->notifySkillInvoked(ctx.invoker, objectName());
        std::unique_ptr<PindianStruct> selection(ctx.invoker->pindianSelect(target, objectName()));
        PindianStruct *pindian = ctx.invoker->finishPindian(selection.get());
        if (!pindian || !pindian->from_card || !pindian->to_card || pindian->from_number <= pindian->to_number
            || !pindian->from || pindian->from->isDead())
            return false;
        // Keep the existing post-pindian source requirement exact to this activation.
        if (!isSourceAvailable(room, ctx)) return false;
        DummyCard cards;
        const int fromId = pindian->from_card->getEffectiveId();
        const int toId = pindian->to_card->getEffectiveId();
        if (room->getCardPlace(fromId) == Player::DiscardPile) cards.addSubcard(fromId);
        if (toId != fromId && room->getCardPlace(toId) == Player::DiscardPile) cards.addSubcard(toId);
        if (!cards.getSubcards().isEmpty()) pindian->from->obtainCard(&cards);
        return false;
    }
};

class Shicai : public TriggerSkillV2
{
public:
    Shicai() : TriggerSkillV2("shicai") { events << Pindian; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const PindianStruct *pindian = data.value<PindianStruct *>();
        if (!pindian || player != pindian->from) return {};
        // Preserve the existing tie handling while selecting the actual skill-owning participant.
        ServerPlayer *winner = pindian->from_number > pindian->to_number ? pindian->from : pindian->to;
        return winner && winner->isAlive() && winner->hasSkill(objectName())
            ? TriggerList{{winner, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        room->sendCompulsoryTriggerLog(ctx.owner, this, pindian && pindian->reason == "tanlan" ? 2 : 1);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class Yicai : public TriggerSkillV2
{
public:
    Yicai() : TriggerSkillV2("yicai") { events << CardUsed; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.from == player
            && use.card && use.card->isNDTrick() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        const bool previous = target->hasFlag("yicairesponding");
        target->setFlags("yicairesponding");
        const auto restore = qScopeGuard([target, previous] { if (!previous) target->setFlags("-yicairesponding"); });
        // Slash selection and payment stay in the ordinary card-use pipeline.
        if (room->askForUseCard(target, "slash", "@askforslash")) {
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(target, objectName());
        }
        return false;
    }
};
class Beifa : public TriggerSkillV2
{
public:
    Beifa() : TriggerSkillV2("beifa") { events << CardsMoveOneTime; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && move.from == player
            && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        Slash slash(Card::NoSuit, 0);
        slash.setSkillName(objectName());
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner))
            if (ctx.owner->canSlash(target, &slash, false)) candidates << target;
        ServerPlayer *target = candidates.isEmpty() ? nullptr : room->askForPlayerChosen(ctx.owner, candidates, objectName());
        if (!target && !ctx.owner->isProhibited(ctx.owner, &slash)) target = ctx.owner;
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->isAlive()) return false;
        auto slash = std::make_unique<Slash>(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        if (target == ctx.owner ? ctx.owner->isProhibited(target, slash.get())
                               : !ctx.owner->canSlash(target, slash.get(), false)) return false;
        CardUseStruct use(slash.get(), ctx.owner, target);
        // Resolution owns the virtual Slash through nested use-card callbacks.
        use.setOwnedCard(slash.release());
        room->useCardFromSkillEffect(use, ctx, true);
        return false;
    }
};
HouyuanCard::HouyuanCard()
{
	setSkillName("houyuan");
}

void HouyuanCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->drawCards(2);
}

class Houyuan : public ViewAsSkillV2
{
public:
	Houyuan() : ViewAsSkillV2("houyuan", 2)
	{ setPhaseName("Play");
		m_baseAmount = 2;
	}

	LimitScope getLimitScope() const override { return Limit_Phase; }
	QString historyKey(const ActiveSkillRequest &) const override { return "HouyuanCard"; }
	TargetMode targetMode() const override { return SelectTargets; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.size() < 2
		    && !request.selectedCardIds.contains(card->getEffectiveId()) && !card->hasFlag("using")
		    && request.initiator->handCards().contains(card->getEffectiveId()) && !request.initiator->isJilei(card);
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 2) return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		for (int id : request.selectedCardIds) {
			if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
			selection.selectedCardIds << id;
		}
		return true;
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
	                     const Player *candidate) const override
	{
		return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty();
	}

	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
	{
		return selected.size() == 1 && canSelectTarget(request, {}, selected.first());
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		// The V2 proxy pays both hand cards atomically before exposing target effects.
		target->drawCards(getEffectiveAmount(ctx), objectName());
		return ContinueEffects;
	}
};

class Chouliang : public TriggerSkillV2
{
public:
    Chouliang() : TriggerSkillV2("chouliang")
    { events << EventPhaseStart; frequency = Frequent; m_baseAmount = 4; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish && player->getHandcardNum() < 3
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = qMax(0, getEffectiveAmount(ctx) - target->getHandcardNum());
        if (count == 0) return false;
        const QList<int> ids = room->getNCards(count, false);
        CardsMoveStruct move;
        move.card_ids = ids;
        move.to_place = Player::PlaceTable;
        move.reason = CardMoveReason(CardMoveReason::S_REASON_TURNOVER, ctx.owner->objectName(), objectName(), "");
        room->moveCardsAtomic(move, true);
        room->getThread()->delay(2 * Config.AIDelay);
        QList<int> basic;
        for (int id : ids)
            if (room->getCardPlace(id) == Player::PlaceTable && Sanguosha->getCard(id)->isKindOf("BasicCard")) basic << id;
        if (target->isAlive() && !basic.isEmpty()) {
            room->broadcastSkillInvoke(objectName());
            DummyCard obtained(basic);
            room->obtainCard(target, &obtained, CardMoveReason(CardMoveReason::S_REASON_GOTBACK, target->objectName()));
        }
        // Nested moves may already have consumed revealed cards; discard only the remaining table material.
        QList<int> remaining;
        for (int id : ids)
            if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
        if (!remaining.isEmpty()) {
            DummyCard discarded(remaining);
            room->throwCard(&discarded, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                ctx.owner->objectName(), objectName(), ""), nullptr);
        }
        return false;
    }
};
BawangCard::BawangCard()
{
    setSkillName("bawang");
	//mute = true ;
}

bool BawangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (targets.length() >= 2)
		return false;
	return Self->canSlash(to_select, false);
}

void BawangCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();

	CardUseStruct use;
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("bawang");
	use.card = slash;
	use.from = effect.from;
	use.to << effect.to;
	slash->deleteLater();
	room->useCard(use, false);
}

class BawangViewAsSkill : public ViewAsSkillV2
{
public:
    BawangViewAsSkill() : ViewAsSkillV2("bawang") { response_pattern = "@@bawang"; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.pattern == "@@bawang"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && target && target->isAlive() && !selected.contains(target) && selected.size() < 2 && request.initiator->canSlash(target, false); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return !selected.isEmpty() && selected.size() <= 2; }
    QString historyKey(const ActiveSkillRequest &) const override { return "BawangCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.invoker->isAlive() && target->isAlive(); ++i) {
            Slash *slash = new Slash(Card::NoSuit, 0); slash->setSkillName("bawang");
            CardUseStruct use; use.setOwnedCard(slash);
            use.from = ctx.invoker; use.to = {target};
            if (ctx.invoker->canSlash(target, use.card, false)) room->useCardFromSkillEffect(use, ctx, false);
        }
        return ContinueEffects;
    }
};

class Bawang : public TriggerSkillV2
{
public:
    Bawang() : TriggerSkillV2("bawang") { view_as_skill = new BawangViewAsSkill; events << CardOffset; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct card = data.value<CardEffectStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && card.from == player && card.card
            && card.card->isKindOf("Slash") && card.to && player->canPindian(card.to) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<CardEffectStruct>().to;
        if (!target || !ctx.owner->canPindian(target) || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {target}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->canPindian(target)) return false;
        room->broadcastSkillInvoke(objectName(), 1);
        if (ctx.owner->pindian(target, objectName(), nullptr)) {
            if (ctx.owner->isDead()) return false;
            room->setPlayerFlag(ctx.owner, "-drank");
            // A won comparison grants this accepted continuation, even if its original skill source retired.
            Room::AcceptedViewAsEffectScope response(room, ctx.owner, objectName(), ctx);
            if (response.isValid()) room->askForUseCard(ctx.owner, "@@bawang", "@bawang");
        } else room->broadcastSkillInvoke(objectName(), 3);
        return false;
    }
    int getEffectIndex(const ServerPlayer *, const Card *card) const override { return card && !card->isKindOf("Slash") ? 2 : 0; }
};
WeidaiCard::WeidaiCard()
{
    setSkillName("weidai");
	target_fixed = true;
	mute = true;
}

const Card *WeidaiCard::validate(CardUseStruct &card_use) const
{
	card_use.m_isOwnerUse = false;
	ServerPlayer *sunce = card_use.from;
	Room *room = sunce->getRoom();
	if (!sunce->isLord() && sunce->hasSkill("weidi"))
		room->broadcastSkillInvoke("weidi");
	else
		room->broadcastSkillInvoke("weidai", 1);
	room->notifySkillInvoked(sunce, "weidai");

	foreach (ServerPlayer *liege, room->getLieges("wu", sunce)) {
		QString prompt = "@weidai-analeptic:"+sunce->objectName();
		const Card *card = room->askForCard(liege,".|spade|2~9|hand",prompt,QVariant::fromValue(sunce),Card::MethodResponse,sunce,false,"",true);
		if (card) {
			Analeptic *ana = new Analeptic(card->getSuit(), card->getNumber());
			ana->setSkillName("weidai");
			ana->addSubcard(card);
			ana->deleteLater();
			return ana;
		}
	}
	room->setPlayerFlag(sunce, "Global_WeidaiFailed");
	return nullptr;
}

const Card *WeidaiCard::validateInResponse(ServerPlayer *user) const
{
	Room *room = user->getRoom();
	if (!user->isLord() && user->hasSkill("weidi"))
		room->broadcastSkillInvoke("weidi");
	else
		room->broadcastSkillInvoke("weidai", 2);
	room->notifySkillInvoked(user, "weidai");

	foreach (ServerPlayer *liege, room->getLieges("wu", user)) {
		QString prompt = "@weidai-analeptic:"+user->objectName();
		const Card *card = room->askForCard(liege,".|spade|2~9|hand",prompt,QVariant::fromValue(user),Card::MethodResponse,user,false,"",true);
		if (card) {
			Analeptic *ana = new Analeptic(card->getSuit(), card->getNumber());
			ana->setSkillName("weidai");
			ana->addSubcard(card);
			ana->deleteLater();
			return ana;
		}
	}
	room->setPlayerFlag(user, "Global_WeidaiFailed");
	return nullptr;
}

class Weidai : public ViewAsSkillV2
{
public:
    Weidai() : ViewAsSkillV2("weidai$") { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.initiator->hasLordSkill(objectName())
            || request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "failed", false).toBool()) return false;
        bool liege = false; for (const Player *p : request.initiator->getSiblings()) if (p->isAlive() && p->getKingdom() == "wu") liege = true;
        return liege && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? Analeptic::IsAvailable(request.initiator) : request.pattern == "peach+analeptic");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !request.selectedCardIds.isEmpty()) return nullptr;
        Analeptic *card = new Analeptic(Card::NoSuit, 0); card->setSkillName(objectName()); return card;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        for (ServerPlayer *liege : room->getLieges("wu", ctx.invoker)) {
            SkillContext ask = ctx; ask.targets = {liege}; ask.updated_card = nullptr; skillEffect(ask, liege);
            if (ask.updated_card) { ctx.updated_card = ask.updated_card; return ContinueEffects; }
            if (ctx.invoker->isDead()) return FinishSkill;
        }
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "failed", true);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const Card *material = room->askForCard(target, ".|spade|2~9|hand", "@weidai-analeptic:" + ctx.invoker->objectName(),
            QVariant::fromValue(ctx.invoker), Card::MethodResponse, ctx.invoker, false, "", true);
        if (!material) return ContinueEffects;
        const QList<int> ids = material->isVirtualCard() ? material->getSubcards() : QList<int>{material->getEffectiveId()};
        if (ids.size() != 1 || ids.first() < 0 || room->getCardOwner(ids.first()) || room->getCardPlace(ids.first()) != Player::PlaceTable) return ContinueEffects;
        QVariantList receipts = ctx.initiator->getTag("WeidaiPayments").toList();
        receipts << QVariantMap{{"execution", ctx.executionID}, {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_id", ctx.activationRef.key.instanceID}, {"id", ids.first()}};
        ctx.initiator->setTag("WeidaiPayments", receipts);
        // The liege's completed provision response paid the material; this accepted conversion owns it until Finished.
        Analeptic *card = new Analeptic(material->getSuit(), material->getNumber()); card->addSubcards(ids); card->setSkillName(objectName()); card->deleteLater();
        ctx.updated_card = card; return ContinueEffects;
    }
};
class WeidaiCleanup : public TriggerSkillV2
{
public:
    WeidaiCleanup() : TriggerSkillV2("#weidai-cleanup") { events << EventPhaseChanging << EventSkillEffectFinished; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::NotActive)
                for (ServerPlayer *owner : room->getAllPlayers(true)) for (int id : owner->getSkillInstanceIds("weidai")) owner->setSkillInstanceStateValue("weidai", id, "failed", false);
            return true;
        }
        const SkillContext finished = data.value<SkillContext>(); ServerPlayer *payer = finished.initiator;
        if (!payer || finished.executionID <= 0 || finished.activationRef.key.skillName != "weidai") return true;
        const QVariantMap completion = finished.interceptor_data.value("native_response_completion");
        const CardResponseStruct response = finished.original_data ? finished.original_data->value<CardResponseStruct>() : CardResponseStruct();
        const bool provision = completion.value("completed").toBool() && completion.value("is_provision").toBool() && !completion.value("nullified").toBool()
            && response.m_card && !response.nullified && response.skillExecutionID == finished.executionID && response.activationRef == finished.activationRef;
        QVariantList keep; QList<int> remaining;
        for (const QVariant &entry : payer->getTag("WeidaiPayments").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("execution").toLongLong() != finished.executionID || receipt.value("activation_owner").toString() != finished.activationRef.ownerObjectName || receipt.value("activation_id").toInt() != finished.activationRef.key.instanceID) { keep << entry; continue; }
            const int id = receipt.value("id", -1).toInt();
            const bool handedOff = provision && (response.m_card->isVirtualCard() ? response.m_card->getSubcards().contains(id) : response.m_card->getEffectiveId() == id);
            if (!handedOff && id >= 0 && !room->getCardOwner(id) && room->getCardPlace(id) == Player::PlaceTable) remaining << id;
        }
        payer->setTag("WeidaiPayments", keep);
        if (!remaining.isEmpty()) { DummyCard cards(remaining); room->throwCard(&cards, nullptr); }
        return true;
    }
};
class Longluo : public TriggerSkillV2
{
public:
    Longluo() : TriggerSkillV2("longluo") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            && discardedCards(room, player) > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = discardedCards(room, ctx.owner);
        if (count <= 0) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@longluo", true, true);
        if (!target) return false;
        ctx.targets = {target}; ctx.extra_data = count;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
        return false;
    }
private:
    static int discardedCards(Room *room, ServerPlayer *player)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return -1;
        QVariantMap query{{"turn_id", turn}, {"from", player->objectName()}, {"limit", 64}};
        int count = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap entry = value.toMap(), move = entry.value("data").toMap();
                if (!move.contains("reason")) return -1;
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) continue;
                if (!entry.contains("phase_id")) return -1;
                const qint64 phaseId = entry.value("phase_id").toLongLong();
                if (phaseId <= 0) continue;
                const QVariantMap phase = room->historyEvent(phaseId).value("data").toMap();
                // Scope identity proves these were discards during this player's discard phase, including extra phases.
                if (!phase.contains("phase") || !phase.contains("player")) return -1;
                if (phase.value("player").toString() == player->objectName() && phase.value("phase").toInt() == Player::Discard) ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            query.insert("after", page.value("next_after"));
        }
    }
};
FuzuoCard::FuzuoCard()
{
    setSkillName("fuzuo");
}

bool FuzuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty() && to_select->hasFlag("fuzuo_target");
}

void FuzuoCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->getRoom()->setPlayerMark(effect.to, "fuzuo", this->getNumber());
}

class FuzuoViewAsSkill : public ViewAsSkillV2
{
public:
    FuzuoViewAsSkill() : ViewAsSkillV2("fuzuo", 1) { response_pattern = "@@fuzuo"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.pattern == "@@fuzuo" && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "pindian").toMap().value("id").toLongLong() > 0; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && request.selectedCardIds.isEmpty() && card && card->getNumber() >= 1 && card->getNumber() <= 7 && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId()) && !request.initiator->isJilei(card); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { ActiveSkillRequest empty = request; empty.selectedCardIds.clear(); return request.selectedCardIds.size() == 1 && canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first())); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        const Card *card = ViewAsSkillV2::createCard(request);
        if (card) { card->setTag("FuzuoNumber", Sanguosha->getCard(request.selectedCardIds.first())->getNumber()); card->setTag("FuzuoPindian", request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "pindian")); }
        return card;
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && target && target->isAlive() && selected.isEmpty() && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "pindian").toMap().value("participants").toStringList().contains(target->objectName()); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "FuzuoCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap pindian = ctx.use_card->getTag("FuzuoPindian").toMap();
        if (pindian.value("id").toLongLong() <= 0 || !pindian.value("participants").toStringList().contains(target->objectName())) return ContinueEffects;
        QVariantList receipts = ctx.initiator->getTag("FuzuoReceipts").toList();
        receipts << QVariantMap{{"pindian", pindian.value("id")}, {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"target", target->objectName()}, {"amount", ctx.use_card->getTag("FuzuoNumber").toInt() / 2 * getEffectiveAmount(ctx)},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID}};
        ctx.initiator->setTag("FuzuoReceipts", receipts); return ContinueEffects;
    }
};

class Fuzuo : public TriggerSkillV2
{
public:
    Fuzuo() : TriggerSkillV2("fuzuo") { events << PindianVerifying; view_as_skill = new FuzuoViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const PindianStruct *pindian = data.value<PindianStruct *>(); TriggerList result;
        if (!pindian || player != pindian->from || !pindian->to) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) if (owner != pindian->from && owner != pindian->to && owner->canDiscard(owner, "h")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.manual_effect = true; return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        const QVariant id = room->historyParent(room->currentHistoryEventId(), "pindian", true).value("id");
        if (!pindian || !pindian->from || !pindian->to || id.toLongLong() <= 0) return false;
        Room::AcceptedViewAsEffectScope selection(room, ctx.owner, objectName(), ctx);
        if (!selection.isValid()) return false;
        const SkillInstanceRef ref = selection.activationRef();
        const QVariant previous = room->getTag("FuzuoPindianData"); room->setTag("FuzuoPindianData", *ctx.original_data);
        const auto isOurs = [&](const QVariantMap &receipt) { return receipt.value("pindian") == id && receipt.value("activation_owner").toString() == ref.ownerObjectName && receipt.value("activation_id").toInt() == ref.key.instanceID; };
        const auto restore = qScopeGuard([&] {
            room->setTag("FuzuoPindianData", previous);
            QVariantList keep; for (const QVariant &entry : ctx.owner->getTag("FuzuoReceipts").toList()) if (!isOurs(entry.toMap())) keep << entry;
            ctx.owner->setTag("FuzuoReceipts", keep);
        });
        ctx.owner->setSkillInstanceStateValue(objectName(), ref.key.instanceID, "pindian", QVariantMap{{"id", id}, {"participants", QStringList{pindian->from->objectName(), pindian->to->objectName()}}});
        room->askForUseCard(ctx.owner, "@@fuzuo", "@fuzuo-pindian", -1, Card::MethodDiscard);
        // The response committed a recipient hook; apply only its exact, still-synchronous comparison receipt.
        for (const QVariant &entry : ctx.owner->getTag("FuzuoReceipts").toList()) {
            const QVariantMap receipt = entry.toMap(); if (!isOurs(receipt)) continue;
            const bool from = receipt.value("target").toString() == pindian->from->objectName();
            if (!from && receipt.value("target").toString() != pindian->to->objectName()) continue;
            int &number = from ? pindian->from_number : pindian->to_number; number += receipt.value("amount").toInt();
            LogMessage log; log.type = "$Fuzuo"; log.from = from ? pindian->from : pindian->to; log.arg = QString::number(number); room->sendLog(log);
        }
        return false;
    }
};
class Jincui : public TriggerSkillV2
{
public:
    Jincui() : TriggerSkillV2("jincui") { events << Death; m_baseAmount = 3; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->hasSkill(objectName()) && data.value<DeathStruct>().who == player
                && !room->getAlivePlayers().isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@jincui", true, true);
        if (!target) return false;
        ctx.targets = {target};
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "draw+throw", QVariant::fromValue(target));
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = qMax(0, getEffectiveAmount(ctx));
        if (ctx.choice == "draw") {
            room->broadcastSkillInvoke(objectName(), 1);
            target->drawCards(amount, objectName());
        } else {
            room->broadcastSkillInvoke(objectName(), 2);
            room->askForDiscard(target, objectName(), amount, amount, false, true);
        }
        return false;
    }
};

class Badao : public TriggerSkillV2
{
public:
    Badao() : TriggerSkillV2("badao") { events << TargetConfirmed; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.card
            && use.card->isKindOf("Slash") && use.card->isBlack() && use.to.contains(player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        const bool previous = target->hasFlag("badaoresponding");
        target->setFlags("badaoresponding");
        const auto restore = qScopeGuard([target, previous] { if (!previous) target->setFlags("-badaoresponding"); });
        // The flag is only a scoped AI projection and cannot grant another instance this effect.
        if (room->askForUseCard(target, "slash", "@askforslash")) {
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(target, objectName());
        }
        return false;
    }
};
class Wenjiu : public TriggerSkillV2
{
public:
    Wenjiu() : TriggerSkillV2("wenjiu")
    { events << ConfirmDamage << TargetConfirmed; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == ConfirmDamage) {
            const DamageStruct damage = data.value<DamageStruct>();
            return damage.card && damage.card->isKindOf("Slash") && damage.card->isBlack()
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.card && use.card->isKindOf("Slash") && use.card->isRed() && use.to.contains(player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {event == ConfirmDamage ? ctx.original_data->value<DamageStruct>().to : ctx.owner};
        return ctx.targets.first() != nullptr;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == ConfirmDamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (target != damage.to) return false;
            room->sendCompulsoryTriggerLog(ctx.owner, this, 1);
            LogMessage log;
            log.type = "#Wenjiu2"; log.from = ctx.owner; log.to << target;
            log.arg = QString::number(damage.damage);
            damage.damage += getEffectiveAmount(ctx);
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);
            *ctx.original_data = QVariant::fromValue(damage);
        } else {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.card || !use.from) return false;
            const QString key = "Jink_" + use.card->toString();
            QVariantList jinks = use.from->getTag(key).toList();
            const int index = use.to.indexOf(target);
            if (index < 0 || index >= jinks.size()) return false;
            // The card-use Jink vector remains authoritative, including nested target changes.
            room->sendCompulsoryTriggerLog(ctx.owner, this, 2);
            LogMessage log; log.type = "#NoJink"; log.from = target; room->sendLog(log);
            jinks[index] = 0;
            use.from->setTag(key, jinks);
        }
        return false;
    }
};
class Shipo : public TriggerSkillV2
{
public:
    Shipo() : TriggerSkillV2("shipo") { events << EventPhaseStart; m_baseAmount = 2; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Judge || player->getJudgingArea().isEmpty()) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->getCardCount(true) >= 2) result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || ctx.invoker->getJudgingArea().isEmpty()) return false;
        const int count = qMax(0, getEffectiveAmount(ctx));
        if (count == 0) return false;
        const Card *payment = room->askForExchange(ctx.owner, objectName(), count, count, true,
            "@shipo:" + ctx.invoker->objectName(), true);
        if (!payment || payment->subcardsLength() != count) return false;
        QVariantList ids;
        for (int id : payment->getSubcards()) ids << id;
        ctx.extra_data = ids;
        ctx.targets = {ctx.invoker};
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card || card->hasFlag("using") || ids.contains(id) || room->getCardOwner(id) != ctx.owner
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || !ctx.owner->canDiscard(ctx.owner, id)) return false;
            ids << id;
        }
        if (ids.size() != qMax(0, getEffectiveAmount(ctx))) return false;
        // Validate the entire selected payment before moving any of its cards.
        DummyCard payment(ids);
        room->throwCard(&payment, ctx.owner);
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->getJudgingArea().isEmpty()) return false;
        room->broadcastSkillInvoke(objectName());
        DummyCard cards;
        cards.addSubcards(target->getJudgingArea());
        room->obtainCard(ctx.owner, &cards);
        return false;
    }
};
class Gushou : public TriggerSkillV2
{
public:
    Gushou() : TriggerSkillV2("gushou")
    { frequency = Frequent; events << CardUsed << CardResponded; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        return player && player->isAlive() && player->hasSkill(objectName()) && room->getCurrent() != player
            && card && card->isKindOf("BasicCard") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Draw recipients pass through the same target hook as selected recipients.
        room->broadcastSkillInvoke(objectName());
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class Yuwen : public TriggerSkillV2
{
public:
    Yuwen() : TriggerSkillV2("yuwen") { events << GameOverJudge; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 4; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        return player && player == death.who && player->hasSkill(objectName()) && (!death.damage || death.damage->from != player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets = {ctx.owner}; return true; }
    bool allowsDeadTarget(const SkillContext &ctx, const ServerPlayer *target) const override
    { return ctx.original_data && ctx.original_data->value<DeathStruct>().who == target; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DeathStruct death = ctx.original_data->value<DeathStruct>();
        if (death.who != target || (death.damage && death.damage->from == target)) return false;
        // DeathStruct's legacy pointer must remain valid through Death/BuryVictim; native ownership is a separate debt.
        if (!death.damage) { death.damage = new DamageStruct; death.damage->to = target; }
        death.damage->from = target; *ctx.original_data = QVariant::fromValue(death);
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true); return false;
    }
};
ShouyeCard::ShouyeCard()
{
    setSkillName("shouye");
}

bool ShouyeCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (targets.length() >= 2)
		return false;

	if (to_select == Self)
		return false;
	return true;
}

void ShouyeCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->drawCards(1,"shouye");
	if (effect.from->getMark("jiehuo") == 0)
		effect.to->getRoom()->addPlayerMark(effect.from, "@shouye");
}

class Shouye : public ViewAsSkillV2
{
public:
    Shouye() : ViewAsSkillV2("shouye", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override { return ctx.initiator && ctx.initiator->getMark("jiehuo") > 0 ? 1 : INT_MAX; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && request.selectedCardIds.isEmpty() && card && card->isRed() && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId()) && !request.initiator->isJilei(card); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { ActiveSkillRequest checked = request; checked.selectedCardIds.clear(); return request.selectedCardIds.size() == 1 && canSelectCard(checked, Sanguosha->getCard(request.selectedCardIds.first())); }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && !selected.contains(target) && selected.size() < 2; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return !selected.isEmpty() && selected.size() <= 2; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ShouyeCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "counter") { if (target->getMark("jiehuo") == 0) target->getRoom()->addPlayerMark(target, "@shouye", getEffectiveAmount(ctx)); return ContinueEffects; }
        target->drawCards(getEffectiveAmount(ctx), objectName());
        // The awakening counter is an explicit shared resource; its recipient receives its own hook.
        if (ctx.invoker->isAlive() && ctx.invoker->getMark("jiehuo") == 0) { SkillContext counter = ctx; counter.choice = "counter"; counter.targets = {ctx.invoker}; skillEffect(counter, ctx.invoker); }
        return ContinueEffects;
    }
};

class Jiehuo : public TriggerSkillV2
{
public:
    Jiehuo() : TriggerSkillV2("jiehuo") { events << CardFinished << EventSkillInvoking; frequency = Wake; waked_skills = "shien"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != CardFinished || !player || player->isDead() || !player->hasSkill(objectName()) || (player->getMark("@shouye") < 7 && !player->canWake(objectName()))) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext ctx; ctx.owner = ctx.initiator = ctx.invoker = player; ctx.skill_name = objectName(); ctx.instanceID = id;
            ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(ctx)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets = {ctx.owner}; return isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->doSuperLightbox(target, objectName());
        // This accepted awakening changes the character's shared Shouye rule, while quota remains exact-instance.
        room->setPlayerMark(target, "jiehuo", 1);
        if (room->changeMaxHpForAwakenSkill(target, -getEffectiveAmount(ctx), objectName())) room->acquireSkillFromEffect(target, "shien", ctx);
        room->setPlayerMark(target, "@shouye", 0);
        return false;
    }
};
class Shien : public TriggerSkillV2
{
public:
    Shien() : TriggerSkillV2("shien") { events << CardUsed << CardResponded; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->hasSkill(objectName())
            || player->getMark("forbid_shien") > 0 || player->hasFlag("forbid_shien")) return result;
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        if (!card || !card->isNDTrick()) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) result[owner] << objectName();
        return result;
    }

    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || ctx.invoker->getMark("forbid_shien") > 0 || ctx.invoker->hasFlag("forbid_shien")) return false;
        if (!room->askForSkillInvoke(ctx.invoker, objectName(), QVariant::fromValue(ctx.owner))) {
            // These remain the user's global prompt preferences, not a skill-instance quota.
            const QString choice = room->askForChoice(ctx.invoker, "forbid_shien", "yes+no+maybe");
            if (choice == "yes") room->setPlayerMark(ctx.invoker, "forbid_shien", 1);
            else if (choice == "maybe") room->setPlayerFlag(ctx.invoker, "forbid_shien");
            return false;
        }
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        LogMessage log; log.type = "#InvokeOthersSkill"; log.from = ctx.invoker;
        log.to << ctx.owner; log.arg = objectName(); room->sendLog(log);
        room->notifySkillInvoked(ctx.owner, objectName());
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
WisdomPackage::WisdomPackage()
	:Package("wisdom")
{

	General *wisxuyou,
		*wisjiangwei, *wisjiangwan,
		*wissunce, *wiszhangzhao,
		*wishuaxiong, *wistianfeng, *wisshuijing;

	wisxuyou = new General(this, "wis_xuyou", "wei", 3);
	wisxuyou->addSkill(new Juao);
	wisxuyou->addSkill(new Tanlan);
	wisxuyou->addSkill(new Shicai);

	wisjiangwei = new General(this, "wis_jiangwei", "shu");
	wisjiangwei->addSkill(new Yicai);
	wisjiangwei->addSkill(new Beifa);

	wisjiangwan = new General(this, "wis_jiangwan", "shu", 3);
	wisjiangwan->addSkill(new Houyuan);
	wisjiangwan->addSkill(new Chouliang);

	wissunce = new General(this, "wis_sunce$", "wu");
	wissunce->addSkill(new Bawang);
	wissunce->addSkill(new Weidai);
    wissunce->addSkill(new WeidaiCleanup);
    related_skills.insertMulti("weidai", "#weidai-cleanup");

	wiszhangzhao = new General(this, "wis_zhangzhao", "wu", 3);
	wiszhangzhao->addSkill(new Longluo);
	wiszhangzhao->addSkill(new Fuzuo);
	wiszhangzhao->addSkill(new Jincui);

	wishuaxiong = new General(this, "wis_huaxiong", "qun");
	wishuaxiong->addSkill(new Badao);
	wishuaxiong->addSkill(new Wenjiu);

	wistianfeng = new General(this, "wis_tianfeng", "qun", 3);
	wistianfeng->addSkill(new Shipo);
	wistianfeng->addSkill(new Gushou);
	wistianfeng->addSkill(new Yuwen);

	wisshuijing = new General(this, "wis_shuijing", "qun");
	wisshuijing->addSkill(new Shouye);
	wisshuijing->addSkill(new Jiehuo);
	wisshuijing->addRelateSkill("shien");

	skills << new Shien;

	addMetaObject<JuaoCard>();
	addMetaObject<BawangCard>();
	addMetaObject<FuzuoCard>();
	addMetaObject<WeidaiCard>();
	addMetaObject<HouyuanCard>();
	addMetaObject<ShouyeCard>();
}
ADD_PACKAGE(Wisdom)
