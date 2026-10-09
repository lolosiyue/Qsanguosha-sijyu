#include "tenshi-reihou.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "skill-instance-types.h"
#include "standard.h"
#include "maneuvering.h"
#include "h-strategic-advantage.h"

namespace {

QString reihouChangedSkill(const QVariant &data)
{
	SkillChangeStruct change;
	if (change.tryParse(data))
		return change.skillName;
	return data.toString();
}

class RhDuanlongViewAs : public ViewAsSkillV2
{
public:
	RhDuanlongViewAs() : ViewAsSkillV2("rhduanlong") {}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.activationRef.isValid();
	}

	TargetMode targetMode() const override { return NoTarget; }

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.isEmpty();
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		if (ctx.invoker)
			ctx.invoker->getRoom()->removeReihouCard(ctx.invoker);
		return FinishSkill;
	}
};

class RhDuanlong : public TriggerSkillV2
{
public:
	RhDuanlong() : TriggerSkillV2("rhduanlong")
	{
		events << EventLoseSkill;
		view_as_skill = new RhDuanlongViewAs;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventLoseSkill || !room || !player || reihouChangedSkill(data) != objectName())
			return false;
		QList<ServerPlayer *> victims;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (player->canDiscard(p, "e"))
				victims << p;
		}
		if (victims.isEmpty())
			return true;
		ServerPlayer *victim = room->askForPlayerChosen(player, victims, objectName(), "@rhduanlong", true, true);
		if (!victim)
			return true;
		const int firstId = room->askForCardChosen(player, victim, "e", objectName(), false, Card::MethodDiscard);
		if (firstId < 0)
			return true;
		QList<int> ids;
		ids << firstId;
		bool another = false;
		foreach (const Card *card, victim->getCards("e")) {
			const int id = card->getEffectiveId();
			if (id != firstId && player->canDiscard(victim, id)) {
				another = true;
				break;
			}
		}
		if (another) {
			const int secondId = room->askForCardChosen(player, victim, "e", objectName(), false,
				Card::MethodDiscard, QList<int>() << firstId);
			if (secondId >= 0 && secondId != firstId)
				ids << secondId;
		}
		room->throwCard(ids, objectName(), victim, player);
		return true;
	}
};

class RhPohuang : public TriggerSkillV2
{
public:
	RhPohuang() : TriggerSkillV2("rhpohuang") { events << Damaged; }

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		const DamageStruct damage = data.value<DamageStruct>();
		if (damage.from && damage.from->isAlive() && player->canSlash(damage.from, false))
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !player->askForSkillInvoke(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !ctx.original_data)
			return false;
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (!damage.from || damage.from->isDead())
			return false;
		if (room->askForUseSlashTo(player, damage.from, "@rhpohuang:" + damage.from->objectName(), false))
			return false;
		Card *slash = Sanguosha->cloneCard("slash");
		if (!slash)
			return false;
		slash->setSkillName("_rhpohuang");
		slash->deleteLater();
		if (!player->canSlash(damage.from, slash, false))
			return false;
		room->removeReihouCard(player);
		room->useCard(CardUseStruct(slash, player, damage.from));
		return false;
	}
};

class RhRuyi : public ViewAsSkillV2
{
public:
	RhRuyi() : ViewAsSkillV2("rhruyi") { setResponseOrUse(true); }

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::guhuo(objectName());
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || !request.activationRef.isValid())
			return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
			return true;
		if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return false;
		if (request.pattern.startsWith('.') || request.pattern.startsWith('@'))
			return false;
		if (request.pattern == "peach" && request.initiator->getMark("Global_PreventPeach") > 0)
			return false;
		foreach (const QChar &ch, request.pattern) {
			if (ch.isUpper() || ch.isDigit())
				return false;
		}
		return !usableNames(request).isEmpty();
	}

	bool isEnabledAtNullification(const ServerPlayer *) const override { return true; }

	EffectFlow effect(SkillContext &ctx) const override
	{
		if (ctx.invoker)
			ctx.invoker->getRoom()->removeReihouCard(ctx.invoker);
		return ContinueEffects;
	}
};

QStringList reihouHuanjieNames()
{
	QStringList names;
	foreach (const Card *card, Sanguosha->findChildren<const Card *>()) {
		if (!card || card->getTypeId() == Card::TypeSkill || card->getTypeId() == Card::TypeEquip)
			continue;
		QString name = card->objectName();
		if (name.isEmpty() || name.startsWith('_'))
			continue;
		if (name.endsWith("slash"))
			name = "slash";
		const bool accepted = card->getTypeId() == Card::TypeBasic || card->isKindOf("SingleTargetTrick") || card->isNDTrick();
		if (accepted && !names.contains(name))
			names << name;
	}
	names.sort();
	return names;
}

class RhHuanjieViewAs : public ViewAsSkillV2
{
public:
	RhHuanjieViewAs() : ViewAsSkillV2("rhhuanjie", 1) { setResponseOrUse(true); }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || !request.activationRef.isValid() || player->getMark(objectName()) > 0
			|| !player->canDiscard(player, "he"))
			return false;
		const QString name = player->property("rhhuanjie").toString();
		if (name.isEmpty())
			return false;
		Card *card = Sanguosha->cloneCard(name);
		if (!card)
			return false;
		card->setSkillName(objectName());
		bool accepted = false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
			accepted = card->isAvailable(player);
		else if (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			accepted = Sanguosha->matchPattern(request.pattern, player, card);
		delete card;
		return accepted;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || request.selectedCardIds.size() >= 1)
			return false;
		const int id = candidate->getEffectiveId();
		if (id < 0 || candidate->getSuit() != Card::Spade)
			return false;
		const bool owned = request.initiator->handCards().contains(id)
			|| request.initiator->getEquipsId().contains(id)
			|| request.initiator->getHandPile().contains(id);
		return owned && request.initiator->canDiscard(request.initiator, id)
			&& !request.selectedCardIds.contains(id);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request) || !request.initiator)
			return nullptr;
		const QString name = request.initiator->property("rhhuanjie").toString();
		Card *card = Sanguosha->cloneCard(name);
		if (!card)
			return nullptr;
		card->setSkillName(objectName());
		card->setCanRecast(false);
		return card;
	}

	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		ServerPlayer *player = ctx.initiator;
		if (!room || !player || request.selectedCardIds.length() != 1)
			return false;
		const int id = request.selectedCardIds.first();
		if (!player->canDiscard(player, id))
			return false;
		room->throwCard(QList<int>() << id, objectName(), player, player);
		if (player->isAlive())
			room->addPlayerMark(player, objectName());
		return true;
	}

	bool isEnabledAtNullification(const ServerPlayer *player) const override
	{
		return player && player->getMark(objectName()) <= 0 && player->canDiscard(player, "he")
			&& player->property("rhhuanjie").toString() == "nullification";
	}
};

class RhHuanjie : public TriggerSkillV2
{
public:
	RhHuanjie() : TriggerSkillV2("rhhuanjie")
	{
		events << EventAcquireSkill << EventLoseSkill << EventPhaseChanging;
		view_as_skill = new RhHuanjieViewAs;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room)
			return false;
		if (event == EventPhaseChanging) {
			const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive) {
				foreach (ServerPlayer *p, room->getAlivePlayers())
					room->setPlayerMark(p, objectName(), 0);
			}
			return false;
		}
		if (event == EventLoseSkill && player && reihouChangedSkill(data) == objectName())
			room->setPlayerProperty(player, "rhhuanjie", QVariant());
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventAcquireSkill && player && player->isAlive() && player->hasSkill(objectName())
			&& reihouChangedSkill(data) == objectName())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		const QStringList names = reihouHuanjieNames();
		if (!player || names.isEmpty())
			return false;
		room->sendCompulsoryTriggerLog(player, objectName());
		const QString chosen = room->askForChoice(player, objectName(), names.join("+"));
		if (!names.contains(chosen))
			return false;
		LogMessage log;
		log.type = "#RhHuanjie";
		log.from = player;
		log.arg = chosen;
		room->sendLog(log);
		room->setPlayerProperty(player, "rhhuanjie", chosen);
		return false;
	}
};

class RhHonghuang : public ProhibitSkill
{
public:
	RhHonghuang() : ProhibitSkill("rhhonghuang") {}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
	{
		return from && to && card && from->getPhase() != Player::NotActive && to->hasSkill(objectName())
			&& card->isKindOf("TrickCard") && card->isBlack();
	}
};

class RhHonghuangTargetMod : public TargetModSkillV2
{
public:
	RhHonghuangTargetMod() : TargetModSkillV2("#rhhonghuang", "TrickCard") {}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || !ctx.card
			|| !ctx.card->isKindOf("TrickCard") || ctx.primary->getPhase() == Player::NotActive)
			return CorrectSkillResult::noEffect();
		if (ctx.primary->hasSkill("rhhonghuang"))
			return CorrectSkillResult::useAmount(1000);
		foreach (const Player *p, ctx.primary->getAliveSiblings()) {
			if (p->hasSkill("rhhonghuang"))
				return CorrectSkillResult::useAmount(1000);
		}
		return CorrectSkillResult::noEffect();
	}
};

class RhLiufu : public TriggerSkillV2
{
public:
	RhLiufu() : TriggerSkillV2("rhliufu") { events << TargetConfirmed; }

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.card && (use.card->isKindOf("BasicCard") || use.card->isNDTrick()) && use.to.contains(player))
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !player->askForSkillInvoke(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !ctx.original_data)
			return false;
		room->removeReihouCard(player);
		QStringList choices;
		choices << "null" << "give";
		QString choice = room->askForChoice(player, objectName(), choices.join("+"), *ctx.original_data);
		while (choice != "cancel") {
			if (!choices.contains(choice))
				break;
			choices.removeOne(choice);
			CardUseStruct use = ctx.original_data->value<CardUseStruct>();
			if (!use.card)
				break;
			if (choice == "null") {
				LogMessage log;
				log.type = "#RhLiufu1";
				log.from = player;
				log.arg = "1";
				log.arg2 = use.card->objectName();
				room->sendLog(log);
				use.nullified_list << player->objectName();
				*ctx.original_data = QVariant::fromValue(use);
			} else if (choice == "give") {
				LogMessage log;
				log.type = "#RhLiufu2";
				log.from = player;
				log.arg = "2";
				room->sendLog(log);
				room->setCardFlag(use.card, "rhliufu");
				room->setTag("rhliufu_" + use.card->toString(), QVariant::fromValue(player));
			}
			if (!choices.contains("cancel"))
				choices << "cancel";
			if (choices.size() == 1)
				break;
			choice = room->askForChoice(player, objectName(), choices.join("+"), *ctx.original_data);
		}
		return false;
	}
};

class RhLiufuGet : public TriggerSkillV2
{
public:
	RhLiufuGet() : TriggerSkillV2("#rhliufu")
	{
		events << BeforeCardsMove;
		frequency = Compulsory;
	}

	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (!room || !data.canConvert<CardsMoveOneTimeStruct>())
			return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!move.reason.m_extraData.canConvert<const Card *>())
			return false;
		const Card *card = move.reason.m_extraData.value<const Card *>();
		if (!card || !card->hasFlag("rhliufu") || move.to_place != Player::DiscardPile
			|| !move.from_places.contains(Player::PlaceTable))
			return false;
		ServerPlayer *player = room->getTag("rhliufu_" + card->toString()).value<ServerPlayer *>();
		room->removeTag("rhliufu_" + card->toString());
		room->setCardFlag(card, "-rhliufu");
		if (!player)
			return true;
		QList<int> ids;
		for (int i = 0; i < move.card_ids.length(); ++i) {
			const int id = move.card_ids.at(i);
			if ((card->getSubcards().contains(id) || card->getEffectiveId() == id)
				&& i < move.from_places.length() && move.from_places.at(i) == Player::PlaceTable)
				ids << id;
		}
		if (ids.isEmpty())
			return true;
		room->sendCompulsoryTriggerLog(player, "rhliufu");
		move.removeCardIds(ids);
		data = QVariant::fromValue(move);
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAllPlayers(), "rhliufu", "@rhliufu", false, true);
		if (!target)
			return true;
		DummyCard dummy(ids);
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "rhliufu", QString());
		room->obtainCard(target, &dummy, reason);
		return true;
	}
};

class RhPujiu : public ViewAsSkillV2
{
public:
	RhPujiu() : ViewAsSkillV2("rhpujiu", 1) { setResponseOrUse(true); }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || !request.activationRef.isValid())
			return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
			Card *peach = Sanguosha->cloneCard("peach");
			if (!peach)
				return false;
			peach->setSkillName(objectName());
			const bool available = peach->isAvailable(player);
			delete peach;
			return available;
		}
		if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
			&& request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return false;
		return request.pattern.contains("peach") && player->getMark("Global_PreventPeach") == 0;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || request.selectedCardIds.size() >= 1)
			return false;
		const int id = candidate->getEffectiveId();
		return id >= 0 && request.initiator->handCards().contains(id) && !request.selectedCardIds.contains(id);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request))
			return nullptr;
		Card *peach = Sanguosha->cloneCard("peach");
		if (!peach)
			return nullptr;
		peach->setSkillName(objectName());
		peach->addSubcards(request.selectedCardIds);
		return peach;
	}

	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		ServerPlayer *player = ctx.initiator ? ctx.initiator : ctx.invoker;
		if (!room || !player)
			return false;
		room->addPlayerMark(player, objectName());
		return true;
	}
};

class RhPujiuTrigger : public TriggerSkillV2
{
public:
	RhPujiuTrigger() : TriggerSkillV2("#rhpujiu")
	{
		events << CardUsed << EventLoseSkill;
		frequency = Compulsory;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventLoseSkill && room && player && reihouChangedSkill(data) == "rhpujiu")
			room->setPlayerMark(player, "rhpujiu", 0);
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != CardUsed || !player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Peach") || use.card->getSkillName() != "rhpujiu")
			return TriggerList();
		if (player->getMark("rhpujiu") != player->getHp())
			return TriggerList();
		return TriggerList{{player, QStringList(objectName())}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !player->isAlive())
			return false;
		room->sendCompulsoryTriggerLog(player, "rhpujiu");
		room->removeReihouCard(player);
		if (player->isAlive())
			player->throwAllHandCards();
		return false;
	}
};

class RhXuesha : public TriggerSkillV2
{
public:
	RhXuesha() : TriggerSkillV2("rhxuesha")
	{
		events << DamageInflicted << DamageDone << TargetSpecified;
		frequency = Compulsory;
	}

