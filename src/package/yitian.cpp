#include "yitian.h"
#include "engine.h"
#include "maneuvering.h"
#include "clientplayer.h"
#include "room.h"
#include "roomthread.h"
#include "util.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>

namespace {

// The Room-local serial identifies an applied effect even when history is disabled.
qint64 nextYitianReceipt(Room *room)
{
    const qint64 serial = room->getTag("YitianV2ReceiptSerial").toLongLong() + 1;
    room->setTag("YitianV2ReceiptSerial", serial);
    return serial;
}
ActiveSkillCard *proxyCard(const ViewAsSkillV2 *skill, const ActiveSkillRequest &request, bool mute)
{
	ActiveSkillCard *card = new ActiveSkillCard;
	card->setActiveSkill(skill);
	card->setSkillName(skill->objectName());
	card->addSubcards(request.selectedCardIds);
	card->setUserString(request.userString);
	card->setMute(mute);
	return card;
}

bool ownsLivingSkill(const ServerPlayer *player, const QString &name)
{
	return player && player->isAlive() && player->hasSkill(name);
}

bool yitianSwordLeaving(ServerPlayer *player, const CardsMoveOneTimeStruct &move)
{
	if (!player || move.from != player || !move.from_places.contains(Player::PlaceEquip))
		return false;
	for (int i = 0; i < move.card_ids.size(); ++i) {
		if (move.from_places[i] != Player::PlaceEquip)
			continue;
		const Card *card = Sanguosha->getEngineCard(move.card_ids[i]);
		if (card && card->isKindOf("YitianSword"))
			return true;
	}
	return false;
}

}

#define YT_MIRROR_ACTIVATION \
	bool isEnabledAtPlay(const Player *player) const override \
	{ \
		ActiveSkillRequest request; \
		request.initiator = player; \
		request.reason = CardUseStruct::CARD_USE_REASON_PLAY; \
		return canActivate(request); \
	} \
	bool isEnabledAtResponse(const Player *player, const QString &pattern) const override \
	{ \
		ActiveSkillRequest request; \
		request.initiator = player; \
		request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE; \
		request.pattern = pattern; \
		return canActivate(request); \
	}

class YitianSwordSkill : public WeaponSkillV2
{
public:
    YitianSwordSkill() : WeaponSkillV2("yitian_sword", "yitian_sword")
    {
        events << DamageComplete << CardsMoveOneTime;
    }

    bool usesEventSource(const SkillContext &ctx) const override
    {
        return ctx.current_event == CardsMoveOneTime && !ctx.extra_data.toMap().isEmpty();
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &out) const override
    {
        if (event != CardsMoveOneTime) return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !yitianSwordLeaving(player, move)) return true;
        const qint64 moveId = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
        if (moveId <= 0) return true;
        for (const QVariant &value : player->getTag("YitianSwordRemovalReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            const int index = move.card_ids.indexOf(receipt.value("card").toInt());
            if (receipt.value("move").toLongLong() != moveId || index < 0
                || move.from_places.value(index) != Player::PlaceEquip) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.extra_data = receipt;
            ctx.amount = getBaseAmount();
            out << ctx;
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DamageComplete || !player || !player->isAlive()) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return !damage.prevented && player != room->getCurrent() && WeaponSkillV2::triggerable(player)
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != CardsMoveOneTime) return true;
        QVariantList receipts = ctx.owner->getTag("YitianSwordRemovalReceipts").toList();
        if (!receipts.removeOne(ctx.extra_data)) return false;
        // Consume the exact removal before prompting; nested removals cannot borrow it.
        if (receipts.isEmpty()) ctx.owner->removeTag("YitianSwordRemovalReceipts");
        else ctx.owner->setTag("YitianSwordRemovalReceipts", receipts);
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@YitianSword-lost", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageComplete && ctx.owner && ctx.owner->isAlive())
            room->askForUseCard(ctx.owner, "slash", "@YitianSword-slash");
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};
YitianSword::YitianSword(Suit suit, int number)
	: Weapon(suit, number, 2)
{
	setObjectName("yitian_sword");
}

void YitianSword::onUninstall(ServerPlayer *player) const
{
    if (player->isAlive() && player->hasWeapon(objectName())) {
        Room *room = player->getRoom();
        const qint64 moveId = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
        if (moveId > 0) {
            QVariantList receipts = player->getTag("YitianSwordRemovalReceipts").toList();
            for (const Card *equip : player->getEquips()) {
                if (equip->getRealCard() != this) continue;
                const QVariantMap receipt{{"move", moveId}, {"card", equip->getEffectiveId()}};
                if (!receipts.contains(receipt)) receipts << receipt;
            }
            player->setTag("YitianSwordRemovalReceipts", receipts);
        }
    }
	Weapon::onUninstall(player);
}

class YTChengxiangViewAsSkill : public ViewAsSkillV2
{
public:
	YTChengxiangViewAsSkill() : ViewAsSkillV2("ytchengxiang", -1)
	{
	}

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
			&& request.pattern == "@@ytchengxiang" && request.initiator->getMark("ytchengxiang") > 0;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		const Player *player = request.initiator;
		if (!player || !candidate || candidate->hasFlag("using"))
			return false;
		const int id = candidate->getEffectiveId();
		if (id < 0 || request.selectedCardIds.contains(id) || !player->canDiscard(player, id))
			return false;
		if (!player->handCards().contains(id) && !player->getEquipsId().contains(id))
			return false;
		int sum = candidate->getNumber();
		foreach (int selected, request.selectedCardIds)
			sum += Sanguosha->getCard(selected)->getNumber();
		return sum <= player->getMark("ytchengxiang");
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.isEmpty())
			return false;
		int sum = 0;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		foreach (int id, request.selectedCardIds) {
			const Card *card = Sanguosha->getCard(id);
			if (!card || !canSelectCard(selection, card))
				return false;
			sum += card->getNumber();
			selection.selectedCardIds << id;
		}
		return request.initiator && sum == request.initiator->getMark("ytchengxiang");
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		return cardSelectionFeasible(request) ? proxyCard(this, request, false) : nullptr;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "YTChengxiangCard";
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return candidate && candidate->isAlive() && !selected.contains(candidate)
            && selected.length() < request.selectedCardIds.length();
	}

	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
	{
		return !selected.isEmpty() && selected.length() <= request.selectedCardIds.length();
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !target)
			return ContinueEffects;
		Room *room = source->getRoom();
		if (target->isWounded()) {
			RecoverStruct recover;
			recover.who = source;
			recover.reason = "ytchengxiang";
			recover.recover = getEffectiveAmount(ctx);
			room->recover(target, recover);
		} else
			target->drawCards(2 * getEffectiveAmount(ctx), objectName());
		return ContinueEffects;
	}
};

class YTChengxiang : public TriggerSkillV2
{
public:
	YTChengxiang() : TriggerSkillV2("ytchengxiang")
	{
		events << Damaged;
		view_as_skill = new YTChengxiangViewAsSkill;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()) || player->isNude())
			return TriggerList();
		const Card *card = data.value<DamageStruct>().card;
		if (!card)
			return TriggerList();
		const int point = card->getNumber();
		if (point < 1 || point > 13)
			return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !ctx.original_data)
			return false;
		const Card *card = ctx.original_data->value<DamageStruct>().card;
		if (!card)
			return false;
		const int point = card->getNumber();
		const int previous = player->getMark(objectName());
		Room::AcceptedViewAsEffectScope continuation(room, player, objectName(), ctx);
		if (!continuation.isValid()) return false;
		const auto restore = qScopeGuard([&] {
			room->setPlayerMark(player, objectName(), previous);
		});
		room->setPlayerMark(player, objectName(), point);
		room->askForUseCard(player, "@@ytchengxiang", QString("@ytchengxiang-card:::%1").arg(point));
		return false;
	}
};

class Conghui : public TriggerSkillV2
{
public:
	Conghui() : TriggerSkillV2("conghui")
	{
		frequency = Compulsory;
		events << EventPhaseChanging;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		return data.value<PhaseChangeStruct>().to == Player::Discard
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{ ctx.targets = {ctx.owner}; return false; }
	bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
	{
		if (!player || !ctx.original_data)
			return false;
		room->sendCompulsoryTriggerLog(player, this);
		player->skip(ctx.original_data->value<PhaseChangeStruct>().to);
		return false;
	}
};

class Zaoyao : public TriggerSkillV2
{
public:
	Zaoyao() : TriggerSkillV2("zaoyao")
	{
		frequency = Compulsory;
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Finish
			&& player->getHandcardNum() > 13
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{ ctx.targets = {ctx.owner}; return false; }
	bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
	{
		if (!player)
			return false;
		room->sendCompulsoryTriggerLog(player, this);
		player->throwAllHandCards(objectName());
		room->loseHp(HpLostStruct(player, getEffectiveAmount(ctx), "zaoyao", ctx.owner));
		return false;
	}
};

class WeiwudiGuixin : public TriggerSkillV2
{
public:
	WeiwudiGuixin() : TriggerSkillV2("weiwudi_guixin")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Finish
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName())) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "modify+obtain");
        ServerPlayer *target = ctx.owner;
        if (ctx.choice == "modify")
            target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName());
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "modify") {
            const QVariant previous = room->getTag("Guixin2Modify");
            const auto restore = qScopeGuard([&] {
                if (previous.isValid()) room->setTag("Guixin2Modify", previous);
                else room->removeTag("Guixin2Modify");
            });
            room->setTag("Guixin2Modify", QVariant::fromValue(target));
            QStringList kingdoms = Sanguosha->getKingdoms();
            kingdoms.removeOne("god");
            kingdoms.removeOne(target->getKingdom());
            if (!kingdoms.isEmpty())
                room->changeKingdom(target, room->askForChoice(ctx.owner, objectName(), kingdoms.join("+")));
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            QStringList lords = Sanguosha->getLords();
            for (ServerPlayer *player : room->getAlivePlayers()) {
                lords.removeOne(player->getGeneralName());
                if (player->getGeneral2()) lords.removeOne(player->getGeneral2Name());
            }
            QStringList choices;
            for (const QString &lord : lords) {
                const General *general = Sanguosha->getGeneral(lord);
                if (!general) continue;
                for (const Skill *skill : general->findChildren<const Skill *>()) {
                    if (skill->isLordSkill() && skill->isVisible() && !target->hasSkill(skill->objectName(), true)
                        && !choices.contains(skill->objectName())) choices << skill->objectName();
                }
            }
            if (choices.isEmpty()) break;
            const QString chosen = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
            const int id = room->acquireSkill(target, chosen);
            if (id <= 0) continue;
            // Permanent acquisition retains the exact granting cause without tying its lifetime to it.
            QVariantList grants = target->getTag("WeiwudiGuixinGrants").toList();
            grants << QVariantMap{{"skill", chosen}, {"instance", id}, {"owner", ctx.sourceRef.ownerObjectName},
                {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
            target->setTag("WeiwudiGuixinGrants", grants);
        }
        return false;
    }
};
class JuejiViewAsSkill : public ViewAsSkillV2
{
public:
	JuejiViewAsSkill() : ViewAsSkillV2("jueji")
	{
	}

