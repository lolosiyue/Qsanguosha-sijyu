#include "yitian.h"
#include "engine.h"
#include "maneuvering.h"
#include "clientplayer.h"
#include "room.h"
#include "roomthread.h"
#include "util.h"

namespace {

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
		return ctx.owner && ctx.owner->hasFlag("YitianSwordDamage");
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive())
			return TriggerList();
		if (triggerEvent == DamageComplete) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (!damage.prevented && player != room->getCurrent() && WeaponSkillV2::triggerable(player))
				return TriggerList{{player, QStringList{objectName()}}};
			return TriggerList();
		}
		if (player->hasFlag("YitianSwordDamage")
			&& yitianSwordLeaving(player, data.value<CardsMoveOneTimeStruct>()))
			return TriggerList{{player, QStringList{objectName()}}};
		return TriggerList();
	}

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player)
			return false;
		if (triggerEvent == DamageComplete) {
			if (!ctx.original_data)
				return false;
			const DamageStruct damage = ctx.original_data->value<DamageStruct>();
			if (!damage.prevented && player != room->getCurrent() && WeaponSkillV2::triggerable(player))
				room->askForUseCard(player, "slash", "@YitianSword-slash");
			return false;
		}
		player->setFlags("-YitianSwordDamage");
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@YitianSword-lost", true, true);
		if (target)
			room->damage(DamageStruct(objectName(), player, target));
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
	if (player->isAlive() && player->hasWeapon(objectName()))
		player->setFlags("YitianSwordDamage");
	Weapon::onUninstall(player);
}

class YTChengxiangViewAsSkill : public ViewAsSkillV2
{
public:
	YTChengxiangViewAsSkill() : ViewAsSkillV2("ytchengxiang", 3)
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
		if (!player || !candidate || candidate->hasFlag("using") || request.selectedCardIds.size() >= 3)
			return false;
		const int id = candidate->getEffectiveId();
		if (id < 0 || request.selectedCardIds.contains(id))
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
		return candidate && selected.length() < request.selectedCardIds.length();
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
			room->recover(target, recover);
		} else
			target->drawCards(2);
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

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
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

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player)
			return false;
		room->sendCompulsoryTriggerLog(player, this);
		player->throwAllHandCards(objectName());
		room->loseHp(HpLostStruct(player, 1, "zaoyao", player));
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
		return ctx.owner && room->askForSkillInvoke(ctx.owner, objectName());
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *weiwudi, SkillContext &) const override
	{
		if (!weiwudi)
			return false;
		const QString choice = room->askForChoice(weiwudi, objectName(), "modify+obtain");
		int index = qsanRandomBounded(2);
		if (choice == "modify") {
			ServerPlayer *to_modify = room->askForPlayerChosen(weiwudi, room->getOtherPlayers(weiwudi), objectName());
			if (!to_modify)
				return false;
			room->setTag("Guixin2Modify", QVariant::fromValue(to_modify));
			QStringList kingdomList = Sanguosha->getKingdoms();
			kingdomList.removeOne("god");
			kingdomList.removeOne(to_modify->getKingdom());
			room->broadcastSkillInvoke(objectName(), index++);
			room->changeKingdom(to_modify, room->askForChoice(weiwudi, objectName(), kingdomList.join("+")));
		} else if (choice == "obtain") {
			room->broadcastSkillInvoke(objectName(), index + 3);
			QStringList lords = Sanguosha->getLords();
			foreach (ServerPlayer *player, room->getAlivePlayers()) {
				lords.removeOne(player->getGeneralName());
				if (player->getGeneral2())
					lords.removeOne(player->getGeneral2Name());
			}
			QStringList lord_skills;
			foreach (const QString &lord, lords) {
				const General *general = Sanguosha->getGeneral(lord);
				if (!general)
					continue;
				foreach (const Skill *skill, general->findChildren<const Skill *>()) {
					if (skill->isLordSkill() && skill->isVisible() && !weiwudi->hasSkill(skill->objectName(), true)
						&& !lord_skills.contains(skill->objectName()))
						lord_skills << skill->objectName();
				}
			}
			if (!lord_skills.isEmpty())
				room->acquireSkill(weiwudi, room->askForChoice(weiwudi, objectName(), lord_skills.join("+")));
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

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed("JuejiCard") && request.initiator->canPindian();
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
	{
		events << Pindian;
		view_as_skill = new JuejiViewAsSkill;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		PindianStruct *pindian = data.value<PindianStruct *>();
		return pindian && pindian->reason == "jueji" && pindian->from == player
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.original_data)
			return false;
		PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
		if (!pindian)
			return false;
		if (pindian->isSuccess())
			ctx.owner->obtainCard(pindian->to_card);
		else
			room->broadcastSkillInvoke(objectName(), 2);
		return false;
	}

	int getEffectIndex(const ServerPlayer *, const Card *) const override
	{
		return 1;
	}
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
			return ctx.owner->askForSkillInvoke(objectName(), "draw2play");
		if (change.to == Player::Play)
			return ctx.owner->askForSkillInvoke(objectName(), "play2draw");
		return false;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.original_data)
			return false;
		PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
		if (change.to == Player::Draw) {
			room->broadcastSkillInvoke(objectName(), 1);
			change.to = Player::Play;
		} else if (change.to == Player::Play) {
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
		events << EventPhaseStart;
	}

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

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		return ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Start
			&& player->getMark(objectName()) < 1 && wakeable(room, player)
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *lukang, SkillContext &) const override
	{
		if (!lukang || !wakeable(room, lukang))
			return false;
		if (!lukang->canWake(objectName())) {
			LogMessage log;
			log.type = "#KegouWake";
			log.from = lukang;
			room->sendLog(log);
		}
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(lukang, objectName());
		room->doSuperLightbox(lukang, "kegou");
		room->setPlayerMark(lukang, objectName(), 1);
		if (room->changeMaxHpForAwakenSkill(lukang, -1, objectName()) && lukang->getMark(objectName()) > 0)
			room->acquireSkill(lukang, "lianying");
		return false;
	}
};