	// Count damage taken this turn; the mark resets at NotActive through "-Clear".
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == DamageDone && room && player && player->hasSkill(objectName()))
			room->addPlayerMark(player, "rhxuesha_damaged-Clear");
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == DamageDone || !player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		if (event == DamageInflicted)
			return player->getMark("rhxuesha_damaged-Clear") > 0 && player->isWounded()
				? TriggerList{{player, QStringList(objectName())}} : TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.card && use.card->isKindOf("Slash") && use.card->isRed())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room)
			return false;
		room->sendCompulsoryTriggerLog(player, objectName());
		if (event == DamageInflicted) {
			room->broadcastSkillInvoke(objectName());
			return true;
		}
		if (!ctx.original_data)
			return false;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.card)
			return false;
		QVariantList jinkList = player->getTag("Jink_" + use.card->toString()).toList();
		int index = 0;
		foreach (ServerPlayer *p, use.to) {
			LogMessage log;
			log.type = "#NoJink";
			log.from = p;
			room->sendLog(log);
			if (index < jinkList.length())
				jinkList.replace(index, QVariant(0));
			++index;
		}
		player->setTag("Jink_" + use.card->toString(), jinkList);
		return false;
	}
};

class RhXueshaTargetMod : public TargetModSkillV2
{
public:
	RhXueshaTargetMod() : TargetModSkillV2("#rhxuesha", "Slash|black") {}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || !ctx.card
			|| !ctx.card->isKindOf("Slash") || !ctx.card->isBlack() || !ctx.primary->hasSkill("rhxuesha"))
			return CorrectSkillResult::noEffect();
		return CorrectSkillResult::useAmount(1000);
	}
};

void reihouClearZhenyao(Room *room, ServerPlayer *player, const Card *card)
{
	if (!room || !player || !card)
		return;
	QStringList tags = player->getTag("RhZhenyao").toStringList();
	if (!tags.removeOne(card->toString()))
		return;
	if (tags.isEmpty())
		player->removeTag("RhZhenyao");
	else
		player->setTag("RhZhenyao", tags);
	if (player->getMark("@repression") > 0)
		room->removePlayerMark(player, "@repression");
}

class RhZhenyao : public TriggerSkillV2
{
public:
	RhZhenyao() : TriggerSkillV2("rhzhenyao")
	{
		events << TargetSpecified << TargetConfirmed << EventPhaseChanging << FinishJudge
			<< PostCardEffected << CardFinished;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room)
			return false;
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to == Player::NotActive) {
				foreach (ServerPlayer *p, room->getAlivePlayers())
					room->setPlayerMark(p, objectName(), 0);
			}
			return false;
		}
		if (event == FinishJudge) {
			if (!player || !player->hasFlag("rhzhenyao"))
				return false;
			room->setPlayerFlag(player, "-rhzhenyao");
			JudgeStruct *judge = data.value<JudgeStruct *>();
			if (judge && judge->card && judge->card->isBlack()) {
				const CardUseStruct use = player->getTag("RhZhenyaoUse").value<CardUseStruct>();
				player->removeTag("RhZhenyaoUse");
				if (use.card) {
					QStringList tags = player->getTag("RhZhenyao").toStringList();
					tags << use.card->toString();
					player->setTag("RhZhenyao", tags);
					room->addPlayerMark(player, "@repression");
				}
			}
			return false;
		}
		if (event == PostCardEffected) {
			reihouClearZhenyao(room, player, data.value<CardEffectStruct>().card);
			return false;
		}
		if (event == CardFinished) {
			const CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card)
				return false;
			foreach (ServerPlayer *p, room->getAllPlayers(true))
				reihouClearZhenyao(room, p, use.card);
		}
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != TargetSpecified && event != TargetConfirmed)
			return TriggerList();
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getMark(objectName()) != 0)
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Slash"))
			return TriggerList();
		if (event == TargetConfirmed && !use.to.contains(player))
			return TriggerList();
		return TriggerList{{player, QStringList(objectName())}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !player->askForSkillInvoke(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, objectName());
		return true;
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		if (event == TargetConfirmed) {
			room->setPlayerFlag(player, "rhzhenyao");
			player->setTag("RhZhenyaoUse", *ctx.original_data);
		}
		JudgeStruct judge;
		judge.pattern = ".|black";
		judge.good = true;
		judge.reason = objectName();
		judge.who = player;
		room->judge(judge);
		if (event == TargetConfirmed && player->hasFlag("rhzhenyao")) {
			room->setPlayerFlag(player, "-rhzhenyao");
			player->removeTag("RhZhenyaoUse");
		}
		if (judge.isBad() || event != TargetSpecified)
			return false;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.card)
			return false;
		QVariantList jinkList = player->getTag("Jink_" + use.card->toString()).toList();
		int index = 0;
		foreach (ServerPlayer *p, use.to) {
			LogMessage log;
			log.type = "#NoJink";
			log.from = p;
			room->sendLog(log);
			if (index < jinkList.length())
				jinkList.replace(index, QVariant(0));
			++index;
		}
		player->setTag("Jink_" + use.card->toString(), jinkList);
		return false;
	}
};

class RhZhenyaoPrevent : public TriggerSkillV2
{
public:
	RhZhenyaoPrevent() : TriggerSkillV2("#rhzhenyao")
	{
		events << DamageForseen << PreHpLost;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (player && player->isAlive() && player->hasSkill(objectName()) && player->getMark("@repression") > 0)
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		room->sendCompulsoryTriggerLog(player, "rhzhenyao");
		room->broadcastSkillInvoke("rhzhenyao");
		return true;
	}
};

class RhGaiming : public ViewAsSkillV2
{
public:
	RhGaiming() : ViewAsSkillV2("rhgaiming") {}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid()
			&& request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed(objectName());
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *to) const override
	{
		if (!request.initiator || !to || !selected.isEmpty())
			return false;
		QList<const Player *> players = request.initiator->getAliveSiblings();
		players << request.initiator;
		int maxHp = -1000;
		int minHp = 1000;
		foreach (const Player *p, players) {
			if (maxHp < p->getHp())
				maxHp = p->getHp();
			if (minHp > p->getHp())
				minHp = p->getHp();
		}
		return to->getHp() == maxHp || (to->getHp() == minHp && to->isWounded());
	}

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *from = ctx.invoker;
		Room *room = target ? target->getRoom() : nullptr;
		if (!from || !room || !target->isAlive())
			return ContinueEffects;
		int maxHp = -1000;
		int minHp = 1000;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (maxHp < p->getHp())
				maxHp = p->getHp();
			if (minHp > p->getHp())
				minHp = p->getHp();
		}
		QStringList choices;
		if (target->getHp() == maxHp)
			choices << "lose";
		if (target->getHp() == minHp && target->isWounded())
			choices << "recover";
		if (choices.isEmpty())
			return ContinueEffects;
		QString choice = choices.first();
		if (choices.length() != 1)
			choice = room->askForChoice(from, objectName(), choices.join("+"));
		if (choice == "lose")
			room->loseHp(target);
		else if (choice == "recover" && target->isAlive())
			room->recover(target, RecoverStruct(from));
		return ContinueEffects;
	}
};

class RhXusheng : public TriggerSkillV2
{
public:
	RhXusheng() : TriggerSkillV2("rhxusheng") { events << Dying; }

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		const DyingStruct dying = data.value<DyingStruct>();
		if (!dying.who || dying.who->getHp() > 0 || dying.who->isDead())
			return TriggerList();
		return TriggerList{{player, QStringList(objectName())}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !ctx.original_data || !player->askForSkillInvoke(objectName(), *ctx.original_data))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->removeReihouCard(player);
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		const DyingStruct dying = ctx.original_data->value<DyingStruct>();
		if (!dying.who || !dying.who->isAlive())
			return false;
		const int amount = 1 - dying.who->getHp();
		if (amount > 0)
			room->recover(dying.who, RecoverStruct(player, nullptr, amount));
		return false;
	}
};

class RhBaiming : public TriggerSkillV2
{
public:
	RhBaiming() : TriggerSkillV2("rhbaiming")
	{
		events << EventAcquireSkill << EventLoseSkill << Damaged;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventLoseSkill || !room || !player || reihouChangedSkill(data) != objectName())
			return false;
		const QString name = player->getTag(objectName()).toString();
		player->removeTag(objectName());
		ServerPlayer *marked = room->findPlayerByObjectName(name);
		if (marked)
			room->setPlayerMark(marked, "@filth", 0);
		return true;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !room)
			return TriggerList();
		if (event == Damaged) {
			if (!player->isAlive())
				return TriggerList();
			TriggerList list;
			foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
				if (holder->getTag(objectName()).toString() == player->objectName())
					list.insert(holder, QStringList(objectName()));
			}
			return list;
		}
		if (event == EventAcquireSkill && player->isAlive() && player->hasSkill(objectName())
			&& reihouChangedSkill(data) == objectName())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == EventAcquireSkill)
			return true;
		if (!player || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room)
			return false;
		if (event == EventAcquireSkill) {
			const QList<ServerPlayer *> others = room->getOtherPlayers(player);
			if (others.isEmpty())
				return false;
			ServerPlayer *target = room->askForPlayerChosen(player, others, objectName(), "@rhbaiming", false, true);
			if (!target)
				return false;
			room->broadcastSkillInvoke(objectName());
			player->setTag(objectName(), target->objectName());
			room->addPlayerMark(target, "@filth");
			return false;
		}
		ServerPlayer *victim = ctx.invoker;
		if (!victim)
			return false;
		if (player->canDiscard(player, "he"))
			room->askForDiscard(player, objectName(), 1, 1, false, true);
		if (!victim->isNude()) {
			if (player->canDiscard(victim, "he")) {
				const int id = room->askForCardChosen(player, victim, "he", objectName(), false, Card::MethodDiscard);
				if (id >= 0)
					room->throwCard(id, victim, player);
			}
		} else
			room->loseHp(victim);
		return false;
	}
};

bool reihouHolds(const Player *player, int id)
{
	return player && id >= 0 && (player->handCards().contains(id) || player->getEquipsId().contains(id)
		|| player->getHandPile().contains(id));
}

Card::Suit reihouPileSuit(const Player *player, const QString &pile)
{
	if (!player)
		return Card::SuitToBeDecided;
	const QList<int> ids = player->getPile(pile);
	if (ids.isEmpty())
		return Card::SuitToBeDecided;
	const Card *card = Sanguosha->getEngineCard(ids.first());
	return card ? card->getSuit() : Card::SuitToBeDecided;
}

void reihouLockJinks(Room *room, ServerPlayer *player, const CardUseStruct &use)
{
	if (!room || !player || !use.card)
		return;
	QVariantList jinkList = player->getTag("Jink_" + use.card->toString()).toList();
	int index = 0;
	foreach (ServerPlayer *p, use.to) {
		LogMessage log;
		log.type = "#NoJink";
		log.from = p;
		room->sendLog(log);
		if (index < jinkList.length())
			jinkList.replace(index, QVariant(0));
		++index;
	}
	player->setTag("Jink_" + use.card->toString(), jinkList);
}

class RhChengfeng : public ViewAsSkillV2
{
public:
	RhChengfeng() : ViewAsSkillV2("rhchengfeng", 1) { setResponseOrUse(true); }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || !request.activationRef.isValid())
			return false;
		if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
			&& request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return false;
		Card *jink = Sanguosha->cloneCard("jink");
		if (!jink)
			return false;
		jink->setSkillName(objectName());
		const bool matched = Sanguosha->matchPattern(request.pattern, player, jink);
		delete jink;
		return matched;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!candidate || request.selectedCardIds.size() >= 1)
			return false;
		const int id = candidate->getEffectiveId();
		return reihouHolds(request.initiator, id) && candidate->isKindOf("Slash") && candidate->isBlack()
			&& !request.selectedCardIds.contains(id);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request))
			return nullptr;
		const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
		if (!original)
			return nullptr;
		Jink *jink = new Jink(original->getSuit(), original->getNumber());
		jink->addSubcard(original);
		jink->setSkillName(objectName());
		return jink;
	}
};

class RhYuhuo : public ViewAsSkillV2
{
public:
	RhYuhuo() : ViewAsSkillV2("rhyuhuo", 1) { setResponseOrUse(true); }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || !request.activationRef.isValid())
			return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
			return Slash::IsAvailable(player);
		if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
			&& request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return false;
		Card *slash = Sanguosha->cloneCard("fire_slash");
		if (!slash)
			return false;
		slash->setSkillName(objectName());
		const bool matched = Sanguosha->matchPattern(request.pattern, player, slash);
		delete slash;
		return matched;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!candidate || request.selectedCardIds.size() >= 1)
			return false;
		const int id = candidate->getEffectiveId();
		return reihouHolds(request.initiator, id) && candidate->isKindOf("BasicCard") && candidate->isRed()
			&& !request.selectedCardIds.contains(id);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request))
			return nullptr;
		const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
		if (!original)
			return nullptr;
		FireSlash *slash = new FireSlash(original->getSuit(), original->getNumber());
		slash->addSubcard(original);
		slash->setSkillName(objectName());
		return slash;
	}
};

class RhZhengyang : public TriggerSkillV2
{
public:
	RhZhengyang() : TriggerSkillV2("rhzhengyang")
	{
		events << EventAcquireSkill << EventLoseSkill << TargetSpecified;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventLoseSkill || !room || !player || reihouChangedSkill(data) != objectName())
			return false;
		const QList<int> pile = player->getPile("yang");
		if (!pile.isEmpty()) {
			room->sendCompulsoryTriggerLog(player, objectName());
			room->broadcastSkillInvoke(objectName());
			room->obtainCard(player, pile.first());
		}
		return true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		if (event == EventAcquireSkill)
			return reihouChangedSkill(data) == objectName() && !player->isKongcheng()
				? TriggerList{{player, QStringList(objectName())}} : TriggerList();
		if (event != TargetSpecified || player->getPile("yang").isEmpty())
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.card && use.card->isKindOf("Slash") && use.card->getSuit() == reihouPileSuit(player, "yang"))
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		if (event == EventAcquireSkill) {
			const Card *card = room->askForCard(player, ".", "@rhzhengyang", QVariant(), Card::MethodNone);
			if (!card || card->getEffectiveId() < 0)
				return false;
			player->setTag("RhZhengyangCard", card->getEffectiveId());
		}
		room->sendCompulsoryTriggerLog(player, objectName());
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room)
			return false;
		if (event == EventAcquireSkill) {
			if (!player->getTag("RhZhengyangCard").isValid())
				return false;
			const int id = player->getTag("RhZhengyangCard").toInt();
			player->removeTag("RhZhengyangCard");
			if (id >= 0)
				player->addToPile("yang", id);
			return false;
		}
		if (ctx.original_data)
			reihouLockJinks(room, player, ctx.original_data->value<CardUseStruct>());
		return false;
	}
};