	LimitScope getLimitScope() const override { return Limit_Phase; }

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->canPindian();
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		return cardSelectionFeasible(request) ? proxyCard(this, request, false) : nullptr;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "JuejiCard";
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return request.initiator && selected.isEmpty() && candidate
			&& request.initiator->canPindian(candidate);
	}

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !target)
			return ContinueEffects;
        const QVariant previous = source->getTag("JuejiResolution");
        const auto restore = qScopeGuard([&] {
            if (previous.isValid()) source->setTag("JuejiResolution", previous);
            else source->removeTag("JuejiResolution");
        });
        // Pindian callbacks resume the accepted activation, even if its grant retires.
        source->setTag("JuejiResolution", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"execution", ctx.executionID}, {"event", target->getRoom()->currentHistoryEventId()},
            {"serial", nextYitianReceipt(target->getRoom())}});
        const QVariant data = QVariant::fromValue(target);
		while (source->isAlive() && target->isAlive() && source->pindian(target, "jueji", nullptr)) {
			if (!source->canPindian(target) || !source->askForSkillInvoke("jueji", data))
				break;
		}
		return ContinueEffects;
	}
};

class Jueji : public TriggerSkillV2
{
public:
    Jueji() : TriggerSkillV2("jueji")
    { events << Pindian; global = true; view_as_skill = new JuejiViewAsSkill; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        const PindianStruct *pindian = data.value<PindianStruct *>();
        if (!player || !pindian || pindian->from != player || pindian->reason != objectName()) return true;
        const QVariantMap receipt = player->getTag("JuejiResolution").toMap();
        if (receipt.isEmpty()) return true;
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = owner;
        ctx.invoker = ctx.initiator = player;
        ctx.instanceID = receipt.value("instance").toInt();
        ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill", objectName()).toString(), ctx.instanceID));
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.is_forced = true; // Mandatory continuation of an already accepted effect.
        ctx.extra_data = receipt;
        out << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->getTag("JuejiResolution") == ctx.extra_data; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        if (pindian && pindian->isSuccess()) ctx.targets = {ctx.invoker};
        else room->broadcastSkillInvoke(objectName(), 2);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        if (pindian && pindian->to_card && room->getCardPlace(pindian->to_card->getEffectiveId()) == Player::PlaceTable)
            target->obtainCard(pindian->to_card);
        return false;
    }
    int getEffectIndex(const ServerPlayer *, const Card *) const override { return 1; }
};
class LukangWeiyan : public TriggerSkillV2
{
public:
	LukangWeiyan() : TriggerSkillV2("lukang_weiyan")
	{
		events << EventPhaseChanging;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to == Player::Draw && !player->isSkipped(Player::Draw))
			return TriggerList{{player, QStringList{objectName()}}};
		if (change.to == Player::Play && !player->isSkipped(Player::Play))
			return TriggerList{{player, QStringList{objectName()}}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.original_data)
			return false;
		const PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
		if (change.to == Player::Draw)
			{ ctx.choice = "draw2play"; return ctx.owner->askForSkillInvoke(objectName(), ctx.choice); }
		if (change.to == Player::Play)
			{ ctx.choice = "play2draw"; return ctx.owner->askForSkillInvoke(objectName(), ctx.choice); }
		return false;
	}

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
	{
		if (!ctx.owner || !ctx.original_data)
			return false;
		PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
		if (ctx.choice == "draw2play" && change.to == Player::Draw) {
			room->broadcastSkillInvoke(objectName(), 1);
			change.to = Player::Play;
		} else if (ctx.choice == "play2draw" && change.to == Player::Play) {
			room->broadcastSkillInvoke(objectName(), 2);
			change.to = Player::Draw;
		}
		*ctx.original_data = QVariant::fromValue(change);
		return false;
	}
};

class Kegou : public TriggerSkillV2
{
public:
	Kegou() : TriggerSkillV2("kegou")
	{
		frequency = Wake;
		events << EventPhaseStart << EventSkillInvoking;
	}

	LimitScope getLimitScope() const override { return Limit_Game; }

	bool wakeable(Room *room, ServerPlayer *lukang) const
	{
		if (lukang->canWake(objectName()))
			return true;
		if (lukang->getKingdom() != "wu")
			return false;
		foreach (ServerPlayer *p, room->getOtherPlayers(lukang)) {
			if (p->getKingdom() == "wu" && !p->isLord())
				return false;
		}
		return true;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		return event == EventPhaseStart && ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Start
			&& wakeable(room, player)
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && isUsable(ctx) && wakeable(room, ctx.owner); }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (!target->canWake(objectName())) {
            LogMessage log;
            log.type = "#KegouWake";
            log.from = target;
            room->sendLog(log);
        }
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());
        room->doSuperLightbox(ctx.owner, objectName());
        room->setPlayerMark(ctx.owner, objectName(), 1); // Wake display; V2 usage owns admission.
        if (room->changeMaxHpForAwakenSkill(target, -getEffectiveAmount(ctx), objectName())) {
            const int id = room->acquireSkill(target, "lianying");
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "granted_lianying", id);
        }
        return false;
    }
};
static SkillInstanceRef lianliSource(const QVariantMap &pair)
{
    return SkillInstanceRef(pair.value("source_owner").toString(),
        SkillInstanceKey(pair.value("source_skill").toString(), pair.value("source_instance").toInt()));
}

LianliSlashCard::LianliSlashCard() { setSkillName("lianli-slash"); }
bool LianliSlashCard::targetFilter(const QList<const Player *> &targets, const Player *candidate, const Player *self) const
{ Slash slash(NoSuit, 0); return slash.targetFilter(targets, candidate, self); }
const Card *LianliSlashCard::validate(CardUseStruct &) const
{ return nullptr; } // Submitted compatibility cards must be rebuilt by the V2 activation entry.

class LianliSlashViewAsSkill : public ViewAsSkillV2
{
public:
    LianliSlashViewAsSkill() : ViewAsSkillV2("lianli-slash") { attached_lord_skill = true; }
    const Player *provider(const Player *actor, int instance) const
    {
        if (!actor) return nullptr;
        const SkillInstance *leaf = actor->findSkillInstance(objectName(), instance);
        if (!leaf || !leaf->parentRef.isValid()) return nullptr;
        for (const Player *candidate : actor->getAliveSiblings(true)) {
            if (candidate->objectName() == leaf->parentRef.ownerObjectName
                && candidate->hasSkillInstance(leaf->parentRef.key.skillName, leaf->parentRef.key.instanceID)
                && !candidate->isSkillInvalid(leaf->parentRef.key.skillName, leaf->parentRef.key.instanceID)) return candidate;
        }
        return nullptr;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *actor = request.initiator;
        if (!actor) return false;
        bool available = false;
        const QList<int> ids = request.activationRef.isValid() ? QList<int>{request.activationRef.key.instanceID}
            : actor->getValidSkillInstanceIds(objectName());
        for (int id : ids)
            if (provider(actor, id) && !actor->getSkillInstanceStateValue(objectName(), id, "failed").toBool()) available = true;
        if (!available) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? Slash::IsAvailable(actor)
            : request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                && (request.pattern.contains("slash") || request.pattern.contains("Slash"));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        return slash;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        const Player *found = provider(request.initiator, request.activationRef.key.instanceID);
        ServerPlayer *donor = found ? room->findPlayerByObjectName(found->objectName()) : nullptr;
        if (!donor || !ctx.invoker) return false;
        const Card *provided = room->askForCard(donor, "slash", "@lianli-slash", QVariant::fromValue(ctx.invoker),
            Card::MethodResponse, ctx.invoker, false, "", true);
        if (!provided) {
            ctx.invoker->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "failed", true);
            return false;
        }
        Card *slash = Sanguosha->cloneCard(provided->objectName(), provided->getSuit(), provided->getNumber());
        if (!slash) return false;
        slash->addSubcard(provided);
        slash->setSkillName(objectName());
        slash->setFlags("YUANBEN");
        slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        slash->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        slash->deleteLater();
        ctx.updated_card = slash;
        if (ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            use.m_isOwnerUse = false;
            *ctx.original_data = QVariant::fromValue(use);
        }
        return true;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};