LianliSlashCard::LianliSlashCard()
{
}

bool LianliSlashCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Slash *slash = new Slash(NoSuit, 0);
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

const Card *LianliSlashCard::validate(CardUseStruct &cardUse) const
{
	cardUse.m_isOwnerUse = false;
	ServerPlayer *zhangfei = cardUse.from;
	Room *room = zhangfei->getRoom();
	ServerPlayer *xiahoujuan = room->findPlayerBySkillName("lianli");
	if (xiahoujuan) {
		const Card *slash = room->askForCard(xiahoujuan, "slash", "@lianli-slash", QVariant(), Card::MethodResponse, nullptr, false, "", true);
		if (slash) {
			room->setCardFlag(slash, "YUANBEN");
			return slash;
		}
	}
	room->setPlayerFlag(zhangfei, "Global_LianliFailed");
	return nullptr;
}

class LianliSlashViewAsSkill : public ViewAsSkillV2
{
public:
	LianliSlashViewAsSkill() : ViewAsSkillV2("lianli-slash")
	{
		attached_lord_skill = true;
	}

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || player->hasFlag("Global_LianliFailed"))
			return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
			return player->getMark("@tied") > 0 && Slash::IsAvailable(player);
		return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& (request.pattern.contains("slash") || request.pattern.contains("Slash"));
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		return cardSelectionFeasible(request) ? new LianliSlashCard : nullptr;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "LianliSlashCard";
	}
};

class LianliSlash : public TriggerSkillV2
{
public:
	LianliSlash() : TriggerSkillV2("#lianli-slash")
	{
		events << CardAsked;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || player->getMark("@tied") <= 0 || player->hasSkill("lianli"))
			return TriggerList();
		const QStringList patterns = data.toStringList();
		if (patterns.size() < 3 || patterns.first() != "slash" || patterns.at(1).contains("lianli-slash")
			|| patterns.last() != "response")
			return TriggerList();
		ServerPlayer *owner = room->findPlayerBySkillName(objectName());
		return owner ? TriggerList{{owner, QStringList{objectName()}}} : TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.invoker && ctx.original_data && ctx.invoker->askForSkillInvoke("lianli-slash", *ctx.original_data);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *xiahoujuan = ctx.owner;
		if (!xiahoujuan || !ctx.original_data)
			return false;
		const Card *slash = room->askForCard(xiahoujuan, "slash", "@lianli-slash", *ctx.original_data,
			Card::MethodResponse, nullptr, false, "", true);
		if (!slash)
			return false;
		room->setCardFlag(slash, "YUANBEN");
		room->provide(slash);
		return true;
	}
};

class LianliJink : public TriggerSkillV2
{
public:
	LianliJink() : TriggerSkillV2("#lianli-jink")
	{
		events << CardAsked;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()) || player->getMark("@tied") <= 0)
			return TriggerList();
		const QStringList patterns = data.toStringList();
		if (patterns.size() < 2 || patterns.first() != "jink" || patterns.at(1).contains("lianli-jink"))
			return TriggerList();
		foreach (ServerPlayer *other, room->getOtherPlayers(player)) {
			if (other->getMark("@tied") > 0)
				return TriggerList{{player, QStringList{objectName()}}};
		}
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke("lianli-jink", *ctx.original_data);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *xiahoujuan = ctx.owner;
		if (!xiahoujuan || !ctx.original_data)
			return false;
		foreach (ServerPlayer *player, room->getOtherPlayers(xiahoujuan)) {
			if (player->getMark("@tied") <= 0)
				continue;
			const Card *jink = room->askForCard(player, "jink", "@lianli-jink", *ctx.original_data,
				Card::MethodResponse, nullptr, false, "", true);
			if (!jink)
				continue;
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
	Lianli() : TriggerSkillV2("lianli")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Start
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *target, SkillContext &) const override
	{
		if (!target)
			return false;
		QList<ServerPlayer *> males;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->isMale())
				males << p;
		}
		ServerPlayer *zhangfei = nullptr;
		if (males.isEmpty()
			|| (zhangfei = room->askForPlayerChosen(target, males, objectName(), "@lianli-card", true, true)) == nullptr) {
			if (target->hasSkill("liqian") && target->getKingdom() != "wei")
				room->setPlayerProperty(target, "kingdom", "wei");
			foreach (ServerPlayer *player, room->getAllPlayers()) {
				if (player->getMark("@tied") <= 0)
					continue;
				player->loseMark("@tied");
				if (player->isMale())
					room->detachSkillFromPlayer(player, "lianli-slash", true, true);
			}
			return false;
		}
		room->broadcastSkillInvoke(objectName());
		LogMessage log;
		log.type = "#LianliConnection";
		log.from = target;
		log.to << zhangfei;
		room->sendLog(log);
		if (target->getMark("@tied") == 0)
			target->gainMark("@tied");
		if (zhangfei->getMark("@tied") == 0) {
			foreach (ServerPlayer *player, room->getOtherPlayers(target)) {
				if (player->getMark("@tied") <= 0)
					continue;
				player->loseMark("@tied");
				room->detachSkillFromPlayer(player, "lianli-slash", true, true);
				break;
			}
			zhangfei->gainMark("@tied");
			room->attachSkillToPlayer(zhangfei, "lianli-slash");
		}
		if (target->hasSkill("liqian") && target->getKingdom() != zhangfei->getKingdom())
			room->setPlayerProperty(target, "kingdom", zhangfei->getKingdom());
		return false;
	}
};