class RhZhengyangTrigger : public TriggerSkillV2
{
public:
	RhZhengyangTrigger() : TriggerSkillV2("#rhzhengyang")
	{
		events << TrickCardCanceling;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
	{
		const CardEffectStruct effect = data.value<CardEffectStruct>();
		if (!effect.from || !effect.from->isAlive() || !effect.card || !effect.from->hasSkill("rhzhengyang"))
			return TriggerList();
		if (effect.from->getPile("yang").isEmpty() || effect.card->getSuit() != reihouPileSuit(effect.from, "yang"))
			return TriggerList();
		return TriggerList{{effect.from, QStringList(objectName())}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (player)
			room->notifySkillInvoked(player, "rhzhengyang");
		return true;
	}
};

class RhZhengyangProhibit : public ProhibitSkill
{
public:
	RhZhengyangProhibit() : ProhibitSkill("#rhzhengyang-prohibit") {}

	bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const override
	{
		if (!to || !card || !to->hasSkill("rhzhengyang") || to->getPile("yang").isEmpty())
			return false;
		return card->getSuit() == reihouPileSuit(to, "yang");
	}
};

class RhChunyin : public TriggerSkillV2
{
public:
	RhChunyin() : TriggerSkillV2("rhchunyin")
	{
		events << EventAcquireSkill << EventLoseSkill << CardUsed;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventLoseSkill || !room || !player || reihouChangedSkill(data) != objectName())
			return false;
		const QList<int> pile = player->getPile("yin");
		if (!pile.isEmpty()) {
			room->sendCompulsoryTriggerLog(player, objectName());
			room->broadcastSkillInvoke(objectName());
			room->obtainCard(player, pile.first());
		}
		return true;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardUsed) {
			const CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card || !room)
				return TriggerList();
			TriggerList list;
			foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
				if (holder->getPile("yin").isEmpty())
					continue;
				if (use.card->getSuit() == reihouPileSuit(holder, "yin"))
					list.insert(holder, QStringList(objectName()));
			}
			return list;
		}
		if (event == EventAcquireSkill && player && player->isAlive() && player->hasSkill(objectName())
			&& reihouChangedSkill(data) == objectName())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room)
			return false;
		if (event == EventAcquireSkill) {
			const Card *card = room->askForCard(player, ".", "@rhchunyin", QVariant(), Card::MethodNone);
			if (!card || card->getEffectiveId() < 0)
				return false;
			player->setTag("RhChunyinCard", card->getEffectiveId());
			room->sendCompulsoryTriggerLog(player, objectName());
			room->broadcastSkillInvoke(objectName());
			return true;
		}
		if (event != CardUsed || !ctx.original_data)
			return false;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (use.from == player) {
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
				"@rhchunyin-draw", true, true);
			if (!target)
				return false;
			room->broadcastSkillInvoke(objectName());
			player->setTag("RhChunyinTarget", target->objectName());
			return true;
		}
		if (!player->askForSkillInvoke(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room)
			return false;
		if (event == EventAcquireSkill) {
			if (!player->getTag("RhChunyinCard").isValid())
				return false;
			const int id = player->getTag("RhChunyinCard").toInt();
			player->removeTag("RhChunyinCard");
			if (id >= 0)
				player->addToPile("yin", id);
			return false;
		}
		if (!ctx.original_data)
			return false;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (use.from == player) {
			ServerPlayer *target = room->findPlayerByObjectName(player->getTag("RhChunyinTarget").toString());
			player->removeTag("RhChunyinTarget");
			if (target)
				room->drawCards(target, 1, objectName());
		} else
			room->drawCards(player, 1, objectName());
		return false;
	}
};

class RhSanmei : public TriggerSkillV2
{
public:
	RhSanmei() : TriggerSkillV2("rhsanmei")
	{
		events << ConfirmDamage << DamageInflicted;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const DamageStruct damage = data.value<DamageStruct>();
		if (event == ConfirmDamage) {
			if (!room || !damage.card || damage.card->getTypeId() == Card::TypeSkill || !damage.card->isRed()
				|| damage.nature == DamageStruct::Fire)
				return TriggerList();
			TriggerList list;
			foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName()))
				list.insert(holder, QStringList(objectName()));
			return list;
		}
		if (player && player->isAlive() && player->hasSkill(objectName()) && damage.nature == DamageStruct::Fire)
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		room->sendCompulsoryTriggerLog(player, objectName());
		room->broadcastSkillInvoke(objectName());
		if (event != ConfirmDamage)
			return true;
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		damage.nature = DamageStruct::Fire;
		*ctx.original_data = QVariant::fromValue(damage);
		return false;
	}
};

class RhXuanrenViewAs : public ViewAsSkillV2
{
public:
	RhXuanrenViewAs() : ViewAsSkillV2("rhxuanren", 1) {}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid()
			&& request.reason != CardUseStruct::CARD_USE_REASON_PLAY
			&& request.pattern == "@@rhxuanren" && request.initiator->canDiscard(request.initiator, "h");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || !request.selectedCardIds.isEmpty())
			return false;
		const int id = candidate->getEffectiveId();
		return request.initiator->handCards().contains(id) && request.initiator->canDiscard(request.initiator, id);
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *to) const override
	{
		if (!request.initiator || !to || !selected.isEmpty())
			return false;
		const QString name = request.initiator->property("rhxuanren").toString();
		const Player *origin = nullptr;
		if (request.initiator->objectName() == name)
			origin = request.initiator;
		else {
			foreach (const Player *p, request.initiator->getAliveSiblings()) {
				if (p->objectName() == name) {
					origin = p;
					break;
				}
			}
		}
		return origin && origin->distanceTo(to) == 1;
	}

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *from = ctx.invoker;
		if (!from || !target || !target->isAlive())
			return ContinueEffects;
		from->getRoom()->damage(DamageStruct(objectName(), from, target));
		return ContinueEffects;
	}
};

class RhXuanren : public TriggerSkillV2
{
public:
	RhXuanren() : TriggerSkillV2("rhxuanren")
	{
		events << Damaged;
		view_as_skill = new RhXuanrenViewAs;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player || !player->isAlive())
			return TriggerList();
		const DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card || !damage.card->isKindOf("Slash"))
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			if (!holder->canDiscard(holder, "h"))
				continue;
			foreach (ServerPlayer *victim, room->getOtherPlayers(holder)) {
				if (player->distanceTo(victim) == 1) {
					list.insert(holder, QStringList(objectName()));
					break;
				}
			}
		}
		return list;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker)
			return false;
		room->setPlayerProperty(player, "rhxuanren", ctx.invoker->objectName());
		Room::AcceptedViewAsEffectScope continuation(room, player, objectName(), ctx);
		if (continuation.isValid())
			room->askForUseCard(player, "@@rhxuanren", "@rhxuanren", -1, Card::MethodDiscard, false);
		room->setPlayerProperty(player, "rhxuanren", QVariant());
		return false;
	}
};

class RhGuozao : public TriggerSkillV2
{
public:
	RhGuozao() : TriggerSkillV2("rhguozao")
	{
		events << EventLoseSkill << EventAcquireSkill;
		frequency = NotCompulsory;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventLoseSkill || !room || !player || reihouChangedSkill(data) != objectName())
			return false;
		room->sendCompulsoryTriggerLog(player, objectName());
		room->broadcastSkillInvoke(objectName());
		const bool draw = !player->isWounded();
		room->loseMaxHp(player, 1, objectName());
		if (draw && player->isAlive())
			player->drawCards(1, objectName());
		return true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventAcquireSkill && player && player->isAlive() && player->hasSkill(objectName())
			&& reihouChangedSkill(data) == objectName())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room || !player->isAlive())
			return false;
		room->sendCompulsoryTriggerLog(player, objectName());
		room->broadcastSkillInvoke(objectName());
		const bool wounded = player->isWounded();
		room->gainMaxHp(player, 1, objectName());
		if (!player->isAlive())
			return false;
		if (wounded)
			room->recover(player, RecoverStruct(player));
		else {
			LogMessage log;
			log.type = "#GetHp";
			log.from = player;
			log.arg = QString::number(player->getHp());
			log.arg2 = QString::number(player->getMaxHp());
			room->sendLog(log);
		}
		return false;
	}
};

void reihouClearAG(Room *room, ServerPlayer *player, QList<int> &cardIds, const QString &skill)
{
	if (!room)
		return;
	room->clearAG();
	if (cardIds.isEmpty())
		return;
	DummyCard dummy(cardIds);
	CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player ? player->objectName() : QString(), skill, QString());
	room->throwCard(&dummy, reason, nullptr);
	cardIds.clear();
}

class RhShenguang : public TriggerSkillV2
{
public:
	RhShenguang() : TriggerSkillV2("rhshenguang") { events << EventPhaseStart; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || player->getPhase() != Player::Draw)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName()))
			list.insert(holder, QStringList(objectName()));
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ServerPlayer *target = ctx.invoker;
		if (!player || !room || !target)
			return false;
		room->removeReihouCard(player);
		QList<int> cardIds = room->getNCards(room->getAllPlayers().length());
		room->fillAG(cardIds);
		try {
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (!p->canSlash(target, false))
					continue;
				if (!room->askForUseSlashTo(p, target, "@rhshenguang:" + target->objectName(), false))
					continue;
				if (cardIds.isEmpty())
					break;
				const int cardId = room->askForAG(p, cardIds, false, objectName());
				if (!cardIds.contains(cardId))
					continue;
				cardIds.removeOne(cardId);
				room->takeAG(p, cardId);
			}
			reihouClearAG(room, player, cardIds, objectName());
			return false;
		} catch (const TriggerCascadeBreak &) {
			reihouClearAG(room, player, cardIds, objectName());
			throw;
		} catch (TriggerEvent event) {
			if (event == TurnBroken || event == StageChange)
				reihouClearAG(room, player, cardIds, objectName());
			throw;
		}
	}
};

class RhMangti : public ViewAsSkillV2
{
public:
	RhMangti() : ViewAsSkillV2("rhmangti", 1) { setResponseOrUse(true); }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || !request.activationRef.isValid())
			return false;
		Card *trick = Sanguosha->cloneCard("dismantlement");
		if (!trick)
			return false;
		trick->setSkillName(objectName());
		bool accepted = false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
			accepted = trick->isAvailable(player);
		else if (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
			|| request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			accepted = Sanguosha->matchPattern(request.pattern, player, trick);
		delete trick;
		return accepted;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!candidate || request.selectedCardIds.size() >= 1)
			return false;
		const int id = candidate->getEffectiveId();
		return reihouHolds(request.initiator, id) && candidate->getSuit() == Card::Club
			&& !request.selectedCardIds.contains(id);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request))
			return nullptr;
		const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
		if (!original)
			return nullptr;
		Dismantlement *trick = new Dismantlement(Card::SuitToBeDecided, -1);
		trick->addSubcard(original);
		trick->setSkillName(objectName());
		return trick;
	}
};

class RhLingwei : public ProhibitSkill
{
public:
	RhLingwei() : ProhibitSkill("rhlingwei") { frequency = NotCompulsory; }

	bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const override
	{
		return to && card && to->hasSkill(objectName())
			&& (card->isKindOf("Dismantlement") || card->isKindOf("Snatch"));
	}
};

// Keeps "hand count < 2" players inside each rhmangti holder's attack range.
// The pairs this skill added are remembered per holder, so other skills' pairs stay.
void reihouSyncMangti(Room *room)
{
	foreach (ServerPlayer *p, room->getAllPlayers(true)) {
		const QStringList had = p->getTag("RhMangtiPairs").toStringList();
		QStringList want;
		if (p->isAlive() && p->hasSkill("rhmangti")) {
			foreach (ServerPlayer *q, room->getOtherPlayers(p)) {
				if (q->getHandcardNum() < 2)
					want << q->objectName();
			}
		}
		if (had == want)
			continue;
		foreach (const QString &name, had) {
			ServerPlayer *q = room->findPlayerByObjectName(name, true);
			if (q && !want.contains(name))
				room->removeAttackRangePair(p, q);
		}
		foreach (const QString &name, want) {
			ServerPlayer *q = room->findPlayerByObjectName(name, true);
			if (q && !had.contains(name))
				room->insertAttackRangePair(p, q);
		}
		if (want.isEmpty())
			p->removeTag("RhMangtiPairs");
		else
			p->setTag("RhMangtiPairs", want);
	}
}

class RhMangtiRange : public TriggerSkillV2
{
public:
	RhMangtiRange() : TriggerSkillV2("#rhmangti")
	{
		events << CardsMoveOneTime << EventAcquireSkill << EventLoseSkill << Death;
		frequency = Compulsory;
	}

	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
	{
		if (room)
			reihouSyncMangti(room);
		return false;
	}
};

class RhWangzhong : public TriggerSkillV2
{
public:
	RhWangzhong() : TriggerSkillV2("rhwangzhong") { events << EventPhaseEnd; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || player->getPhase() != Player::Play)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			if (holder->getHandcardNum() < holder->getMaxHp())
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !player->askForSkillInvoke(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker)
			return false;
		player->drawCards(1, objectName());
		if (!player->isAlive())
			return false;
		if (!player->canSlash(ctx.invoker, false)
			|| !room->askForUseSlashTo(player, ctx.invoker, "@rhwangzhong:" + ctx.invoker->objectName(), false))
			room->loseHp(player);
		return false;
	}
};

class RhCuigu : public TriggerSkillV2
{
public:
	RhCuigu() : TriggerSkillV2("rhcuigu")
	{
		events << TargetSpecified << EventPhaseChanging;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (!room || event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
			return false;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getMark(objectName()) <= 0)
				continue;
			room->setPlayerMark(p, objectName(), 0);
			room->removePlayerCardLimitation(p, "use", "Peach");
		}
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != TargetSpecified || !player || !player->isAlive() || !player->hasSkill(objectName())
			|| player->getPhase() != Player::Play)
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.card && use.card->isKindOf("Slash") && !use.to.isEmpty())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		foreach (ServerPlayer *p, use.to) {
			if (!p || !player->askForSkillInvoke(objectName(), p))
				continue;
			room->broadcastSkillInvoke(objectName());
			room->removeReihouCard(player);
			room->setPlayerCardLimitation(p, "use", "Peach", false);
			room->addPlayerMark(p, objectName());
			LogMessage log;
			log.type = "#RhCuigu";
			log.from = p;
			log.arg = "peach";
			room->sendLog(log);
			break;
		}
		return false;
	}
};

class RhCuiguProhibit : public ProhibitSkill
{
public:
	RhCuiguProhibit() : ProhibitSkill("#rhcuigu") {}

	bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const override
	{
		return to && card && to->getMark("rhcuigu") > 0 && card->isKindOf("Peach");
	}
};

class RhLinghai : public TriggerSkillV2
{
public:
	RhLinghai() : TriggerSkillV2("rhlinghai") { events << DamageInflicted; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player || !player->isAlive())
			return TriggerList();
		const DamageStruct damage = data.value<DamageStruct>();
		if (damage.damage < player->getHp())
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName()))
			list.insert(holder, QStringList(objectName()));
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->removeReihouCard(player);
		return true;
	}

	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override
	{
		return true;
	}
};