static bool lianliHelperMatches(const Player *holder, const QString &skill, int id, const QVariantMap &pair)
{
    const SkillInstance *instance = holder ? holder->findSkillInstance(skill, id) : nullptr;
    if (!instance) return false;
    const SkillInstanceRef parent = instance->parentRef.isValid() ? instance->parentRef
        : SkillInstanceRef(holder->objectName(), instance->parent);
    return parent == SkillInstanceRef(pair.value("holder").toString(),
        SkillInstanceKey("lianli", pair.value("activation").toInt()));
}
class LianliSlash : public TriggerSkillV2
{
public:
    LianliSlash() : TriggerSkillV2("#lianli-slash") { events << CardAsked; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const QStringList patterns = data.toStringList();
        if (!player || patterns.size() < 3 || patterns.first() != "slash"
            || patterns.at(1).contains("lianli-slash") || patterns.last() != "response") return result;
        for (const QVariant &value : room->getTag("LianliPairs").toList()) {
            const QVariantMap pair = value.toMap();
            if (pair.value("partner").toString() != player->objectName()) continue;
            ServerPlayer *provider = room->findPlayerByObjectName(pair.value("holder").toString());
            if (!provider || !provider->isAlive()) continue;
            for (int id : provider->getValidSkillInstanceIds(objectName())) {
                const SkillInstanceRef leaf(provider->objectName(), SkillInstanceKey(objectName(), id));
                if (room->resolveSkillInstanceRootRef(leaf) == lianliSource(pair)
                    && lianliHelperMatches(provider, objectName(), id, pair))
                    result[provider] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.invoker->askForSkillInvoke("lianli-slash", *ctx.original_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.manual_effect = true; return skillEffect(event, room, owner, ctx, ctx.invoker); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const Card *slash = room->askForCard(ctx.owner, "slash", "@lianli-slash", *ctx.original_data,
            Card::MethodResponse, target, false, "", true);
        if (!slash) return false;
        room->setCardFlag(slash, "YUANBEN");
        room->provide(slash);
        return true;
    }
};

class LianliJink : public TriggerSkillV2
{
public:
    LianliJink() : TriggerSkillV2("#lianli-jink") { events << CardAsked; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        const QStringList patterns = data.toStringList();
        if (!player || patterns.size() < 2 || patterns.first() != "jink" || patterns.at(1).contains("lianli-jink")) return true;
        for (const QVariant &value : room->getTag("LianliPairs").toList()) {
            const QVariantMap pair = value.toMap();
            if (pair.value("holder").toString() != player->objectName()) continue;
            ServerPlayer *partner = room->findPlayerByObjectName(pair.value("partner").toString());
            if (!partner || !partner->isAlive()) continue;
            for (int id : player->getValidSkillInstanceIds(objectName())) {
                const SkillInstanceRef leaf(player->objectName(), SkillInstanceKey(objectName(), id));
                if (!lianliHelperMatches(player, objectName(), id, pair) || room->resolveSkillInstanceRootRef(leaf) != lianliSource(pair)) continue;
                SkillContext ctx;
                ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = player;
                ctx.activationRef = leaf; ctx.sourceRef = lianliSource(pair); ctx.instanceID = id;
                bool ok = false;
                ctx.amount = room->getSkillInstanceAmount(leaf, &ok);
                if (!ok) continue;
                ctx.extra_data = pair; // Freeze necessary effect data before bypass_cost interception.
                ctx.original_data = &data; ctx.current_event = event;
                out << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner->askForSkillInvoke("lianli-jink", *ctx.original_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.manual_effect = true; return skillEffect(event, room, owner, ctx, ctx.owner); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (const QVariant &value : QVariantList{ctx.extra_data}) {
            const QVariantMap pair = value.toMap();
            ServerPlayer *partner = room->findPlayerByObjectName(pair.value("partner").toString());
            if (!partner || !partner->isAlive()) continue;
            const Card *jink = room->askForCard(partner, "jink", "@lianli-jink", *ctx.original_data,
                Card::MethodResponse, target, false, "", true);
            if (!jink) continue;
            room->setCardFlag(jink, "YUANBEN");
            room->provide(jink);
            return true;
        }
        return false;
    }
};

class Lianli : public TriggerSkillV2
{
public:
    Lianli() : TriggerSkillV2("lianli") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Start
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> males;
        for (ServerPlayer *player : room->getAlivePlayers()) if (player->isMale()) males << player;
        if (males.isEmpty()) return false;
        ServerPlayer *partner = room->askForPlayerChosen(ctx.owner, males, objectName(), "@lianli-card", true, true);
        if (!partner) return false;
        ctx.targets = {partner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const SkillInstanceRef attached = room->attachSkillToPlayer(target, "lianli-slash", ctx.activationRef);
        if (!attached.isValid()) return false;
        if (!ctx.owner->isAlive() || !target->isAlive()
            || !ctx.owner->hasSkillInstance(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)
            || !target->hasSkillInstance(attached.key.skillName, attached.key.instanceID)) {
            room->detachAttachedSkill(attached);
            return false;
        }
        QVariantList pairs = room->getTag("LianliPairs").toList();
        pairs << QVariantMap{{"holder", ctx.owner->objectName()}, {"partner", target->objectName()},
            {"activation", ctx.activationRef.key.instanceID}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID},
            {"attached", attached.key.instanceID}, {"serial", nextYitianReceipt(room)}};
        room->setTag("LianliPairs", pairs);
        ctx.owner->gainMark("@tied");
        if (target != ctx.owner) target->gainMark("@tied");
        LogMessage log;
        log.type = "#LianliConnection";
        log.from = ctx.owner;
        log.to << target;
        room->sendLog(log);
        if (ctx.owner->hasSkill("liqian") && ctx.owner->getKingdom() != target->getKingdom())
            room->changeKingdom(ctx.owner, target->getKingdom());
        return false;
    }
};

class Tongxin : public TriggerSkillV2
{
public:
    Tongxin() : TriggerSkillV2("tongxin") { frequency = Frequent; events << Damaged; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (!player || data.value<DamageStruct>().damage <= 0) return true;
        const QVariantList pairs = room->getTag("LianliPairs").toList();
        for (int i = 0; i < pairs.size(); ++i) {
            const QVariantMap pair = pairs.at(i).toMap();
            if (pair.value("holder").toString() != player->objectName() && pair.value("partner").toString() != player->objectName()) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(pair.value("holder").toString());
            ServerPlayer *partner = room->findPlayerByObjectName(pair.value("partner").toString());
            if (!owner || !owner->isAlive() || !partner || !partner->isAlive()) continue;
            for (int id : owner->getValidSkillInstanceIds(objectName())) {
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = owner;
                ctx.invoker = ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                bool ok = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &ok);
                if (!ok) ctx.amount = getBaseAmount();
                ctx.preferredTarget = partner;
                ctx.trigger_count = i;
                ctx.current_event = event;
                ctx.original_data = &data;
                ctx.extra_data = pair;
                out << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner->askForSkillInvoke(this, *ctx.original_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *partner = room->findPlayerByObjectName(ctx.extra_data.toMap().value("partner").toString());
        if (!partner) return false;
        const int count = ctx.original_data->value<DamageStruct>().damage;
        for (int i = 0; i < count && ctx.owner->isAlive(); ++i) {
            if (i > 0 && !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) break;
            QList<ServerPlayer *> recipients{ctx.owner};
            if (partner != ctx.owner) recipients << partner;
            room->sortByActionOrder(recipients);
            for (ServerPlayer *recipient : recipients) {
                SkillContext draw = ctx;
                skillEffect(event, room, owner, draw, recipient);
            }
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};

class LianliClear : public TriggerSkillV2
{
public:
    LianliClear() : TriggerSkillV2("#lianli-clear")
    { events << EventPhaseStart << EventPhaseChanging << Death << EventLoseSkill; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return false;
        if (event == EventPhaseChanging) {
            for (int id : player->getSkillInstanceIds("lianli-slash"))
                player->removeSkillInstanceStateValue("lianli-slash", id, "failed");
            return false;
        }
        if (event == EventPhaseStart && player->getPhase() != Player::Start) return false;
        const QVariantList original = room->getTag("LianliPairs").toList();
        QVariantList kept, expired;
        for (const QVariant &value : original) {
            const QVariantMap pair = value.toMap();
            bool remove = event == EventPhaseStart && pair.value("holder").toString() == player->objectName();
            if (event == Death && data.value<DeathStruct>().who == player)
                remove = pair.value("holder").toString() == player->objectName() || pair.value("partner").toString() == player->objectName();
            if (event == EventLoseSkill) {
                ServerPlayer *holder = room->findPlayerByObjectName(pair.value("holder").toString(), true);
                remove = !holder || !holder->hasSkillInstance("lianli", pair.value("activation").toInt());
            }
            if (remove) expired << value;
            else kept << value;
        }
        // Publish the new pairs before detach callbacks; unrelated owners keep their grants and marks.
        room->setTag("LianliPairs", kept);
        for (const QVariant &value : expired) {
            const QVariantMap pair = value.toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(pair.value("holder").toString(), true);
            ServerPlayer *partner = room->findPlayerByObjectName(pair.value("partner").toString(), true);
            if (holder) holder->loseMark("@tied");
            if (partner) {
                if (partner != holder) partner->loseMark("@tied");
                room->detachAttachedSkill(SkillInstanceRef(partner->objectName(), SkillInstanceKey("lianli-slash", pair.value("attached").toInt())));
            }
            if (holder && holder->hasSkill("liqian") && holder->getMark("@tied") == 0 && holder->getKingdom() != "wei")
                room->changeKingdom(holder, "wei");
        }
        return false;
    }
};
class WulingEffect : public TriggerSkillV2
{
public:
    WulingEffect() : TriggerSkillV2("#wuling-effect")
    { events << DamageInflicted << PreHpRecover; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (!player) return true;
        const QVariantList receipts = room->getTag("WulingEffects").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            const QString effect = receipt.value("effect").toString();
            bool matches = false;
            if (event == PreHpRecover) {
                const RecoverStruct recover = data.value<RecoverStruct>();
                matches = effect == "water" && recover.card && recover.card->isKindOf("Peach");
            } else {
                const DamageStruct damage = data.value<DamageStruct>();
                matches = (effect == "wind" && damage.nature == DamageStruct::Fire)
                    || (effect == "thunder" && damage.nature == DamageStruct::Thunder)
                    || (effect == "fire" && damage.nature != DamageStruct::Fire)
                    || (effect == "earth" && damage.nature != DamageStruct::Normal && damage.damage > 1);
            }
            if (!matches) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
            ctx.amount = receipt.value("amount", 1).toInt();
            ctx.trigger_count = i;
            ctx.preferredTarget = player;
            ctx.current_event = event;
            ctx.is_forced = true; // Mandatory continuation of an already accepted effect.
            ctx.original_data = &data;
            ctx.extra_data = receipt;
            ctx.targets = {player};
            out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return room->getTag("WulingEffects").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QString effect = ctx.extra_data.toMap().value("effect").toString();
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        LogMessage log;
        log.from = target;
        if (event == PreHpRecover) {
            RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
            if (effect != "water" || !recover.card || !recover.card->isKindOf("Peach")) return false;
            recover.recover += amount;
            *ctx.original_data = QVariant::fromValue(recover);
            log.type = "#WulingWater";
        } else {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            log.arg = QString::number(damage.damage);
            if (effect == "wind" && damage.nature == DamageStruct::Fire) {
                damage.damage += amount;
                log.type = "#WulingWind";
            } else if (effect == "thunder" && damage.nature == DamageStruct::Thunder) {
                damage.damage += amount;
                log.type = "#WulingThunder";
            } else if (effect == "fire" && damage.nature != DamageStruct::Fire) {
                damage.nature = DamageStruct::Fire;
                log.type = "#WulingFire";
            } else if (effect == "earth" && damage.nature != DamageStruct::Normal && damage.damage > 1) {
                // Earth prevents the excess as one boolean rule; repeated sources cannot lower the cap.
                damage.damage = 1;
                log.type = "#WulingEarth";
            } else return false;
            log.arg2 = QString::number(damage.damage);
            *ctx.original_data = QVariant::fromValue(damage);
        }
        room->sendLog(log);
        return false;
    }
};

class Wuling : public TriggerSkillV2
{
public:
    Wuling() : TriggerSkillV2("wuling") { events << EventPhaseStart; global = true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->getPhase() != Player::Start) return false;
        QVariantList receipts = room->getTag("WulingEffects").toList();
        QStringList expired;
        for (int i = receipts.size() - 1; i >= 0; --i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("holder").toString() != player->objectName()) continue;
            expired << receipt.value("effect").toString();
            receipts.removeAt(i);
        }
        // Publish removal before mark callbacks can reenter or append new auras.
        room->setTag("WulingEffects", receipts);
        for (const QString &effect : expired) player->loseMark("@" + effect);
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Start
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName())) return false;
        QStringList choices{"wind", "thunder", "water", "fire", "earth"};
        choices.removeOne(ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "previous").toString());
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
        return choices.contains(ctx.choice);
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "previous", ctx.choice);
        QVariantList receipts = room->getTag("WulingEffects").toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"holder", target->objectName()}, {"effect", ctx.choice},
            {"serial", nextYitianReceipt(room)}, {"amount", getEffectiveAmount(ctx)}};
        room->setTag("WulingEffects", receipts);
        target->gainMark("@" + ctx.choice);
        return false;
    }
};
class Guihan : public ViewAsSkillV2
{
public:
	Guihan() : ViewAsSkillV2("guihan", 2)
	{
	}