class Tongxin : public TriggerSkillV2
{
public:
	Tongxin() : TriggerSkillV2("tongxin")
	{
		frequency = Frequent;
		events << Damaged;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || player->getMark("@tied") <= 0 || data.value<DamageStruct>().damage <= 0)
			return TriggerList();
		ServerPlayer *owner = player->hasSkill(objectName()) ? player : nullptr;
		if (!owner) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getMark("@tied") > 0 && p->hasSkill(objectName())) {
					owner = p;
					break;
				}
			}
		}
		return owner ? TriggerList{{owner, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *target = ctx.invoker;
		if (!target || !ctx.original_data)
			return false;
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		auto drawWith = [&](ServerPlayer *asker, ServerPlayer *partner) {
			bool has = false;
			for (int i = 0; i < damage.damage; ++i) {
				if (!asker->askForSkillInvoke(this, QVariant::fromValue(damage)))
					break;
				room->broadcastSkillInvoke(objectName());
				QList<ServerPlayer *> aps;
				aps << partner << target;
				room->sortByActionOrder(aps);
				room->drawCards(aps, 1, objectName());
				has = true;
			}
			return has;
		};
		if (target->hasSkill(objectName())) {
			foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
				if (p->getMark("@tied") > 0 && drawWith(target, p))
					break;
			}
		} else {
			foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
				if (p->getMark("@tied") > 0 && p->hasSkill(objectName()) && drawWith(p, p))
					break;
			}
		}
		return false;
	}
};

class LianliClear : public TriggerSkillV2
{
public:
	LianliClear() : TriggerSkillV2("#lianli-clear")
	{
		events << Death << EventLoseSkill;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->hasSkill(objectName()))
			return TriggerList();
		if (triggerEvent == Death)
			return data.value<DeathStruct>().who == player
				? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
		return triggerEvent == EventLoseSkill && data.toString() == "lianli"
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
	{
		foreach (ServerPlayer *player, room->getAlivePlayers()) {
			if (player->getMark("@tied") <= 0)
				continue;
			player->loseMark("@tied");
			if (player->isMale())
				room->detachSkillFromPlayer(player, "lianli-slash", true, true);
		}
		return false;
	}
};

class WulingEffect : public TriggerSkillV2
{
public:
	WulingEffect() : TriggerSkillV2("#wuling-effect")
	{
		events << DamageInflicted << PreHpRecover;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player)
			return TriggerList();
		ServerPlayer *xuandi = room->findPlayerBySkillName(objectName());
		if (!xuandi)
			return TriggerList();
		const QString wuling = xuandi->getTag("wuling").toString();
		if (event == PreHpRecover) {
			const RecoverStruct recover = data.value<RecoverStruct>();
			return recover.card && recover.card->isKindOf("Peach") && wuling == "water"
				? TriggerList{{xuandi, QStringList{objectName()}}} : TriggerList();
		}
		const DamageStruct damage = data.value<DamageStruct>();
		const bool matched = (wuling == "wind" && damage.nature == DamageStruct::Fire)
			|| (wuling == "thunder" && damage.nature == DamageStruct::Thunder)
			|| (wuling == "fire" && damage.nature != DamageStruct::Fire)
			|| (wuling == "earth" && damage.nature != DamageStruct::Normal && damage.damage > 1);
		return matched ? TriggerList{{xuandi, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		if (!player || !ctx.original_data)
			return false;
		ServerPlayer *xuandi = room->findPlayerBySkillName(objectName());
		if (!xuandi)
			return false;
		const QString wuling = xuandi->getTag("wuling").toString();
		if (event == PreHpRecover) {
			RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
			if (recover.card && recover.card->isKindOf("Peach") && wuling == "water") {
				LogMessage log;
				log.type = "#WulingWater";
				log.from = player;
				room->sendLog(log);
				recover.recover++;
				*ctx.original_data = QVariant::fromValue(recover);
			}
			return false;
		}
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (wuling == "wind" && damage.nature == DamageStruct::Fire) {
			LogMessage log;
			log.type = "#WulingWind";
			log.from = damage.to;
			log.arg = QString::number(damage.damage++);
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			*ctx.original_data = QVariant::fromValue(damage);
		} else if (wuling == "thunder" && damage.nature == DamageStruct::Thunder) {
			LogMessage log;
			log.type = "#WulingThunder";
			log.from = damage.to;
			log.arg = QString::number(damage.damage++);
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			*ctx.original_data = QVariant::fromValue(damage);
		} else if (wuling == "fire" && damage.nature != DamageStruct::Fire) {
			damage.nature = DamageStruct::Fire;
			*ctx.original_data = QVariant::fromValue(damage);
			LogMessage log;
			log.type = "#WulingFire";
			log.from = damage.to;
			room->sendLog(log);
		} else if (wuling == "earth" && damage.nature != DamageStruct::Normal && damage.damage > 1) {
			damage.damage = 1;
			*ctx.original_data = QVariant::fromValue(damage);
			LogMessage log;
			log.type = "#WulingEarth";
			log.from = player;
			room->sendLog(log);
		}
		return false;
	}
};

class Wuling : public TriggerSkillV2
{
public:
	Wuling() : TriggerSkillV2("wuling")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return ownsLivingSkill(player, objectName()) && player->getPhase() == Player::Start
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *xuandi, SkillContext &) const override
	{
		if (!xuandi)
			return false;
		static QStringList effects;
		if (effects.isEmpty())
			effects << "wind" << "thunder" << "water" << "fire" << "earth";
		const QString current = xuandi->getTag("wuling").toString();
		QStringList choices;
		foreach (const QString &effectName, effects) {
			if (effectName != current)
				choices << effectName;
		}
		const QString choice = room->askForChoice(xuandi, objectName(), choices.join("+"));
		if (!current.isEmpty())
			xuandi->loseMark("@" + current);
		xuandi->gainMark("@" + choice);
		xuandi->setTag("wuling", choice);
		room->broadcastSkillInvoke(objectName(), effects.indexOf(choice) + 1);
		return false;
	}
};