class RhHaoqiangViewAs : public ViewAsSkillV2
{
public:
	RhHaoqiangViewAs() : ViewAsSkillV2("rhhaoqiang", 1) { setResponseOrUse(true); }

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::juguan(objectName(), "slash,duel");
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || !request.activationRef.isValid())
			return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
			if (request.initiator->getMark(objectName()) > 0)
				return false;
			Card *slash = Sanguosha->cloneCard("slash", Card::SuitToBeDecided, -1);
			Card *duel = Sanguosha->cloneCard("duel", Card::SuitToBeDecided, -1);
			bool available = false;
			if (slash) {
				slash->setSkillName(objectName());
				available = slash->isAvailable(request.initiator);
			}
			if (!available && duel) {
				duel->setSkillName(objectName());
				available = duel->isAvailable(request.initiator);
			}
			delete slash;
			delete duel;
			return available;
		}
		if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
			&& request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return false;
		Card *slash = Sanguosha->cloneCard("slash", Card::SuitToBeDecided, -1);
		if (!slash)
			return false;
		slash->setSkillName(objectName());
		const bool matched = Sanguosha->matchPattern(request.pattern, request.initiator, slash);
		delete slash;
		return matched;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || !request.selectedCardIds.isEmpty())
			return false;
		const int id = candidate->getEffectiveId();
		return reihouHolds(request.initiator, id) && candidate->isKindOf("TrickCard");
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request))
			return nullptr;
		QString name = "slash";
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
			name = request.userString;
			if (name.isEmpty()) {
				const Card *declared = request.initiator->getTag(objectName()).value<const Card *>();
				if (declared)
					name = declared->objectName();
			}
		}
		if (name != "slash" && name != "duel")
			return nullptr;
		Card *card = Sanguosha->cloneCard(name);
		if (!card)
			return nullptr;
		card->setSkillName(objectName());
		card->addSubcards(request.selectedCardIds);
		return card;
	}

	bool pay(Room *room, SkillContext &, const ActiveSkillRequest &request) const override
	{
		ServerPlayer *player = const_cast<ServerPlayer *>(dynamic_cast<const ServerPlayer *>(request.initiator));
		if (!room || !player)
			return false;
		room->addPlayerMark(player, objectName());
		return true;
	}
};

class RhHaoqiang : public TriggerSkillV2
{
public:
	RhHaoqiang() : TriggerSkillV2("rhhaoqiang")
	{
		events << EventPhaseChanging;
		view_as_skill = new RhHaoqiangViewAs;
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::juguan(objectName(), "slash,duel");
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (!room || event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
			return false;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			room->setPlayerMark(p, objectName(), 0);
		return false;
	}
};

class RhLiedan : public TriggerSkillV2
{
public:
	RhLiedan() : TriggerSkillV2("rhliedan")
	{
		events << Damaged << EventPhaseChanging;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (!room || event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
			return false;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getMark(objectName()) <= 0)
				continue;
			room->setPlayerMark(p, objectName(), 0);
			room->detachSkillFromPlayer(p, "wushuang", false, true);
		}
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (event != Damaged || !player || !player->isAlive() || !player->hasSkill(objectName())
			|| !player->canDiscard(player, "h"))
			return TriggerList();
		return TriggerList{{player, QStringList(objectName())}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		if (!room->askForCard(player, ".", "@rhliedan", *ctx.original_data, objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		if (!player->hasSkill("wushuang")) {
			room->addPlayerMark(player, objectName());
			room->acquireSkill(player, "wushuang");
		}
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (!damage.from || !damage.from->isAlive())
			return false;
		Duel *duel = new Duel(Card::NoSuit, 0);
		duel->setSkillName(objectName());
		if (!player->isCardLimited(duel, Card::MethodUse) && !player->isProhibited(damage.from, duel)) {
			duel->deleteLater();
			room->useCard(CardUseStruct(duel, player, damage.from));
		} else
			delete duel;
		return false;
	}
};

class RhYarenViewAs : public ViewAsSkillV2
{
public:
	RhYarenViewAs() : ViewAsSkillV2("rhyaren") {}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid()
			&& request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed(objectName()) && !request.initiator->isKongcheng();
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *to) const override
	{
		return request.initiator && to && selected.isEmpty() && to != request.initiator && !to->isKongcheng();
	}

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *from = ctx.invoker;
		if (!from || !target || !target->isAlive())
			return ContinueEffects;
		Room *room = from->getRoom();
		if (!room)
			return ContinueEffects;
		room->addPlayerMark(from, "rhyaren");
		if (from->isKongcheng() || target->isKongcheng())
			return ContinueEffects;
		if (from->pindian(target, "rhyaren")) {
			from->setTag("RhYarenTarget", target->objectName());
			room->setFixedDistance(from, target, 1);
		} else if (target->isAlive() && target->isWounded() && from->askForSkillInvoke("rhyaren_recover", QVariant("yes")))
			room->recover(target, RecoverStruct(from));
		return ContinueEffects;
	}
};

class RhYaren : public TriggerSkillV2
{
public:
	RhYaren() : TriggerSkillV2("rhyaren")
	{
		events << EventPhaseChanging << Death << EventLoseSkill;
		view_as_skill = new RhYarenViewAs;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player)
			return false;
		if (event == EventLoseSkill) {
			if (reihouChangedSkill(data) == objectName())
				room->setPlayerMark(player, "rhyaren", 0);
			return false;
		}
		const QString stored = player->getTag("RhYarenTarget").toString();
		if (stored.isEmpty())
			return false;
		if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)
			return false;
		if (event == Death) {
			const DeathStruct death = data.value<DeathStruct>();
			if (death.who != player) {
				if (death.who && death.who->objectName() == stored) {
					room->removeFixedDistance(player, death.who, 1);
					player->removeTag("RhYarenTarget");
				}
				return false;
			}
		}
		ServerPlayer *target = room->findPlayerByObjectName(stored, true);
		if (target)
			room->removeFixedDistance(player, target, 1);
		player->removeTag("RhYarenTarget");
		return false;
	}
};

class RhShixiang : public TriggerSkillV2
{
public:
	RhShixiang() : TriggerSkillV2("rhshixiang")
	{
		events << EventPhaseStart << EventPhaseChanging;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player || event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive
			|| player->getMark("rhshixiang") <= 0)
			return false;
		room->setPlayerMark(player, "rhshixiang", 0);
		room->detachSkillFromPlayer(player, "rhyaren", false, true);
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event != EventPhaseStart || !room || !player || player->getPhase() != Player::RoundStart
			|| player->hasSkill("rhyaren"))
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			if (holder != player && holder->getMark("rhyaren") == 0)
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->removeReihouCard(player);
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!room || !ctx.invoker || !ctx.invoker->isAlive())
			return false;
		room->acquireSkill(ctx.invoker, "rhyaren");
		room->addPlayerMark(ctx.invoker, "rhshixiang");
		return false;
	}
};

class RhYinren : public ViewAsSkillV2
{
public:
	RhYinren() : ViewAsSkillV2("rhyinren", 2) {}

	bool willThrowSelectedCards() const override { return false; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid()
			&& request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getHandcardNum() > 1;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || request.selectedCardIds.length() >= 2 || candidate->isEquipped())
			return false;
		const int id = candidate->getEffectiveId();
		return request.initiator->handCards().contains(id) && !request.selectedCardIds.contains(id);
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *to) const override
	{
		return request.initiator && to && selected.isEmpty() && to != request.initiator;
	}

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *from = ctx.invoker;
		if (!from || !target || !target->isAlive() || !ctx.use_card)
			return ContinueEffects;
		Room *room = from->getRoom();
		if (!room)
			return ContinueEffects;
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, from->objectName(), target->objectName(), objectName(), QString());
		room->obtainCard(target, ctx.use_card, reason, false);
		if (!from->isAlive())
			return ContinueEffects;
		room->addPlayerMark(from, objectName());
		if (target->isAlive()) {
			Slash *slash = new Slash(Card::NoSuit, 0);
			slash->setSkillName("_rhyinren");
			if (from->canSlash(target, slash, false)) {
				slash->deleteLater();
				room->useCard(CardUseStruct(slash, from, target));
			} else
				delete slash;
		}
		if (from->isAlive() && from->getMark(objectName()) == 2) {
			room->sendCompulsoryTriggerLog(from, objectName());
			room->removeReihouCard(from);
			if (from->isAlive())
				room->loseHp(from);
		}
		return ContinueEffects;
	}
};

class RhYinrenTrigger : public TriggerSkillV2
{
public:
	RhYinrenTrigger() : TriggerSkillV2("#rhyinren")
	{
		events << EventLoseSkill;
		frequency = Compulsory;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player || event != EventLoseSkill || reihouChangedSkill(data) != "rhyinren")
			return false;
		room->setPlayerMark(player, "rhyinren", 0);
		return false;
	}
};

class RhYeming : public TriggerSkillV2
{
public:
	RhYeming() : TriggerSkillV2("rhyeming")
	{
		events << EventPhaseStart << EventPhaseChanging;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player || event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive
			|| player->getMark("rhyeming") <= 0)
			return false;
		room->setPlayerMark(player, "rhyeming", 0);
		room->detachSkillFromPlayer(player, "rhyinren", false, true);
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event != EventPhaseStart || !room || !player || player->getPhase() != Player::RoundStart
			|| player->hasSkill("rhyinren"))
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			if (holder != player)
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!room || !ctx.invoker || !ctx.invoker->isAlive())
			return false;
		room->acquireSkill(ctx.invoker, "rhyinren");
		room->addPlayerMark(ctx.invoker, "rhyeming");
		return false;
	}
};

class RhYiqie : public TriggerSkillV2
{
public:
	RhYiqie() : TriggerSkillV2("rhyiqie") { events << TargetSpecified; }

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (player != use.from || !use.card || !use.card->isKindOf("Slash"))
			return TriggerList();
		foreach (ServerPlayer *p, use.to) {
			if (p && p->canDiscard(p, "e"))
				return TriggerList{{player, QStringList(objectName())}};
		}
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		foreach (ServerPlayer *p, use.to) {
			if (!p || !p->isAlive() || !p->canDiscard(p, "e") || !player->askForSkillInvoke(objectName(), p))
				continue;
			room->broadcastSkillInvoke(objectName());
			const int id = room->askForCardChosen(p, p, "e", objectName(), false, Card::MethodDiscard);
			if (id >= 0)
				room->throwCard(id, p);
		}
		return false;
	}
};

class RhYaozhang : public TriggerSkillV2
{
public:
	RhYaozhang() : TriggerSkillV2("rhyaozhang")
	{
		events << TargetConfirmed;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.card && use.card->isKindOf("Slash") && use.to.contains(player))
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		room->sendCompulsoryTriggerLog(player, objectName());
		room->broadcastSkillInvoke(objectName());
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.from || use.from->isDead() || !room->askForCard(use.from, ".Equip", "@rhyaozhang:" + player->objectName())) {
			use = ctx.original_data->value<CardUseStruct>();
			if (!use.nullified_list.contains(player->objectName()))
				use.nullified_list << player->objectName();
			*ctx.original_data = QVariant::fromValue(use);
		}
		return false;
	}
};

class RhSanglv : public TriggerSkillV2
{
public:
	RhSanglv() : TriggerSkillV2("rhsanglv")
	{
		events << PreDamageDone << CardsMoveOneTime << EventPhaseEnd;
	}

	bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == PreDamageDone) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (damage.from && damage.from->getPhase() == Player::Play && !damage.from->hasFlag("RhSanglvDamageInPlayPhase"))
				damage.from->setFlags("RhSanglvDamageInPlayPhase");
			return false;
		}
		if (event != CardsMoveOneTime || !player || player->getPhase() != Player::Discard)
			return false;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
			return false;
		foreach (int id, move.card_ids) {
			const Card *card = Sanguosha->getCard(id);
			if (card && card->isKindOf("Slash"))
				player->setFlags("RhSanglvDiscardSlash");
		}
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event != EventPhaseEnd || !room || !player || !player->isAlive() || player->getPhase() != Player::Discard
			|| player->hasFlag("RhSanglvDamageInPlayPhase") || !player->hasFlag("RhSanglvDiscardSlash"))
			return TriggerList();
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("_rhsanglv");
		slash->deleteLater();
		if (player->isCardLimited(slash, Card::MethodUse))
			return TriggerList();
		bool canHit = false;
		foreach (ServerPlayer *target, room->getAlivePlayers()) {
			if (player->canSlash(target, slash)) {
				canHit = true;
				break;
			}
		}
		if (!canHit)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName()))
			list.insert(holder, QStringList(objectName()));
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *actor = ctx.invoker;
		if (!room || !actor || !actor->isAlive())
			return false;
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("_rhsanglv");
		if (actor->isCardLimited(slash, Card::MethodUse)) {
			delete slash;
			return false;
		}
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *target, room->getAlivePlayers()) {
			if (actor->canSlash(target, slash))
				targets << target;
		}
		if (targets.isEmpty()) {
			delete slash;
			return false;
		}
		ServerPlayer *victim = room->askForPlayerChosen(actor, targets, objectName(), "@dummy-slash");
		if (!victim) {
			delete slash;
			return false;
		}
		slash->deleteLater();
		room->useCard(CardUseStruct(slash, actor, victim));
		return false;
	}
};

// cloneCard("lure_tiger") is null outside hegemony; construct the class directly.
Card *reihouLureTiger()
{
	Card *card = new HLureTiger(Card::NoSuit, 0);
	card->setSkillName("_rhwuyin");
	return card;
}

class RhWuyin : public TriggerSkillV2
{
public:
	RhWuyin() : TriggerSkillV2("rhwuyin") { events << EventPhaseStart; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || player->getPhase() != Player::Play)
			return TriggerList();
		Card *tiger = reihouLureTiger();
		if (!tiger)
			return TriggerList();
		tiger->deleteLater();
		if (player->isCardLimited(tiger, Card::MethodUse))
			return TriggerList();
		bool open = false;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!player->isProhibited(p, tiger)) {
				open = true;
				break;
			}
		}
		if (!open)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName()))
			list.insert(holder, QStringList(objectName()));
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ServerPlayer *actor = ctx.invoker;
		if (!player || !room || !actor)
			return false;
		Card *tiger = reihouLureTiger();
		if (!tiger)
			return false;
		tiger->deleteLater();
		QList<ServerPlayer *> victims;
		foreach (ServerPlayer *p, room->getOtherPlayers(actor)) {
			if (!actor->isProhibited(p, tiger))
				victims << p;
		}
		if (victims.isEmpty())
			return false;
		ServerPlayer *victim = room->askForPlayerChosen(player, victims, objectName(), "@rhwuyin", true, true);
		if (!victim)
			return false;
		room->broadcastSkillInvoke(objectName());
		player->setTag("RhWuyinTarget", victim->objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room)
			return false;
		const QString name = player->getTag("RhWuyinTarget").toString();
		player->removeTag("RhWuyinTarget");
		ServerPlayer *actor = ctx.invoker;
		ServerPlayer *target = room->findPlayerByObjectName(name);
		if (!actor || !actor->isAlive() || !target || !target->isAlive())
			return false;
		Card *tiger = reihouLureTiger();
		if (!tiger)
			return false;
		tiger->deleteLater();
		room->useCard(CardUseStruct(tiger, actor, target));
		return false;
	}
};