	LimitScope getLimitScope() const override { return Limit_Phase; }

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || candidate->isEquipped() || candidate->hasFlag("using"))
			return false;
		const int id = candidate->getEffectiveId();
		if (id < 0 || !request.initiator->handCards().contains(id) || !request.initiator->canDiscard(request.initiator, id) || request.selectedCardIds.contains(id) || request.selectedCardIds.size() >= 2)
			return false;
		if (request.selectedCardIds.isEmpty())
			return candidate->isRed();
		const Card *first = Sanguosha->getCard(request.selectedCardIds.first());
		return first && candidate->getSuit() == first->getSuit();
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 2)
			return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		foreach (int id, request.selectedCardIds) {
			const Card *card = Sanguosha->getCard(id);
			if (!card || !canSelectCard(selection, card))
				return false;
			selection.selectedCardIds << id;
		}
		return true;
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		return cardSelectionFeasible(request) ? proxyCard(this, request, false) : nullptr;
	}

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request); }
	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "GuihanCard";
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return request.initiator && candidate && selected.isEmpty() && candidate != request.initiator;
	}

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!ctx.invoker || !target)
			return ContinueEffects;
		ctx.invoker->getRoom()->swapSeat(ctx.invoker, target);
		return ContinueEffects;
	}
};

class CaizhaojiHujia : public TriggerSkillV2
{
public:
    CaizhaojiHujia() : TriggerSkillV2("caizhaoji_hujia")
    { events << EventPhaseStart << FinishJudge; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != FinishJudge) return false;
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!player || !judge || judge->who != player || judge->reason != objectName()) return true;
        const QVariantMap receipt = player->getTag("HujiaResolution").toMap();
        if (receipt.isEmpty()) return true;
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = owner;
        ctx.invoker = ctx.initiator = player;
        ctx.instanceID = receipt.value("instance").toInt();
        ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
        ctx.extra_data = receipt;
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.is_forced = true; // Mandatory continuation of an already accepted effect.
        out << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.invoker && ctx.invoker->getTag("HujiaResolution") == ctx.extra_data;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Finish
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return event == FinishJudge || ctx.owner->askForSkillInvoke(objectName()); }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {event == FinishJudge ? ctx.invoker : ctx.owner}; return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "flip") { target->turnOver(); return false; }
        if (event == FinishJudge) {
            const QVariant phase = room->historyScopes().value("phase_id");
            const qulonglong current = room->historyParent(room->currentHistoryEventId(), "judge", true).value("id").toULongLong();
            const QString flipKey = QString::number(phase.toULongLong());
            if (phase.toULongLong() > 0 && current > 0 && ctx.invoker->getTag("HujiaFlippedPhase").toString() != flipKey) {
                QVariantMap query{{"kind", "judge_start"}, {"phase_id", phase}, {"player", ctx.invoker->objectName()}};
                int count = 0;
                bool complete = true;
                while (true) {
                    const QVariantMap page = room->queryHistoryFacts(query);
                    if (!page.value("complete").toBool()) { complete = false; break; }
                    for (const QVariant &entry : page.value("items").toList())
                        if (entry.toMap().value("data").toMap().value("reason").toString() == objectName()) ++count;
                    if (!page.value("has_more").toBool()) break;
                    query.insert("watermark", page.value("watermark"));
                    query.insert("after", page.value("next_after"));
                }
                if (complete && count >= 3) {
                    // This is an applied flip latch, not a replacement for the phase's judge history.
                    ctx.invoker->setTag("HujiaFlippedPhase", flipKey);
                    SkillContext flip = ctx;
                    flip.choice = "flip";
                    skillEffect(event, room, owner, flip, ctx.invoker);
                }
            }
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (!judge || !judge->card || !judge->card->isRed()) return false;
            const int id = judge->card->getEffectiveId();
            if (room->getCardPlace(id) != Player::PlaceJudge) return false;
            const QVariantMap before = room->queryHistoryMoves({{"to", target->objectName()}, {"limit", 1}});
            target->obtainCard(judge->card);
            // Require an immutable move into this recipient's hand, even if nested callbacks moved it again.
            bool obtained = false;
            bool complete = current > 0 && before.value("complete").toBool();
            QVariantMap query{{"to", target->objectName()}, {"after", before.value("watermark")}};
            while (complete) {
                const QVariantMap page = room->queryHistoryMoves(query);
                if (!page.value("complete").toBool()) { complete = false; break; }
                for (const QVariant &entry : page.value("items").toList()) {
                    const QVariantMap fact = entry.toMap();
                    const QVariantMap move = fact.value("data").toMap();
                    if (move.value("card_id").toInt() == id && move.value("from_place").toInt() == Player::PlaceJudge
                        && move.value("to_place").toInt() == Player::PlaceHand
                        && room->historyParent(fact.value("event_id").toULongLong(), "judge", true).value("id").toULongLong() == current)
                        obtained = true;
                }
                if (!page.value("has_more").toBool()) break;
                query.insert("watermark", page.value("watermark"));
                query.insert("after", page.value("next_after"));
            }
            const QVariantMap result = current > 0 ? room->queryHistoryFacts({{"kind", "judge_result"}, {"event_id", current}}) : QVariantMap();
            bool redResult = false;
            if (result.value("complete").toBool()) for (const QVariant &entry : result.value("items").toList()) {
                const QVariantMap value = entry.toMap().value("data").toMap();
                const QVariantMap card = value.value("card").toMap();
                if (value.value("outcome").toString() == "completed" && value.value("result_available").toBool()
                    && card.value("id").toInt() == id && card.value("red").toBool()) redResult = true;
            }
            QVariantMap receipt = ctx.invoker->getTag("HujiaResolution").toMap();
            receipt.insert("obtained", complete && obtained && redResult);
            ctx.invoker->setTag("HujiaResolution", receipt);
            return false;
        }        const QVariant oldReceipt = target->getTag("HujiaResolution");
        const auto restore = qScopeGuard([&] {
            if (oldReceipt.isValid()) target->setTag("HujiaResolution", oldReceipt);
            else target->removeTag("HujiaResolution");
        });
        target->setTag("HujiaResolution", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"serial", nextYitianReceipt(room)}});
        do {
            QVariantMap receipt = target->getTag("HujiaResolution").toMap();
            receipt.insert("obtained", false);
            target->setTag("HujiaResolution", receipt);
            JudgeStruct judge;
            judge.pattern = ".|red";
            judge.good = true;
            judge.reason = objectName();
            judge.play_animation = false;
            judge.who = target;
            judge.time_consuming = true;
            room->judge(judge);

            if (!target->isAlive() || !target->getTag("HujiaResolution").toMap().value("obtained").toBool()) break;
        } while (target->askForSkillInvoke(objectName()));
        return false;
    }
};
class Shenjun : public TriggerSkillV2
{
public:
	Shenjun() : TriggerSkillV2("shenjun")
	{
		events << GameStart << EventPhaseStart << DamageInflicted;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		if (triggerEvent == GameStart)
			return TriggerList{{player, QStringList{objectName()}}};
		if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Start)
			return TriggerList{{player, QStringList{objectName()}}};
		if (triggerEvent == DamageInflicted) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature != DamageStruct::Thunder && damage.from && damage.from->isMale() != player->isMale())
				return TriggerList{{player, QStringList{objectName()}}};
		}
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{ ctx.targets = {ctx.owner}; return false; }
	bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
	{
		if (!player)
			return false;
		if (triggerEvent == GameStart) {
			room->sendCompulsoryTriggerLog(player, this);
			const QString gender = room->askForChoice(player, objectName(), "male+female");
			const bool is_male = player->isMale();
			if (gender == "female" && is_male) {
				room->setPlayerProperty(player, "avatarIcon", "luboyanf");
				player->setGender(General::Female);
			} else if (gender == "male" && !is_male) {
				room->setPlayerProperty(player, "avatarIcon", "luboyan");
				player->setGender(General::Male);
			}
			LogMessage log;
			log.type = "#ShenjunChoose";
			log.from = player;
			log.arg = gender;
			room->sendLog(log);
			return false;
		}
		if (triggerEvent == EventPhaseStart) {
			room->sendCompulsoryTriggerLog(player, this);
			LogMessage log;
			log.from = player;
			log.type = "#ShenjunFlip";
			log.arg = objectName();
			room->sendLog(log);
			if (player->isMale()) {
				room->setPlayerProperty(player, "avatarIcon", "luboyanf");
				player->setGender(General::Female);
			} else {
				room->setPlayerProperty(player, "avatarIcon", "luboyan");
				player->setGender(General::Male);
			}
			return false;
		}
		if (!ctx.original_data)
			return false;
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		LogMessage log;
		log.type = "#ShenjunProtect";
		log.to << player;
		log.from = damage.from;
		log.arg = objectName();
		room->sendLog(log);
		return true;
	}
};

class Shaoying : public TriggerSkillV2
{
public:
    Shaoying() : TriggerSkillV2("shaoying")
    { events << DamageDone << DamageComplete << EventPhaseChanging; global = true; }
    static qint64 damageEvent(Room *room)
    { return room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong(); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            for (ServerPlayer *owner : room->getAllPlayers(true)) {
                QVariantList receipts = owner->getTag("ShaoyingReceipts").toList();
                for (int i = receipts.size() - 1; i >= 0; --i)
                    if (room->historyEvent(receipts.at(i).toMap().value("damage").toLongLong())
                        .value("status").toString() != "active") receipts.removeAt(i);
                if (receipts.isEmpty()) owner->removeTag("ShaoyingReceipts");
                else owner->setTag("ShaoyingReceipts", receipts);
            }
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != DamageComplete) return false;
        const qint64 current = damageEvent(room);
        if (current <= 0) return true;
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            const QVariantList receipts = holder->getTag("ShaoyingReceipts").toList();
            for (int i = 0; i < receipts.size(); ++i) {
                const QVariantMap receipt = receipts.at(i).toMap();
                if (receipt.value("damage").toLongLong() != current || receipt.value("consumed").toBool()) continue;
                ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
                ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
                if (!owner || !target) continue;
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = owner;
                ctx.invoker = holder;
                ctx.initiator = player;
                ctx.instanceID = receipt.value("instance").toInt();
                ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
                ctx.preferredTarget = target;
                ctx.trigger_count = i;
                ctx.amount = receipt.value("amount", 1).toInt();
                ctx.current_event = event;
                ctx.is_forced = true; // Mandatory continuation of an already accepted effect.
                ctx.original_data = &data;
                ctx.extra_data = receipt;
                out << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.invoker && ctx.invoker->getTag("ShaoyingReceipts").toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DamageDone || !player || player->isChained() || damageEvent(room) <= 0) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from || !damage.from->isAlive() || !damage.from->hasSkill(objectName()) || damage.nature != DamageStruct::Fire) return {};
        for (ServerPlayer *target : room->getAlivePlayers())
            if (player->distanceTo(target) == 1) return TriggerList{{damage.from, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageComplete) return true;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (ctx.invoker->distanceTo(target) == 1) candidates << target;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@shaoying", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != DamageComplete) return false;
        QVariantList receipts = ctx.invoker->getTag("ShaoyingReceipts").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return false;
        QVariantMap consumed = ctx.extra_data.toMap();
        consumed.insert("consumed", true);
        receipts[index] = consumed;
        ctx.invoker->setTag("ShaoyingReceipts", receipts);
        if (!ctx.invoker->isAlive()) return false;
        ctx.manual_effect = true;
        ctx.choice = "judge";
        skillEffect(event, room, owner, ctx, ctx.invoker);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DamageDone) {
            const qint64 current = damageEvent(room);
            if (current <= 0) return false;
            QVariantList receipts = ctx.owner->getTag("ShaoyingReceipts").toList();
            receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"target", target->objectName()}, {"damage", current},
                {"serial", nextYitianReceipt(room)}, {"amount", getEffectiveAmount(ctx)}};
            ctx.owner->setTag("ShaoyingReceipts", receipts);
            LogMessage log;
            log.type = "#Shaoying";
            log.from = ctx.owner;
            log.to << target;
            log.arg = objectName();
            room->sendLog(log);
        } else if (ctx.choice == "judge") {
            JudgeStruct judge;
            judge.pattern = ".|red";
            judge.good = true;
            judge.reason = objectName();
            judge.who = target;
            room->judge(judge);
            if (judge.isGood()) {
                SkillContext hit = ctx;
                hit.choice = "damage";
                ServerPlayer *recipient = room->findPlayerByObjectName(ctx.extra_data.toMap().value("target").toString());
                skillEffect(event, room, owner, hit, recipient);
            }
        } else {
            room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        }
        return false;
    }
};
class Zonghuo : public TriggerSkillV2
{
public:
	Zonghuo() : TriggerSkillV2("zonghuo")
	{
		frequency = Compulsory;
		events << ChangeSlash;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		const Card *card = data.value<CardUseStruct>().card;
		return card && card->isKindOf("Slash") && !card->isKindOf("FireSlash")
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !ctx.original_data)
			return false;
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Slash") || use.card->isKindOf("FireSlash"))
			return false;
		FireSlash *fire_slash = new FireSlash(Card::SuitToBeDecided, 0);
		if (!use.card->isVirtualCard())
			fire_slash->addSubcard(use.card);
		else if (use.card->subcardsLength() > 0) {
			foreach (int id, use.card->getSubcards())
				fire_slash->addSubcard(id);
		}
		fire_slash->setSkillName("_" + objectName());
		room->sendCompulsoryTriggerLog(player, this);
		LogMessage log;
		log.type = "#Zonghuo";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);
		use.changeCard(fire_slash);
		use.setOwnedCard(fire_slash);
		*ctx.original_data = QVariant::fromValue(use);
		return false;
	}
};