class Guihan : public ViewAsSkillV2
{
public:
	Guihan() : ViewAsSkillV2("guihan", 2)
	{
	}

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed("GuihanCard");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || candidate->isEquipped() || candidate->hasFlag("using"))
			return false;
		const int id = candidate->getEffectiveId();
		if (id < 0 || request.selectedCardIds.contains(id) || request.selectedCardIds.size() >= 2)
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
	{
		events << EventPhaseStart << FinishJudge;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish)
			return TriggerList{{player, QStringList{objectName()}}};
		if (triggerEvent == FinishJudge) {
			JudgeStruct *judge = data.value<JudgeStruct *>();
			if (judge && judge->reason == objectName())
				return TriggerList{{player, QStringList{objectName()}}};
		}
		return TriggerList();
	}

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *caizhaoji, SkillContext &ctx) const override
	{
		if (!caizhaoji || !ctx.original_data)
			return false;
		if (triggerEvent == EventPhaseStart) {
			int times = 0;
			const bool canRetrial = caizhaoji->hasSkills("guicai|nosguicai|guidao|huanshi");
			bool first = true;
			while (caizhaoji->askForSkillInvoke("caizhaoji_hujia")) {
				if (first) {
					room->broadcastSkillInvoke(objectName());
					first = false;
				}
				times++;
				if (times == 3)
					caizhaoji->turnOver();
				JudgeStruct judge;
				judge.pattern = ".|red";
				judge.good = true;
				judge.reason = objectName();
				judge.play_animation = false;
				judge.who = caizhaoji;
				judge.time_consuming = true;
				if (canRetrial)
					caizhaoji->setFlags("HujiaRetrial");
				try {
					room->judge(judge);
				} catch (TriggerEvent thrown) {
					if ((thrown == TurnBroken || thrown == StageChange) && caizhaoji->hasFlag("HujiaRetrial"))
						caizhaoji->setFlags("-HujiaRetrial");
					throw thrown;
				}
				if (judge.isBad())
					break;
			}
			if (canRetrial && caizhaoji->getTag(objectName()).isValid()) {
				DummyCard *dummy = new DummyCard(ListV2I(caizhaoji->getTag(objectName()).toList()));
				if (dummy->subcardsLength() > 0)
					caizhaoji->obtainCard(dummy);
				caizhaoji->removeTag(objectName());
				dummy->deleteLater();
			}
			return false;
		}
		JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
		if (!judge || !judge->card || judge->reason != objectName())
			return false;
		const bool canRetrial = caizhaoji->hasFlag("HujiaRetrial");
		if (judge->card->isRed()) {
			if (room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge) {
				if (canRetrial) {
					CardMoveReason reason(CardMoveReason::S_REASON_JUDGEDONE, caizhaoji->objectName(), "", judge->reason);
					room->moveCardTo(judge->card, caizhaoji, nullptr, Player::PlaceTable, reason, true);
					QVariantList luoshen_list = caizhaoji->getTag(objectName()).toList();
					luoshen_list << judge->card->getEffectiveId();
					caizhaoji->setTag(objectName(), luoshen_list);
				} else
					caizhaoji->obtainCard(judge->card);
			}
		} else if (canRetrial) {
			DummyCard *dummy = new DummyCard(ListV2I(caizhaoji->getTag(objectName()).toList()));
			if (dummy->subcardsLength() > 0)
				caizhaoji->obtainCard(dummy);
			caizhaoji->removeTag(objectName());
			dummy->deleteLater();
		}
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

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
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
	{
		events << DamageDone << DamageComplete;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const DamageStruct damage = data.value<DamageStruct>();
		if (!damage.from || !damage.from->isAlive() || !damage.from->hasSkill(objectName()))
			return TriggerList();
		if (triggerEvent == DamageDone) {
			if (!player || player->isChained() || damage.nature != DamageStruct::Fire)
				return TriggerList();
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (player->distanceTo(p) == 1)
					return TriggerList{{damage.from, QStringList{objectName()}}};
			}
			return TriggerList();
		}
		return damage.from->getTag("ShaoyingTarget").value<ServerPlayer *>()
			? TriggerList{{damage.from, QStringList{objectName()}}} : TriggerList();
	}

	bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (triggerEvent != DamageDone)
			return true;
		if (!ctx.owner || !ctx.invoker || !ctx.original_data)
			return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (ctx.invoker->distanceTo(p) == 1)
				targets << p;
		}
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@shaoying", true, true);
		if (!target)
			return false;
		ctx.targets = QList<ServerPlayer *>() << target;
		return true;
	}

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.original_data)
			return false;
		if (triggerEvent == DamageDone) {
			if (ctx.targets.isEmpty())
				return false;
			LogMessage log;
			log.type = "#Shaoying";
			log.from = ctx.owner;
			log.to << ctx.targets.first();
			log.arg = objectName();
			room->sendLog(log);
			ctx.owner->setTag("ShaoyingTarget", QVariant::fromValue(ctx.targets.first()));
			return false;
		}
		ServerPlayer *target = ctx.owner->getTag("ShaoyingTarget").value<ServerPlayer *>();
		ctx.owner->removeTag("ShaoyingTarget");
		if (!target || ctx.owner->isDead())
			return false;
		JudgeStruct judge;
		judge.pattern = ".|red";
		judge.good = true;
		judge.reason = objectName();
		judge.who = ctx.owner;
		room->judge(judge);
		if (!judge.isGood())
			return false;
		room->broadcastSkillInvoke(objectName());
		DamageStruct shaoying_damage;
		shaoying_damage.nature = DamageStruct::Fire;
		shaoying_damage.from = ctx.owner;
		shaoying_damage.to = target;
		room->damage(shaoying_damage);
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
		*ctx.original_data = QVariant::fromValue(use);
		return false;
	}
};