class RhChanling : public TriggerSkillV2
{
public:
	RhChanling() : TriggerSkillV2("rhchanling") { events << Damaged; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || !player->isAlive())
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			const int distance = holder->distanceTo(player);
			if (distance != -1 && distance <= 1)
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ServerPlayer *victim = ctx.invoker;
		if (!player || !room || !ctx.original_data || !victim)
			return false;
		const int n = ctx.original_data->value<DamageStruct>().damage;
		player->drawCards(n, objectName());
		if (n > 0 && player->isAlive() && player->getHandcardNum() >= n && player != victim && victim->isAlive()) {
			const Card *dummy = room->askForExchange(player, objectName(), n, n, false,
				QString("@rhchanling:%1::%2").arg(victim->objectName()).arg(n), true);
			if (dummy) {
				CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), victim->objectName(),
					objectName(), QString());
				room->obtainCard(victim, dummy, reason, false);
				delete dummy;
			}
		}
		return false;
	}
};

class RhShendai : public TriggerSkillV2
{
public:
	RhShendai() : TriggerSkillV2("rhshendai") { events << BeforeCardsMove; }

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		const Player *destination = move.to ? move.to : move.origin_to;
		const Player::Place destinationPlace = move.to ? move.to_place : move.origin_to_place;
		const bool dismantle = move.reason.m_reason == CardMoveReason::S_REASON_DISMANTLE
			&& move.reason.m_playerId != move.reason.m_targetId;
		const bool taken = destination && destination != move.from && destinationPlace == Player::PlaceHand
			&& move.reason.m_reason != CardMoveReason::S_REASON_GIVE
			&& move.reason.m_reason != CardMoveReason::S_REASON_SWAP;
		if (move.from && move.from->isAlive() && move.from_places.contains(Player::PlaceHand) && (dismantle || taken))
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		if (move.from)
			move.from->setFlags("RhShendaiMoveFrom");
		const bool invoke = player->askForSkillInvoke(objectName(), *ctx.original_data);
		if (move.from)
			move.from->setFlags("-RhShendaiMoveFrom");
		if (!invoke)
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		room->removeReihouCard(player);
		CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		move.removeCardIds(move.card_ids);
		*ctx.original_data = QVariant::fromValue(move);
		return false;
	}
};

class RhJiyu : public TriggerSkillV2
{
public:
	RhJiyu() : TriggerSkillV2("rhjiyu")
	{
		events << EventPhaseChanging << CardFinished << PreDamageDone;
		frequency = Frequent;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room)
			return false;
		if (event == CardFinished) {
			if (!player || room->getCurrent() != player)
				return false;
			const CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card || use.card->getTypeId() == Card::TypeSkill)
				return false;
			foreach (ServerPlayer *p, use.to) {
				if (p)
					p->setMark("rhjiyu_use", 1);
			}
			return false;
		}
		if (event == PreDamageDone) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (player && damage.from && room->getCurrent() == damage.from)
				player->setMark("rhjiyu_damage", 1);
			return false;
		}
		if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::RoundStart) {
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				p->setMark("rhjiyu_use", 0);
				p->setMark("rhjiyu_damage", 0);
			}
		}
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (event != EventPhaseChanging || !room || data.value<PhaseChangeStruct>().to != Player::NotActive)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			if (holder->getMark("rhjiyu_use") > 0 && holder->getMark("rhjiyu_damage") == 0)
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room || !player->askForSkillInvoke(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
	{
		if (player && player->isAlive())
			player->drawCards(1, objectName());
		return false;
	}
};

class RhJinbei : public TriggerSkillV2
{
public:
	RhJinbei() : TriggerSkillV2("rhjinbei") { events << EventPhaseStart; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || player->getPhase() != Player::Play)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName()))
			list.insert(holder, QStringList(objectName()));
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room || !player->askForSkillInvoke(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->removeReihouCard(player);
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *actor = ctx.invoker;
		if (!room || !actor || !actor->isAlive())
			return false;
		Peach *peach = new Peach(Card::NoSuit, 0);
		peach->setSkillName("_rhjinbei");
		Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
		analeptic->setSkillName("_rhjinbei");
		QStringList choices;
		if (peach->isAvailable(actor))
			choices << "peach";
		if (analeptic->isAvailable(actor))
			choices << "analeptic";
		if (choices.isEmpty()) {
			delete peach;
			delete analeptic;
			return false;
		}
		QString choice = choices.first();
		if (choices.length() != 1)
			choice = room->askForChoice(actor, objectName(), choices.join("+"));
		if (choice == "peach") {
			delete analeptic;
			peach->deleteLater();
			room->useCard(CardUseStruct(peach, actor, QList<ServerPlayer *>()));
		} else if (choice == "analeptic") {
			delete peach;
			analeptic->deleteLater();
			room->useCard(CardUseStruct(analeptic, actor, QList<ServerPlayer *>()), true);
		} else {
			delete peach;
			delete analeptic;
		}
		return false;
	}
};

void reihouRefreshSkillMarks(Room *room, bool acquiring)
{
	if (!room)
		return;
	foreach (ServerPlayer *p, room->getAllPlayers())
		room->filterCards(p, p->getCards("he"), acquiring);
	JsonArray args;
	args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
}

class RhPihuai : public TriggerSkillV2
{
public:
	RhPihuai() : TriggerSkillV2("rhpihuai") { events << Damaged; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || !player->isAlive())
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			if (holder != player && holder->canDiscard(player, "h") && !holder->inMyAttackRange(player))
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ServerPlayer *victim = ctx.invoker;
		if (!player || !room || !victim || !victim->isAlive() || !player->canDiscard(victim, "h"))
			return false;
		const int id = room->askForCardChosen(player, victim, "h", objectName(), false, Card::MethodDiscard);
		if (id >= 0)
			room->throwCard(id, victim, player);
		return false;
	}
};

class RhDangmo : public TriggerSkillV2
{
public:
	RhDangmo() : TriggerSkillV2("rhdangmo")
	{
		events << TargetSpecified << EventPhaseChanging << Death;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room)
			return false;
		bool clear = false;
		if (event == EventPhaseChanging)
			clear = data.value<PhaseChangeStruct>().to == Player::NotActive;
		else if (event == Death) {
			const DeathStruct death = data.value<DeathStruct>();
			clear = death.who && death.who == player && room->getCurrent() == player;
		}
		if (!clear)
			return false;
		foreach (ServerPlayer *marked, room->getAllPlayers(true)) {
			const int mark = marked->getMark("rhdangmo");
			if (mark <= 0)
				continue;
			room->removePlayerMark(marked, "@skill_invalidity", mark);
			marked->setMark("rhdangmo", 0);
			reihouRefreshSkillMarks(room, false);
		}
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != TargetSpecified || !player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.card && use.card->isKindOf("Slash"))
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		QList<ServerPlayer *> applied;
		foreach (ServerPlayer *p, use.to) {
			if (!player->isAlive())
				break;
			if (!p || !p->isAlive() || !player->askForSkillInvoke(objectName(), p))
				continue;
			room->broadcastSkillInvoke(objectName());
			if (applied.contains(p))
				continue;
			p->addMark("rhdangmo");
			room->addPlayerMark(p, "@skill_invalidity");
			applied << p;
			reihouRefreshSkillMarks(room, true);
		}
		return false;
	}
};

class RhBumo : public TriggerSkillV2
{
public:
	RhBumo() : TriggerSkillV2("rhbumo")
	{
		events << CardsMoveOneTime;
		frequency = Frequent;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player || !player->isAlive() || !player->hasSkill(objectName()) || room->getCurrent() != player)
			return TriggerList();
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player)
			return TriggerList();
		if (!move.from_places.contains(Player::PlaceHand) && !move.from_places.contains(Player::PlaceEquip))
			return TriggerList();
		if (move.to == player && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip))
			return TriggerList();
		return TriggerList{{player, QStringList(objectName())}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data || !player->askForSkillInvoke(objectName(), *ctx.original_data))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !player->isAlive() || !ctx.original_data)
			return false;
		const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		int n = 0;
		for (int i = 0; i < move.card_ids.length() && i < move.from_places.length(); ++i) {
			if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip)
				++n;
		}
		if (n > 0)
			player->drawCards(n, objectName());
		return false;
	}
};

class RhNieji : public TriggerSkillV2
{
public:
	RhNieji() : TriggerSkillV2("rhnieji") { events << Dying; }

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		const DyingStruct dying = data.value<DyingStruct>();
		if (!dying.who || dying.who->getHp() > 0 || dying.who->isDead())
			return TriggerList();
		return TriggerList{{player, QStringList(objectName())}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data || !player->askForSkillInvoke(objectName(), *ctx.original_data))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->removeReihouCard(player);
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		const DyingStruct dying = ctx.original_data->value<DyingStruct>();
		if (!dying.who || dying.who->isDead())
			return false;
		if (!dying.who->faceUp())
			dying.who->turnOver();
		if (dying.who->isAlive() && dying.who->isChained())
			room->setPlayerProperty(dying.who, "chained", false);
		if (!dying.who->isAlive())
			return false;
		const int amount = 1 - dying.who->getHp();
		if (amount > 0)
			room->recover(dying.who, RecoverStruct(player, nullptr, amount));
		return false;
	}
};

class RhShenluo : public TriggerSkillV2
{
public:
	RhShenluo() : TriggerSkillV2("rhshenluo")
	{
		events << EventAcquireSkill << EventLoseSkill << Death;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player)
			return false;
		bool clear = false;
		if (event == EventLoseSkill)
			clear = reihouChangedSkill(data) == objectName();
		else if (event == Death)
			clear = data.value<DeathStruct>().who == player;
		if (!clear)
			return false;
		const QString name = player->getTag(objectName()).toString();
		player->removeTag(objectName());
		ServerPlayer *marked = room->findPlayerByObjectName(name, true);
		if (marked)
			room->setPlayerMark(marked, "@constellation", 0);
		return event == EventLoseSkill;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventAcquireSkill && player && player->isAlive() && player->hasSkill(objectName())
			&& reihouChangedSkill(data) == objectName())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
			"@rhshenluo", true, true);
		if (!target)
			return false;
		room->broadcastSkillInvoke(objectName());
		player->setTag(objectName(), target->objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		ServerPlayer *target = room->findPlayerByObjectName(player->getTag(objectName()).toString());
		if (target)
			room->setPlayerMark(target, "@constellation", 1);
		return false;
	}
};

class RhShenluoRecover : public TriggerSkillV2
{
public:
	RhShenluoRecover() : TriggerSkillV2("#rhshenluo")
	{
		events << Damaged;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || !player->isAlive())
			return TriggerList();
		const QList<ServerPlayer *> holders = room->findPlayersBySkillName(objectName());
		if (holders.isEmpty())
			return TriggerList();
		foreach (ServerPlayer *marked, room->getOtherPlayers(player)) {
			if (marked->isAlive() && marked->getMark("@constellation") > 0 && marked->isWounded())
				return TriggerList{{holders.first(), QStringList(objectName())}};
		}
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!room || !ctx.invoker)
			return false;
		foreach (ServerPlayer *marked, room->getOtherPlayers(ctx.invoker)) {
			if (!marked->isAlive() || marked->getMark("@constellation") <= 0 || !marked->isWounded())
				continue;
			room->sendCompulsoryTriggerLog(marked, "rhshenluo");
			room->recover(marked, RecoverStruct(marked));
		}
		return false;
	}
};

QStringList reihouDelayedTrickNames()
{
	QStringList names;
	foreach (const DelayedTrick *card, Sanguosha->findChildren<const DelayedTrick *>()) {
		if (!card || card->isKindOf("Lightning"))
			continue;
		const QString name = card->objectName();
		if (name.isEmpty() || name.startsWith('_') || names.contains(name))
			continue;
		names << name;
	}
	return names;
}

Card *reihouDelayedTrick(const QString &name, const QList<int> &ids)
{
	Card *trick = Sanguosha->cloneCard(name, Card::SuitToBeDecided, -1);
	if (!trick)
		return nullptr;
	trick->addSubcards(ids);
	trick->setSkillName("rhyousheng");
	return trick;
}

bool reihouYoushengLightning(const Player *from, const QList<int> &ids)
{
	if (!from)
		return false;
	Card *trick = reihouDelayedTrick("lightning", ids);
	if (!trick)
		return false;
	const bool ok = !from->containsTrick("lightning") && !from->isProhibited(from, trick)
		&& !from->isCardLimited(trick, Card::MethodUse, true);
	delete trick;
	return ok;
}

class RhYoushengViewAs : public ViewAsSkillV2
{
public:
	RhYoushengViewAs() : ViewAsSkillV2("rhyousheng", 1) { setResponseOrUse(true); }

	bool willThrowSelectedCards() const override { return false; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid()
			&& request.reason != CardUseStruct::CARD_USE_REASON_PLAY
			&& request.pattern == "@@rhyousheng" && !request.initiator->isKongcheng();
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || !request.selectedCardIds.isEmpty())
			return false;
		return request.initiator->handCards().contains(candidate->getEffectiveId());
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *to) const override
	{
		if (!request.initiator || !to || !selected.isEmpty())
			return false;
		foreach (const QString &name, reihouDelayedTrickNames()) {
			Card *trick = reihouDelayedTrick(name, request.selectedCardIds);
			if (!trick)
				continue;
			const bool ok = !request.initiator->isCardLimited(trick, Card::MethodUse, true)
				&& trick->targetFilter(QList<const Player *>(), to, request.initiator)
				&& !request.initiator->isProhibited(to, trick);
			delete trick;
			if (ok)
				return true;
		}
		return false;
	}

	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
	{
		if (!request.initiator)
			return false;
		if (selected.isEmpty())
			return reihouYoushengLightning(request.initiator, request.selectedCardIds);
		if (selected.length() != 1)
			return false;
		foreach (const QString &name, reihouDelayedTrickNames()) {
			Card *trick = reihouDelayedTrick(name, request.selectedCardIds);
			if (!trick)
				continue;
			QList<const Player *> targets;
			targets << selected.first();
			const bool ok = !request.initiator->isCardLimited(trick, Card::MethodUse, true)
				&& trick->targetsFeasible(targets, request.initiator);
			delete trick;
			if (ok)
				return true;
		}
		return false;
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request) || !request.initiator)
			return nullptr;
		if (request.selectedTargetNames.isEmpty())
			return reihouYoushengLightning(request.initiator, request.selectedCardIds)
				? reihouDelayedTrick("lightning", request.selectedCardIds) : nullptr;
		ServerPlayer *from = const_cast<ServerPlayer *>(dynamic_cast<const ServerPlayer *>(request.initiator));
		const Player *target = nullptr;
		if (from)
			target = from->getRoom()->findPlayerByObjectName(request.selectedTargetNames.first());
		QStringList choices;
		foreach (const QString &name, reihouDelayedTrickNames()) {
			if (target && target->containsTrick(name))
				continue;
			Card *trick = reihouDelayedTrick(name, request.selectedCardIds);
			if (!trick)
				continue;
			const bool banned = from && target
				&& (from->isProhibited(target, trick) || from->isCardLimited(trick, Card::MethodUse, true));
			delete trick;
			if (!banned)
				choices << name;
		}
		if (choices.isEmpty())
			return nullptr;
		QString choice = choices.first();
		if (from && choices.length() != 1)
			choice = from->getRoom()->askForChoice(from, objectName(), choices.join("+"));
		if (!choices.contains(choice))
			return nullptr;
		return reihouDelayedTrick(choice, request.selectedCardIds);
	}

	bool pay(Room *room, SkillContext &, const ActiveSkillRequest &request) const override
	{
		ServerPlayer *player = const_cast<ServerPlayer *>(dynamic_cast<const ServerPlayer *>(request.initiator));
		if (!room || !player)
			return false;
		room->addPlayerMark(player, objectName());
		return true;
	}
};