class Gongmou : public TriggerSkillV2
{
public:
    Gongmou() : TriggerSkillV2("gongmou") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Finish
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "@gongmou", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList receipts = target->getTag("GongmouReceipts").toList();
        receipts << QVariantMap{{"actor", ctx.owner->objectName()}, {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"execution", ctx.executionID}, {"event", target->getRoom()->currentHistoryEventId()},
            {"serial", nextYitianReceipt(target->getRoom())},
            {"amount", getEffectiveAmount(ctx)}, {"consumed", false}};
        target->setTag("GongmouReceipts", receipts);
        target->gainMark("@conspiracy");
        return false;
    }
};

class GongmouExchange : public TriggerSkillV2
{
public:
    GongmouExchange() : TriggerSkillV2("#gongmou-exchange")
    { events << EventPhaseEnd << EventPhaseStart << EventSkillEffectFinished; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && !finished.activationRef.isValid()) consume(room, finished);
            return false;
        }
        if (event != EventPhaseStart) return false;
        // Keep consumed rows through this callback so trigger ordinals stay stable.
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            QVariantList receipts = target->getTag("GongmouReceipts").toList();
            for (int i = receipts.size() - 1; i >= 0; --i)
                if (receipts.at(i).toMap().value("consumed").toBool()
                    && room->historyEvent(receipts.at(i).toMap().value("consumed_event").toLongLong())
                        .value("status").toString() != "active") receipts.removeAt(i);
            if (receipts.isEmpty()) target->removeTag("GongmouReceipts");
            else target->setTag("GongmouReceipts", receipts);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != EventPhaseEnd || !player || player->getPhase() != Player::Draw) return true;
        const QVariantList receipts = player->getTag("GongmouReceipts").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("consumed").toBool()) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
                SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
            ctx.amount = receipt.value("amount", 1).toInt();
            ctx.trigger_count = i;
            ctx.preferredTarget = player;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.is_forced = true; // Mandatory continuation of an already accepted effect.
            ctx.extra_data = receipt;
            out << ctx;
        }
        return true;
    }
    void consume(Room *room, const SkillContext &ctx) const
    {
        if (!ctx.invoker) return;
        QVariantList receipts = ctx.invoker->getTag("GongmouReceipts").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return;
        QVariantMap receipt = ctx.extra_data.toMap();
        receipt.insert("consumed", true);
        receipt.insert("consumed_event", room->currentHistoryEventId());
        receipts[index] = receipt;
        ctx.invoker->setTag("GongmouReceipts", receipts);
        ctx.invoker->loseMark("@conspiracy");
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->getTag("GongmouReceipts").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        consume(room, ctx);
        if (!owner->isAlive() || !ctx.invoker->isAlive()) return false;
        const int count = qMin(owner->getHandcardNum(), ctx.invoker->getHandcardNum());
        if (count <= 0) return false;
        ctx.manual_effect = true;
        SkillContext first = ctx;
        first.extra_data = QVariantMap{{"from", ctx.invoker->objectName()}, {"count", count}};
        skillEffect(event, room, owner, first, owner);
        if (!first.extra_data.toMap().value("given").toBool() || !owner->isAlive() || !ctx.invoker->isAlive()) return false;
        SkillContext second = ctx;
        second.extra_data = QVariantMap{{"from", owner->objectName()}, {"count", count}};
        skillEffect(event, room, owner, second, ctx.invoker);
        if (second.extra_data.toMap().value("given").toBool()) {
            LogMessage log;
            log.type = "#GongmouExchange";
            log.from = owner;
            log.to << ctx.invoker;
            log.arg = QString::number(count);
            log.arg2 = "gongmou";
            room->sendLog(log);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantMap transfer = ctx.extra_data.toMap();
        ServerPlayer *from = room->findPlayerByObjectName(transfer.value("from").toString());
        if (!from || !from->isAlive()) return false;
        const int count = qMin(from->getHandcardNum(), transfer.value("count").toInt() * getEffectiveAmount(ctx));
        if (count <= 0) return false;
        const Card *selected = room->askForExchange(from, "gongmou", count, count, false);
        if (!selected || selected->subcardsLength() != count) return false;
        const QList<int> ids = selected->getSubcards();
        for (int id : ids) if (!from->handCards().contains(id)) return false;
        DummyCard gift(ids);
        room->giveCard(from, target, &gift, "gongmou");
        transfer.insert("given", true);
        ctx.extra_data = transfer;
        return false;
    }
};
class Lexue : public ViewAsSkillV2
{
public:
    Lexue() : ViewAsSkillV2("lexue", 1) { response_or_use = true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QVariantMap state(const ActiveSkillRequest &request) const
    {
        if (!request.initiator || !request.activationRef.isValid()) return {};
        for (const Player *holder : request.initiator->getSiblings(true))
            if (holder->objectName() == request.activationRef.ownerObjectName)
                return holder->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "lexue").toMap();
        return {};
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        ServerPlayer *holder = getUsageHolder(ctx);
        if (!holder) return false;
        const QVariantMap value = holder->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "lexue").toMap();
        return !value.value("spent").toBool() || !value.value("name").toString().isEmpty();
    }
    void addUsage(const SkillContext &ctx) const override
    {
        ServerPlayer *holder = getUsageHolder(ctx);
        if (!holder) return;
        QVariantMap value = holder->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "lexue").toMap();
        value.insert("spent", true);
        value.insert("actor", ctx.initiator->objectName());
        holder->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "lexue", value);
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.activationRef.isValid()) return false;
        const QVariantMap value = state(request);
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !value.value("spent").toBool()) return true;
        const QString name = value.value("name").toString();
        if (name.isEmpty()) return false;
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return false;
        const bool accepted = request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? card->isAvailable(request.initiator)
            : ((request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
                && Sanguosha->matchPattern(request.pattern, request.initiator, card));
        delete card;
        return accepted;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !candidate || candidate->hasFlag("using") || !request.selectedCardIds.isEmpty()) return false;
        const QVariantMap value = state(request);
        const int id = candidate->getEffectiveId();
        return !value.value("name").toString().isEmpty() && id >= 0
            && candidate->getSuit() == value.value("suit").toInt()
            && (request.initiator->handCards().contains(id) || request.initiator->getEquipsId().contains(id));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request)) return false;
        if (request.selectedCardIds.isEmpty())
            return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !state(request).value("spent").toBool();
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        if (request.selectedCardIds.isEmpty()) return proxyCard(this, request, true);
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = Sanguosha->cloneCard(state(request).value("name").toString(), material->getSuit(), material->getNumber());
        if (card) {
            card->addSubcard(material);
            card->setSkillName(objectName());
        }
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return "LexueCard";
        Card *card = Sanguosha->cloneCard(state(request).value("name").toString());
        const QString name = card ? card->getClassName() : objectName();
        delete card;
        return name;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
        const Player *candidate) const override
    {
        if (!request.selectedCardIds.isEmpty()) {
            const Card *card = createCard(request);
            const bool accepted = card && candidate && card->targetFilter(selected, candidate, request.initiator)
                && !request.initiator->isProhibited(candidate, card);
            delete card;
            return accepted;
        }
        return request.initiator && selected.isEmpty() && candidate && candidate->isAlive()
            && candidate != request.initiator && !candidate->isKongcheng();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (request.selectedCardIds.isEmpty()) return selected.length() == 1;
        const Card *card = createCard(request);
        const bool accepted = card && card->targetsFeasible(selected, request.initiator);
        delete card;
        return accepted;
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!isUsable(ctx)) return false;
        // Custom usage excludes coordinator reservation; only revealing spends the phase quota.
        if (request.selectedCardIds.isEmpty()) addUsage(ctx);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !target) return ContinueEffects;
        Room *room = actor->getRoom();
        if (ctx.choice == "obtain") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            const int id = receipt.value("card").toInt();
            ServerPlayer *from = room->getCardOwner(id);
            if (from && from->objectName() == receipt.value("from").toString()
                && room->getCardPlace(id) == Player::PlaceHand) room->obtainCard(target, id, false);
            return ContinueEffects;
        }
        if (!ctx.use_card || !ctx.use_card->isKindOf("ActiveSkillCard") || target->isKongcheng()) return ContinueEffects;
        const Card *card = room->askForCardShow(target, actor, objectName());
        if (!card || room->getCardOwner(card->getEffectiveId()) != target) return ContinueEffects;
        room->showCard(target, card->getEffectiveId());
        if (card->getTypeId() == Card::TypeBasic || card->isNDTrick()) {
            ServerPlayer *holder = getUsageHolder(ctx);
            if (!holder) return ContinueEffects;
            QVariantMap value = holder->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "lexue").toMap();
            // Snapshot the learned identity; later wrapping or movement cannot change it.
            value.insert("name", card->objectName());
            value.insert("suit", int(card->getSuit()));
            holder->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "lexue", value);
        } else {
            SkillContext obtain = ctx;
            obtain.choice = "obtain";
            obtain.extra_data = QVariantMap{{"card", card->getEffectiveId()}, {"from", target->objectName()}};
            skillEffect(obtain, actor);
        }
        return ContinueEffects;
    }
    int getEffectIndex(const ServerPlayer *, const Card *card) const override
    { return card && card->getTypeId() == Card::TypeBasic ? 2 : 3; }
};