class Gongmou : public TriggerSkillV2
{
public:
	Gongmou() : TriggerSkillV2("gongmou")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		return player->getPhase() == Player::Finish || player->getPhase() == Player::Start
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *zhongshiji, SkillContext &) const override
	{
		if (!zhongshiji)
			return false;
		if (zhongshiji->getPhase() == Player::Finish) {
			ServerPlayer *target = room->askForPlayerChosen(zhongshiji, room->getOtherPlayers(zhongshiji),
				"gongmou", "@gongmou", true, true);
			if (target) {
				room->broadcastSkillInvoke(objectName());
				target->gainMark("@conspiracy");
			}
		} else if (zhongshiji->getPhase() == Player::Start) {
			foreach (ServerPlayer *player, room->getOtherPlayers(zhongshiji)) {
				if (player->getMark("@conspiracy") > 0)
					player->loseMark("@conspiracy");
			}
		}
		return false;
	}
};

class GongmouExchange : public TriggerSkillV2
{
public:
	GongmouExchange() : TriggerSkillV2("#gongmou-exchange")
	{
		events << EventPhaseEnd;
	}

	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player && player->getPhase() == Player::Draw && player->getMark("@conspiracy") > 0
			&& !room->findPlayerBySkillName(objectName()))
			player->loseMark("@conspiracy");
		return false;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || player->getPhase() != Player::Draw || player->getMark("@conspiracy") <= 0)
			return TriggerList();
		ServerPlayer *owner = room->findPlayerBySkillName(objectName());
		return owner ? TriggerList{{owner, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		ServerPlayer *zhongshiji = ctx.owner;
		if (!player || player->getPhase() != Player::Draw)
			return false;
		player->loseMark("@conspiracy");
		if (!zhongshiji)
			return false;
		const int x = qMin(zhongshiji->getHandcardNum(), player->getHandcardNum());
		if (x == 0)
			return false;
		const Card *to_exchange = room->askForExchange(player, "gongmou", x, x);
		if (!to_exchange)
			return false;
		room->moveCardTo(to_exchange, zhongshiji, Player::PlaceHand, false);
		to_exchange = room->askForExchange(zhongshiji, "gongmou", x, x);
		if (!to_exchange)
			return false;
		room->moveCardTo(to_exchange, player, Player::PlaceHand, false);
		LogMessage log;
		log.type = "#GongmouExchange";
		log.from = zhongshiji;
		log.to << player;
		log.arg = QString::number(x);
		log.arg2 = "gongmou";
		room->sendLog(log);
		return false;
	}
};

class Lexue : public ViewAsSkillV2
{
public:
	Lexue() : ViewAsSkillV2("lexue", 1)
	{
	}

	YT_MIRROR_ACTIVATION

	bool learned(const Player *player) const
	{
		return player && player->hasUsed("LexueCard") && player->hasFlag("lexue");
	}