class RhYousheng : public TriggerSkillV2
{
public:
	RhYousheng() : TriggerSkillV2("rhyousheng")
	{
		events << TargetSpecified << TargetConfirmed << EventPhaseChanging;
		view_as_skill = new RhYoushengViewAs;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (!room || event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
			return false;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			room->setPlayerMark(p, objectName(), 0);
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseChanging || !player || !player->isAlive() || !player->hasSkill(objectName())
			|| player->getMark(objectName()) > 0)
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill)
			return TriggerList();
		if (event == TargetSpecified || (event == TargetConfirmed && use.to.contains(player)))
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		return room->askForUseCard(player, "@@rhyousheng", "@rhyousheng");
	}
};


bool reihouDiscardToPile(const CardsMoveOneTimeStruct &move)
{
	if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
		return false;
	if (move.to_place == Player::DiscardPile)
		return true;
	return move.to_place == Player::PlaceTable && move.origin_to_place == Player::DiscardPile;
}

Card *reihouDrowning()
{
	Card *card = Sanguosha->cloneCard("drowning", Card::NoSuit, 0);
	if (card)
		card->setSkillName("_rhchensheng");
	return card;
}

class RhJianuo : public TriggerSkillV2
{
public:
	RhJianuo() : TriggerSkillV2("rhjianuo")
	{
		events << EventPhaseStart << BeforeCardsMove << EventPhaseChanging;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player)
			return false;
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive)
				return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				const QString obj = p->getTag(objectName()).toString();
				if (obj.isEmpty())
					continue;
				p->removeTag(objectName());
				ServerPlayer *watched = room->findPlayerByObjectName(obj);
				if (watched)
					room->removePlayerMark(watched, "@charm");
			}
			return false;
		}
		if (event != BeforeCardsMove)
			return false;
		const QString watchedName = player->getTag(objectName()).toString();
		if (watchedName.isEmpty())
			return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!move.from || move.from->objectName() != watchedName || !reihouDiscardToPile(move))
			return false;
		ServerPlayer *from = room->findPlayerByObjectName(move.from->objectName(), true);
		if (!from)
			return true;
		QList<int> allIds = move.card_ids;
		QList<int> ids = move.card_ids;
		QList<int> disabled;
		while (!ids.isEmpty()) {
			room->fillAG(allIds, player, disabled);
			const bool only = allIds.length() == 1;
			const int cardId = only ? ids.first() : room->askForAG(player, ids, true, objectName());
			room->clearAG(player);
			if (cardId < 0)
				break;
			const Card *card = Sanguosha->getCard(cardId);
			QList<ServerPlayer *> receivers = room->getOtherPlayers(from);
			if (!card || receivers.isEmpty())
				break;
			ServerPlayer *target = room->askForPlayerChosen(player, receivers, objectName(),
				QString("@ikyanyu-give:::%1:%2\\%3").arg(card->objectName())
					.arg(card->getSuitString() + "_char").arg(card->getNumberString()),
				only, true);
			if (!target)
				break;
			const int index = move.card_ids.indexOf(cardId);
			Player::Place place = Player::PlaceUnknown;
			if (index >= 0 && index < move.from_places.length())
				place = move.from_places.at(index);
			QList<int> one;
			one << cardId;
			move.removeCardIds(one);
			data = QVariant::fromValue(move);
			ids.removeOne(cardId);
			disabled << cardId;
			if (move.from->objectName() == target->objectName() && place != Player::PlaceTable) {
				LogMessage log;
				log.type = "$MoveCard";
				log.from = target;
				log.to << target;
				log.card_str = QString::number(cardId);
				room->sendLog(log);
			}
			room->obtainCard(target, card, move.reason);
		}
		return true;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event != EventPhaseStart || !room || !player || player->getPhase() != Player::Play)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName()))
			list.insert(holder, QStringList(objectName()));
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->removeReihouCard(player);
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker)
			return false;
		player->setTag(objectName(), ctx.invoker->objectName());
		room->addPlayerMark(ctx.invoker, "@charm");
		return false;
	}
};

class RhYizhi : public ViewAsSkillV2
{
public:
	RhYizhi() : ViewAsSkillV2("rhyizhi") {}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || !request.activationRef.isValid() || request.initiator->getHp() <= 0)
			return false;
		return request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern == "@@rhyizhi";
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *to) const override
	{
		if (!request.initiator || !to)
			return false;
		if (selected.isEmpty())
			return to->hasEquip();
		const Player *source = selected.first();
		foreach (const Card *card, source->getEquips()) {
			const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
			if (equip && !to->getEquip(equip->location()))
				return true;
		}
		return false;
	}

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 2;
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		ServerPlayer *from = ctx.invoker;
		Room *room = from ? from->getRoom() : nullptr;
		if (!room || ctx.targets.length() != 2)
			return FinishSkill;
		ServerPlayer *first = ctx.targets.at(0);
		ServerPlayer *second = ctx.targets.at(1);
		if (!from->isAlive() || !first || !first->isAlive() || !second || !second->isAlive())
			return FinishSkill;
		const int alive = room->getAlivePlayers().length();
		room->loseHp(from);
		if (!from->isAlive() || !first->isAlive() || !second->isAlive()
			|| room->getAlivePlayers().length() != alive)
			return FinishSkill;
		QList<int> disabled;
		foreach (const Card *card, first->getEquips()) {
			const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
			if (equip && second->getEquip(equip->location()))
				disabled << card->getEffectiveId();
		}
		const int id = room->askForCardChosen(from, first, "e", objectName(), false, Card::MethodNone, disabled);
		if (id < 0)
			return FinishSkill;
		const Card *card = Sanguosha->getCard(id);
		if (!card)
			return FinishSkill;
		CardMoveReason reason(CardMoveReason::S_REASON_PUT, from->objectName(), second->objectName(),
			objectName(), QString());
		room->moveCardTo(card, first, second, Player::PlaceEquip, reason, true);
		return FinishSkill;
	}
};

class RhGuozhu : public TriggerSkillV2
{
public:
	RhGuozhu() : TriggerSkillV2("rhguozhu")
	{
		events << Damaged;
		frequency = Frequent;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		return TriggerList{{player, QStringList(objectName())}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		if (player->isWounded() && player->askForSkillInvoke(objectName())) {
			room->broadcastSkillInvoke(objectName());
			room->recover(player, RecoverStruct(player));
		}
		if (player->isAlive() && player->getHp() > 0)
			room->askForUseCard(player, "@@rhyizhi", "@rhyizhi", -1, Card::MethodNone);
		return false;
	}
};

class RhShuguangGiven : public ViewAsSkillV2
{
public:
	RhShuguangGiven() : ViewAsSkillV2("rhshuguangv")
	{
		attached_lord_skill = true;
	}

	bool shouldBeVisible(const Player *player) const override
	{
		return player && player->getMark("@time") > 0;
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid()
			&& request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getMark("@time") > 0;
	}

	TargetMode targetMode() const override { return NoTarget; }

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.isEmpty();
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		if (!player || !player->isAlive() || player->getMark("@time") <= 0)
			return FinishSkill;
		player->loseMark("@time");
		player->drawCards(1, "rhshuguang");
		player->getRoom()->addPlayerMark(player, "rhshuguang");
		return FinishSkill;
	}
};

class RhShuguang : public TriggerSkillV2
{
public:
	RhShuguang() : TriggerSkillV2("rhshuguang")
	{
		events << EventAcquireSkill << EventLoseSkill << EventPhaseChanging;
		frequency = NotCompulsory;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room)
			return false;
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive)
				return false;
			foreach (ServerPlayer *p, room->getAlivePlayers())
				room->setPlayerMark(p, objectName(), 0);
			return false;
		}
		if (event != EventLoseSkill || !player || reihouChangedSkill(data) != objectName())
			return false;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getMark("@time") <= 0)
				continue;
			room->sendCompulsoryTriggerLog(player, objectName());
			room->broadcastSkillInvoke(objectName());
			foreach (ServerPlayer *seat, room->getAllPlayers(true)) {
				room->detachSkillFromPlayer(seat, "rhshuguangv", false, true);
				if (seat->getMark("@time") > 0)
					seat->loseAllMarks("@time");
			}
			return true;
		}
		return true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventAcquireSkill && player && player->isAlive() && player->hasSkill(objectName())
			&& reihouChangedSkill(data) == objectName())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		room->sendCompulsoryTriggerLog(player, objectName());
		room->broadcastSkillInvoke(objectName());
		player->gainMark("@time", 4);
		foreach (ServerPlayer *p, room->getAlivePlayers())
			room->attachSkillToPlayer(p, "rhshuguangv");
		return false;
	}
};

class RhShuguangMaxCards : public MaxCardsSkillV2
{
public:
	RhShuguangMaxCards() : MaxCardsSkillV2("#rhshuguang")
	{
		setHolderSelector(CorrectSkill_System);
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
	{
		const int mark = context.primary ? context.primary->getMark("rhshuguang") : 0;
		return mark > 0 ? CorrectSkillResult::useAmount(-mark) : CorrectSkillResult::noEffect();
	}
};

class RhKuili : public TriggerSkillV2
{
public:
	RhKuili() : TriggerSkillV2("rhkuili") { events << EventPhaseStart; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || player->getPhase() != Player::RoundStart)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			if (holder->getMark("@time") > 0)
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker)
			return false;
		const int marks = player->getMark("@time");
		if (marks <= 0)
			return false;
		QStringList choices;
		for (int i = 1; i <= marks; ++i)
			choices << QString::number(i);
		const QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		const int n = choice.toInt();
		if (n <= 0 || n > marks)
			return false;
		player->loseMark("@time", n);
		if (ctx.invoker->isAlive())
			ctx.invoker->gainMark("@time", n);
		return false;
	}
};

class RhChenshengViewAs : public ViewAsSkillV2
{
public:
	RhChenshengViewAs() : ViewAsSkillV2("rhchensheng") {}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || !request.activationRef.isValid()
			|| request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
			return false;
		Card *card = reihouDrowning();
		if (!card)
			return false;
		const bool available = card->isAvailable(request.initiator);
		delete card;
		return available;
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request))
			return nullptr;
		return reihouDrowning();
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		if (ctx.invoker && ctx.invoker->isAlive())
			ctx.invoker->getRoom()->removeReihouCard(ctx.invoker);
		return ContinueEffects;
	}
};

class RhChensheng : public TriggerSkillV2
{
public:
	RhChensheng() : TriggerSkillV2("rhchensheng")
	{
		events << Damaged;
		view_as_skill = new RhChenshengViewAs;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()))
			return TriggerList();
		return TriggerList{{player, QStringList(objectName())}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room || !player->askForSkillInvoke(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->removeReihouCard(player);
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room || !player->isAlive())
			return false;
		Card *card = reihouDrowning();
		if (!card)
			return false;
		QList<ServerPlayer *> targets;
		if (card->isAvailable(player)) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (card->targetFilter(QList<const Player *>(), p, player))
					targets << p;
			}
		}
		if (targets.isEmpty()) {
			delete card;
			return false;
		}
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@rhchensheng", false, true);
		if (!target) {
			delete card;
			return false;
		}
		card->deleteLater();
		room->useCard(CardUseStruct(card, player, QList<ServerPlayer *>() << target));
		return false;
	}
};

class RhXinmo : public TriggerSkillV2
{
public:
	RhXinmo() : TriggerSkillV2("rhxinmo")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (!room || event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
			return false;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			room->setPlayerMark(p, objectName(), 0);
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != CardsMoveOneTime || !player || !player->isAlive() || !player->hasSkill(objectName())
			|| player->getMark(objectName()) != 0)
			return TriggerList();
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player)
			return TriggerList();
		Player *dest = move.to ? move.to : move.origin_to;
		const Player::Place place = move.to ? move.to_place : move.origin_to_place;
		if (dest == player && (place == Player::PlaceHand || place == Player::PlaceEquip))
			return TriggerList();
		int count = 0;
		for (int i = 0; i < move.card_ids.length() && i < move.from_places.length(); ++i) {
			if (move.from_places.at(i) != Player::PlaceHand && move.from_places.at(i) != Player::PlaceEquip)
				continue;
			const Card *card = Sanguosha->getCard(move.card_ids.at(i));
			if (card && card->isRed())
				++count;
		}
		if (count <= 0)
			return TriggerList();
		return TriggerList{{player, QStringList(objectName() + "*" + QString::number(count))}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
			"@rhxinmo", true, true);
		if (!target)
			return false;
		player->setTag("RhXinmoTarget", target->objectName());
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		const QString name = player->getTag("RhXinmoTarget").toString();
		player->removeTag("RhXinmoTarget");
		ServerPlayer *target = room->findPlayerByObjectName(name);
		if (!target)
			return false;
		target->drawCards(1, objectName());
		if (player->isAlive() && !player->faceUp())
			player->turnOver();
		if (player->isAlive() && player->isChained())
			room->setPlayerProperty(player, "chained", false);
		return false;
	}
};

bool reihouHasRed(const Player *player)
{
	if (!player)
		return false;
	QList<int> ids = player->handCards();
	ids << player->getEquipsId() << player->getHandPile();
	foreach (int id, ids) {
		const Card *card = Sanguosha->getCard(id);
		if (card && card->isRed())
			return true;
	}
	return false;
}

class RhFuyu : public TriggerSkillV2
{
public:
	RhFuyu() : TriggerSkillV2("rhfuyu") { events << EventPhaseStart; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || player->getPhase() != Player::Play)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName()))
			list.insert(holder, QStringList(objectName()));
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker)
			return false;
		const QString choice = room->askForChoice(player, objectName(), "reduce+increase");
		LogMessage log;
		log.type = "#RhFuyu";
		log.from = player;
		log.to << ctx.invoker;
		if (choice == "reduce") {
			room->setPlayerFlag(ctx.invoker, "RhFuyuReduce");
			log.arg = "1";
			log.arg2 = "-1";
		} else {
			room->setPlayerFlag(ctx.invoker, "RhFuyuIncrease");
			log.arg = "2";
			log.arg2 = "+1";
		}
		room->sendLog(log);
		return false;
	}
};