class LexueRecord : public TriggerSkillV2
{
public:
    LexueRecord() : TriggerSkillV2("#lexue-record") { events << EventPhaseChanging << EventSkillInvoking; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.skill_name == "lexue" && accepted.bypass_cost && accepted.activationRef.isValid()
                && accepted.use_card && accepted.use_card->isKindOf("ActiveSkillCard")
                && accepted.use_card->subcardsLength() == 0) {
                const auto *skill = dynamic_cast<const Lexue *>(Sanguosha->getViewAsSkill("lexue"));
                if (skill) skill->addUsage(accepted);
            }
            return false;
        }
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (!player || (change.from != Player::Play && change.to != Player::NotActive)) return true;
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            for (int id : holder->getSkillInstanceIds("lexue")) {
                QVariantMap value = holder->getSkillInstanceStateValue("lexue", id, "lexue").toMap();
                if (change.to == Player::NotActive) holder->removeSkillInstanceStateValue("lexue", id, "lexue");
                else if (value.value("actor").toString() == player->objectName()) {
                    value.remove("spent");
                    value.remove("actor");
                    holder->setSkillInstanceStateValue("lexue", id, "lexue", value);
                }
            }
        }
        return true;
    }
};
class XunzhiViewAsSkill : public ViewAsSkillV2
{
public:
    XunzhiViewAsSkill() : ViewAsSkillV2("xunzhi") {}
    LimitScope getLimitScope() const override { return Limit_Turn; }
    YT_MIRROR_ACTIVATION
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        ActiveSkillCard *card = proxyCard(this, request, true);
        const SkillInstance *instance = request.initiator->findSkillInstance(request.activationRef.key.skillName,
            request.activationRef.key.instanceID);
        // Authoritative card creation runs before cost interception, including bypass_cost.
        card->setUserString(instance && instance->bindHead == 2 ? "secondary" : "primary");
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "XunzhiCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    EffectFlow effect(SkillContext &ctx) const override
    { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        room->doSuperLightbox("jiangboyue", objectName());
        target->drawCards(3 * getEffectiveAmount(ctx), objectName());
        if (!target->isAlive()) return ContinueEffects;
        QSet<QString> occupied;
        for (ServerPlayer *player : room->getAlivePlayers()) {
            occupied.insert(player->getGeneralName());
            if (player->getGeneral2()) occupied.insert(player->getGeneral2Name());
        }
        QStringList choices;
        for (const QString &name : Sanguosha->getLimitedGeneralNames()) {
            const General *general = Sanguosha->getGeneral(name);
            if (general && general->getKingdom() == "shu" && !occupied.contains(name)) choices << name;
        }
        if (choices.isEmpty()) return ContinueEffects;
        const QString chosen = room->askForGeneral(target, choices);
        if (!choices.contains(chosen)) return ContinueEffects;
        const SkillCard *activation = qobject_cast<const SkillCard *>(ctx.use_card);
        const bool secondary = activation && activation->getUserString() == "secondary" && target->getGeneral2();
        const QString original = secondary ? target->getGeneral2Name() : target->getGeneralName();
        QVariantList receipts = target->getTag("XunzhiTransformations").toList();
        // Store the death obligation before hero callbacks can retire the originating grant.
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"serial", nextYitianReceipt(room)},
            {"original", original}, {"transformed", chosen}, {"secondary", secondary}};
        target->setTag("XunzhiTransformations", receipts);
        room->changeHero(target, chosen, false, true, secondary);
        return ContinueEffects;
    }
};

class Xunzhi : public TriggerSkillV2
{
public:
    Xunzhi() : TriggerSkillV2("xunzhi")
    { events << EventPhaseChanging << EventSkillEffectFinished; global = true; view_as_skill = new XunzhiViewAsSkill; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        const QVariantList receipts = player->getTag("XunzhiTransformations").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("consumed").toBool()) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
            ctx.preferredTarget = player;
            ctx.trigger_count = i;
            ctx.current_event = event;
            ctx.is_forced = true; // Mandatory continuation of an already accepted effect.
            ctx.original_data = &data;
            ctx.extra_data = receipt;
            out << ctx;
        }
        return true;
    }
    void consume(Room *room, const SkillContext &ctx) const
    {
        if (!ctx.invoker) return;
        QVariantList receipts = ctx.invoker->getTag("XunzhiTransformations").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return;
        QVariantMap consumed = ctx.extra_data.toMap();
        consumed.insert("consumed", true);
        receipts[index] = consumed;
        ctx.invoker->setTag("XunzhiTransformations", receipts);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && !finished.activationRef.isValid()) consume(room, finished);
        }
        return false;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->getTag("XunzhiTransformations").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        consume(room, ctx);
        ctx.targets = {ctx.invoker};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        const bool secondary = receipt.value("secondary").toBool();
        const QString current = secondary ? target->getGeneral2Name() : target->getGeneralName();
        // Restore only the slot still occupied by this transformation, never a later unrelated hero change.
        if (current == receipt.value("transformed").toString())
            room->changeHero(target, receipt.value("original").toString(), false, false, secondary);
        room->killPlayer(target);
        return false;
    }
};
class Dongcha : public TriggerSkillV2
{
public:
    Dongcha() : TriggerSkillV2("dongcha")
    { events << EventPhaseStart << EventPhaseChanging << Death; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || (event != EventPhaseChanging && event != Death)) return false;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        if (event == Death && data.value<DeathStruct>().who != player) return false;
        const QVariantList receipts = player->getTag("DongchaVisibility").toList();
        player->removeTag("DongchaVisibility");
        QSet<QString> ownedFlags;
        for (const QVariant &value : receipts) {
            const QVariantMap receipt = value.toMap();
            const QString key = "HandcardVisible_" + receipt.value("target").toString();
            room->removePlayerMark(player, key);
            if (receipt.value("owns_flag").toBool()) ownedFlags.insert(key);
        }
        for (const QString &key : ownedFlags)
            if (player->getMark(key) == 0) room->setPlayerFlag(player, "-" + key);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Start
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@dongcha", true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        LogMessage log;
        log.type = "#ChoosePlayerWithSkill";
        log.from = ctx.owner;
        log.to << target;
        log.arg = objectName();
        room->sendLog(log, ctx.owner);
        log.type = "#InvokeSkill";
        room->sendLog(log, room->getOtherPlayers(ctx.owner, true));
        room->doAnimate(1, ctx.owner->objectName(), target->objectName(), {ctx.owner});
        const QString key = "HandcardVisible_" + target->objectName();
        QVariantList receipts = ctx.owner->getTag("DongchaVisibility").toList();
        receipts << QVariantMap{{"target", target->objectName()}, {"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"serial", nextYitianReceipt(room)}, {"owns_flag", !ctx.owner->hasFlag(key)}};
        ctx.owner->setTag("DongchaVisibility", receipts);
        // Shared visibility marks are only a projection; cleanup subtracts this effect's contribution.
        room->addPlayerMark(ctx.owner, key);
        room->setPlayerFlag(ctx.owner, key);
        room->showAllCards(target, ctx.owner);
        return false;
    }
};
class Dushi : public TriggerSkillV2
{
public:
	Dushi() : TriggerSkillV2("dushi")
	{
		events << Death;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->hasSkill(objectName()))
			return TriggerList();
		const DeathStruct death = data.value<DeathStruct>();
		ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
		return death.who == player && killer && killer != player
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		if (ctx.current_event == Death && ctx.owner && ctx.owner->hasSkillInstance(objectName(), ctx.instanceID)
		&& !ctx.owner->isSkillInvalid(objectName(), ctx.instanceID))
			return true;
		return TriggerSkillV2::isSourceAvailable(room, ctx);
	}

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        if (death.who == ctx.owner && killer && killer != ctx.owner) ctx.targets = {killer};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        target->gainMark("@collapse", getEffectiveAmount(ctx));
        const int id = room->acquireSkill(target, "benghuai");
        QVariantList grants = target->getTag("DushiGrants").toList();
        grants << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"instance", ctx.sourceRef.key.instanceID},
            {"execution", ctx.executionID}, {"event", target->getRoom()->currentHistoryEventId()},
            {"serial", nextYitianReceipt(target->getRoom())}, {"grant", id}};
        target->setTag("DushiGrants", grants);
        return false;
    }
};
class Sizhan : public TriggerSkillV2
{
public:
    Sizhan() : TriggerSkillV2("sizhan")
    { events << DamageInflicted << EventPhaseStart << EventSkillEffectFinished; frequency = Compulsory; global = true; }
    void consume(Room *room, const SkillContext &ctx) const
    {
        if (!ctx.invoker) return;
        QVariantList receipts = ctx.invoker->getTag("SizhanDebts").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return;
        QVariantMap receipt = receipts.at(index).toMap();
        receipt.insert("consumed", true);
        receipts[index] = receipt;
        ctx.invoker->setTag("SizhanDebts", receipts);
        room->removePlayerMark(ctx.invoker, "@struggle", receipt.value("amount").toInt());
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && !finished.activationRef.isValid()) consume(room, finished);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != EventPhaseStart) return false;
        if (!player || player->getPhase() != Player::Finish) return true;
        const QVariantList receipts = player->getTag("SizhanDebts").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("consumed").toBool()) continue;
            ServerPlayer *source = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!source) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = source;
            ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(source->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = ctx.sourceRef.key.instanceID;
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt;
            ctx.trigger_count = i;
            ctx.preferredTarget = player;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.is_forced = true;
            out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.invoker && ctx.invoker->getTag("SizhanDebts").toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    { return event == DamageInflicted && ownsLivingSkill(player, objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) consume(room, ctx);
        ctx.targets = {ctx.invoker};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DamageInflicted) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to != target || damage.damage <= 0 || getEffectiveAmount(ctx) <= 0) return false;
            QVariantList receipts = target->getTag("SizhanDebts").toList();
            QVariantMap debt{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
                {"activation_skill", ctx.activationRef.key.skillName}, {"activation", ctx.activationRef.key.instanceID},
                {"amount", damage.damage}, {"serial", nextYitianReceipt(room)}};
            bool merged = false;
            for (int i = 0; i < receipts.size(); ++i) {
                const QVariantMap previous = receipts.at(i).toMap();
                if (previous.value("consumed").toBool() || previous.value("activation_owner") != debt.value("activation_owner")
                    || previous.value("activation_skill") != debt.value("activation_skill")
                    || previous.value("activation") != debt.value("activation")) continue;
                debt.insert("serial", previous.value("serial"));
                debt.insert("amount", previous.value("amount").toInt() + damage.damage);
                receipts[i] = debt;
                merged = true;
                break;
            }
            if (!merged) receipts << debt;
            // Store the fixed prevented amount before mark callbacks; losing the grant cannot erase the debt.
            target->setTag("SizhanDebts", receipts);
            target->damageRevises(*ctx.original_data, -damage.damage);
            target->gainMark("@struggle", damage.damage);
            return true;
        }
        room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return false;
    }
};
class Shenli : public TriggerSkillV2
{
public:
    Shenli() : TriggerSkillV2("shenli") { events << DamageCaused; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!ownsLivingSkill(player, objectName()) || player->getPhase() != Player::Play) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from != player || !damage.to || !damage.card || !damage.card->isKindOf("Slash")) return {};
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toULongLong() == 0) return {};
        QVariantMap query{{"phase_id", phase}, {"from", player->objectName()}};
        // Prevention is not actual damage and must not spend the first-damage condition.
        while (true) {
            const QVariantMap page = room->queryActualDamage(query);
            if (!page.value("complete").toBool()) return {};
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap card = entry.toMap().value("data").toMap().value("card").toMap();
                if (card.value("classes").toStringList().contains("Slash")) return {};
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark"));
            query.insert("after", page.value("next_after"));
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.original_data) ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data || !ctx.owner) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target) return false;
        const int bonus = qMin(3, ctx.owner->getMark("@struggle")) * getEffectiveAmount(ctx);
        if (bonus > 0) {
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            damage.damage += bonus;
            *ctx.original_data = QVariant::fromValue(damage);
        }
        return false;
    }
};
class Zhenggong : public TriggerSkillV2
{
public:
	Zhenggong() : TriggerSkillV2("zhenggong")
	{
		events << TurnStart;
	}

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != player && owner->isAlive() && owner->faceUp()) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->faceUp() && ctx.owner->askForSkillInvoke(this); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {ctx.owner};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (ctx.choice == "turn-over") {
            target->turnOver();
            return false;
        }
        const int turns = getEffectiveAmount(ctx);
        for (int i = 0; i < turns && target->isAlive(); ++i)
            room->executeExtraTurn(target, {}, objectName(), ctx.sourceRef);
        if (turns > 0 && target->isAlive()) {
            SkillContext flip = ctx;
            flip.choice = "turn-over";
            skillEffect(event, room, player, flip, target);
        }
        return false;
    }
};
class TouduViewAsSkill : public ViewAsSkillV2
{
public:
	TouduViewAsSkill() : ViewAsSkillV2("toudu", 1)
	{
	}

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
			&& request.pattern == "@@toudu";
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		const Player *player = request.initiator;
		if (!player || !candidate || candidate->hasFlag("using") || !request.selectedCardIds.isEmpty())
			return false;
		const int id = candidate->getEffectiveId();
		return id >= 0 && player->handCards().contains(id) && !player->isJilei(candidate);
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1)
			return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
		return card && canSelectCard(selection, card);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		return cardSelectionFeasible(request) ? proxyCard(this, request, true) : nullptr;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "TouduCard";
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return request.initiator && selected.isEmpty() && candidate
			&& request.initiator->canSlash(candidate, nullptr, false);
	}

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}

    EffectFlow effect(SkillContext &ctx) const override
    {
        SkillContext flip = ctx;
        flip.choice = "flip";
        skillEffect(flip, ctx.invoker);
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !target)
			return ContinueEffects;
        if (ctx.choice == "flip") { target->turnOver(); return ContinueEffects; }
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("toudu");
		CardUseStruct use(slash, source, target);
        use.setOwnedCard(slash);
        source->getRoom()->useCardFromSkillEffect(use, ctx, true);
		return ContinueEffects;
	}
};