	const Card *learnedCard(const Player *player) const
	{
		return player ? Sanguosha->getCard(player->getMark("lexue")) : nullptr;
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player)
			return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
			if (!player->hasUsed("LexueCard"))
				return true;
			const Card *card = learnedCard(player);
			return learned(player) && card && card->isAvailable(player);
		}
		if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
			&& request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return false;
		if (!learned(player))
			return false;
		const Card *card = learnedCard(player);
		if (!card)
			return false;
		QString name = card->objectName();
		if (name.contains("slash"))
			name = "slash";
		return request.pattern.contains(name);
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		const Player *player = request.initiator;
		if (!learned(player) || !candidate || candidate->hasFlag("using") || !request.selectedCardIds.isEmpty())
			return false;
		const Card *card = learnedCard(player);
		const int id = candidate->getEffectiveId();
		return card && id >= 0 && candidate->getSuit() == card->getSuit()
			&& (player->handCards().contains(id) || player->getEquipsId().contains(id));
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (!learned(request.initiator))
			return request.selectedCardIds.isEmpty();
		if (request.selectedCardIds.size() != 1)
			return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
		return card && canSelectCard(selection, card);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request))
			return nullptr;
		if (!learned(request.initiator))
			return proxyCard(this, request, true);
		const Card *source = learnedCard(request.initiator);
		const Card *first = Sanguosha->getCard(request.selectedCardIds.first());
		if (!source || !first)
			return nullptr;
		Card *card = Sanguosha->cloneCard(source->objectName(), first->getSuit(), first->getNumber());
		if (!card)
			return nullptr;
		card->addSubcard(first);
		card->setSkillName(objectName());
		return card;
	}

	QString historyKey(const ActiveSkillRequest &request) const override
	{
		if (!learned(request.initiator))
			return "LexueCard";
		const Card *card = learnedCard(request.initiator);
		return card ? card->getClassName() : objectName();
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return request.initiator && !learned(request.initiator) && selected.isEmpty() && candidate
			&& candidate != request.initiator && !candidate->isKongcheng();
	}

	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
	{
		if (learned(request.initiator))
			return true;
		return selected.length() == 1;
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !target || !ctx.use_card || !ctx.use_card->isKindOf("ActiveSkillCard"))
			return ContinueEffects;
		Room *room = source->getRoom();
		room->broadcastSkillInvoke(objectName(), 1);
		const Card *card = room->askForCardShow(target, source, "lexue");
		if (!card)
			return ContinueEffects;
		const int card_id = card->getEffectiveId();
		room->showCard(target, card_id);
		if (card->getTypeId() == Card::TypeBasic || card->isNDTrick()) {
			room->setPlayerMark(source, "lexue", card_id);
			room->setPlayerFlag(source, "lexue");
		} else {
			source->obtainCard(card);
			room->setPlayerFlag(source, "-lexue");
		}
		return ContinueEffects;
	}

	int getEffectIndex(const ServerPlayer *, const Card *card) const override
	{
		return card && card->getTypeId() == Card::TypeBasic ? 2 : 3;
	}
};

class XunzhiViewAsSkill : public ViewAsSkillV2
{
public:
	XunzhiViewAsSkill() : ViewAsSkillV2("xunzhi")
	{
	}

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasFlag("xunzhi");
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		return cardSelectionFeasible(request) ? proxyCard(this, request, true) : nullptr;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "XunzhiCard";
	}

	TargetMode targetMode() const override
	{
		return NoTarget;
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source)
			return FinishSkill;
		Room *room = source->getRoom();
		room->broadcastSkillInvoke("xunzhi", qsanRandomBounded(2) + 1);
		room->doSuperLightbox("jiangboyue", "xunzhi");
		source->drawCards(3);
		QSet<QString> general_names;
		foreach (ServerPlayer *player, room->getAlivePlayers())
			general_names << player->getGeneralName();
		QStringList shu_generals;
		foreach (const QString &name, Sanguosha->getLimitedGeneralNames()) {
			const General *general = Sanguosha->getGeneral(name);
			if (general && general->getKingdom() == "shu" && !general_names.contains(name))
				shu_generals << name;
		}
		const QString general = room->askForGeneral(source, shu_generals);
		source->setTag("newgeneral", general);
		const bool isSecondaryHero = source->getGeneralName() != "jiangboyue";
		room->changeHero(source, general, false, true, isSecondaryHero);
		room->acquireSkill(source, "xunzhi", false);
		room->setPlayerFlag(source, "xunzhi");
		return FinishSkill;
	}
};

class Xunzhi : public TriggerSkillV2
{
public:
	Xunzhi() : TriggerSkillV2("xunzhi")
	{
		events << EventPhaseChanging;
		view_as_skill = new XunzhiViewAsSkill;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		return ownsLivingSkill(player, objectName()) && player->hasFlag("xunzhi")
			&& data.value<PhaseChangeStruct>().to == Player::NotActive
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		if (ctx.owner && ctx.owner->hasFlag("xunzhi") && ctx.current_event == EventPhaseChanging)
			return true;
		return TriggerSkillV2::isSourceAvailable(room, ctx);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *target, SkillContext &) const override
	{
		if (!target || !target->hasFlag("xunzhi"))
			return false;
		const bool isSecondaryHero = target->getGeneralName() != target->getTag("newgeneral").toString();
		const QString original = parent() ? parent()->objectName() : QString("jiangboyue");
		room->changeHero(target, original, false, false, isSecondaryHero);
		room->killPlayer(target);
		return false;
	}
};

class Dongcha : public TriggerSkillV2
{
public:
	Dongcha() : TriggerSkillV2("dongcha")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		return player->getPhase() == Player::Start || player->getPhase() == Player::NotActive
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player)
			return false;
		if (player->getPhase() == Player::Start) {
			ServerPlayer *dongchaee = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@dongcha", true);
			if (!dongchaee)
				return false;
			room->broadcastSkillInvoke(objectName());
			LogMessage log;
			log.type = "#ChoosePlayerWithSkill";
			log.from = player;
			log.to << dongchaee;
			log.arg = objectName();
			room->sendLog(log, player);
			log.type = "#InvokeSkill";
			room->sendLog(log, room->getOtherPlayers(player, true));
			room->doAnimate(1, player->objectName(), dongchaee->objectName(), QList<ServerPlayer *>() << player);
			room->notifySkillInvoked(player, objectName());
			room->addPlayerMark(player, "HandcardVisible_" + dongchaee->objectName());
			player->setFlags("HandcardVisible_" + dongchaee->objectName());
			room->showAllCards(dongchaee, player);
		} else if (player->getPhase() == Player::NotActive) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->hasFlag("HandcardVisible_" + p->objectName()))
					room->removePlayerMark(player, "HandcardVisible_" + p->objectName());
			}
		}
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
		if (ctx.current_event == Death && ctx.owner && ctx.owner->hasSkillInstance(objectName(), ctx.instanceID))
			return true;
		return TriggerSkillV2::isSourceAvailable(room, ctx);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !ctx.original_data)
			return false;
		const DeathStruct death = ctx.original_data->value<DeathStruct>();
		ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
		if (death.who != player || !killer || killer == player)
			return false;
		room->sendCompulsoryTriggerLog(player, this);
		killer->gainMark("@collapse");
		room->acquireSkill(killer, "benghuai");
		return false;
	}
};