class RhFuyuDistance : public DistanceSkillV2
{
public:
	RhFuyuDistance() : DistanceSkillV2("#rhfuyu")
	{
		setHolderSelector(CorrectSkill_System);
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
	{
		if (!context.primary)
			return CorrectSkillResult::noEffect();
		int correct = 0;
		if (context.primary->hasFlag("RhFuyuReduce"))
			--correct;
		if (context.primary->hasFlag("RhFuyuIncrease"))
			++correct;
		return correct == 0 ? CorrectSkillResult::noEffect() : CorrectSkillResult::useAmount(correct);
	}
};

class RhYaodao : public TriggerSkillV2
{
public:
	RhYaodao() : TriggerSkillV2("rhyaodao")
	{
		events << EventAcquireSkill << EventLoseSkill;
		frequency = NotCompulsory;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventLoseSkill || !room || !player || reihouChangedSkill(data) != objectName())
			return false;
		QList<ServerPlayer *> borrowed;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (!p->getTag("Reihou2").toString().isEmpty())
				borrowed << p;
		}
		if (borrowed.isEmpty())
			return true;
		room->sendCompulsoryTriggerLog(player, objectName());
		room->broadcastSkillInvoke(objectName());
		foreach (ServerPlayer *p, borrowed)
			room->removeReihouCard(p, true);
		return true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventAcquireSkill && player && player->isAlive() && player->hasSkill(objectName())
			&& reihouChangedSkill(data) == objectName())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
			"@rhyaodao", true, true);
		if (!target)
			return false;
		player->setTag("RhYaodaoTarget", target->objectName());
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return true;
		const QString name = player->getTag("RhYaodaoTarget").toString();
		player->removeTag("RhYaodaoTarget");
		ServerPlayer *target = room->findPlayerByObjectName(name);
		Package *pack = Sanguosha->getPackage("tenshi-reihou");
		QList<const General *> pool;
		if (pack) {
			foreach (const General *general, pack->findChildren<const General *>()) {
				if (general && general->objectName() != "reihou042")
					pool << general;
			}
		}
		if (target && !pool.isEmpty()) {
			const General *general = pool.at(room->roomRuntime()->rng().bounded(pool.length()));
			LogMessage log;
			log.type = "#RhYaodao";
			log.from = target;
			log.arg = general->objectName();
			room->sendLog(log);
			room->attachReihouCard(target, general->objectName(), true);
		}
		return true;
	}
};

// Peach from rhchuilu: during play it may target another wounded player.
// Rescue uses keep the target fixed, like an ordinary Peach.
class RhChuiluPeach : public Peach
{
public:
	RhChuiluPeach(Card::Suit suit, int number) : Peach(suit, number) { target_fixed = false; }

	static bool responseUse()
	{
		RoomState *state = Sanguosha->currentRoomState();
		return state && state->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
	}

	bool targetFixed() const override { return responseUse(); }

	bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override
	{
		if (responseUse())
			return Peach::targetFilter(targets, to_select, Self);
		const int limit = 1 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
		return targets.length() < limit && to_select->isWounded() && !Self->isProhibited(to_select, this, targets);
	}

	bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const override
	{
		if (responseUse())
			return true;
		return !targets.isEmpty() || (Self && Self->isWounded() && !Self->isProhibited(Self, this));
	}

	bool isAvailable(const Player *player) const override
	{
		if (!player || !BasicCard::isAvailable(player))
			return false;
		QList<const Player *> candidates = player->getAliveSiblings();
		candidates << player;
		foreach (const Player *p, candidates) {
			if (p->isWounded() && !player->isProhibited(p, this))
				return true;
		}
		return false;
	}
};

class RhChuilu : public ViewAsSkillV2
{
public:
	RhChuilu() : ViewAsSkillV2("rhchuilu", 1) { setResponseOrUse(true); }

	bool willThrowSelectedCards() const override { return false; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || !request.activationRef.isValid() || !reihouHasRed(request.initiator))
			return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
			RhChuiluPeach *peach = new RhChuiluPeach(Card::NoSuit, 0);
			peach->setSkillName(objectName());
			const bool available = peach->isAvailable(request.initiator);
			delete peach;
			return available;
		}
		return request.pattern.contains("peach");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || !request.selectedCardIds.isEmpty())
			return false;
		return candidate->isRed() && reihouHolds(request.initiator, candidate->getEffectiveId());
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request))
			return nullptr;
		const Card *origin = Sanguosha->getCard(request.selectedCardIds.first());
		if (!origin)
			return nullptr;
		Card *peach = new RhChuiluPeach(origin->getSuit(), origin->getNumber());
		peach->setSkillName(objectName());
		peach->addSubcard(origin);
		return peach;
	}
};

class RhNajieViewAs : public ViewAsSkillV2
{
public:
	RhNajieViewAs() : ViewAsSkillV2("rhnajie", 1)
	{
		setExpandPile("tea");
	}

	bool willThrowSelectedCards() const override { return false; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid()
			&& request.pattern == "@@rhnajie" && !request.initiator->getPile("tea").isEmpty();
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || !request.selectedCardIds.isEmpty())
			return false;
		return request.initiator->getPile("tea").contains(candidate->getEffectiveId());
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !ctx.use_card)
			return FinishSkill;
		CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString());
		source->getRoom()->throwCard(ctx.use_card, reason, source);
		return FinishSkill;
	}
};

class RhNajie : public TriggerSkillV2
{
public:
	RhNajie() : TriggerSkillV2("rhnajie")
	{
		events << EventAcquireSkill << TargetSpecified;
		view_as_skill = new RhNajieViewAs;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player)
			return TriggerList();
		if (event == EventAcquireSkill) {
			if (player->isAlive() && player->hasSkill(objectName()) && !player->isKongcheng()
				&& reihouChangedSkill(data) == objectName())
				return TriggerList{{player, QStringList(objectName())}};
			return TriggerList();
		}
		if (player->getPhase() != Player::Play)
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Slash"))
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			if (!holder->getPile("tea").isEmpty())
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		if (event == EventAcquireSkill) {
			Card *dummy = room->askForExchange(player, objectName(), 998, 1, false, "@rhnajie-invoke", true);
			if (dummy) {
				room->sendCompulsoryTriggerLog(player, objectName());
				room->broadcastSkillInvoke(objectName());
				player->addToPile("tea", dummy->getSubcards(), true);
				delete dummy;
			}
			return false;
		}
		return room->askForUseCard(player, "@@rhnajie", "@rhnajie", -1, Card::MethodNone);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!room || !ctx.invoker || !ctx.original_data)
			return false;
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (use.m_addHistory && use.card) {
			room->addPlayerHistory(ctx.invoker, use.card->getClassName(), -1);
			use.m_addHistory = false;
			*ctx.original_data = QVariant::fromValue(use);
		}
		return false;
	}
};

class RhNajieClear : public TriggerSkillV2
{
public:
	RhNajieClear() : TriggerSkillV2("#rhnajie")
	{
		events << EventLoseSkill;
		frequency = NotCompulsory;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventLoseSkill || !room || !player || reihouChangedSkill(data) != "rhnajie")
			return false;
		if (player->getPile("tea").isEmpty())
			return true;
		room->sendCompulsoryTriggerLog(player, "rhnajie");
		room->broadcastSkillInvoke("rhnajie");
		player->clearOnePrivatePile("tea");
		return true;
	}
};

class RhXiaozhangViewAs : public ViewAsSkillV2
{
public:
	RhXiaozhangViewAs() : ViewAsSkillV2("rhxiaozhang")
	{
		setExpandPile("soil");
	}

	bool willThrowSelectedCards() const override { return false; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid()
			&& request.pattern == "@@rhxiaozhang" && !request.initiator->getPile("soil").isEmpty();
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || request.selectedCardIds.length() >= 2)
			return false;
		return request.initiator->getPile("soil").contains(candidate->getEffectiveId());
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.length() >= 1 && request.selectedCardIds.length() <= 2;
	}

	TargetMode targetMode() const override { return NoTarget; }

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.isEmpty();
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
			return FinishSkill;
		Room *room = source->getRoom();
		LogMessage log;
		log.type = "$MoveCard";
		log.from = source;
		log.to << source;
		QStringList ids;
		foreach (int id, ctx.use_card->getSubcards())
			ids << QString::number(id);
		log.card_str = ids.join("+");
		room->sendLog(log);
		room->obtainCard(source, ctx.use_card);
		return FinishSkill;
	}
};

class RhXiaozhang : public TriggerSkillV2
{
public:
	RhXiaozhang() : TriggerSkillV2("rhxiaozhang")
	{
		events << EventAcquireSkill << EventLoseSkill;
		frequency = NotCompulsory;
		view_as_skill = new RhXiaozhangViewAs;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventLoseSkill || !room || !player || reihouChangedSkill(data) != objectName())
			return false;
		if (!player->getPile("soil").isEmpty()) {
			room->sendCompulsoryTriggerLog(player, objectName());
			room->broadcastSkillInvoke(objectName());
			room->askForUseCard(player, "@@rhxiaozhang", "@rhxiaozhang-get", -1, Card::MethodNone);
			player->clearOnePrivatePile("soil");
		}
		return true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventAcquireSkill && player && player->isAlive() && player->hasSkill(objectName())
			&& reihouChangedSkill(data) == objectName())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		room->sendCompulsoryTriggerLog(player, objectName());
		room->broadcastSkillInvoke(objectName());
		player->addToPile("soil", room->getNCards(room->alivePlayerCount()));
		return false;
	}
};

class RhXiaozhangTrigger : public TriggerSkillV2
{
public:
	RhXiaozhangTrigger() : TriggerSkillV2("#rhxiaozhang") { events << EventPhaseStart; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!room || !player || !player->isAlive() || player->getPhase() != Player::Finish
			|| !player->canDiscard(player, "he"))
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName("rhxiaozhang")) {
			if (!holder->getPile("soil").isEmpty() && holder->hasSkill(objectName()))
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !ctx.original_data)
			return false;
		if (!room->askForCard(ctx.invoker, ".|.|.|hand,equipped", "@rhxiaozhang:" + player->objectName(),
				*ctx.original_data, "rhxiaozhang"))
			return false;
		LogMessage log;
		log.type = "#InvokeOthersSkill";
		log.from = ctx.invoker;
		log.to << player;
		log.arg = "rhxiaozhang";
		room->sendLog(log);
		room->broadcastSkillInvoke("rhxiaozhang");
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker)
			return false;
		const QList<int> ids = player->getPile("soil");
		if (ids.isEmpty())
			return false;
		room->fillAG(ids, ctx.invoker);
		const int id = room->askForAG(ctx.invoker, ids, false, "rhxiaozhang");
		room->clearAG(ctx.invoker);
		if (id >= 0)
			room->obtainCard(ctx.invoker, id);
		return false;
	}
};
int reihouHandSubcards(Room *room, const Card *card, ServerPlayer *owner)
{
	if (!room || !card || !owner)
		return 0;
	int count = 0;
	foreach (int id, card->getSubcards()) {
		if (room->getCardOwner(id) == owner && room->getCardPlace(id) == Player::PlaceHand)
			++count;
	}
	return count;
}

QList<ServerPlayer *> reihouFapoVictims(Room *room, ServerPlayer *player, const CardUseStruct &use)
{
	QList<ServerPlayer *> victims;
	if (!room || !player || !use.card)
		return victims;
	const int num = player->getHandcardNum() - reihouHandSubcards(room, use.card, player);
	foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
		if (use.to.contains(p) || !player->canSlash(p, use.card))
			continue;
		if (num < p->getHandcardNum() - reihouHandSubcards(room, use.card, p))
			victims << p;
	}
	return victims;
}

class RhFapo : public TriggerSkillV2
{
public:
	RhFapo() : TriggerSkillV2("rhfapo") { events << PreCardUsed << EventPhaseChanging; }

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event != EventPhaseChanging || !room || !player)
			return false;
		room->setPlayerMark(player, objectName(), 0);
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != PreCardUsed || !room || !player || !player->isAlive() || !player->hasSkill(objectName())
			|| player->getPhase() != Player::Play || player->getMark(objectName()) != 0)
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Slash"))
			return TriggerList();
		if (reihouFapoVictims(room, player, use).isEmpty())
			return TriggerList();
		return TriggerList{{player, QStringList(objectName())}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.original_data)
			return false;
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		QList<ServerPlayer *> victims = reihouFapoVictims(room, player, use);
		while (!victims.isEmpty()) {
			ServerPlayer *victim = room->askForPlayerChosen(player, victims, objectName(), "@rhfapo", true, true);
			if (!victim)
				break;
			room->broadcastSkillInvoke(objectName());
			LogMessage log;
			log.type = "#BecomeTarget";
			log.from = victim;
			log.card_str = use.card ? use.card->toString() : QString();
			room->sendLog(log);
			room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), victim->objectName());
			use.to << victim;
			room->sortByActionOrder(use.to);
			*ctx.original_data = QVariant::fromValue(use);
			victims.removeOne(victim);
			room->addPlayerMark(player, objectName());
		}
		return false;
	}
};

class RhHujuan : public TriggerSkillV2
{
public:
	RhHujuan() : TriggerSkillV2("rhhujuan")
	{
		events << DamageForseen << PreHpLost;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (player && player->isAlive() && player->hasSkill(objectName()))
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		room->sendCompulsoryTriggerLog(player, objectName());
		room->broadcastSkillInvoke(objectName());
		return true;
	}
};

class RhDanshen : public TriggerSkillV2
{
public:
	RhDanshen() : TriggerSkillV2("rhdanshen") { events << Damage; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player || !player->isAlive())
			return TriggerList();
		const DamageStruct damage = data.value<DamageStruct>();
		if (!damage.to || !damage.to->isAlive() || damage.to->getHp() >= player->getHp())
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName()))
			list.insert(holder, QStringList(objectName()));
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		player->drawCards(1, objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || player == ctx.invoker || player->isNude())
			return false;
		const QVariant payload = ctx.original_data ? *ctx.original_data : QVariant();
		const Card *card = room->askForCard(player, ".|.|.|hand,equipped",
			"@rhdanshen:" + ctx.invoker->objectName(), payload, Card::MethodNone, nullptr, false, objectName());
		if (!card) {
			const QList<const Card *> cards = player->getCards("he");
			if (cards.isEmpty())
				return false;
			card = cards.at(room->roomRuntime()->rng().bounded(cards.length()));
		}
		if (!card)
			return false;
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), ctx.invoker->objectName(),
			objectName(), QString());
		room->obtainCard(ctx.invoker, card, reason, false);
		return false;
	}
};

class RhZhangchi : public TriggerSkillV2
{
public:
	RhZhangchi() : TriggerSkillV2("rhzhangchi")
	{
		events << EventAcquireSkill << EventLoseSkill << PreCardUsed;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room || !player)
			return false;
		if (event == EventLoseSkill && reihouChangedSkill(data) == objectName()) {
			const QString name = player->getTag(objectName()).toString();
			player->removeTag(objectName());
			ServerPlayer *target = name.isEmpty() ? nullptr : room->findPlayerByObjectName(name, true);
			if (target) {
				room->setPlayerMark(target, "@knot", 0);
				room->detachSkillFromPlayer(target, "rhzhangchiv", false, true);
			}
			return true;
		}
		if (event == PreCardUsed && player->hasFlag("RhZhangchiUsed")
			&& data.value<CardUseStruct>().card->getTypeId() != Card::TypeSkill) {
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());
			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->setPlayerFlag(player, "-RhZhangchiUsed");
			room->setPlayerFlag(player, "-cardIgnoreLegality:adjacent");
		}
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventAcquireSkill && player && player->isAlive() && player->hasSkill(objectName())
			&& reihouChangedSkill(data) == objectName())
			return TriggerList{{player, QStringList(objectName())}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
			"@rhzhangchi", true, true);
		if (!target)
			return false;
		player->setTag(objectName(), target->objectName());
		room->broadcastSkillInvoke(objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || !room)
			return false;
		ServerPlayer *target = room->findPlayerByObjectName(player->getTag(objectName()).toString());
		if (target) {
			room->setPlayerMark(target, "@knot", 1);
			room->attachSkillToPlayer(target, "rhzhangchiv");
		}
		return false;
	}
};