class Toudu : public TriggerSkillV2
{
public:
	Toudu() : TriggerSkillV2("toudu")
	{
		events << Damaged;
		view_as_skill = new TouduViewAsSkill;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return ownsLivingSkill(player, objectName()) && !player->faceUp() && !player->isKongcheng()
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || player->faceUp() || player->isKongcheng())
			return false;
		Room::AcceptedViewAsEffectScope continuation(room, player, objectName(), ctx);
        if (!continuation.isValid()) return false;
        room->askForUseCard(player, "@@toudu", "@toudu", -1, Card::MethodDiscard, false);
		return false;
	}
};

class YtYisheViewAsSkill : public ViewAsSkillV2
{
public:
	YtYisheViewAsSkill() : ViewAsSkillV2("ytyishe", 5)
	{
		expand_pile = "ytrice";
	}

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& (!request.initiator->isKongcheng() || !request.initiator->getPile("ytrice").isEmpty());
	}

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        const Player *actor = request.initiator;
        if (!actor || !candidate || candidate->hasFlag("using")) return false;
        const int id = candidate->getEffectiveId();
        if (id < 0 || request.selectedCardIds.contains(id)) return false;
        const QList<int> rice = actor->getPile("ytrice");
        const bool take = rice.contains(id);
        if (!take && !actor->handCards().contains(id)) return false;
        if (!request.selectedCardIds.isEmpty() && rice.contains(request.selectedCardIds.first()) != take) return false;
        return take ? request.selectedCardIds.size() < rice.size()
            : request.selectedCardIds.size() + rice.size() < 5;
    }
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.isEmpty() || request.selectedCardIds.size() > 5)
			return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		foreach (int id, request.selectedCardIds) {
			const Card *card = Sanguosha->getCard(id);
			if (!card || !canSelectCard(selection, card))
				return false;
			selection.selectedCardIds << id;
		}
		return true;
	}

	bool willThrowSelectedCards() const override
	{
		return false;
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		        if (!cardSelectionFeasible(request)) return nullptr;
        ActiveSkillCard *card = proxyCard(this, request, true);
        card->setUserString(request.initiator->getPile("ytrice").contains(request.selectedCardIds.first()) ? "take" : "store");
        return card;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "YtYisheCard";
	}

	TargetMode targetMode() const override
	{
		return NoTarget;
	}

	EffectFlow effect(SkillContext &ctx) const override
	{ skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !target || !ctx.use_card) return ContinueEffects;
        Room *room = target->getRoom();
        const QList<int> ids = ctx.use_card->getSubcards();
        const SkillCard *selection = qobject_cast<const SkillCard *>(ctx.use_card);
        if (!selection) return ContinueEffects;
        const bool take = selection->getUserString() == "take";
        if (ids.isEmpty()) return ContinueEffects;
        for (int id : ids) {
            if (take ? !actor->getPile("ytrice").contains(id) : !actor->handCards().contains(id)) return ContinueEffects;
        }
        if (!take) {
            if (target->getPile("ytrice").size() + ids.size() > 5) return ContinueEffects;
            target->addToPile("ytrice", ids);
        } else {
            DummyCard cards(ids);
            CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, actor->objectName());
            room->moveCardTo(&cards, target, Player::PlaceHand, reason, true);
        }
        return ContinueEffects;
    }
};
class YtYisheAsk : public ViewAsSkillV2
{
public:
    YtYisheAsk() : ViewAsSkillV2("ytyishe_ask", 1) { attached_lord_skill = true; expand_pile = "%ytrice"; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    const Player *provider(const ActiveSkillRequest &request) const
    {
        if (!request.initiator || !request.activationRef.isValid()) return nullptr;
        const SkillInstance *activation = nullptr;
        const QList<const Player *> players = request.initiator->getSiblings(true);
        for (const Player *holder : players)
            if (holder->objectName() == request.activationRef.ownerObjectName)
                activation = holder->findSkillInstance(objectName(), request.activationRef.key.instanceID);
        if (!activation || !activation->parentRef.isValid() || activation->parentRef.key.skillName != "ytyishe") return nullptr;
        for (const Player *holder : players)
            if (holder->objectName() == activation->parentRef.ownerObjectName && holder->isAlive()
                && holder->getValidSkillInstanceIds("ytyishe").contains(activation->parentRef.key.instanceID)) return holder;
        return nullptr;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *holder = provider(request);
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasSkill("ytyishe") && holder && !holder->getPile("ytrice").isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        const Player *holder = provider(request);
        return holder && candidate && request.selectedCardIds.isEmpty()
            && holder->getPile("ytrice").contains(candidate->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool willThrowSelectedCards() const override { return false; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Player *holder = provider(request);
        if (!holder || !cardSelectionFeasible(request)) return nullptr;
        ActiveSkillCard *card = proxyCard(this, request, true);
        card->setUserString(holder->objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "YtYisheAskCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    EffectFlow effect(SkillContext &ctx) const override
    { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.use_card || ctx.use_card->getSubcards().size() != 1) return ContinueEffects;
        Room *room = target->getRoom();
        const SkillCard *activation = qobject_cast<const SkillCard *>(ctx.use_card);
        ServerPlayer *holder = activation ? room->findPlayerByObjectName(activation->getUserString()) : nullptr;
        const int id = ctx.use_card->getSubcards().first();
        if (!holder || !holder->getPile("ytrice").contains(id)) return ContinueEffects;
        target->skillInvoked("ytyishe", 0, holder);
        room->fillAG({id});
        const auto clear = qScopeGuard([&] { room->clearAG(); });
        if (room->askForChoice(holder, objectName(), "allow+disallow") == "allow") {
            if (holder->isAlive() && target->isAlive() && holder->getPile("ytrice").contains(id)) {
                holder->peiyin("ytyishe", 2);
                room->giveCard(holder, target, Sanguosha->getCard(id), "ytyishe", true);
            }
        } else holder->peiyin("ytyishe", 3);
        return ContinueEffects;
    }
};

class YtYishe : public TriggerSkillV2
{
public:
    YtYishe() : TriggerSkillV2("ytyishe")
    {
        view_as_skill = new YtYisheViewAsSkill;
        events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill << EventLoseSkill;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == EventPhaseEnd && player->getPhase() == Player::Play) {
            const QVariantList grants = player->getTag("YtYisheAskGrants").toList();
            player->removeTag("YtYisheAskGrants");
            for (const QVariant &id : grants)
                room->detachAttachedSkill(SkillInstanceRef(player->objectName(), SkillInstanceKey("ytyishe_ask", id.toInt())));
            return true;
        }
        if ((event == EventAcquireSkill || event == EventLoseSkill)
            && data.value<SkillChangeStruct>().skillName != objectName()) return true;
        if (event == EventPhaseStart && player->getPhase() != Player::Play) return true;
        if (event == EventPhaseEnd) return true;
        // One exact parent grant per provider instance; source removal uses native child cleanup.
        for (ServerPlayer *actor : room->getAlivePlayers()) {
            if (actor->getPhase() != Player::Play || actor->hasSkill(objectName())) continue;
            QVariantList grants = actor->getTag("YtYisheAskGrants").toList();
            for (ServerPlayer *holder : room->getOtherPlayers(actor)) {
                for (int id : holder->getValidSkillInstanceIds(objectName())) {
                    const SkillInstanceRef parent(holder->objectName(), SkillInstanceKey(objectName(), id));
                    const SkillInstanceRef grant = room->attachSkillToPlayer(actor, "ytyishe_ask", parent);
                    if (grant.isValid() && !grants.contains(grant.key.instanceID)) grants << grant.key.instanceID;
                }
            }
            actor->setTag("YtYisheAskGrants", grants);
        }
        return true;
    }
};
class Xiliang : public TriggerSkillV2
{
public:
	Xiliang() : TriggerSkillV2("xiliang")
	{
		events << CardsMoveOneTime;
	}

	QList<int> redDiscards(Room *room, const CardsMoveOneTimeStruct &move) const
	{
		QList<int> ids;
		if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
			return ids;
        const qulonglong moveId = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toULongLong();
        if (moveId == 0) return ids;
        QVariantMap query{{"event_id", moveId}};
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool()) return {};
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap value = entry.toMap().value("data").toMap();
                const int id = value.value("card_id").toInt();
                if (move.card_ids.contains(id) && !ids.contains(id)
                    && value.value("to_place").toInt() == Player::DiscardPile
                    && value.value("card_before").toMap().value("red").toBool()
                    && room->getCardPlace(id) == Player::DiscardPile) ids << id;
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark"));
            query.insert("after", page.value("next_after"));
        }
		return ids;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || player->getPhase() != Player::Discard)
			return TriggerList();
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player || redDiscards(room, move).isEmpty())
			return TriggerList();
		TriggerList result;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->isAlive() && p->hasSkill(objectName()))
				result[p] << objectName();
		}
		return result;
	}

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QList<int> ids = redDiscards(room, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (ids.isEmpty()) return false;
        room->fillAG(ids, ctx.owner);
        const auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
        return ctx.owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> ids = redDiscards(room, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (ids.isEmpty()) return false;
        room->fillAG(ids, target);
        const auto clear = qScopeGuard([&] { room->clearAG(target); });
        QList<int> rice, hand;
        int capacity = qMax(0, 5 - target->getPile("ytrice").length());
        bool first = true;
        while (!ids.isEmpty()) {
            const int id = room->askForAG(target, ids, !first, objectName());
            if (!ids.removeOne(id)) break;
            first = false;
            room->takeAG(target, id, false, {target});
            if (capacity > 0 && room->askForChoice(target, objectName(), "put+obtain") == "put") {
                rice << id;
                --capacity;
            } else hand << id;
        }
        // A selected physical card may occur only once, and must still be in the discard pile.
        for (int i = rice.size() - 1; i >= 0; --i)
            if (room->getCardPlace(rice.at(i)) != Player::DiscardPile) rice.removeAt(i);
        if (!rice.isEmpty()) target->addToPile("ytrice", rice);
        for (int i = hand.size() - 1; i >= 0; --i)
            if (room->getCardPlace(hand.at(i)) != Player::DiscardPile) hand.removeAt(i);
        if (!hand.isEmpty() && target->isAlive()) {
            DummyCard cards(hand);
            target->obtainCard(&cards);
        }
        return false;
    }
};
class Zhengfeng : public AttackRangeSkillV2
{
public:
	Zhengfeng() : AttackRangeSkillV2("zhengfeng")
	{
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &) const override
	{
		return CorrectSkillResult::noEffect();
	}

	CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
	{
		const Player *target = ctx.holder;
		if (target && target->getHp() > 0 && !target->getWeapon())
			return CorrectSkillResult::useAmount(target->getHp());
		return CorrectSkillResult::noEffect();
	}
};

class YTZhenwei : public TriggerSkillV2
{
public:
	YTZhenwei() : TriggerSkillV2("ytzhenwei")
	{
		events << CardOffset;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		const CardEffectStruct effect = data.value<CardEffectStruct>();
		return effect.card && effect.card->isKindOf("Slash") && effect.offset_card
			&& room->getCardPlace(effect.offset_card->getEffectiveId()) == Player::DiscardPile
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(this, *ctx.original_data);
	}

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!ctx.owner || !ctx.original_data)
			return false;
		const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
		if (!effect.offset_card
			|| room->getCardPlace(effect.offset_card->getEffectiveId()) != Player::DiscardPile)
			return false;
		room->broadcastSkillInvoke(objectName());
		target->obtainCard(effect.offset_card);
		return false;
	}
};

class Yitian : public TriggerSkillV2
{
public:
	Yitian() : TriggerSkillV2("yitian")
	{
		events << DamageCaused;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		const DamageStruct damage = data.value<DamageStruct>();
		return damage.to && damage.to->getGeneralName().contains("caocao")
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(this, *ctx.original_data);
	}

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.original_data->value<DamageStruct>().to}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
	{
		if (!player || !ctx.original_data)
			return false;
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		LogMessage log;
		log.type = "#YitianSolace";
		log.from = player;
		log.to << damage.to;
		log.arg = QString::number(damage.damage);
		damage.damage -= getEffectiveAmount(ctx);
        log.arg2 = QString::number(damage.damage);
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		*ctx.original_data = QVariant::fromValue(damage);
		return damage.damage <= 0;
	}
};

class Taichen : public ViewAsSkillV2
{
public:
	Taichen() : ViewAsSkillV2("taichen", 1)
	{
	}

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		return request.initiator && candidate && !candidate->hasFlag("using")
			&& request.selectedCardIds.isEmpty() && candidate->isKindOf("Weapon")
			&& candidate->getEffectiveId() >= 0
			&& (request.initiator->handCards().contains(candidate->getEffectiveId())
				|| request.initiator->getEquipsId().contains(candidate->getEffectiveId()))
			&& request.initiator->canDiscard(request.initiator, candidate->getEffectiveId());
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.isEmpty())
			return true;
		if (request.selectedCardIds.size() != 1)
			return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
		return card && canSelectCard(selection, card);
	}

	bool willThrowSelectedCards() const override
	{
		return false;
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		return cardSelectionFeasible(request) ? proxyCard(this, request, false) : nullptr;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "TaichenCard";
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return request.initiator && candidate && selected.isEmpty()
			&& request.initiator->inMyAttackRange(candidate, request.selectedCardIds)
			&& request.initiator->canDiscard(candidate, "hej");
	}

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !ctx.use_card) return false;
        if (request.selectedCardIds.isEmpty()) {
            room->loseHp(HpLostStruct(ctx.initiator, 1, objectName(), ctx.initiator));
        } else {
            const int id = request.selectedCardIds.first();
            if (!ctx.initiator->canDiscard(ctx.initiator, id)
                || room->getCardOwner(id) != ctx.initiator) return false;
            room->throwCard(id, objectName(), ctx.initiator, ctx.initiator);
        }
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !target)
			return ContinueEffects;
		Room *room = source->getRoom();
        // Payment is already committed; bypass_cost must not pay it again here.
        for (int i = 0; i < 2 * getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            if (!source->canDiscard(target, "hej")) break;
            const int id = room->askForCardChosen(source, target, "hej", objectName());
            if (id < 0 || room->getCardOwner(id) != target || !source->canDiscard(target, id)) break;
            room->throwCard(id, objectName(), target, source);
        }
		return ContinueEffects;
	}
};

YitianCardPackage::YitianCardPackage()
	: Package("yitian_cards")
{
	(new YitianSword)->setParent(this);
	type = CardPack;
	skills << new YitianSwordSkill;
}

ADD_PACKAGE(YitianCard)

YitianPackage::YitianPackage()
	: Package("yitian")
{
	General *weiwudi = new General(this, "yt_shencaocao", "god", 3);
	weiwudi->addSkill(new WeiwudiGuixin);
	weiwudi->addSkill("feiying");

	General *caochong = new General(this, "yt_caochong", "wei", 3);
	caochong->addSkill(new YTChengxiang);
	caochong->addSkill(new Conghui);
	caochong->addSkill(new Zaoyao);

	General *zhangjunyi = new General(this, "zhangjunyi", "qun");
	zhangjunyi->addSkill(new Jueji);

	General *lukang = new General(this, "lukang", "wu", 4);
	lukang->addSkill(new LukangWeiyan);
	lukang->addSkill(new Kegou);

	General *jinxuandi = new General(this, "jinxuandi", "god");
	jinxuandi->addSkill(new Wuling);
	jinxuandi->addSkill(new WulingEffect);
	related_skills.insert("wuling", "#wuling-effect");

	General *xiahoujuan = new General(this, "xiahoujuan", "wei", 3, false);
	xiahoujuan->addSkill(new Lianli);
	xiahoujuan->addSkill(new LianliSlash);
	xiahoujuan->addSkill(new LianliJink);
	xiahoujuan->addSkill(new LianliClear);
	xiahoujuan->addSkill(new Tongxin);
	xiahoujuan->addSkill(new Skill("liqian", Skill::Compulsory));
	related_skills.insert("lianli", "#lianli-slash");
	related_skills.insert("lianli", "#lianli-jink");
	related_skills.insert("lianli", "#lianli-clear");

	General *caizhaoji = new General(this, "caizhaoji", "qun", 3, false);
	caizhaoji->addSkill(new Guihan);
	caizhaoji->addSkill(new CaizhaojiHujia);

	General *luboyan = new General(this, "luboyan", "wu", 3);
	luboyan->addSkill(new Shenjun);
	luboyan->addSkill(new Shaoying);
	luboyan->addSkill(new Zonghuo);

	General *zhongshiji = new General(this, "zhongshiji", "wei");
	zhongshiji->addSkill(new Gongmou);
	zhongshiji->addSkill(new GongmouExchange);
	related_skills.insert("gongmou", "#gongmou-exchange");

	General *jiangboyue = new General(this, "jiangboyue", "shu");
	jiangboyue->addSkill(new Lexue);
	jiangboyue->addSkill(new LexueRecord);
	related_skills.insert("lexue", "#lexue-record");
	jiangboyue->addSkill(new Xunzhi);

	General *jiawenhe = new General(this, "jiawenhe", "qun");
	jiawenhe->addSkill(new Dongcha);
	jiawenhe->addSkill(new Dushi);

	General *elai = new General(this, "guzhielai", "wei");
	elai->addSkill(new Sizhan);
	elai->addSkill(new Shenli);

	General *dengshizai = new General(this, "dengshizai", "wei", 3);
	dengshizai->addSkill(new Zhenggong);
	dengshizai->addSkill(new Toudu);

	General *zhanggongqi = new General(this, "zhanggongqi", "qun", 3);
	zhanggongqi->addSkill(new YtYishe);
	zhanggongqi->addSkill(new Xiliang);

	General *yitianjian = new General(this, "yitianjian", "wei");
	yitianjian->addSkill(new Zhengfeng);
	yitianjian->addSkill(new YTZhenwei);
	yitianjian->addSkill(new Yitian);

	General *panglingming = new General(this, "panglingming", "wei");
	panglingming->addSkill(new Taichen);

	skills << new LianliSlashViewAsSkill << new YtYisheAsk;
	addMetaObject<LianliSlashCard>();
}

ADD_PACKAGE(Yitian)