class Sizhan : public TriggerSkillV2
{
public:
	Sizhan() : TriggerSkillV2("sizhan")
	{
		events << DamageInflicted << EventPhaseStart;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!ownsLivingSkill(player, objectName()))
			return TriggerList();
		if (triggerEvent == DamageInflicted)
			return TriggerList{{player, QStringList{objectName()}}};
		return triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish && player->getMark("@struggle") > 0
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *elai, SkillContext &ctx) const override
	{
		if (!elai || !ctx.original_data)
			return false;
		if (triggerEvent == DamageInflicted) {
			const DamageStruct damage = ctx.original_data->value<DamageStruct>();
			room->sendCompulsoryTriggerLog(elai, this, 2);
			elai->damageRevises(*ctx.original_data, -damage.damage);
			elai->gainMark("@struggle", damage.damage);
			return true;
		}
		const int x = elai->getMark("@struggle");
		if (x <= 0)
			return false;
		room->sendCompulsoryTriggerLog(elai, this, 1);
		elai->loseMark("@struggle", x);
		room->loseHp(HpLostStruct(elai, x, "sizhan", elai));
		return false;
	}
};

class Shenli : public TriggerSkillV2
{
public:
	Shenli() : TriggerSkillV2("shenli")
	{
		events << DamageCaused;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!ownsLivingSkill(player, objectName()) || player->getPhase() != Player::Play
			|| player->getMark("shenliUse-PlayClear") >= 1)
			return TriggerList();
		const DamageStruct damage = data.value<DamageStruct>();
		return damage.card && damage.card->isKindOf("Slash")
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *elai, SkillContext &ctx) const override
	{
		if (!elai || !ctx.original_data)
			return false;
		elai->addMark("shenliUse-PlayClear");
		const int x = elai->getMark("@struggle");
		if (x > 0) {
			room->sendCompulsoryTriggerLog(elai, this);
			elai->damageRevises(*ctx.original_data, qMin(3, x));
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
		if (!player || player->hasSkill(objectName()))
			return TriggerList();
		ServerPlayer *owner = room->findPlayerBySkillName(objectName());
		return owner && owner->faceUp() ? TriggerList{{owner, QStringList{objectName()}}} : TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.owner && ctx.owner->faceUp() && ctx.owner->askForSkillInvoke(this);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner)
			return false;
		room->broadcastSkillInvoke(objectName());
		ctx.owner->gainAnExtraTurn();
		ctx.owner->turnOver();
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

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !target)
			return ContinueEffects;
		source->turnOver();
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("toudu");
		source->getRoom()->useCard(CardUseStruct(slash, source, target));
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

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player || player->faceUp() || player->isKongcheng())
			return false;
		room->askForUseCard(player, "@@toudu", "@toudu", -1, Card::MethodDiscard, false);
		return false;
	}
};

class YtYisheViewAsSkill : public ViewAsSkillV2
{
public:
	YtYisheViewAsSkill() : ViewAsSkillV2("ytyishe", 6)
	{
		expand_pile = "ytrice";
	}

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->getPile("ytrice").isEmpty();
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		const Player *player = request.initiator;
		if (!player || !candidate || candidate->isEquipped() || candidate->hasFlag("using")
			|| request.selectedCardIds.size() > 5)
			return false;
		const int id = candidate->getEffectiveId();
		return id >= 0 && !request.selectedCardIds.contains(id)
			&& (player->handCards().contains(id) || player->getPile("ytrice").contains(id));
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.isEmpty() || request.selectedCardIds.size() > 6)
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
		return cardSelectionFeasible(request) ? proxyCard(this, request, true) : nullptr;
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
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !ctx.use_card)
			return FinishSkill;
		Room *room = source->getRoom();
		source->peiyin("ytyishe", 1);
		const QList<int> rice = source->getPile("ytrice");
		QList<int> to_handcard, to_rice;
		foreach (int id, ctx.use_card->getSubcards()) {
			if (rice.contains(id))
				to_handcard << id;
			else
				to_rice << id;
		}
		if (!to_rice.isEmpty())
			source->addToPile("ytrice", to_rice);
		if (!to_handcard.isEmpty()) {
			CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, source->objectName());
			room->moveCardTo(dummyCard(to_handcard), source, Player::PlaceHand, reason, true);
		}
		return FinishSkill;
	}
};

class YtYisheAsk : public ViewAsSkillV2
{
public:
	YtYisheAsk() : ViewAsSkillV2("ytyishe_ask", 1)
	{
		attached_lord_skill = true;
		expand_pile = "%ytrice";
	}