class RhZhangchiProhibit : public ProhibitSkill
{
public:
	RhZhangchiProhibit() : ProhibitSkill("#rhzhangchi") { frequency = NotCompulsory; }

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
	{
		return from && to && card && to->getMark("@knot") > 0 && to->isAdjacentTo(from)
			&& card->getTypeId() != Card::TypeSkill;
	}
};

class RhZhangchiVS : public ViewAsSkillV2
{
public:
	RhZhangchiVS() : ViewAsSkillV2("rhzhangchiv") { attached_lord_skill = true; }

	bool shouldBeVisible(const Player *player) const override
	{
		return player && player->getMark("@knot") > 0;
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid() && request.pattern.isEmpty()
			&& request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getMark("@knot") > 0
			&& !request.initiator->hasFlag("Global_RhZhangchiFailed");
	}

	TargetMode targetMode() const override { return NoTarget; }

	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.isEmpty();
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		if (!player)
			return FinishSkill;
		Room *room = player->getRoom();
		RoomState *state = Sanguosha->currentRoomState();
		if (!room || !state)
			return FinishSkill;
		const CardUseStruct::CardUseReason previousReason = state->getCurrentCardUseReason();
		const QString previousPattern = state->getCurrentCardUsePattern();
		// The neighbour target comes from the cardIgnoreLegality:adjacent hook. A response use
		// keeps the server from re-checking isAvailable(), whose target conditions it lifts.
		room->setPlayerFlag(player, "RhZhangchiUsed");
		room->setPlayerFlag(player, "cardIgnoreLegality:adjacent");
		QString pattern = "^Jink+^Nullification";
		if (!player->canSlashWithoutCrossbow())
			pattern.append("+^Slash");
		if (!Analeptic::IsAvailable(player))
			pattern.append("+^Analeptic");
		const Card *used = room->askForUseCard(player, pattern, "@rhzhangchi-use");
		state->setCurrentCardUseReason(previousReason);
		state->setCurrentCardUsePattern(previousPattern);
		if (!used) {
			room->setPlayerFlag(player, "Global_RhZhangchiFailed");
			room->setPlayerFlag(player, "-RhZhangchiUsed");
			room->setPlayerFlag(player, "-cardIgnoreLegality:adjacent");
		}
		return FinishSkill;
	}
};

class RhQiaozouViewAs : public ViewAsSkillV2
{
public:
	RhQiaozouViewAs() : ViewAsSkillV2("rhqiaozou", 1) { setResponseOrUse(true); }

	bool willThrowSelectedCards() const override { return false; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.activationRef.isValid() && request.pattern == "@@rhqiaozou"
			&& !request.initiator->property("rhqiaozou").toString().isEmpty();
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || !request.selectedCardIds.isEmpty())
			return false;
		return candidate->isRed() && request.initiator->handCards().contains(candidate->getEffectiveId());
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || !cardSelectionFeasible(request))
			return nullptr;
		const QString name = request.initiator->property("rhqiaozou").toString();
		const Card *origin = Sanguosha->getCard(request.selectedCardIds.first());
		if (name.isEmpty() || !origin)
			return nullptr;
		Card *card = Sanguosha->cloneCard(name, origin->getSuit(), origin->getNumber());
		if (!card)
			return nullptr;
		card->setSkillName(objectName());
		card->addSubcard(origin);
		return card;
	}
};

class RhQiaozou : public TriggerSkillV2
{
public:
	RhQiaozou() : TriggerSkillV2("rhqiaozou")
	{
		events << CardFinished << EventPhaseChanging;
		view_as_skill = new RhQiaozouViewAs;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (!room || event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
			return false;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			room->setPlayerMark(p, objectName(), 0);
		return false;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != CardFinished || !room || !player || room->getCurrent() != player)
			return TriggerList();
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill || use.card->getTypeId() == Card::TypeEquip)
			return TriggerList();
		Card *probe = Sanguosha->cloneCard(use.card);
		if (!probe)
			return TriggerList();
		const bool available = probe->isAvailable(player);
		delete probe;
		if (!available)
			return TriggerList();
		TriggerList list;
		foreach (ServerPlayer *holder, room->findPlayersBySkillName(objectName())) {
			if (holder->getMark(objectName()) == 0)
				list.insert(holder, QStringList(objectName()));
		}
		return list;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !player->askForSkillInvoke(objectName(), ctx.invoker))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, objectName());
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !room || !ctx.invoker || !ctx.original_data)
			return false;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.card)
			return false;
		room->setPlayerProperty(ctx.invoker, "rhqiaozou", use.card->objectName());
		room->askForUseCard(ctx.invoker, "@@rhqiaozou",
			QString("@rhqiaozou:%1::%2").arg(player->objectName()).arg(use.card->objectName()));
		room->setPlayerProperty(ctx.invoker, "rhqiaozou", QVariant());
		return false;
	}
};
}
TenshiReihouPackage::TenshiReihouPackage()
	: Package("tenshi-reihou")
{
	General *reihou001 = new General(this, "reihou001", "rei", 4, true, true);
	reihou001->addSkill(new RhDuanlong);
	reihou001->addSkill(new RhPohuang);
	reihou001->addSkill(new SlashNoDistanceLimitSkill("rhpohuang"));
	related_skills.insert("rhpohuang", "#rhpohuang-slash-ndl");

	General *reihou002 = new General(this, "reihou002", "rei", 4, true, true);
	reihou002->addSkill(new RhRuyi);

	General *reihou003 = new General(this, "reihou003", "rei", 4, true, true);
	reihou003->addSkill(new RhHuanjie);

	General *reihou004 = new General(this, "reihou004", "rei", 4, true, true);
	reihou004->addSkill(new RhHonghuang);
	reihou004->addSkill(new RhHonghuangTargetMod);
	related_skills.insert("rhhonghuang", "#rhhonghuang");

	General *reihou005 = new General(this, "reihou005", "rei", 4, true, true);
	reihou005->addSkill(new RhLiufu);
	reihou005->addSkill(new RhLiufuGet);
	related_skills.insert("rhliufu", "#rhliufu");

	General *reihou006 = new General(this, "reihou006", "rei", 4, true, true);
	reihou006->addSkill(new RhPujiu);
	reihou006->addSkill(new RhPujiuTrigger);
	related_skills.insert("rhpujiu", "#rhpujiu");

	General *reihou007 = new General(this, "reihou007", "rei", 4, true, true);
	reihou007->addSkill(new RhXuesha);
	reihou007->addSkill(new RhXueshaTargetMod);
	related_skills.insert("rhxuesha", "#rhxuesha");

	General *reihou008 = new General(this, "reihou008", "rei", 4, true, true);
	reihou008->addSkill(new RhZhenyao);
	reihou008->addSkill(new RhZhenyaoPrevent);
	related_skills.insert("rhzhenyao", "#rhzhenyao");

	General *reihou009 = new General(this, "reihou009", "rei", 4, true, true);
	reihou009->addSkill(new RhGaiming);
	reihou009->addSkill(new RhXusheng);

	General *reihou010 = new General(this, "reihou010", "rei", 4, true, true);
	reihou010->addSkill(new RhBaiming);

	General *reihou011 = new General(this, "reihou011", "rei", 4, true, true);
	reihou011->addSkill(new RhChengfeng);
	reihou011->addSkill(new RhYuhuo);

	General *reihou012 = new General(this, "reihou012", "rei", 4, true, true);
	reihou012->addSkill(new RhZhengyang);
	reihou012->addSkill(new RhZhengyangTrigger);
	reihou012->addSkill(new RhZhengyangProhibit);
	related_skills.insert("rhzhengyang", "#rhzhengyang");
	related_skills.insert("rhzhengyang", "#rhzhengyang-prohibit");

	General *reihou013 = new General(this, "reihou013", "rei", 4, true, true);
	reihou013->addSkill(new RhChunyin);

	General *reihou014 = new General(this, "reihou014", "rei", 4, true, true);
	reihou014->addSkill(new RhSanmei);

	General *reihou015 = new General(this, "reihou015", "rei", 4, true, true);
	reihou015->addSkill(new RhXuanren);

	General *reihou016 = new General(this, "reihou016", "rei", 4, true, true);
	reihou016->addSkill(new RhGuozao);

	General *reihou017 = new General(this, "reihou017", "rei", 4, true, true);
	reihou017->addSkill(new RhShenguang);

	General *reihou018 = new General(this, "reihou018", "rei", 4, true, true);
	reihou018->addSkill(new RhMangti);
	reihou018->addSkill(new RhMangtiRange);
	related_skills.insert("rhmangti", "#rhmangti");
	reihou018->addSkill(new RhLingwei);

	General *reihou019 = new General(this, "reihou019", "rei", 4, true, true);
	reihou019->addSkill(new RhWangzhong);

	General *reihou020 = new General(this, "reihou020", "rei", 4, true, true);
	reihou020->addSkill(new RhCuigu);
	reihou020->addSkill(new RhCuiguProhibit);
	related_skills.insert("rhcuigu", "#rhcuigu");
	reihou020->addSkill(new RhLinghai);

	General *reihou021 = new General(this, "reihou021", "rei", 4, true, true);
	reihou021->addSkill(new RhHaoqiang);
	reihou021->addSkill(new RhLiedan);

	General *reihou022 = new General(this, "reihou022", "rei", 4, true, true);
	reihou022->addSkill(new RhYaren);
	reihou022->addSkill(new RhShixiang);

	General *reihou023 = new General(this, "reihou023", "rei", 4, true, true);
	reihou023->addSkill(new RhYinren);
	reihou023->addSkill(new RhYinrenTrigger);
	related_skills.insert("rhyinren", "#rhyinren");
	reihou023->addSkill(new SlashNoDistanceLimitSkill("rhyinren"));
	related_skills.insert("rhyinren", "#rhyinren-slash-ndl");
	reihou023->addSkill(new RhYeming);

	General *reihou024 = new General(this, "reihou024", "rei", 4, true, true);
	reihou024->addSkill(new RhYiqie);
	reihou024->addSkill(new RhYaozhang);

	General *reihou025 = new General(this, "reihou025", "rei", 4, true, true);
	reihou025->addSkill(new RhSanglv);

	General *reihou026 = new General(this, "reihou026", "rei", 4, true, true);
	reihou026->addSkill(new RhWuyin);

	General *reihou027 = new General(this, "reihou027", "rei", 4, true, true);
	reihou027->addSkill(new RhChanling);

	General *reihou028 = new General(this, "reihou028", "rei", 4, true, true);
	reihou028->addSkill(new RhShendai);

	General *reihou029 = new General(this, "reihou029", "rei", 4, true, true);
	reihou029->addSkill(new RhJiyu);

	General *reihou030 = new General(this, "reihou030", "rei", 4, true, true);
	reihou030->addSkill(new RhJinbei);

	General *reihou031 = new General(this, "reihou031", "rei", 4, true, true);
	reihou031->addSkill(new RhPihuai);

	General *reihou032 = new General(this, "reihou032", "rei", 4, true, true);
	reihou032->addSkill(new RhDangmo);
	reihou032->addSkill(new RhBumo);

	General *reihou033 = new General(this, "reihou033", "rei", 4, true, true);
	reihou033->addSkill(new RhNieji);

	General *reihou034 = new General(this, "reihou034", "rei", 4, true, true);
	reihou034->addSkill(new RhShenluo);
	reihou034->addSkill(new RhShenluoRecover);
	related_skills.insert("rhshenluo", "#rhshenluo");

	General *reihou035 = new General(this, "reihou035", "rei", 4, true, true);
	reihou035->addSkill(new RhYousheng);


	General *reihou036 = new General(this, "reihou036", "rei", 4, true, true);
	reihou036->addSkill(new RhJianuo);

	General *reihou037 = new General(this, "reihou037", "rei", 4, true, true);
	reihou037->addSkill(new RhYizhi);
	reihou037->addSkill(new RhGuozhu);

	General *reihou038 = new General(this, "reihou038", "rei", 4, true, true);
	reihou038->addSkill(new RhShuguang);
	reihou038->addSkill(new RhShuguangMaxCards);
	related_skills.insert("rhshuguang", "#rhshuguang");
	reihou038->addSkill(new RhKuili);

	General *reihou039 = new General(this, "reihou039", "rei", 4, true, true);
	reihou039->addSkill(new RhChensheng);

	General *reihou040 = new General(this, "reihou040", "rei", 4, true, true);
	reihou040->addSkill(new RhXinmo);

	skills << new RhShuguangGiven;
	skills << new RhZhangchiVS;


	General *reihou041 = new General(this, "reihou041", "rei", 4, true, true);
	reihou041->addSkill(new RhFuyu);
	reihou041->addSkill(new RhFuyuDistance);
	related_skills.insert("rhfuyu", "#rhfuyu");

	General *reihou042 = new General(this, "reihou042", "rei", 4, true, true);
	reihou042->addSkill(new RhYaodao);

	General *reihou043 = new General(this, "reihou043", "rei", 4, true, true);
	reihou043->addSkill(new RhChuilu);

	General *reihou044 = new General(this, "reihou044", "rei", 4, true, true);
	reihou044->addSkill(new RhNajie);
	reihou044->addSkill(new RhNajieClear);
	related_skills.insert("rhnajie", "#rhnajie");

	General *reihou045 = new General(this, "reihou045", "rei", 4, true, true);
	reihou045->addSkill(new RhXiaozhang);
	reihou045->addSkill(new RhXiaozhangTrigger);
	related_skills.insert("rhxiaozhang", "#rhxiaozhang");
	General *reihou046 = new General(this, "reihou046", "rei", 4, true, true);
	reihou046->addSkill(new RhFapo);
	reihou046->addSkill(new RhHujuan);

	General *reihou047 = new General(this, "reihou047", "rei", 4, true, true);
	reihou047->addSkill(new RhDanshen);

	General *reihou048 = new General(this, "reihou048", "rei", 4, true, true);
	reihou048->addSkill(new RhZhangchi);
	reihou048->addSkill(new RhZhangchiProhibit);
	related_skills.insert("rhzhangchi", "#rhzhangchi");

	General *reihou049 = new General(this, "reihou049", "rei", 4, true, true);
	reihou049->addSkill(new RhQiaozou);


}

ADD_PACKAGE(TenshiReihou)