	YT_MIRROR_ACTIVATION

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || player->hasSkill("ytyishe")
			|| player->usedTimes("YtYisheAskCard") >= 2)
			return false;
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->hasSkill("ytyishe") && !p->getPile("ytrice").isEmpty())
				return true;
		}
		return false;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		const Player *player = request.initiator;
		if (!player || !candidate || candidate->hasFlag("using") || !request.selectedCardIds.isEmpty())
			return false;
		const int id = candidate->getEffectiveId();
		return id >= 0 && getExpandPileCardIds(player).contains(id);
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

	bool willThrowSelectedCards() const override
	{
		return false;
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		return cardSelectionFeasible(request) ? proxyCard(this, request, true) : nullptr;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "YtYisheAskCard";
	}

	TargetMode targetMode() const override
	{
		return NoTarget;
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		ServerPlayer *from = ctx.invoker;
		if (!from || !ctx.use_card || ctx.use_card->getSubcards().isEmpty())
			return FinishSkill;
		Room *room = from->getRoom();
		ServerPlayer *zhanglu = room->getCardOwner(ctx.use_card->getSubcards().first());
		if (!zhanglu)
			return FinishSkill;
		from->skillInvoked("ytyishe", 0, zhanglu);
		room->fillAG(ctx.use_card->getSubcards());
		if (room->askForChoice(zhanglu, "ytyishe_ask", "allow+disallow") == "allow") {
			zhanglu->peiyin("ytyishe", 2);
			room->giveCard(zhanglu, from, ctx.use_card, "ytyishe", true);
		} else
			zhanglu->peiyin("ytyishe", 3);
		room->clearAG();
		return FinishSkill;
	}
};

class YtYishe : public TriggerSkillV2
{
public:
	YtYishe() : TriggerSkillV2("ytyishe")
	{
		view_as_skill = new YtYisheViewAsSkill;
		events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive())
			return TriggerList();
		if (triggerEvent == EventAcquireSkill) {
			if (!player->hasSkill(objectName(), true))
				return TriggerList();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase() == Player::Play && !p->hasSkill("ytyishe_ask", true))
					return TriggerList{{player, QStringList{objectName()}}};
			}
			return TriggerList();
		}
		if (player->getPhase() != Player::Play)
			return TriggerList();
		if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasSkill(objectName(), true))
					return TriggerList{{p, QStringList{objectName()}}};
			}
			return TriggerList();
		}
		if (!player->hasSkill("ytyishe_ask", true))
			return TriggerList();
		if (player->hasSkill(objectName(), true))
			return TriggerList{{player, QStringList{objectName()}}};
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->hasSkill(objectName(), true))
				return TriggerList{{p, QStringList{objectName()}}};
		}
		return TriggerList();
	}

	bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (triggerEvent == EventPhaseEnd && player && player->getPhase() == Player::Play
			&& player->hasSkill("ytyishe_ask", true) && !room->findPlayerBySkillName(objectName()))
			room->detachSkillFromPlayer(player, "ytyishe_ask", true);
		return false;
	}

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		if (!player)
			return false;
		if (triggerEvent == EventAcquireSkill) {
			if (!player->hasSkill(objectName(), true))
				return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase() == Player::Play && !p->hasSkill("ytyishe_ask", true))
					room->attachSkillToPlayer(p, "ytyishe_ask");
			}
			return false;
		}
		if (player->getPhase() != Player::Play)
			return false;
		if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasSkill(objectName(), true)) {
					room->attachSkillToPlayer(player, "ytyishe_ask");
					break;
				}
			}
		} else if (player->hasSkill("ytyishe_ask", true))
			room->detachSkillFromPlayer(player, "ytyishe_ask", true);
		return false;
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
		foreach (int id, move.card_ids) {
			const Card *card = Sanguosha->getCard(id);
			if (card && card->isRed() && room->getCardPlace(id) == Player::DiscardPile)
				ids << id;
		}
		return ids;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || player->getPhase() == Player::Discard)
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

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		ServerPlayer *p = ctx.owner;
		if (!player || !p || !p->isAlive() || !ctx.original_data)
			return false;
		QList<int> ids = redDiscards(room, ctx.original_data->value<CardsMoveOneTimeStruct>());
		if (ids.isEmpty())
			return false;
		room->fillAG(ids, p);
		if (p->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) {
			const int n = ids.length();
			QList<int> ids2, ids3;
			int x = 5 - p->getPile("ytrice").length();
			for (int i = 0; i < n; ++i) {
				const int id = room->askForAG(p, ids, i > 0, "xiliang");
				if (id < 0)
					break;
				room->takeAG(p, id, false, QList<ServerPlayer *>() << p);
				if (x > 0 && room->askForChoice(p, objectName(), "put+obtain") == "put") {
					ids2 << id;
					x--;
				} else
					ids3 << id;
			}
			p->addToPile("ytrice", ids2);
			p->obtainCard(dummyCard(ids3));
		}
		room->clearAG(p);
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

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.original_data)
			return false;
		const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
		if (!effect.offset_card
			|| room->getCardPlace(effect.offset_card->getEffectiveId()) != Player::DiscardPile)
			return false;
		room->broadcastSkillInvoke(objectName());
		ctx.owner->obtainCard(effect.offset_card);
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

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player || !ctx.original_data)
			return false;
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		LogMessage log;
		log.type = "#YitianSolace";
		log.from = player;
		log.to << damage.to;
		log.arg = QString::number(damage.damage);
		log.arg2 = QString::number(--damage.damage);
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		if (damage.damage <= 0)
			return true;
		*ctx.original_data = QVariant::fromValue(damage);
		return false;
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
			&& candidate->getEffectiveId() >= 0;
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

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ServerPlayer *source = ctx.invoker;
		if (!source || !target)
			return ContinueEffects;
		Room *room = source->getRoom();
		if (!ctx.use_card || ctx.use_card->getSubcards().isEmpty())
			room->loseHp(HpLostStruct(source, 1, "taichen", source));
		else
			room->throwCard(ctx.use_card, source);
		for (int i = 0; i < 2; ++i) {
			if (source->canDiscard(target, "hej"))
				room->throwCard(room->askForCardChosen(source, target, "hej", "taichen"), target, source);
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
