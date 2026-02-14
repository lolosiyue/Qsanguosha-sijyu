#include "tenyear-strengthen.h"
#include "settings.h"
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
//#include "json.h"
#include "exppattern.h"
#include "yjcm2013.h"

TenyearZhihengCard::TenyearZhihengCard()
{
	target_fixed = true;
	will_throw = true;
	mute = true;
}

void TenyearZhihengCard::onUse(Room *room, CardUseStruct &card_use) const
{
	bool allhand = true;
	foreach(int id, card_use.from->handCards()) {
		if (!subcards.contains(id)) {
			allhand = false;
			break;
		}
	}
	if (allhand&&!card_use.from->isKongcheng())
		room->setCardFlag(this, "tenyearzhiheng_all_handcard_" + card_use.from->objectName());
	SkillCard::onUse(room, card_use);
}

void TenyearZhihengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->hasInnateSkill("tenyearzhiheng") || !source->hasSkill("jilve"))
		room->broadcastSkillInvoke("tenyearzhiheng",qrand()%2+1);
	else
		room->broadcastSkillInvoke("jilve", 4);
	int x = subcardsLength();
	if (hasFlag("tenyearzhiheng_all_handcard_" + source->objectName()))
		x++;
	source->drawCards(x, "tenyearzhiheng");
}

class TenyearZhiheng : public ViewAsSkill
{
public:
	TenyearZhiheng() : ViewAsSkill("tenyearzhiheng")
	{
		response_pattern = "@@tenyearzhiheng";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		TenyearZhihengCard *zhiheng_card = new TenyearZhihengCard;
		zhiheng_card->addSubcards(cards);
		return zhiheng_card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->canDiscard(player, "he") && !player->hasUsed("TenyearZhihengCard");
	}
};

class TenyearJiuyuan : public TriggerSkill
{
public:
	TenyearJiuyuan() : TriggerSkill("tenyearjiuyuan$")
	{
		events << CardUsed;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getKingdom() == "wu";
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Peach") || !use.to.contains(player)) return false;
		QList<ServerPlayer *> sunquans;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->hasLordSkill(this)) continue;
			if (player->getHp() > p->getHp() && p->getLostHp() > 0)
				sunquans << p;
		}
		if (sunquans.isEmpty()) return false;
		ServerPlayer *sunquan = room->askForPlayerChosen(player, sunquans, objectName(), "@tenyearjiuyuan-invoke", true);
		if (!sunquan) return false;
		LogMessage log;
		log.type = "#InvokeOthersSkill";
		log.from = player;
		log.to << sunquan;
		log.arg = "tenyearjiuyuan";
		room->sendLog(log);
		if (sunquan->isWeidi()) {
			room->broadcastSkillInvoke("weidi");
			room->notifySkillInvoked(sunquan, "weidi");
		} else {
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(sunquan, objectName());
		}
		room->recover(sunquan, RecoverStruct(objectName(), player));
		player->drawCards(1, objectName());
		return true;
	}
};

TenyearJieyinCard::TenyearJieyinCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool TenyearJieyinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	if (!targets.isEmpty()||!to_select->isMale()) return false;
	const Card *card = Sanguosha->getCard(getEffectiveId());
	if (card->isKindOf("EquipCard")&&!Self->handCards().contains(getEffectiveId())) {
		const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
		return !to_select->getEquip(equip->location()) && !Self->isProhibited(to_select, card);
	}
	return true;
}

void TenyearJieyinCard::onEffect(CardEffectStruct &effect) const
{
	const Card *card = Sanguosha->getCard(getEffectiveId());
	Room *room = effect.from->getRoom();
	QStringList choices;
	if (card->isKindOf("EquipCard")) {
		const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
		if (!effect.to->getEquip(equip->location()) && !effect.from->isProhibited(effect.to, card))
			choices << "enter";
	}
	if (effect.from->canDiscard(effect.from, getEffectiveId()))
		choices << "throw";
	if (room->askForChoice(effect.from,"tenyearjieyin",choices.join("+"),QVariant::fromValue(effect))=="enter") {
		LogMessage log;
		log.type = "$ZhijianEquip";
		log.from = effect.to;
		log.card_str = QString::number(getEffectiveId());
		room->sendLog(log);
		room->moveCardTo(card, effect.from, effect.to, Player::PlaceEquip,
			CardMoveReason(CardMoveReason::S_REASON_PUT,
			effect.from->objectName(), "tenyearjieyin", ""));
	} else {
		CardMoveReason reason(CardMoveReason::S_REASON_THROW, effect.from->objectName(), "tenyearjieyin", "");
		room->throwCard(this, reason, effect.from, nullptr);
	}

	if (effect.from->getHp() == effect.to->getHp()) return;
	RecoverStruct recover("tenyearjieyin", effect.from);
	if (effect.from->getHp() < effect.to->getHp()) {
		room->recover(effect.from, recover, true);
		effect.to->drawCards(1, "tenyearjieyin");
	} else {
		effect.from->drawCards(1, "tenyearjieyin");
		room->recover(effect.to, recover, true);
	}
}

class TenyearJieyin : public OneCardViewAsSkill
{
public:
	TenyearJieyin() :OneCardViewAsSkill("tenyearjieyin")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		if (to_select->isKindOf("EquipCard")) return true;
		return !to_select->isEquipped()&&Self->canDiscard(Self, to_select->getEffectiveId());
	}

	const Card *viewAs(const Card *originalCard) const
	{
		TenyearJieyinCard *c = new TenyearJieyinCard();
		c->addSubcard(originalCard);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearJieyinCard");
	}
};

TenyearRendeCard::TenyearRendeCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool TenyearRendeCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getMark("tenyearrendetarget-PlayClear") <= 0;
}

void TenyearRendeCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "tenyearrende", "");
	room->obtainCard(effect.to, this, reason, false);
	room->addPlayerMark(effect.to, "tenyearrendetarget-PlayClear");

	int old_value = effect.from->getMark("tenyearrende-PlayClear");
	int new_value = old_value + subcards.length();
	room->setPlayerMark(effect.from, "tenyearrende-PlayClear", new_value);

	if (old_value < 2 && new_value >= 2) {
		QList<int> list = room->getAvailableCardList(effect.from, "basic", "tenyearrende");
		if (list.isEmpty()) return;
		room->fillAG(list, effect.from);
		int id = room->askForAG(effect.from, list, true, "tenyearrende", "@tenyearrende-basic");
		room->clearAG(effect.from);
		if (id < 0) return;
		QString name = Sanguosha->getEngineCard(id)->objectName();
		room->setPlayerMark(effect.from, "tenyearrende_id-PlayClear", id + 1);
		room->askForUseCard(effect.from, "@@tenyearrende", "@tenyearrende:" + name);
	}
}

class TenyearRende : public ViewAsSkill
{
public:
	TenyearRende() : ViewAsSkill("tenyearrende")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@tenyearrende")
			return false;
		return !to_select->isEquipped();
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return true;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@tenyearrende";
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			if (cards.isEmpty())
				return nullptr;

			TenyearRendeCard *rende_card = new TenyearRendeCard;
			rende_card->addSubcards(cards);
			return rende_card;
		} else {
			if (!cards.isEmpty()) return nullptr;
			int id = Self->getMark("tenyearrende_id-PlayClear") - 1;
			if (id < 0) return nullptr;
			QString name = Sanguosha->getEngineCard(id)->objectName();
			Card *card = Sanguosha->cloneCard(name);
			card->setSkillName("_tenyearrende");
			return card;
		}
	}
};

class TenyearWusheng : public OneCardViewAsSkill
{
public:
	TenyearWusheng() : OneCardViewAsSkill("tenyearwusheng")
	{
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("slash") || pattern.contains("Slash");
	}

	bool viewFilter(const Card *card) const
	{
		if (!card->isRed())
			return false;

		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			Slash *slash = new Slash(Card::SuitToBeDecided, -1);
			slash->addSubcard(card->getEffectiveId());
			slash->deleteLater();
			return slash->isAvailable(Self);
		}
		return true;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Card *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
		slash->addSubcard(originalCard->getId());
		slash->setSkillName(objectName());
		return slash;
	}

	int getEffectIndex(const ServerPlayer *player, const Card *) const
	{
		int index = qrand() % 2 + 1;
		if (player->getKingdom() == "wei")
			index += 2;
		return index;
	}
};

class TenyearWushengMod : public TargetModSkill
{
public:
	TenyearWushengMod() : TargetModSkill("#tenyearwushengmod")
	{
		frequency = NotFrequent;
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const
	{
		if (card->getSuit()==Card::Diamond&&card->isKindOf("Slash")&&from->hasSkill("tenyearwusheng"))
			return 1000;
		return 0;
	}
};

TenyearYijueCard::TenyearYijueCard()
{
}

bool TenyearYijueCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void TenyearYijueCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isKongcheng()) return;
	Room *room = effect.from->getRoom();
	const Card *show_card = room->askForCardShow(effect.to, effect.from, "tenyearyijue");
	room->showCard(effect.to, show_card->getEffectiveId());

	if (show_card->isRed()) {
		room->obtainCard(effect.from, show_card, true);
		if (effect.to->isAlive() && effect.to->getLostHp() > 0 &&
				effect.from->askForSkillInvoke("tenyearyijue", QString("recover:%1").arg(effect.to->objectName()), false)) {
			room->recover(effect.to, RecoverStruct("tenyearyijue", effect.from));
		}
	} else if (show_card->isBlack()) {
		effect.to->addMark("tenyearyijue");
		room->setPlayerCardLimitation(effect.to, "use,response", ".|.|.|hand", true);
		room->addPlayerMark(effect.to, "@skill_invalidity");

		foreach(ServerPlayer *p, room->getAllPlayers())
			room->filterCards(p, p->getCards("he"), true);
		JsonArray args;
		args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
		room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
	}
}

class TenyearYijueVS : public OneCardViewAsSkill
{
public:
	TenyearYijueVS() : OneCardViewAsSkill("tenyearyijue")
	{
		filter_pattern = ".";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearYijueCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		TenyearYijueCard *card = new TenyearYijueCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class TenyearYijue : public TriggerSkill
{
public:
	TenyearYijue() : TriggerSkill("tenyearyijue")
	{
		events << EventPhaseChanging << Death << DamageCaused;
		view_as_skill = new TenyearYijueVS;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event != DamageCaused)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const
	{
		if (triggerEvent == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash") || damage.card->getSuit() != Card::Heart) return false;
			if (damage.from->hasSkill(objectName()) && damage.to->getMark("tenyearyijue") > 0 && damage.from == room->getCurrent()) {
				LogMessage log;
				log.type = "#TenyearyijueBuff";
				log.from = damage.from;
				log.to << damage.to;
				log.arg = QString::number(damage.damage);
				log.arg2 = QString::number(damage.damage += damage.to->getMark("tenyearyijue"));
				room->sendLog(log);
				data = QVariant::fromValue(damage);
			}
		} else {
			if (triggerEvent == EventPhaseChanging) {
				PhaseChangeStruct change = data.value<PhaseChangeStruct>();
				if (change.to != Player::NotActive)
					return false;
			} else if (triggerEvent == Death) {
				DeathStruct death = data.value<DeathStruct>();
				if (death.who != target || target != room->getCurrent())
					return false;
			}
			QList<ServerPlayer *> players = room->getAllPlayers(true);
			foreach (ServerPlayer *player, players) {
				int mark = player->getMark("tenyearyijue");
				if (mark == 0) continue;
				player->removeMark("tenyearyijue", mark);
				room->removePlayerMark(player, "@skill_invalidity", mark);

				foreach(ServerPlayer *p, room->getAllPlayers())
					room->filterCards(p, p->getCards("he"), false);

				JsonArray args;
				args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
				room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

				room->removePlayerCardLimitation(player, "use,response", ".|.|.|hand$1");
			}
		}
		return false;
	}
};

class TenyearPaoxiao : public TargetModSkill
{
public:
	TenyearPaoxiao() : TargetModSkill("tenyearpaoxiao")
	{
	}

	int getResidueNum(const Player *from, const Card *, const Player *) const
	{
		if (from->hasSkill(objectName()))
			return 1000;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *) const
	{
		if (from->getMark("tenyearpaoxiao-PlayClear") > 0 && from->hasSkill(objectName()))
			return 1000;
		return 0;
	}
};

class TenyearTishen : public TriggerSkill
{
public:
	TenyearTishen() : TriggerSkill("tenyeartishen")
	{
		events << CardFinished << EventLoseSkill << EventPhaseChanging << EventPhaseEnd << Damage;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash")) return false;
			foreach(ServerPlayer *p, use.to) {
				if (p->isDead()) continue;
				if (use.card->hasFlag("tenyeartishen" + p->objectName())) {
					use.card->setFlags("-tenyeartishen" + p->objectName());
					continue;
				}
				if (!p->hasSkill(objectName()) || p->getMark("&tenyeartishen") <= 0) continue;
				if (!room->CardInPlace(use.card, Player::DiscardPile)) continue;
				room->sendCompulsoryTriggerLog(p, objectName(), true, true);
				room->obtainCard(p, use.card, true);
			}

		} else if (event == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash")) return false;
			if (damage.to->isDead()) return false;
			room->setCardFlag(damage.card, "tenyeartishen" + damage.to->objectName());
		} else if (event == EventPhaseChanging) {
			if (player->isDead()) return false;
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::RoundStart) return false;
			room->setPlayerMark(player, "&tenyeartishen", 0);
		} else if (event == EventLoseSkill) {
			if (player->isDead() || data.toString() != objectName()) return false;
			room->setPlayerMark(player, "&tenyeartishen", 0);
		} else if (event == EventPhaseEnd) {
			if (player->isDead() || player->getPhase() != Player::Play || !player->hasSkill(objectName())) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			DummyCard *dummy = new DummyCard;
			foreach (const Card *c, player->getCards("he")) {
				if (!c->isKindOf("TrickCard") && !c->isKindOf("OffensiveHorse") && !c->isKindOf("DefensiveHorse")) continue;
				if (player->canDiscard(player, c->getEffectiveId()))
					dummy->addSubcard(c);
			}
			if (dummy->subcardsLength() > 0)
				room->throwCard(dummy, player, nullptr);
			delete dummy;
			room->setPlayerMark(player, "&tenyeartishen", 1);
		}
		return false;
	}
};

class TenyearGuanxing : public PhaseChangeSkill
{
public:
	TenyearGuanxing() : PhaseChangeSkill("tenyearguanxing")
	{
		frequency = Frequent;
	}

	int getPriority(TriggerEvent) const
	{
		return 1;
	}

	bool onPhaseChange(ServerPlayer *zhuge, Room *room) const
	{
		if (zhuge->getPhase() == Player::Start || (zhuge->getPhase() == Player::Finish && zhuge->getMark("tenyearguanxing-Clear") > 0)) {
			if (!zhuge->askForSkillInvoke(this)) return false;

			int index = qrand() % 2 + 1;
			if (zhuge->isJieGeneral("jiangwei", "zhugeliang") && zhuge->isJieGeneral("jiangwei", "wolong"))
				index += 2;
			room->broadcastSkillInvoke(objectName(), index);

			int num = 5;
			if (room->alivePlayerCount() < 4) num = 3;
			QList<int> guanxing = room->getNCards(num);
			LogMessage log;
			log.type = "$ViewDrawPile";
			log.from = zhuge;
			log.card_str = ListI2S(guanxing).join("+");
			room->sendLog(log, zhuge);
			QList<int> top_cards = room->askForGuanxing(zhuge, guanxing);
			/*bool allbottom = true;  //如果牌堆数量太少，就算选择放在牌堆底，还是会检测到id
			int n = qMin(room->getDrawPile().length(), num);
			for (int i = 0; i < n; i++) {
				int id = room->getDrawPile().at(i);
				if (guanxing.contains(id)) {
					allbottom = false;
					break;
				}
			}
			if ( n == 0 || !allbottom) return false;*/
			if (!top_cards.isEmpty()) return false;
			room->addPlayerMark(zhuge, "tenyearguanxing-Clear");
		}
		return false;
	}
};

class TenyearYajiao : public TriggerSkill
{
public:
	TenyearYajiao() : TriggerSkill("tenyearyajiao")
	{
		events << CardUsed << CardResponded;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->hasFlag("CurrentPlayer")) return false;
		const Card *cardstar = nullptr;
		bool isHandcard = false;
		if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			cardstar = use.card;
			isHandcard = use.m_isHandcard;
		} else {
			CardResponseStruct resp = data.value<CardResponseStruct>();
			cardstar = resp.m_card;
			isHandcard = resp.m_isHandcard;
		}
		if (isHandcard && room->askForSkillInvoke(player, objectName(), data)) {
			room->broadcastSkillInvoke(objectName());
			QList<int> ids = room->getNCards(1, false);
			CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
				CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), "tenyearyajiao", ""));
			room->moveCardsAtomic(move, true);

			const Card *card = Sanguosha->getCard(ids.first());
			player->setMark("tenyearyajiao", ids.first()); // For AI
			ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
				QString("@tenyearyajiao-give:::%1:%2\\%3").arg(card->objectName()).arg(card->getSuitString() + "_char").arg(card->getNumberString()));
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "tenyearyajiao", "");
			room->obtainCard(target, card, reason, true);
			if (card->getTypeId() != cardstar->getTypeId())
				room->askForDiscard(player, objectName(), 1, 1, false, true);
		}
		return false;
	}
};

class TenyearJizhi : public TriggerSkill
{
public:
	TenyearJizhi() : TriggerSkill("tenyearjizhi")
	{
		frequency = Frequent;
		events << CardUsed;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("TrickCard")) return false;
		if (player->getMark("JilveEvent") > 0 || player->askForSkillInvoke(this)) {
			if (player->getMark("JilveEvent") > 0)
				room->broadcastSkillInvoke("jilve", 5);
			else
				room->broadcastSkillInvoke(objectName());
			QList<int> list = player->drawCardsList(1, objectName());
			int id = list.first();
			const Card *card = Sanguosha->getCard(id);
			if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand) return false;
			if (!card->isKindOf("BasicCard") || !player->canDiscard(player, id)) return false;
			room->fillAG(list, player);
			player->tag["tenyearjizhi_id"] = id;
			bool invoke = room->askForSkillInvoke(player, "tenyearjizhi_discard", "discard", false);
			player->tag.remove("tenyearjizhi_id");
			room->clearAG(player);
			if (!invoke) return false;
			room->throwCard(card, player, nullptr);
			room->addMaxCards(player, 1);
		}
		return false;
	}
};

class TenyearJianxiong : public MasochismSkill
{
public:
	TenyearJianxiong() : MasochismSkill("tenyearjianxiong")
	{
		frequency = Frequent;
	}

	void onDamaged(ServerPlayer *caocao, const DamageStruct &damage) const
	{
		if (!caocao->askForSkillInvoke(this)) return;
		Room *room = caocao->getRoom();
		room->broadcastSkillInvoke(objectName());
		caocao->drawCards(1, objectName());

		const Card *card = damage.card;
		if (card && room->CardInTable(card))
			caocao->obtainCard(card);
	}
};

TenyearQingjianCard::TenyearQingjianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void TenyearQingjianCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	if (room->getCurrent())
		room->addPlayerMark(effect.from, "tenyearqingjian-Clear");
	QList<int> ids= getSubcards();
	LogMessage log;
	log.type = "$ShowCard";
	log.from = effect.from;
	log.card_str = ListI2S(ids).join("+");
	room->sendLog(log);
	room->fillAG(ids);
	room->getThread()->delay(1000);
	room->clearAG();
	if(effect.from != effect.to) {
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "tenyearqingjian", "");
		room->obtainCard(effect.to, this, reason, true);
	}
	QList<int> list;
	foreach (int id, ids) {
		const Card *card = Sanguosha->getCard(id);
		if (list.contains(card->getTypeId())) continue;
		list << card->getTypeId();
	}
	if (list.isEmpty() || !room->hasCurrent(true)) return;
	room->addMaxCards(room->getCurrent(), list.length());
}

class TenyearQingjianVS : public ViewAsSkill
{
public:
	TenyearQingjianVS() : ViewAsSkill("tenyearqingjian")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *) const
	{
		return true;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		TenyearQingjianCard *c = new TenyearQingjianCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@tenyearqingjian";
	}
};

class TenyearQingjian : public TriggerSkill
{
public:
	TenyearQingjian() : TriggerSkill("tenyearqingjian")
	{
		events << CardsMoveOneTime;
		view_as_skill = new TenyearQingjianVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!room->getTag("FirstRound").toBool() && player->getPhase() != Player::Draw && move.to == player && move.to_place == Player::PlaceHand) {
			if (player->isNude() || player->getMark("tenyearqingjian-Clear") > 0) return false;
			room->askForUseCard(player, "@@tenyearqingjian", "@tenyearqingjian");
		}
		return false;
	}
};

class TenyearLuoyi : public TriggerSkill
{
public:
	TenyearLuoyi() : TriggerSkill("tenyearluoyi")
	{
		events << EventPhaseStart << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase() != Player::Draw || !player->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			QList<int> ids = room->showDrawPile(player, 3, objectName());
			QList<int> card_to_throw;
			player->tag["tenyearluoyi_ids"] = ListI2V(ids);
			bool invoke = player->askForSkillInvoke(this);
			player->tag.remove("tenyearluoyi_ids");
			if (!invoke) {
				foreach (int id, ids) {
					if (room->getCardPlace(id) == Player::PlaceTable)
						card_to_throw << id;
				}
				if (!card_to_throw.isEmpty()){
					DummyCard *dummy = new DummyCard(card_to_throw);
					CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "tenyearluoyi", "");
					room->throwCard(dummy, reason, nullptr);
					delete dummy;
				}
				return false;
			}
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player, "&tenyearluoyi");
			QList<int> card_to_gotback;
			for (int i = 0; i < 3; i++) {
				const Card *card = Sanguosha->getCard(ids[i]);
				if (card->getTypeId() == Card::TypeBasic || card->isKindOf("Weapon") || card->isKindOf("Duel"))
					card_to_gotback << ids[i];
				else {
					if (room->getCardPlace(ids[i]) == Player::PlaceTable)
						card_to_throw << ids[i];
				}
			}
			if (!card_to_throw.isEmpty()) {
				DummyCard *dummy = new DummyCard(card_to_throw);
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "tenyearluoyi", "");
				room->throwCard(dummy, reason, nullptr);
				delete dummy;
			}
			if (!card_to_gotback.isEmpty()) {
				DummyCard *dummy = new DummyCard(card_to_gotback);
				room->obtainCard(player, dummy);
				delete dummy;
			}
			return true;
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::RoundStart && player->getMark("&tenyearluoyi") > 0)
				room->setPlayerMark(player, "&tenyearluoyi", 0);
		}
		return false;
	}
};

class TenyearLuoyiBuff : public TriggerSkill
{
public:
	TenyearLuoyiBuff() : TriggerSkill("#tenyearluoyibuff")
	{
		events << DamageCaused;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getMark("&tenyearluoyi") > 0 && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *xuchu, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		const Card *reason = damage.card;
		if (reason && (reason->isKindOf("Slash") || reason->isKindOf("Duel"))) {
			LogMessage log;
			log.type = "#LuoyiBuff";
			log.from = xuchu;
			log.to << damage.to;
			log.arg = QString::number(damage.damage);
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);

			data = QVariant::fromValue(damage);
		}

		return false;
	}
};

class TenyearYiji : public MasochismSkill
{
public:
	TenyearYiji() : MasochismSkill("tenyearyiji")
	{
		frequency = Frequent;
	}

	void onDamaged(ServerPlayer *target, const DamageStruct &damage) const
	{
		Room *room = target->getRoom();
		for (int i = 0; i < damage.damage; i++) {
			if (target->isAlive() && room->askForSkillInvoke(target, objectName(), QVariant::fromValue(damage))) {
				room->broadcastSkillInvoke(objectName());
				target->drawCards(2, objectName());
				if (!target->isKongcheng()) {
					QList<int> handcards = target->handCards();
					int n = 0;
					QHash<ServerPlayer *, QStringList> hash;
					while (n < 2) {
						CardsMoveStruct yiji_move = room->askForYijiStruct(target, handcards, objectName(), false, false, true, 2 - n,
													room->getOtherPlayers(target), CardMoveReason(), "", false, false);
						if (!yiji_move.to || yiji_move.card_ids.isEmpty()) break;
						n += yiji_move.card_ids.length();

						QStringList id_strings = hash[(ServerPlayer *)yiji_move.to];
						foreach (int id, yiji_move.card_ids)
							id_strings << QString::number(id);
						hash[(ServerPlayer *)yiji_move.to] = id_strings;
					}

					QList<CardsMoveStruct> moves;
					foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
						if (p->isDead()) continue;
						QList<int> ids = ListS2I(hash[p]);
						if (ids.isEmpty()) continue;
						hash.remove(p);
						CardsMoveStruct move(ids, target, p, Player::PlaceHand, Player::PlaceHand,
							CardMoveReason(CardMoveReason::S_REASON_GIVE, target->objectName(), p->objectName(), "tenyearyiji", ""));
						moves.append(move);
					}
					if (moves.isEmpty()) return;
					room->moveCardsAtomic(moves, false);
				}
			} else
				break;
		}
	}
};

class TenyearLuoshen : public TriggerSkill
{
public:
	TenyearLuoshen() : TriggerSkill("tenyearluoshen")
	{
		events << EventPhaseStart << FinishJudge << EventPhaseProceeding << CardsMoveOneTime;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *zhenji, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if (zhenji->getPhase() != Player::Start) return false;
			bool first = true;
			while (zhenji->askForSkillInvoke("tenyearluoshen")) {
				if (first) {
					room->broadcastSkillInvoke(objectName());
					first = false;
				}

				JudgeStruct judge;
				judge.pattern = ".|black";
				judge.good = true;
				judge.reason = objectName();
				//judge.play_animation = false;
				judge.who = zhenji;
				//judge.time_consuming = true;
				room->judge(judge);

				if (!judge.isGood()) break;
			}
		} else if (triggerEvent == FinishJudge) {
			JudgeStruct *judge = data.value<JudgeStruct *>();
			if (judge->reason == objectName()) {
				if (judge->card->isBlack()) {
					if (room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge) {
						CardMoveReason reason(CardMoveReason::S_REASON_GOTCARD, zhenji->objectName(), "tenyearluoshen", "");
						room->obtainCard(zhenji, judge->card, reason, true);
					}
				}
			}
		} else if (triggerEvent == EventPhaseProceeding) {
			if (zhenji->getPhase() != Player::Discard) return false;
			QList<int> luoshenlist;
			foreach (int id, zhenji->handCards()) {
				const Card *c = Sanguosha->getCard(id);
				if (!c->hasTip(objectName())) continue;
				luoshenlist << id;
			}
			if (luoshenlist.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(zhenji, this);
			room->ignoreCards(zhenji, luoshenlist);
		} else if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.reason.m_skillName != objectName()) return false;
			if (move.to == zhenji && move.to_place == Player::PlaceHand) {
				QList<int> hands = zhenji->handCards();
				foreach (int id, move.card_ids) {
					if (!hands.contains(id)) continue;
					room->setCardTip(id, "tenyearluoshen-Clear");
				}
			}
		}
		return false;
	}
};

TenyearTuxiCard::TenyearTuxiCard()
{
}

bool TenyearTuxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (targets.length() >= Self->getMark("tenyeartuxi") || to_select == Self)
		return false;

	return !to_select->isKongcheng();
}

void TenyearTuxiCard::onEffect(CardEffectStruct &effect) const
{
	effect.from->addMark("tenyeartuxiNum");
	if (effect.to->isKongcheng()||effect.from->isDead()) return;
    Room *room = effect.to->getRoom();
    int card_id = room->askForCardChosen(effect.from, effect.to, "h", "tenyeartuxi");
	CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,effect.from->objectName(),effect.to->objectName(),"tenyeartuxi","");
    room->obtainCard(effect.from, Sanguosha->getCard(card_id), reason, false);
}

class TenyearTuxiViewAsSkill : public ZeroCardViewAsSkill
{
public:
	TenyearTuxiViewAsSkill() : ZeroCardViewAsSkill("tenyeartuxi")
	{
		response_pattern = "@@tenyeartuxi";
	}

	const Card *viewAs() const
	{
		return new TenyearTuxiCard;
	}
};

class TenyearTuxi : public DrawCardsSkill
{
public:
	TenyearTuxi() : DrawCardsSkill("tenyeartuxi")
	{
		view_as_skill = new TenyearTuxiViewAsSkill;
	}

	int getPriority(TriggerEvent) const
	{
		return 1;
	}

	int getDrawNum(ServerPlayer *zhangliao, int n) const
	{
		Room *room = zhangliao->getRoom();
		int num = qMin(room->getOtherPlayers(zhangliao).length(), n);

		if (num > 0) {
			zhangliao->setMark("tenyeartuxiNum", 0);
			room->setPlayerMark(zhangliao, "tenyeartuxi", num);
			if (room->askForUseCard(zhangliao, "@@tenyeartuxi", "@tuxi-card:::" + QString::number(num))) {
				n -= zhangliao->getMark("tenyeartuxiNum");
			}
		}
		return n;
	}
};

TenyearQingnangCard::TenyearQingnangCard()
{
}

bool TenyearQingnangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty() && to_select->isWounded() && to_select->getMark("tenyearqingnang_target-PlayClear") <= 0;
}

void TenyearQingnangCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->recover(effect.to, RecoverStruct("tenyearqingnang", effect.from));
	room->setPlayerMark(effect.to, "tenyearqingnang_target-PlayClear", 1);
	const Card *card = Sanguosha->getCard(getSubcards().first());
	if (card->isRed()) return;
	room->setPlayerMark(effect.from, "tenyearqingnang-PlayClear", 1);
}

class TenyearQingnang : public OneCardViewAsSkill
{
public:
	TenyearQingnang() : OneCardViewAsSkill("tenyearqingnang")
	{
		filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->canDiscard(player, "h") && player->getMark("tenyearqingnang-PlayClear") <= 0;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		TenyearQingnangCard *qingnang_card = new TenyearQingnangCard;
		qingnang_card->addSubcard(originalCard);
		return qingnang_card;
	}
};

class TenyearLiyu : public TriggerSkill
{
public:
	TenyearLiyu() : TriggerSkill("tenyearliyu")
	{
		events << Damage;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to->isAlive() && player != damage.to && !damage.to->hasFlag("Global_DebutFlag") && !damage.to->isAllNude()
			&& damage.card && damage.card->isKindOf("Slash")) {
			if (!player->askForSkillInvoke(this, QVariant::fromValue(damage.to))) return false;
			room->broadcastSkillInvoke(objectName());
			int card_id = room->askForCardChosen(player, damage.to, "hej", "tenyearliyu");
			const Card *card = Sanguosha->getCard(card_id);
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
			room->obtainCard(player, card, reason, true);

			if (!card->isKindOf("EquipCard"))
				damage.to->drawCards(1, objectName());
			else {
				Duel *duel = new Duel(Card::NoSuit, 0);
				duel->setSkillName("_tenyearliyu");

				QList<ServerPlayer *> targets;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (p != damage.to && player->canUse(duel, p))
						targets << p;
				}
				ServerPlayer *target = room->askForPlayerChosen(damage.to, targets, objectName(), "@tenyearliyu:" + player->objectName());
				if (target&&player->isAlive())
					room->useCard(CardUseStruct(duel, player, target));
				delete duel;
			}
		}
		return false;
	}
};

class TenyearBiyue : public PhaseChangeSkill
{
public:
	TenyearBiyue() : PhaseChangeSkill("tenyearbiyue")
	{
		frequency = Frequent;
	}

	bool onPhaseChange(ServerPlayer *diaochan, Room *room) const
	{
		if (diaochan->getPhase() == Player::Finish) {
			if (room->askForSkillInvoke(diaochan, objectName())) {
				room->broadcastSkillInvoke(objectName());
				int n = 1;
				if (diaochan->isKongcheng()) n = 2;
				diaochan->drawCards(n, objectName());
			}
		}
		return false;
	}
};

class TenyearYaowu : public TriggerSkill
{
public:
	TenyearYaowu() : TriggerSkill("tenyearyaowu")
	{
		events << DamageInflicted;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.card && damage.card->isKindOf("Slash") && player->isAlive()) {
			if (damage.card->isRed()) {
				if (!damage.from || damage.from->isDead()) return false;
				room->sendCompulsoryTriggerLog(player, objectName(), true, true, 2);
				QStringList choices;
				choices << "draw";
				if (damage.from->getLostHp() > 0) choices << "recover";
				if (room->askForChoice(damage.from, objectName(), choices.join("+")) == "draw")
					damage.from->drawCards(1, objectName());
				else
					room->recover(damage.from, RecoverStruct("tenyearyaowu", damage.to));
			} else {
				room->sendCompulsoryTriggerLog(player, objectName(), true, true, 1);
				player->drawCards(1, objectName());
			}
		}
		return false;
	}
};


class TenyearLiegong : public TriggerSkill
{
public:
	TenyearLiegong() : TriggerSkill("tenyearliegong")
	{
		events << TargetSpecified << DamageCaused;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash")) return false;
			int handnum = player->getHandcardNum();
			int hp = player->getHp();
			foreach (ServerPlayer *p, use.to) {
				if (p->getHandcardNum() > handnum && p->getHp() < hp) continue;
				if (!player->askForSkillInvoke(this, QVariant::fromValue(p))) continue;
				player->peiyin(this);
				if (p->getHandcardNum() <= handnum) {
					LogMessage log;
					log.type = "#NoJink";
					log.from = p;
					room->sendLog(log);
					use.no_respond_list << p->objectName();
				}
				if (p->getHp() >= hp)
					room->setCardFlag(use.card, "tenyearliegong_damage" + p->objectName());
			}
			data = QVariant::fromValue(use);
		} else if (event == DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash")) return false;
			if (damage.to->isDead()) return false;
			if (!damage.card->hasFlag("tenyearliegong_damage" + damage.to->objectName())) return false;
			++damage.damage;
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class TenyearLiegongMod : public TargetModSkill
{
public:
	TenyearLiegongMod() : TargetModSkill("#tenyearliegongmod")
	{
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const
	{
		if (from->hasSkill("tenyearliegong") || from->hasSkill("mobilemouliegong"))
			return qMax(0, card->getNumber() - from->getAttackRange());
		return 0;
	}
};

class TenyearKuanggu : public TriggerSkill
{
public:
	TenyearKuanggu() : TriggerSkill("tenyearkuanggu")
	{
		events << Damage;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (player->tag["InvokeKuanggu"].toBool()) {
			for (int i = 0; i < damage.damage; i++) {
				if (!player->isAlive()||!player->askForSkillInvoke(this)) break;
				int index = qrand() % 2 + 1;
				if (player->getGeneralName().startsWith("ol_") || player->getGeneral2Name().startsWith("ol_"))
					index += 2;
				room->broadcastSkillInvoke(objectName(), index);
				QStringList choices;
				if(player->getLostHp()>0)
					choices << "recover";
				choices << "draw";
				if(choices.length()>1&&player->getMark("zhongaoUptenyearkuanggu")>0&&player->canDiscard(player,"he"))
					choices << "beishui";
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
				if (choice=="beishui"){
					room->askForDiscard(player,objectName(),1,1,false,true);
					room->addSlashCishu(player,1);
				}
				if (choice!="draw") room->recover(player, RecoverStruct(objectName(), player));
				if (choice!="recover") player->drawCards(1, objectName());
			}
		}
		return false;
	}
};

TenyearQimouCard::TenyearQimouCard()
{
	target_fixed = true;
}

void TenyearQimouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@tenyearqimouMark");
	room->doSuperLightbox(source, "tenyearqimou");
	QStringList choices;
	for (int i = 1; i <= source->getHp(); i++)
		choices << QString::number(i);
	QString choice = room->askForChoice(source, "tenyearqimou", choices.join("+"));
	int n = choice.toInt();
	room->loseHp(HpLostStruct(source, n, "tenyearqimou", source));
	if (source->isAlive()) {
		room->addDistance(source, -n);
		room->addSlashCishu(source, n);
	}
}

class TenyearQimou : public ZeroCardViewAsSkill
{
public:
	TenyearQimou() : ZeroCardViewAsSkill("tenyearqimou")
	{
		frequency = Limited;
		limit_mark = "@tenyearqimouMark";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getHp() > 0 && player->getMark("@tenyearqimouMark") > 0;
	}

	const Card *viewAs() const
	{
		return new TenyearQimouCard;
	}
};

TenyearShensuCard::TenyearShensuCard()
{
}

bool TenyearShensuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearshensu");
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

void TenyearShensuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if(user_string.contains("1")){
		source->skip(Player::Judge, true);
		source->skip(Player::Draw, true);
	}else if(user_string.contains("2")){
		source->skip(Player::Play, true);
	}else{
		source->skip(Player::Discard, true);
		source->turnOver();
	}
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearshensu");
	room->useCard(CardUseStruct(slash, source, targets));
	slash->deleteLater();
}

class TenyearShensuViewAsSkill : public ViewAsSkill
{
public:
	TenyearShensuViewAsSkill() : ViewAsSkill("tenyearshensu")
	{
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@tenyearshensu");
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if (pattern.endsWith("1") || pattern.endsWith("3"))
			return false;
		return selected.isEmpty() && to_select->isKindOf("EquipCard") && !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if (pattern.endsWith("1") || pattern.endsWith("3")) {
		} else {
			if (cards.length() != 1)
				return nullptr;
		}
		TenyearShensuCard *card = new TenyearShensuCard;
		card->setUserString(pattern);
		card->addSubcards(cards);
		return card;
	}
};

class TenyearShensu : public TriggerSkill
{
public:
	TenyearShensu() : TriggerSkill("tenyearshensu")
	{
		events << EventPhaseChanging;
		view_as_skill = new TenyearShensuViewAsSkill;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *xiahouyuan, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to == Player::Judge && !xiahouyuan->isSkipped(Player::Judge) && !xiahouyuan->isSkipped(Player::Draw)) {
            room->askForUseCard(xiahouyuan, "@@tenyearshensu1", "@shensu1", 1);
		} else if (change.to == Player::Play && !xiahouyuan->isSkipped(Player::Play)) {
            room->askForUseCard(xiahouyuan, "@@tenyearshensu2", "@shensu2", 2, Card::MethodDiscard);
		} else if (change.to == Player::Discard && !xiahouyuan->isSkipped(Player::Discard)) {
            room->askForUseCard(xiahouyuan, "@@tenyearshensu3", "@tenyearshensu3", 3);
		}
		return false;
	}
};

class TenyearJushou : public PhaseChangeSkill
{
public:
	TenyearJushou() : PhaseChangeSkill("tenyearjushou")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		player->turnOver();
		player->drawCards(4, objectName());
		if (player->isKongcheng()) return false;

		QStringList list;
		foreach (const Card *c, player->getCards("h")) {
			if (c->isKindOf("EquipCard") && c->isAvailable(player) && !player->isCardLimited(c, Card::MethodUse))
				list << c->toString();
			else if (!c->isKindOf("EquipCard") && player->canDiscard(player, c->getEffectiveId()))
				list << c->toString();
		}

		if (list.isEmpty()) {
			LogMessage log;
			log.type = "#TenyearjushouShow";
			log.from = player;
			room->sendLog(log);
			room->showAllCards(player);
			return false;
		}

		QString pattern = list.join(",");
		if (!pattern.endsWith("!"))
			pattern = pattern + "!";
		const Card *card = room->askForCard(player, pattern, "@tenyearjushou", QVariant(), Card::MethodNone);
		if (!card) {
			QList<int> ids = ListS2I(list);
			card = Sanguosha->getCard(ids.at(qrand() % ids.length()));
		}
		if (card->isKindOf("EquipCard"))
			room->useCard(CardUseStruct(card, player));
		else {
			CardMoveReason reason(CardMoveReason::S_REASON_THROW, player->objectName(), "tenyearjushou", "");
			room->throwCard(card, reason, player, nullptr);
		}
		return false;
	}
};

class TenyearJieweiVS : public OneCardViewAsSkill
{
public:
	TenyearJieweiVS() : OneCardViewAsSkill("tenyearjiewei")
	{
		filter_pattern = ".|.|.|equipped";
		response_pattern = "nullification";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Card *ncard = new Nullification(originalCard->getSuit(), originalCard->getNumber());
		ncard->addSubcard(originalCard);
		ncard->setSkillName(objectName());
		return ncard;
	}

	bool isEnabledAtNullification(const ServerPlayer *player) const
	{
		return !player->getEquips().isEmpty();
	}
};

class TenyearJiewei : public TriggerSkill
{
public:
	TenyearJiewei() : TriggerSkill("tenyearjiewei")
	{
		events << TurnedOver;
		view_as_skill = new TenyearJieweiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (!player->faceUp()) return false;
		if (!player->canDiscard(player, "he")) return false;
		if (!room->askForCard(player, "..", "@tenyearjiewei", QVariant(), objectName())) return false;
		room->broadcastSkillInvoke(objectName());
		room->moveField(player, "tenyearjiewei");
		return false;
	}
};

TenyearTianxiangCard::TenyearTianxiangCard(QString this_skill_name) : this_skill_name(this_skill_name)
{
	handling_method = Card::MethodDiscard;
}

void TenyearTianxiangCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->isDead() || effect.to->isDead()) return;
	Room *room = effect.from->getRoom();
	if (room->askForChoice(effect.from, this_skill_name, "damage+losehp") == "damage") {
		room->damage(DamageStruct(this_skill_name, nullptr, effect.to));
		if (effect.to->isDead()) return;
		int n = qMin(5, effect.to->getLostHp());
		if (n <= 0) return;
		effect.to->drawCards(n, this_skill_name);
	} else {
		room->loseHp(HpLostStruct(effect.to, 1, "tenyeartianxiang", effect.from));
		if (effect.to->isDead()) return;
		room->obtainCard(effect.to, this, true);
	}
}

class TenyearTianxiangViewAsSkill : public OneCardViewAsSkill
{
public:
	TenyearTianxiangViewAsSkill() : OneCardViewAsSkill("tenyeartianxiang")
	{
		filter_pattern = ".|heart|.|hand";
		response_pattern = "@@tenyeartianxiang";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		TenyearTianxiangCard *tianxiangCard = new TenyearTianxiangCard;
		tianxiangCard->addSubcard(originalCard);
		return tianxiangCard;
	}
};

class TenyearTianxiang : public TriggerSkill
{
public:
	TenyearTianxiang() : TriggerSkill("tenyeartianxiang")
	{
		events << DamageInflicted;
		view_as_skill = new TenyearTianxiangViewAsSkill;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *xiaoqiao, QVariant &) const
	{
		if (xiaoqiao->canDiscard(xiaoqiao, "h")) {
			return room->askForUseCard(xiaoqiao, "@@tenyeartianxiang", "@tenyeartianxiang", -1, Card::MethodDiscard);
		}
		return false;
	}
};

TenyearSanyaoCard::TenyearSanyaoCard()
{
}

bool TenyearSanyaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	int max = -1000;
	foreach (const Player *p, Self->getAliveSiblings()) {
		if (max < p->getHp())
			max = p->getHp();
	}
	return to_select->getHp() == max && targets.length() < getSubcards().length() && to_select != Self;
}

bool TenyearSanyaoCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == getSubcards().length();
}

void TenyearSanyaoCard::onEffect(CardEffectStruct &effect) const
{
	effect.from->getRoom()->damage(DamageStruct("tenyearsanyao", effect.from, effect.to));
}

class TenyearSanyao : public ViewAsSkill
{
public:
	TenyearSanyao() : ViewAsSkill("tenyearsanyao")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		QList<const Player *> players = Self->getAliveSiblings();
		int max = -1000;
		foreach (const Player *p, players) {
			if (max < p->getHp())
				max = p->getHp();
		}
		int num = 0;
		foreach (const Player *p, players) {
			if (p->getHp() == max)
				num++;
		}
		return !Self->isJilei(to_select) && selected.length() < num;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearSanyaoCard");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		TenyearSanyaoCard *first = new TenyearSanyaoCard;
		first->addSubcards(cards);
		return first;
	}
};

class TenyearZhiman : public TriggerSkill
{
public:
	TenyearZhiman() : TriggerSkill("tenyearzhiman")
	{
		events << DamageCaused;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to == player || damage.to->isDead()) return false;
		if (player->askForSkillInvoke(this, data)) {
			room->broadcastSkillInvoke(objectName());
			LogMessage log;
			log.type = "#Yishi";
			log.from = player;
			log.arg = objectName();
			log.to << damage.to;
			room->sendLog(log);
			if (damage.to->isAllNude()) return true;
			int card_id = room->askForCardChosen(player, damage.to, "hej", objectName());
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
			room->obtainCard(player, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);
			return true;
		}
		return false;
	}
};

class TenyearZhenjun : public PhaseChangeSkill
{
public:
	TenyearZhenjun() : PhaseChangeSkill("tenyearzhenjun")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Start) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (player->canDiscard(p, "he"))
				targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@tenyearzhenjun-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		int n = qMax(target->getHandcardNum() - target->getHp(), 1);
		n = qMin(target->getCards("he").length(), n);
		if (n <= 0) return false;
		QList<int> cards;

		for (int i = 0; i < n; ++i) {
			if (target->getCardCount()<=i) break;
			int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard, cards);
			if(id<0) break;
			cards << id;
		}

		if (!cards.isEmpty()) {
			DummyCard dummy(cards);
			room->throwCard(&dummy, target, player);
			int equips = 0;
			foreach (int id, cards) {
				const Card *card = Sanguosha->getCard(id);
				if (!card->isKindOf("EquipCard"))
					equips++;
			}
			if (equips == 0) return false;

			int candis = 0;
			foreach (const Card *card, player->getCards("he")) {
				if (player->canDiscard(player, card->getId()))
					candis++;
			}
			if (candis < equips)
				target->drawCards(equips, objectName());
			else {
				if (!room->askForDiscard(player, objectName(), equips, equips, true, true, "tenyearzhenjun-discard:" + QString::number(equips)))
					target->drawCards(equips, objectName());
			}
		}
		return false;
	}
};

class SecondTenyearZhenjun : public PhaseChangeSkill
{
public:
	SecondTenyearZhenjun() : PhaseChangeSkill("secondtenyearzhenjun")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Start && player->getPhase() != Player::Finish) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (player->canDiscard(p, "he"))
				targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@tenyearzhenjun-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		int n = qMax(target->getHandcardNum() - target->getHp(), 1);
		n = qMin(target->getCards("he").length(), n);
		if (n <= 0) return false;

		QList<int> cards;

		for (int i = 0; i < n; ++i) {
			if (target->getCardCount()<=i) break;
			int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard, cards);
			if(id<0) break;
			cards << id;
		}

		if (!cards.isEmpty()) {
			DummyCard dummy(cards);
			room->throwCard(&dummy, target, player);
			foreach (int id, cards) {
				if (Sanguosha->getCard(id)->isKindOf("EquipCard"))
					return false;
			}

			QString prompt = QString("@secondtenyearzhenjun-discard:%1::%2").arg(target->objectName()).arg(cards.length());
			if (player->canDiscard(player, "he") && room->askForDiscard(player, objectName(), 1, 1, true, true, prompt)) return false;
			target->drawCards(cards.length(), objectName());
		}
		return false;
	}
};

class TenyearJingce : public PhaseChangeSkill
{
public:
	TenyearJingce() : PhaseChangeSkill("tenyearjingce")
	{
		frequency = Frequent;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() == Player::Finish) {
			if (player->getMark("tenyearjingce-Clear") < player->getHp()) return false;
			if (room->askForSkillInvoke(player, objectName())) {
				room->broadcastSkillInvoke(objectName());
				player->drawCards(2, objectName());
			}
		}
		return false;
	}
};

class SecondTenyearJingce : public PhaseChangeSkill
{
public:
	SecondTenyearJingce() : PhaseChangeSkill("secondtenyearjingce")
	{
		frequency = Frequent;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() == Player::Finish) {
			if (player->getMark("tenyearjingce-Clear") < player->getHp()) return false;
			if (room->askForSkillInvoke(player, objectName())) {
				room->broadcastSkillInvoke(objectName());

				int mark = 0;
				QStringList suit_strings;
				suit_strings << "spade" << "heart" << "club" << "diamond" << "no_suit";
				foreach (QString s, suit_strings)
					mark += player->getMark("secondtenyearjingce" + s + "-Clear");

				//RoomThread *thread = room->getThread();
				if (mark >= player->getHp()) {
					player->insertPhase(Player::Draw);/*
					room->broadcastProperty(player, "phase");
					if (!thread->trigger(EventPhaseStart, room, player))
						thread->trigger(EventPhaseProceeding, room, player);
					thread->trigger(EventPhaseEnd, room, player);*/

					player->insertPhase(Player::Play);/*
					room->broadcastProperty(player, "phase");
					if (!thread->trigger(EventPhaseStart, room, player))
						thread->trigger(EventPhaseProceeding, room, player);
					thread->trigger(EventPhaseEnd, room, player);

					player->setPhase(Player::Finish);
					room->broadcastProperty(player, "phase");*/
				} else {
					if (room->askForChoice(player, objectName(), "draw+play") == "draw") {
						player->insertPhase(Player::Draw);/*
						room->broadcastProperty(player, "phase");
						RoomThread *thread = room->getThread();
						if (!thread->trigger(EventPhaseStart, room, player))
							thread->trigger(EventPhaseProceeding, room, player);
						thread->trigger(EventPhaseEnd, room, player);*/
					} else {
						player->insertPhase(Player::Play);/*
						room->broadcastProperty(player, "phase");
						if (!thread->trigger(EventPhaseStart, room, player))
							thread->trigger(EventPhaseProceeding, room, player);
						thread->trigger(EventPhaseEnd, room, player);*/
					}/*
					player->setPhase(Player::Finish);
					room->broadcastProperty(player, "phase");*/
				}
			}
		}
		return false;
	}
};

class TenyearDangxian : public PhaseChangeSkill
{
public:
	TenyearDangxian() : PhaseChangeSkill("tenyeardangxian")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() == Player::RoundStart) {
			//room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			LogMessage log;
			log.type = "#TenyeardangxianPlayPhase";
			log.from = player;
			log.arg = "tenyeardangxian";
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());

			room->setPlayerFlag(player, "tenyeardangxian");
			player->insertPhase(Player::Play);/*
			room->broadcastProperty(player, "phase");
			RoomThread *thread = room->getThread();
			if (!thread->trigger(EventPhaseStart, room, player)) {
				if (player->hasFlag("tenyeardangxian"))
					room->setPlayerFlag(player, "-tenyeardangxian");
				thread->trigger(EventPhaseProceeding, room, player);
			}
			if (player->hasFlag("tenyeardangxian"))
				room->setPlayerFlag(player, "-tenyeardangxian");
			thread->trigger(EventPhaseEnd, room, player);

			player->setPhase(Player::RoundStart);
			room->broadcastProperty(player, "phase");*/
		} else if (player->getPhase() == Player::Play) {
			if (!player->hasFlag("tenyeardangxian")) return false;
			room->setPlayerFlag(player, "-tenyeardangxian");
			if (player->getMark(objectName()) <= 0)
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			else {
				if (!player->askForSkillInvoke(objectName()))
					return false;
				room->broadcastSkillInvoke(objectName());
			}
			room->loseHp(HpLostStruct(player, 1, objectName(), player));
			if (player->isDead()) return  false;
			QList<int> slash;
			foreach (int id, room->getDiscardPile()) {
				const Card *card = Sanguosha->getCard(id);
				if (!card->isKindOf("Slash")) continue;
				slash << id;
			}
			if (slash.isEmpty()) return false;
			room->obtainCard(player, slash.at(qrand() % slash.length()),true);
		}
		return false;
	}
};

class TenyearFuli : public TriggerSkill
{
public:
	TenyearFuli() : TriggerSkill("tenyearfuli")
	{
		events << AskForPeaches;
		frequency = Limited;
		limit_mark = "@tenyearfuliMark";
	}

	int getKingdoms(Room *room) const
	{
		QSet<QString> kingdom_set;
		foreach(ServerPlayer *p, room->getAlivePlayers())
			kingdom_set << p->getKingdom();
		return kingdom_set.size();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *liaohua, QVariant &data) const
	{
		DyingStruct dying_data = data.value<DyingStruct>();
		if (dying_data.who != liaohua || liaohua->getMark("@tenyearfuliMark") <= 0) return false;
		if (liaohua->askForSkillInvoke(this, data)) {
			room->broadcastSkillInvoke(objectName());

			room->doSuperLightbox(liaohua, "tenyearfuli");

			room->removePlayerMark(liaohua, "@tenyearfuliMark");
			int x = getKingdoms(room);
			int n = qMin(x - liaohua->getHp(), liaohua->getMaxHp() - liaohua->getHp());
			if (n > 0) room->recover(liaohua, RecoverStruct(liaohua, nullptr, n, objectName()));
			if (liaohua->getHandcardNum() < x) liaohua->drawCards(x - liaohua->getHandcardNum(), objectName());
			room->addPlayerMark(liaohua, "tenyeardangxian");
			room->changeTranslation(liaohua, "tenyeardangxian", 2);
			if (x >= 3) liaohua->turnOver();
		}
		return false;
	}
};

TenyearChunlaoCard::TenyearChunlaoCard(QString tenyearchunlao) : tenyearchunlao(tenyearchunlao)
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void TenyearChunlaoCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->addToPile("wine", this);
}

TenyearChunlaoWineCard::TenyearChunlaoWineCard(QString tenyearchunlao) : tenyearchunlao(tenyearchunlao)
{
	m_skillName = tenyearchunlao;
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void TenyearChunlaoWineCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	ServerPlayer *who = room->getCurrentDyingPlayer();
	if (!who) return;

	if (subcards.length() != 0) {
		room->throwCard(subcards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), tenyearchunlao, ""), nullptr);
		Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
		analeptic->setSkillName("_" + tenyearchunlao);
		room->useCard(CardUseStruct(analeptic, who, who, false));
		analeptic->deleteLater();

		const Card *card = Sanguosha->getCard(getSubcards().first());
		if (card->getClassName() == "FireSlash")
			room->recover(source, RecoverStruct(tenyearchunlao, source));
		else if (card->getClassName() == "ThunderSlash")
			source->drawCards(2, tenyearchunlao);
	}
}

class TenyearChunlaoViewAsSkill : public ViewAsSkill
{
public:
	TenyearChunlaoViewAsSkill(const QString &tenyearchunlao) : ViewAsSkill(tenyearchunlao), tenyearchunlao(tenyearchunlao)
	{
		expand_pile = "wine";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern == "@@" + tenyearchunlao
			|| (pattern.contains("peach") && !player->getPile("wine").isEmpty());
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@" + tenyearchunlao)
			return to_select->isKindOf("Slash");
		else {
			ExpPattern pattern(".|.|.|wine");
			if (!pattern.match(Self, to_select)) return false;
			return selected.length() == 0;
		}
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@" + tenyearchunlao) {
			if (cards.length() == 0) return nullptr;

			Card *acard = nullptr;
			if (tenyearchunlao == "tenyearchunlao")
				acard = new TenyearChunlaoCard;
			else if (tenyearchunlao == "secondtenyearchunlao")
				acard = new SecondTenyearChunlaoCard;
			if (!acard) return nullptr;
			acard->addSubcards(cards);
			acard->setSkillName(tenyearchunlao);
			return acard;
		} else {
			if (cards.length() != 1) return nullptr;
			Card *wine = nullptr;
			if (tenyearchunlao == "tenyearchunlao")
				wine = new TenyearChunlaoWineCard;
			else if (tenyearchunlao == "secondtenyearchunlao")
				wine = new SecondTenyearChunlaoWineCard;
			if (!wine) return nullptr;
			wine->addSubcards(cards);
			wine->setSkillName(tenyearchunlao);
			return wine;
		}
	}

private:
	QString tenyearchunlao;
};

class TenyearChunlao : public PhaseChangeSkill
{
public:
	TenyearChunlao() : PhaseChangeSkill("tenyearchunlao")
	{
		view_as_skill = new TenyearChunlaoViewAsSkill("tenyearchunlao");
	}

	bool onPhaseChange(ServerPlayer *chengpu, Room *room) const
	{
		if (chengpu->getPhase() == Player::Finish && !chengpu->isKongcheng() && chengpu->getPile("wine").isEmpty())
			room->askForUseCard(chengpu, "@@tenyearchunlao", "@tenyearchunlao", -1, Card::MethodNone);
		return false;
	}
};

class SecondTenyearLihuoViewAsSkill : public OneCardViewAsSkill
{
public:
	SecondTenyearLihuoViewAsSkill() : OneCardViewAsSkill("secondtenyearlihuo")
	{
		filter_pattern = "%slash";
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& (pattern.contains("slash") || pattern.contains("Slash"));
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Card *acard = new FireSlash(originalCard->getSuit(), originalCard->getNumber());
		acard->addSubcard(originalCard->getId());
		acard->setSkillName(objectName());
		return acard;
	}
};

class SecondTenyearLihuo : public TriggerSkill
{
public:
	SecondTenyearLihuo() : TriggerSkill("secondtenyearlihuo")
	{
		events << CardFinished << ChangeSlash;
		view_as_skill = new SecondTenyearLihuoViewAsSkill;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == ChangeSlash) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->objectName() != "slash") return false;
			bool has_changed = false;
			QString skill_name = use.card->getSkillName();
			if (!skill_name.isEmpty()) {
				const Skill *skill = Sanguosha->getSkill(skill_name);
				if (skill && !skill->inherits("FilterSkill") && !skill_name.contains("guhuo"))
					has_changed = true;
			}
			if (!has_changed || (use.card->isVirtualCard() && use.card->subcardsLength() == 0)) {
				FireSlash *fire_slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
				fire_slash->setSkillName("secondtenyearlihuo");
				fire_slash->addSubcard(use.card);
				bool can_use = true;
				foreach (ServerPlayer *p, use.to) {
					if (!player->canSlash(p, fire_slash, false)) {
						can_use = false;
						break;
					}
				}
				if (can_use && room->askForSkillInvoke(player, "secondtenyearlihuo", data, false)) {
					use.changeCard(fire_slash);
					data = QVariant::fromValue(use);
				}
				fire_slash->deleteLater();
			}
		} else if (triggerEvent == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->hasFlag("DamageDone")&&use.card->isKindOf("Slash")&&use.card->getSkillNames().contains(objectName())) {
				room->sendCompulsoryTriggerLog(player, objectName());
				room->loseHp(HpLostStruct(player, 1, objectName(), player));
			}

			if (player->isDead() || !use.card->isKindOf("Slash")) return false;
			if (!use.card->hasFlag("first_card_in_one_turn")) return false;
			if (!room->CardInPlace(use.card, Player::DiscardPile) || !player->askForSkillInvoke(this, "put")) return false;
			room->broadcastSkillInvoke(objectName());
			player->addToPile("wine", use.card);
		}
		return false;
	}
};

class SecondTenyearLihuoTargetMod : public TargetModSkill
{
public:
	SecondTenyearLihuoTargetMod() : TargetModSkill("#secondtenyearlihuo-target")
	{
		frequency = NotFrequent;
	}

	int getExtraTargetNum(const Player *from, const Card *card) const
	{
		if (card->isKindOf("FireSlash")&&from->hasSkills("secondtenyearlihuo|ollihuo"))
			return 1;
		return 0;
	}
};

SecondTenyearChunlaoCard::SecondTenyearChunlaoCard() : TenyearChunlaoCard("secondtenyearchunlao")
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

SecondTenyearChunlaoWineCard::SecondTenyearChunlaoWineCard() : TenyearChunlaoWineCard("secondtenyearchunlao")
{
	m_skillName = "secondtenyearchunlao";
	mute = true;
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

class SecondTenyearChunlao : public TriggerSkill
{
public:
	SecondTenyearChunlao() : TriggerSkill("secondtenyearchunlao")
	{
		events << EventPhaseEnd;
		view_as_skill = new TenyearChunlaoViewAsSkill("secondtenyearchunlao");
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		{
			if (player->getPhase() == Player::Play && !player->isKongcheng() && player->getPile("wine").isEmpty())
				room->askForUseCard(player, "@@secondtenyearchunlao", "@secondtenyearchunlao", -1, Card::MethodNone);
			return false;
		}
	}
};

TenyearJiangchiCard::TenyearJiangchiCard()
{
	target_fixed = true;
	mute = true;
}

void TenyearJiangchiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (getSubcards().isEmpty()) {
		room->broadcastSkillInvoke("tenyearjiangchi", 1);
		source->drawCards(1, "tenyearjiangchi");
		room->setPlayerCardLimitation(source, "use,response", "Slash", true);
	} else if (getSubcards().length() == 1) {
		room->broadcastSkillInvoke("tenyearjiangchi", 2);
		room->addSlashJuli(source, 1000);
		room->addSlashCishu(source, 1);
	}
}

class TenyearJiangchiVS : public ViewAsSkill
{
public:
	TenyearJiangchiVS() : ViewAsSkill("tenyearjiangchi")
	{
	}
	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@tenyearjiangchi";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return !Self->isJilei(to_select) && selected.isEmpty();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != 0 && cards.length() != 1) return nullptr;
		Card *acard = new TenyearJiangchiCard;
		if (!cards.isEmpty())
			acard->addSubcards(cards);
		return acard;
	}
};

class TenyearJiangchi : public TriggerSkill
{
public:
	TenyearJiangchi() : TriggerSkill("tenyearjiangchi")
	{
		events << EventPhaseEnd;
		view_as_skill = new TenyearJiangchiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *caozhang, QVariant &) const
	{
		if (caozhang->getPhase() != Player::Draw) return false;
		room->askForUseCard(caozhang, "@@tenyearjiangchi", "@tenyearjiangchi");
		return false;
	}
};

class SecondTenyearJiangchi : public PhaseChangeSkill
{
public:
	SecondTenyearJiangchi() : PhaseChangeSkill("secondtenyearjiangchi")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play || !player->askForSkillInvoke(this)) return false;

		QString choices = "two+one";
		if (player->canDiscard(player, "he"))
			choices += "+discard";
		QString choice = room->askForChoice(player, objectName(), choices);

		if (choice == "two") {
			room->broadcastSkillInvoke(objectName(), 1);
			player->drawCards(2, objectName());
			room->addPlayerMark(player, "secondtenyearjiangchi_limit-Clear");
			room->setPlayerCardLimitation(player, "use,response", "Slash", true);
		} else if (choice == "one") {
			room->broadcastSkillInvoke(objectName(), 1);
			player->drawCards(1, objectName());
		} else {
			room->broadcastSkillInvoke(objectName(), 2);
			room->askForDiscard(player, objectName(), 1, 1, false, true, "", ".", objectName());
			room->addPlayerMark(player, "secondtenyearjiangchi_slash-PlayClear");
		}
		return false;
	}
};

class SecondTenyearJiangchiClear : public TriggerSkill
{
public:
	SecondTenyearJiangchiClear() : TriggerSkill("#secondtenyearjiangchi-clear")
	{
		events << EventPhaseChanging << Death;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who->getMark("secondtenyearjiangchi_limit-Clear") <= 0) return false;
			room->removePlayerCardLimitation(death.who, "use,response", "Slash$1");
		} else {
			if (data.value<PhaseChangeStruct>().from != Player::Play) return false;
			if (player->getMark("secondtenyearjiangchi_limit-Clear") <= 0) return false;
			room->setPlayerMark(player, "secondtenyearjiangchi_limit-Clear", 0);
			room->removePlayerCardLimitation(player, "use,response", "Slash$1");
		}
		return false;
	}
};

class SecondTenyearJiangchiMod : public TargetModSkill
{
public:
	SecondTenyearJiangchiMod() : TargetModSkill("#secondtenyearjiangchi-target")
	{
		frequency = NotFrequent;
	}

	int getResidueNum(const Player *from, const Card *, const Player *) const
	{
		if (from->getMark("secondtenyearjiangchi_slash-PlayClear") > 0)
			return 1;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *) const
	{
		if (from->getMark("secondtenyearjiangchi_slash-PlayClear") > 0)
			return 1000;
		return 0;
	}
};

TenyearWurongCard::TenyearWurongCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool TenyearWurongCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (targets.length() > 0 || to_select == Self)
		return false;
	return !to_select->isKongcheng();
}

void TenyearWurongCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();

	int index = qrand() % 2 + 1;
	if (effect.from->isJieGeneral("second_tenyear_zhangyi"))
		index += 2;
	room->broadcastSkillInvoke("tenyearwurong", index);

	const Card *c = room->askForExchange(effect.to, "tenyearwurong", 1, 1, false, "@tenyearwurong-show");

	room->showCard(effect.from, subcards.first());
	room->showCard(effect.to, c->getSubcards().first());

	const Card *card1 = Sanguosha->getCard(subcards.first());
	const Card *card2 = Sanguosha->getCard(c->getSubcards().first());

	if (card1->isKindOf("Slash") && !card2->isKindOf("Jink")) {
		room->damage(DamageStruct(objectName(), effect.from, effect.to));
	} else if (!card1->isKindOf("Slash") && card2->isKindOf("Jink")) {
		if (!effect.to->isNude()) {
			int id = room->askForCardChosen(effect.from, effect.to, "he", objectName());
			room->obtainCard(effect.from, id, false);
		}
	}
}

class TenyearWurong : public OneCardViewAsSkill
{
public:
	TenyearWurong() : OneCardViewAsSkill("tenyearwurong")
	{
		filter_pattern = ".|.|.|hand";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		TenyearWurongCard *fr = new TenyearWurongCard;
		fr->addSubcard(originalCard);
		return fr;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearWurongCard");
	}
};

class SecondTenyearShizhi : public FilterSkill
{
public:
	SecondTenyearShizhi() : FilterSkill("secondtenyearshizhi")
	{

	}

	bool viewFilter(const Card *to_select) const
	{
		if(to_select->isKindOf("Jink")){
			const Player *player = Sanguosha->getCardOwner(to_select->getId());
			return player && player->getHp() == 1;
		}
		return false;
	}

	const Card *viewAs(const Card *original) const
	{
		Slash *slash = new Slash(original->getSuit(), original->getNumber());
		slash->setSkillName(objectName());/*
		WrappedCard *card = Sanguosha->getWrappedCard(original->getId());
		card->takeOver(slash);*/
		return slash;
	}
};

class SecondTenyearShizhiTrigger : public TriggerSkill
{
public:
	SecondTenyearShizhiTrigger() : TriggerSkill("#secondtenyearshizhi")
	{
		events << HpChanged << MaxHpChanged << Revived << Damage;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card&&damage.card->isKindOf("Slash")&&damage.by_user
			&&damage.card->getSkillNames().contains("secondtenyearshizhi")&&player->isWounded()&&player->hasSkill("secondtenyearshizhi")){
				room->sendCompulsoryTriggerLog(player, "secondtenyearshizhi", true, true);
				room->recover(player, RecoverStruct("secondtenyearshizhi", player));
			}
		}else{
			if(player->getHp()==1){
				if(player->getMark("secondtenyearshizhi")<1){
					player->setMark("secondtenyearshizhi",1);
					room->filterCards(player, player->getHandcards(), false);
				}
			}else{
				if(player->getMark("secondtenyearshizhi")>0){
					player->setMark("secondtenyearshizhi",0);
					QList<const Card*>hs = player->getHandcards();
					foreach (const Card *c, hs) {
						if(c->getSkillName()!="secondtenyearshizhi")
							hs.removeOne(c);
					}
					room->filterCards(player, hs, true);
				}
			}
		}
		return false;
	}
};

class TenyearYaoming : public TriggerSkill
{
public:
	TenyearYaoming() : TriggerSkill("tenyearyaoming")
	{
		events << Damage << Damaged;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (player->getMark("tenyearyaoming_dayu-Clear") <= 0 && p->getHandcardNum() > player->getHandcardNum() && player->canDiscard(p, "h"))
				targets << p;
			if (player->getMark("tenyearyaoming_xiaoyu-Clear") <= 0 && p->getHandcardNum() < player->getHandcardNum())
				targets << p;
			if (player->getMark("tenyearyaoming_dengyu-Clear") <= 0 && p->canDiscard(p, "he") && p->getHandcardNum() == player->getHandcardNum())
				targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@tenyearyaoming-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		if (target->getHandcardNum() > player->getHandcardNum()) {
			room->addPlayerMark(player, "tenyearyaoming_dayu-Clear");
			if (!player->canDiscard(target, "h")) return false;
			int card_id = room->askForCardChosen(player, target, "h", objectName(), false, Card::MethodDiscard);
			room->throwCard(Sanguosha->getCard(card_id), target, player);
		} else if (target->getHandcardNum() < player->getHandcardNum()) {
			room->addPlayerMark(player, "tenyearyaoming_xiaoyu-Clear");
			target->drawCards(1, objectName());
		} else {
			room->addPlayerMark(player, "tenyearyaoming_dengyu-Clear");
			const Card * card = room->askForDiscard(target, objectName(), 2, 1, true, true, "tenyearyaoming-discard");
			if (!card) return false;
			target->drawCards(card->getSubcards().length(), objectName());
		}
		return false;
	}
};

class TenyearDanshou : public TriggerSkill
{
public:
	TenyearDanshou() : TriggerSkill("tenyeardanshou")
	{
		events << TargetConfirmed << EventPhaseStart;
        global = true;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetConfirmed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.to.contains(player)&&use.card->getTypeId()>0){
				player->addMark("tenyeardanshou_target-Clear");
				if(player->getMark("tenyeardanshou-Clear")<1&&use.card->getTypeId()<3&&player->hasSkill(objectName())){
					int x = player->getMark("tenyeardanshou_target-Clear");
					if(player->askForSkillInvoke(this, QString("tenyeardanshou_invoke:%1").arg(x))){
						player->addMark("tenyeardanshou-Clear");
						room->broadcastSkillInvoke(objectName());
						player->drawCards(x, objectName());
					}
				}
			}
		} else {
			if (player->getPhase() != Player::Finish) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (player->isDead()||p->isDead()||!p->hasSkill(objectName())||p->getMark("tenyeardanshou-Clear")>0) continue;
				if (player->isKongcheng()) {
					if (!p->askForSkillInvoke(this, QString("tenyeardanshou_damage:%1").arg(player->objectName()))) continue;
					room->broadcastSkillInvoke(objectName());
					room->damage(DamageStruct(objectName(), p, player));
				} else {
					int candis = 0;
					foreach (const Card *c, p->getCards("he")) {
						if (p->canDiscard(p, c->getEffectiveId()))
							candis++;
					}
					int handnum = player->getHandcardNum();
					if (candis < handnum) continue;
					if (!room->askForDiscard(p, objectName(), handnum, handnum, true, true,
					QString("@tenyeardanshou-dis:%1::%2").arg(player->objectName()).arg(handnum), ".", objectName())) continue;
					room->broadcastSkillInvoke(objectName());
					room->damage(DamageStruct(objectName(), p, player));
				}
			}
		}
		return false;
	}
};

class TenyearZenhui : public TriggerSkill
{
public:
	TenyearZenhui() : TriggerSkill("tenyearzenhui")
	{
		events << TargetSpecifying << CardFinished;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (triggerEvent == CardFinished && (use.card->isKindOf("Slash") || use.card->isNDTrick())) {
			use.from->setFlags("-TenyearZenhuiUser_" + use.card->toString());
			return false;
		}
		if (!TriggerSkill::triggerable(player) || player->hasFlag(objectName()))
			return false;

		if (use.to.length() == 1 && (use.card->isKindOf("Slash") || use.card->isNDTrick())) {
			QList<ServerPlayer *> targets = room->getOtherPlayers(use.to.first());
			targets.removeOne(player);
			player->tag["tenyearzenhui"] = data;
			ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@tenyearzenhui-invoke:" + use.to.first()->objectName(), true, true);
			if (target) {
				room->broadcastSkillInvoke(objectName());

				bool canbeextra = true;
				if (room->isProhibited(player, target, use.card) || !use.card->targetFilter(QList<const Player *>(), target, player))
					canbeextra = false;
				if (target->isNude() && !canbeextra) return false;
				bool extra_target = true;
				if (!target->isNude()) {
					QString pattern = "..";
					QString prompt = "tenyearzenhui-give:" + player->objectName();
					if (!canbeextra) {
						pattern = "..!";
						prompt = "tenyearzenhui-mustgive:" + player->objectName();
					}
					const Card *card = room->askForCard(target, pattern, prompt, data, Card::MethodNone);
					if (!canbeextra && !card) {
						card = target->getCards("he").at(qrand() % target->getCards("he").length());
					}
					if (card) {
						extra_target = false;
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "tenyearzenhui", "");
						room->obtainCard(player, card, reason, false);

						if (target->isAlive()) {
							LogMessage log;
							log.type = "#BecomeUser";
							log.from = target;
							log.card_str = use.card->toString();
							room->sendLog(log);

							target->setFlags("TenyearZenhuiUser_" + use.card->toString()); // For AI
							use.from = target;
							data = QVariant::fromValue(use);
						}
					}
				}
				if (extra_target) {
					player->setFlags(objectName());
					LogMessage log;
					log.type = "#BecomeTarget";
					log.from = target;
					log.card_str = use.card->toString();
					room->sendLog(log);

					room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

					use.to.append(target);
					room->sortByActionOrder(use.to);
					data = QVariant::fromValue(use);
				}
			}
		}
		return false;
	}
};

class TenyearJiaojin : public TriggerSkill
{
public:
	TenyearJiaojin() : TriggerSkill("tenyearjiaojin")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.to.contains(player) || player->getMark("tenyearjiaojin-Clear") > 0) return false;
		if (!use.from || use.from->isDead() || use.from == player) return false;
		if (!use.card->isKindOf("Slash") && !use.card->isNDTrick()) return false;
		if (!room->askForCard(player, ".Equip", "@tenyearjiaojin:" + use.from->objectName() + "::" + use.card->objectName(), data, objectName())) return false;
		room->broadcastSkillInvoke(objectName());
		use.nullified_list << player->objectName();
		data = QVariant::fromValue(use);
		//if (room->getCardPlace(use.card->getEffectiveId() != Player::PlaceTable)) return false;
		if (!room->CardInPlace(use.card, Player::PlaceTable)) return false;
		room->obtainCard(player, use.card);
		if (use.from->isFemale() && room->hasCurrent())
			room->addPlayerMark(player, "tenyearjiaojin-Clear");
		return false;
	}
};

class TenyearBenxiVS : public ZeroCardViewAsSkill
{
public:
	TenyearBenxiVS() : ZeroCardViewAsSkill("tenyearbenxi")
	{
		response_pattern = "@@tenyearbenxi!";
	}

	const Card *viewAs() const
	{
		return new ExtraCollateralCard;
	}
};

class TenyearBenxi : public TriggerSkill
{
public:
	TenyearBenxi() : TriggerSkill("tenyearbenxi")
	{
		events << CardUsed << DamageCaused << PreCardUsed;
		view_as_skill = new TenyearBenxiVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!player->hasFlag("CurrentPlayer") || use.card->isKindOf("SkillCard")) return false;
			//room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			room->addDistance(player, -1);
			room->addPlayerMark(player, "&tenyearbenxi-Clear");
		}else if (event == PreCardUsed) {
			if (!player->hasFlag("CurrentPlayer")) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") && !use.card->isNDTrick()) return false;
			if (use.to.length() != 1) return false;
			bool allone = true;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->distanceTo(p) != 1) {
					allone = false;
					break;
				}
			}
			if (!allone) return false;
			QStringList choices, excepts;
			QList<ServerPlayer *> available_targets;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (use.to.contains(p)) continue;
				if (player->canUse(use.card,p))
					available_targets << p;
			}
			if (!available_targets.isEmpty()) choices << "extra";
			choices << "ignore" << "noresponse" << "draw" <<"cancel";
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);

			for (int i = 0; i < 2; i++) {
				if (choices.isEmpty()) break;
				QString choice = room->askForChoice(player, objectName(), choices.join("+"), data, excepts.join("+"));
				if (choice == "cancel") break;
				choices.removeOne(choice);
				excepts << choice;
				LogMessage log;
				log.type = "#FumianFirstChoice";
				log.from = player;
				log.arg = "tenyearbenxi:" + choice;
				room->sendLog(log);
				if (choice == "extra") {
					ServerPlayer *target;
					if (use.card->isKindOf("Collateral")){
						QStringList tos;
						tos << use.card->toString();
						foreach(ServerPlayer *t, use.to)
							tos << t->objectName();
						tos << objectName();
						room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
						room->askForUseCard(player, "@@tenyearbenxi!", "@tenyearbenxi-extra:" + use.card->objectName());
						target = player->tag["ExtraCollateralTarget"].value<ServerPlayer *>();
						player->tag.remove("ExtraCollateralTarget");
						if (!target) {
							QList<ServerPlayer *> victims;
							target = available_targets.at(qrand() % available_targets.length() - 1);
							foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
								if (target->canSlash(p))
									victims << p;
							}
							target->tag["attachTarget"] = QVariant::fromValue((victims.at(qrand() % victims.length() - 1)));
							log.type = "#QiaoshuiAdd";
							log.to << target;
							log.card_str = use.card->toString();
							log.arg = "tenyearbenxi";
							room->sendLog(log);
						}
					}else{
						target = room->askForPlayerChosen(player, available_targets, objectName(), "@tenyearbenxi-extra:" + use.card->objectName());
						log.type = "#QiaoshuiAdd";
						log.to << target;
						log.card_str = use.card->toString();
						log.arg = "tenyearbenxi";
						room->sendLog(log);
					}
					use.to.append(target);
					room->sortByActionOrder(use.to);
					if (use.card->hasFlag("tenyearbenxi_ignore"))
						target->addQinggangTag(use.card);
					data = QVariant::fromValue(use);
				} else if (choice == "ignore") {
					room->setCardFlag(use.card, "tenyearbenxi_ignore");
					foreach (ServerPlayer *p, use.to)
						p->addQinggangTag(use.card);
				} else if (choice == "noresponse") {
					use.no_offset_list << "_ALL_TARGETS";
					data = QVariant::fromValue(use);
				} else
					room->setCardFlag(use.card, "tenyearbenxi_damage");
			}
		} else if (event == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->hasFlag("tenyearbenxi_damage")) return false;
			player->drawCards(1, objectName());
		}
		return false;
	}
};

class TenyearPojun : public TriggerSkill
{
public:
	TenyearPojun() : TriggerSkill("tenyearpojun")
	{
		events << TargetSpecified << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card != nullptr && use.card->isKindOf("Slash") && TriggerSkill::triggerable(player)) {
				foreach (ServerPlayer *t, use.to) {
					if (player->isDead()) return false;
					if (t->isDead()) continue;
					int n = qMin(t->getCards("he").length(), t->getHp());
					if (n > 0 && player->askForSkillInvoke(this, QVariant::fromValue(t))) {
						room->broadcastSkillInvoke(objectName());
						DummyCard *dummy = new DummyCard;
						for (int i = 0; i < n; ++i) {
							int id = room->askForCardChosen(player, t, "he", objectName() + "_dis", false, Card::MethodNone, dummy->getSubcards(), i>0);
							if (id<0) break;
							dummy->addSubcard(id);
						}

						t->addToPile("tenyearpojun", dummy, false);
						dummy->deleteLater();

						QList<int> equips;
						bool has_trick = false;
						foreach (int id, dummy->getSubcards()) {
							if (Sanguosha->getCard(id)->isKindOf("EquipCard"))
								equips << id;
							else if (Sanguosha->getCard(id)->isKindOf("TrickCard"))
								has_trick = true;
						}

						if (!equips.isEmpty()) {
							room->fillAG(equips, player);
							int id = room->askForAG(player, equips, false, objectName());
							room->clearAG(player);
							room->throwCard(id, t, player);
						}

						if (has_trick)
							player->drawCards(1, objectName());
					}
				}
			}
		} else if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				QList<int> to_obtain = p->getPile("tenyearpojun");
				if (!to_obtain.isEmpty()) {
					DummyCard dummy(to_obtain);
					room->obtainCard(p, &dummy, false);
				}
			}
		}
		return false;
	}
};

TenyearYanzhuCard::TenyearYanzhuCard()
{
}

void TenyearYanzhuCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isDead()) return;
	if (effect.from->isDead() && !effect.to->canDiscard(effect.to, "he")) return;
	Room *room = effect.from->getRoom();

	if (effect.from->property("tenyearyanzhu_level_up").toBool()) {
		room->addPlayerMark(effect.to, "&tenyearyanzhu");
		return;
	}

	bool optional = effect.from->isAlive() ? true : false;
	optional = !effect.to->getEquips().isEmpty() ? true : false;
	QString prompt = optional ? "@tenyearyanzhu-discard:" + effect.from->objectName() : "@tenyearyanzhu-discard2";
	if (!room->askForDiscard(effect.to, "tenyearyanzhu", 1, 1, optional, true, prompt)) {
		QList<int> list = effect.to->getEquipsId();
		DummyCard *dummy = new DummyCard(list);
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
		room->obtainCard(effect.from, dummy, reason);
		delete dummy;
		room->setPlayerProperty(effect.from, "tenyearyanzhu_level_up", true);
		room->setPlayerProperty(effect.from, "tenyearxingxue_level_up", true);
		LogMessage log;
		log.type = "#JiexunChange";
		log.from = effect.from;
		if (effect.from->hasSkill("tenyearyanzhu"), true) {
			log.arg = "tenyearyanzhu";
			room->sendLog(log);
		}
		if (effect.from->hasSkill("tenyearxingxue"), true) {
			log.arg = "tenyearxingxue";
			room->sendLog(log);
		}
		QString translate = Sanguosha->translate(":tenyearyanzhu2");
		room->changeTranslation(effect.from, "tenyearyanzhu", translate);
		QString translate2 = Sanguosha->translate(":tenyearxingxue2");
		room->changeTranslation(effect.from, "tenyearxingxue", translate2);
	} else
		room->addPlayerMark(effect.to, "&tenyearyanzhu");
}

class TenyearYanzhuVS : public ZeroCardViewAsSkill
{
public:
	TenyearYanzhuVS() : ZeroCardViewAsSkill("tenyearyanzhu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearYanzhuCard");
	}

	const Card *viewAs() const
	{
		return new TenyearYanzhuCard;
	}
};

class TenyearYanzhu : public TriggerSkill
{
public:
	TenyearYanzhu() : TriggerSkill("tenyearyanzhu")
	{
		events << EventPhaseChanging << DamageInflicted;
		view_as_skill = new TenyearYanzhuVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->getMark("&tenyearyanzhu") > 0;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::RoundStart) return false;
			room->setPlayerMark(player, "&tenyearyanzhu", 0);
		} else {
			LogMessage log;
			log.type = "#TenyearyanzhuDamage";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(player->getMark("&tenyearyanzhu"));
			room->sendLog(log);

			DamageStruct damage = data.value<DamageStruct>();
			damage.damage += player->getMark("&tenyearyanzhu");
			data = QVariant::fromValue(damage);
			room->setPlayerMark(player, "&tenyearyanzhu", 0);
		}
		return false;
	}
};

TenyearXingxueCard::TenyearXingxueCard()
{
}

bool TenyearXingxueCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
	int n = Self->property("tenyearxingxue_level_up").toBool() ? Self->getMaxHp() : Self->getHp();

	return targets.length() < n;
}

void TenyearXingxueCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
	room->drawCards(targets, 1, "tenyearxingxue");

	foreach (ServerPlayer *p, targets) {
		if (p->isDead() || p->getHandcardNum() <= p->getHp()) continue;
		const Card *c = room->askForExchange(p, "tenyearxingxue", 1, 1, true, "@tenyearxingxue-put");
		int id = c->getSubcards().first();
		CardsMoveStruct m(id, nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, p->objectName()));
		room->setPlayerFlag(p, "Global_GongxinOperator");
		room->moveCardsAtomic(m, false);
		room->setPlayerFlag(p, "-Global_GongxinOperator");
	}
}

class TenyearXingxueVS : public ZeroCardViewAsSkill
{
public:
	TenyearXingxueVS() : ZeroCardViewAsSkill("tenyearxingxue")
	{
		response_pattern = "@@tenyearxingxue";
	}

	const Card *viewAs() const
	{
		return new TenyearXingxueCard;
	}
};

class TenyearXingxue : public PhaseChangeSkill
{
public:
	TenyearXingxue() : PhaseChangeSkill("tenyearxingxue")
	{
		view_as_skill = new TenyearXingxueVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return PhaseChangeSkill::triggerable(target) && target->getPhase() == Player::Finish;
	}

	bool onPhaseChange(ServerPlayer *target, Room *room) const
	{
		int n = target->property("tenyearxingxue_level_up").toBool() ? target->getMaxHp() : target->getHp();
		if (n <= 0) return false;
		room->askForUseCard(target, "@@tenyearxingxue", "@tenyearxingxue:" + QString::number(n));
		return false;
	}
};

class TenyearQingxi : public TriggerSkill
{
public:
	TenyearQingxi() : TriggerSkill("tenyearqingxi")
	{
		events << TargetSpecified;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash") && !use.card->isKindOf("Duel")) return false;
		foreach (ServerPlayer *p, use.to) {
			if (player->isDead()) return false;
			if (p->isDead()) continue;
			if (!player->askForSkillInvoke(this, QVariant::fromValue(p))) continue;
			room->broadcastSkillInvoke(objectName());
			if (p->isDead()) continue;
			int n = 0;
			foreach (ServerPlayer *d, room->getOtherPlayers(player)) {
				if (player->inMyAttackRange(d))
					n++;
			}
			int min = player->getWeapon() ? 4 : 2;
			n = qMin(n, min);

			if (n <= 0 || player->isDead()) {
				room->setCardFlag(use.card, "tenyearqingxi_" + p->objectName());
				JudgeStruct judge;
				judge.who = p;
				judge.reason = objectName();
				judge.pattern = ".|red";
				judge.good = false;
				room->judge(judge);

				if (judge.isBad())
					use.no_respond_list << p->objectName();
			} else {
				if (!room->askForDiscard(p, objectName(), n, n, true, false, "@tenyearqingxi-discard:" + QString::number(n))) {
					room->setCardFlag(use.card, "tenyearqingxi_" + p->objectName());
					JudgeStruct judge;
					judge.who = p;
					judge.reason = objectName();
					judge.pattern = ".|red";
					judge.good = false;
					room->judge(judge);

					if (judge.isBad())
						use.no_respond_list << p->objectName();
				} else {
					if (p->isDead() || !player->getWeapon() || !p->canDiscard(player, player->getWeapon()->getEffectiveId())) continue;
					room->throwCard(player->getWeapon(), player, p);
				}
			}
		}
		data = QVariant::fromValue(use);
		return false;
	}
};

class TenyearQingxiDamage : public TriggerSkill
{
public:
	TenyearQingxiDamage() : TriggerSkill("#tenyearqingxi-damage")
	{
		events << DamageInflicted;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card) return false;
		if (!damage.card->hasFlag("tenyearqingxi_" + player->objectName())) return false;
		//room->setCardFlag(damage.card, "-tenyearqingxi_" + player->objectName());
		++damage.damage;
		data = QVariant::fromValue(damage);
		return false;
	}
};

class TenyearDuodao : public TriggerSkill
{
public:
	TenyearDuodao() : TriggerSkill("tenyearduodao")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash") || !use.to.contains(player) || !player->canDiscard(player, "he")) return false;
		if (!room->askForCard(player, "..", "@tenyearduodao:" + use.from->objectName(), data, objectName())) return false;
		room->broadcastSkillInvoke(objectName());
		if (!use.from || use.from->isDead() || !use.from->getWeapon()) return false;
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
		room->obtainCard(player, use.from->getWeapon(), reason);
		return false;
	}
};

class TenyearAnjian : public TriggerSkill
{
public:
	TenyearAnjian() : TriggerSkill("tenyearanjian")
	{
		events << TargetSpecified << CardFinished << DamageCaused;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash")) return false;
			//damage.damage += damage.card->tag["Tenyearanjian_Damage"].toInt();
			damage.damage += room->getTag("Tenyearanjian_Damage" + damage.card->toString()).toInt();
			data = QVariant::fromValue(damage);
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash")) return false;
			if (event == TargetSpecified) {
				foreach (ServerPlayer *p, use.to) {
					if (p->inMyAttackRange(player)) continue;
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
					p->addQinggangTag(use.card);
					room->setCardFlag(use.card, "tenyearanjian_" + p->objectName());
					//int damage = use.card->tag["Tenyearanjian_Damage"].toInt();
					//use.card->tag["Tenyearanjian_Damage"] = ++damage;
					int damage = room->getTag("Tenyearanjian_Damage" + use.card->toString()).toInt();
					room->setTag("Tenyearanjian_Damage" + use.card->toString(), ++damage);
				}
			} else
				room->removeTag("Tenyearanjian_Damage" + use.card->toString());
		}
		return false;
	}
};

class TenyearAnjianEffect : public TriggerSkill
{
public:
	TenyearAnjianEffect() : TriggerSkill("#tenyearanjian-effect")
	{
		events << Dying << AskForPeachesDone;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Dying) {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.who != player) return false;
			if (!dying.damage || !dying.damage->card || !dying.damage->card->isKindOf("Slash")) return false;
			if (!dying.damage->card->hasFlag("tenyearanjian_" + player->objectName())) return false;
			player->tag["TenyearanjianForbidden"] = true;
			room->setPlayerCardLimitation(player, "use", "Peach", false);
		} else {
			if (!player->tag["TenyearanjianForbidden"].toBool()) return false;
			player->tag.remove("TenyearanjianForbidden");
			room->removePlayerCardLimitation(player, "use", "Peach$0");
		}
		return false;
	}
};

TenyearShenduanCard::TenyearShenduanCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool TenyearShenduanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	const Card *cc = Sanguosha->getCard(getSubcards().first());
	SupplyShortage *ss = new SupplyShortage(cc->getSuit(), cc->getNumber());
	ss->addSubcard(cc);
	ss->setSkillName("tenyearshenduan");
	ss->deleteLater();
	return ss->targetFilter(targets, to_select, Self) && !Self->isCardLimited(ss, Card::MethodUse);
}

void TenyearShenduanCard::onUse(Room *room, CardUseStruct &card_use) const
{
	room->setPlayerFlag(card_use.to.first(), "tenyearshenduan_target");
}

class TenyearShenduanVS : public OneCardViewAsSkill
{
public:
	TenyearShenduanVS() : OneCardViewAsSkill("tenyearshenduan")
	{
		response_pattern = "@@tenyearshenduan";
		expand_pile = "#tenyearshenduan";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return Self->getPile("#tenyearshenduan").contains(to_select->getEffectiveId());
	}

	const Card *viewAs(const Card *card) const
	{
		//TenyearShenduanCard *c = new TenyearShenduanCard;
		//c->addSubcard(card);
		SupplyShortage *c = new SupplyShortage(card->getSuit(), card->getNumber());
		c->setSkillName("tenyearshenduan");
		c->addSubcard(card);
		return c;
	}
};

class TenyearShenduan : public TriggerSkill
{
public:
	TenyearShenduan() : TriggerSkill("tenyearshenduan")
	{
		events << CardsMoveOneTime;
		view_as_skill = new TenyearShenduanVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (Sanguosha->getBanPackages().contains("maneuvering")) return false;

		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place == Player::DiscardPile&&move.from == player
			&& ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {
			QList<int> shenduan_card;
			for (int i = 0; i < move.card_ids.length(); i++) {
				if (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip) {
					if (room->getCardPlace(move.card_ids.at(i)) == Player::DiscardPile) {
						const Card *c = Sanguosha->getCard(move.card_ids.at(i));
						if (c->isBlack() && (c->isKindOf("BasicCard") || c->isKindOf("EquipCard")))
							shenduan_card << move.card_ids.at(i);
					}
				}
			}
			while (!shenduan_card.isEmpty()&&player->isAlive()) {
				room->notifyMoveToPile(player, shenduan_card, objectName(), Player::DiscardPile, true);
				const Card *c = room->askForUseCard(player, "@@tenyearshenduan", "@tenyearshenduan");
				if (!c) break;
				shenduan_card.removeOne(c->getEffectiveId());
				foreach (int id, shenduan_card) {
					if(room->getCardOwner(id))
						shenduan_card.removeOne(id);
				}
			}
		}
		return false;
	}
};

class TenyearShenduanTargetMod : public TargetModSkill
{
public:
	TenyearShenduanTargetMod() : TargetModSkill("#tenyearshenduan-target")
	{
		frequency = NotFrequent;
		pattern = "SupplyShortage";
	}

	int getDistanceLimit(const Player *, const Card *card, const Player *) const
	{
		if (card->getSkillName() == "tenyearshenduan")
			return 1000;
		return 0;
	}
};

class TenyearYonglve : public PhaseChangeSkill
{
public:
	TenyearYonglve() : PhaseChangeSkill("tenyearyonglve")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && !target->getJudgingArea().isEmpty() && target->getPhase() == Player::Judge;
	}

	bool onPhaseChange(ServerPlayer *target, Room *room) const
	{
		foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
			if (target->isDead() || target->getJudgingArea().isEmpty()) return false;
			if (p->isDead() || !p->hasSkill(objectName()) || !p->canDiscard(target, "j")) continue;
			QString prompt = "tenyearyonglve_out:" + target->objectName();
			if (p->inMyAttackRange(target))
				prompt = "tenyearyonglve_in:" + target->objectName();
			if (!p->askForSkillInvoke(this, prompt)) continue;
			room->broadcastSkillInvoke(objectName());
			int id = room->askForCardChosen(p, target, "j", objectName(), false, Card::MethodDiscard);
			room->throwCard(id, nullptr, p);
			if (p->isDead()) continue;
			if (p->inMyAttackRange(target))
				p->drawCards(1, objectName());
			else {
				Slash *slash = new Slash(Card::NoSuit, 0);
				slash->setSkillName("_tenyearyonglve");
				slash->deleteLater();
				if (!p->canSlash(target, slash, false)) continue;
				room->useCard(CardUseStruct(slash, p, target));
			}
		}
		return false;
	}
};

TenyearQiaoshuiCard::TenyearQiaoshuiCard()
{
}

bool TenyearQiaoshuiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self->canPindian(to_select);
}

void TenyearQiaoshuiCard::onEffect(CardEffectStruct &effect) const
{
	if (!effect.from->canPindian(effect.to, false)) return;
	Room *room = effect.from->getRoom();
	if (effect.from->pindian(effect.to, "tenyearqiaoshui"))
		room->addPlayerMark(effect.from, "&tenyearqiaoshui-Clear");
	else {
		effect.from->endPlayPhase();
		room->addPlayerMark(effect.from, "tenyearqiaoshui_lose-Clear");
	}
}

TenyearQiaoshuiTargetCard::TenyearQiaoshuiTargetCard()
{
	mute = true;
}

bool TenyearQiaoshuiTargetCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < Self->getMark("tenyearqiaoshui_max_target-Clear") && to_select->hasFlag("tenyearqiaoshui_canchoose");
}

void TenyearQiaoshuiTargetCard::onUse(Room *room, CardUseStruct &card_use) const
{
	room->setPlayerMark(card_use.from, "tenyearqiaoshui_max_target-Clear", 0);
	foreach (ServerPlayer *p, card_use.to)
		room->setPlayerFlag(p, "tenyearqiaoshui_choose_target");
}

class TenyearQiaoshuiVS : public ZeroCardViewAsSkill
{
public:
	TenyearQiaoshuiVS() : ZeroCardViewAsSkill("tenyearqiaoshui")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->canPindian();
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@tenyearqiaoshui");
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern=="@@tenyearqiaoshui2")
			return new ExtraCollateralCard;
		else {
			if (pattern.startsWith("@@tenyearqiaoshui"))
				return new TenyearQiaoshuiTargetCard;
			return new TenyearQiaoshuiCard;
		}
	}
};

class TenyearQiaoshui : public TriggerSkill
{
public:
	TenyearQiaoshui() : TriggerSkill("tenyearqiaoshui")
	{
		events << PreCardUsed << EventPhaseProceeding;
		view_as_skill = new TenyearQiaoshuiVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseProceeding) {
			if (player->getPhase() != Player::Discard) return false;
			if (player->getMark("tenyearqiaoshui_lose-Clear") <= 0) return false;
			QList<int> ids;
			foreach (const Card *c, player->getCards("h")) {
				if (!c->isKindOf("TrickCard")) continue;
				ids << c->getEffectiveId();
			}
			if (ids.isEmpty()) return false;
			room->ignoreCards(player, ids);
			return false;
		}
		const Card *card = nullptr;
		if (event == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			card = use.card;
		}
		if (card == nullptr || (!card->isKindOf("BasicCard") && !card->isNDTrick())) return false;

		int n = player->getMark("&tenyearqiaoshui-Clear");
		if (n <= 0) return false;
		room->setPlayerMark(player, "&tenyearqiaoshui-Clear", 0);
		room->setPlayerMark(player, "tenyearqiaoshui_max_target-Clear", n);

		CardUseStruct use = data.value<CardUseStruct>();
		if (use.to.isEmpty()) return false;
		room->setCardFlag(card, "tenyearqiaoshui_distance");

		bool canextra = false;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (use.card->isKindOf("AOE") && p == player) continue;
			if (use.to.contains(p) || room->isProhibited(player, p, use.card)) continue;
			if (use.card->targetFixed()) {
				if (!use.card->isKindOf("Peach") || p->getLostHp() > 0) {
					canextra = true;
					break;
				}
			} else {
				if (use.card->targetFilter(QList<const Player *>(), p, player)) {
					canextra = true;
					break;
				}
			}
		}
		room->setCardFlag(use.card, "-tenyearqiaoshui_distance");

		QStringList choices;
		if (canextra)
			choices << "add";
		if (use.to.length() > 1)
			choices << "remove";
		if (choices.isEmpty()) return false;
		choices << "cancel";
		QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		if (choice == "cancel") return false;

		if (choice == "add") {
			if (card->isKindOf("Collateral")) {
				for (int i = 1; i <= n; i++) {
					bool canextra = false;
					foreach (ServerPlayer *p, room->getAlivePlayers()) {
						if (use.to.contains(p)) continue;
						if (player->canUse(use.card,p)) {
							canextra = true;
							break;
						}
					}
					if (!canextra)
						break;
					QStringList tos;
					tos << use.card->toString();
					foreach (ServerPlayer *t, use.to)
						tos << t->objectName();
					tos << objectName();
					room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
					if (!room->askForUseCard(player, "@@tenyearqiaoshui2", "@tenyearqiaoshui1:" + use.card->objectName(), 1)) break;
					ServerPlayer *p = player->tag["ExtraCollateralTarget"].value<ServerPlayer *>();
					player->tag.remove("ExtraCollateralTarget");
					if (p) use.to.append(p);
				}
			} else {
				room->setCardFlag(use.card, "tenyearqiaoshui_distance");
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (use.to.contains(p)) continue;
					if (player->canUse(use.card,p))
						room->setPlayerFlag(p, "tenyearqiaoshui_canchoose");
				}
				room->setCardFlag(use.card, "-tenyearqiaoshui_distance");
				if (!room->askForUseCard(player, "@@tenyearqiaoshui1", "@tenyearqiaoshui1:" + use.card->objectName(), 1)) return false;
				LogMessage log;
				foreach(ServerPlayer *p, room->getAlivePlayers()) {
					room->setPlayerFlag(p, "-tenyearqiaoshui_canchoose");
					if (p->hasFlag("tenyearqiaoshui_choose_target")) {
						room->setPlayerFlag(p,"-tenyearqiaoshui_choose_target");
						log.to << p;
					}
				}
				if (log.to.isEmpty()) return false;
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.card_str = use.card->toString();
				log.arg = "tenyearqiaoshui";
				room->sendLog(log);
				use.to << log.to;
			}
		} else {
			foreach (ServerPlayer *p, use.to)
				room->setPlayerFlag(p, "tenyearqiaoshui_canchoose");
			if (!room->askForUseCard(player, "@@tenyearqiaoshui2", "@tenyearqiaoshui2:" + use.card->objectName(), 2)) return false;
			LogMessage log;
			foreach (ServerPlayer *p, use.to) {
				room->setPlayerFlag(p, "-tenyearqiaoshui_canchoose");
				if (p->hasFlag("tenyearqiaoshui_choose_target")) {
					room->setPlayerFlag(p, "-tenyearqiaoshui_choose_target");
					log.to << p;
					use.to.removeOne(p);
				}
			}
			if (log.to.isEmpty()) return false;
			log.type = "#QiaoshuiRemove";
			log.from = player;
			log.card_str = use.card->toString();
			log.arg = "tenyearqiaoshui";
			room->sendLog(log);
		}
		room->sortByActionOrder(use.to);
		data = QVariant::fromValue(use);
		return false;
	}
};

class TenyearQiaoshuiTargetMod : public TargetModSkill
{
public:
	TenyearQiaoshuiTargetMod() : TargetModSkill("#tenyearqiaoshui-target")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getDistanceLimit(const Player *, const Card *card, const Player *) const
	{
		if (card->hasFlag("tenyearqiaoshui_distance"))
			return 1000;
		return 0;
	}
};

TenyearXianzhenCard::TenyearXianzhenCard()
{
	m_skillName = "tenyearxianzhen";
}

bool TenyearXianzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self->canPindian(to_select);
}

void TenyearXianzhenCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->addPlayerMark(effect.from, m_skillName + "_Used-Clear");

	if (effect.from->pindian(effect.to, m_skillName)) {
		room->addPlayerMark(effect.to, "Armor_Nullified");
		room->addPlayerMark(effect.from, m_skillName + "_from-Clear");
		room->addPlayerMark(effect.to, m_skillName + "_to-Clear");
	} else {
		room->setPlayerCardLimitation(effect.from, "use", "Slash", true);
		room->addPlayerMark(effect.from, m_skillName + "_slash-Clear");
	}
}

SecondTenyearXianzhenCard::SecondTenyearXianzhenCard() : TenyearXianzhenCard()
{
	m_skillName = "secondtenyearxianzhen";
}

class TenyearXianzhenViewAsSkill : public ZeroCardViewAsSkill
{
public:
	TenyearXianzhenViewAsSkill(const QString &xianzhen) : ZeroCardViewAsSkill(xianzhen), xianzhen(xianzhen)
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (xianzhen == "tenyearxianzhen")
			return !player->hasUsed("TenyearXianzhenCard") && player->canPindian();
		else if (xianzhen == "secondtenyearxianzhen")
			return player->getMark("secondtenyearxianzhen_Used-Clear") <= 0 && player->canPindian();
		return false;
	}

	const Card *viewAs() const
	{
		if (xianzhen == "tenyearxianzhen")
			return new TenyearXianzhenCard;
		else if (xianzhen == "secondtenyearxianzhen")
			return new SecondTenyearXianzhenCard;
		return nullptr;
	}
private:
	QString xianzhen;
};

class TenyearXianzhen : public TriggerSkill
{
public:
	TenyearXianzhen(const QString &xianzhen) : TriggerSkill(xianzhen), xianzhen(xianzhen)
	{
		events << EventPhaseChanging << Death << CardUsed << DamageCaused;
		view_as_skill = new TenyearXianzhenViewAsSkill(xianzhen);
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getMark(xianzhen + "_from-Clear") > 0;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *gaoshun, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers(true)) {
				if (p->getMark(xianzhen + "_to-Clear") <= 0) continue;
				room->removePlayerMark(p, "Armor_Nullified");
			}
		} else if (triggerEvent == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who->getMark(xianzhen + "_to-Clear") <= 0) return false;
			room->removePlayerMark(death.who, "Armor_Nullified");
		} else if (triggerEvent == DamageCaused) {
			if (gaoshun->isDead() || xianzhen != "secondtenyearxianzhen") return false;
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || damage.to->isDead() || damage.to->getMark("secondtenyearxianzhen_to-Clear") <= 0) return false;
			QString name = damage.card->objectName();
			if (damage.card->isKindOf("Slash"))
				name = "slash";
			if (gaoshun->getMark("secondtenyearxianzhen_" + name + "_" + damage.to->objectName() + "-Clear") > 0) return false;
			LogMessage log;
			log.type = "#YHHankaiDamage";
			log.from = gaoshun;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(damage.damage);
			log.arg3 = QString::number(damage.damage += 1);
			room->sendLog(log);
			room->notifySkillInvoked(gaoshun, objectName());
			data = QVariant::fromValue(damage);
			room->addPlayerMark(gaoshun, "secondtenyearxianzhen_" + name + "_" + damage.to->objectName() + "-Clear");
		} else {
			if (gaoshun->isDead() || xianzhen != "tenyearxianzhen") return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.to.length() != 1 || use.card->isKindOf("Collateral")) return false;
			if (use.card->isKindOf("Slash") || use.card->isNDTrick()) {
				QList<ServerPlayer *> targets;
				foreach (ServerPlayer *target, room->getAlivePlayers()) {
					if (target->getMark("tenyearxianzhen_to-Clear") <= 0) continue;
					if (!use.card->isKindOf("AOE") && !use.card->isKindOf("GlobalEffect")) {
						if (use.to.contains(target) || room->isProhibited(gaoshun, target, use.card)) continue;
						if (use.card->targetFixed()) {
							if (!use.card->isKindOf("Peach") || target->isWounded())
								targets << target;
						} else {
							if (use.card->targetFilter(QList<const Player *>(), target, gaoshun))
								targets << target;
						}
					}
				}
				while (!targets.isEmpty()&&gaoshun->isAlive()) {
					ServerPlayer *target = room->askForPlayerChosen(gaoshun, targets, objectName(),
									"@tenyearxianzhen-target:" + use.card->objectName(), true);
					if (!target) break;
					LogMessage log;
					log.type = "#QiaoshuiAdd";
					log.from = gaoshun;
					log.to << target;
					log.card_str = use.card->toString();
					log.arg = objectName();
					room->sendLog(log);
					room->doAnimate(1, gaoshun->objectName(), target->objectName());
					use.to << target;
					targets.removeOne(target);
					room->sortByActionOrder(use.to);
					data = QVariant::fromValue(use);
				}
			}
		}
		return false;
	}
private:
	QString xianzhen;
};

class TenyearXianzhenSlash : public TriggerSkill
{
public:
	TenyearXianzhenSlash(const QString &xianzhen) : TriggerSkill("#" + xianzhen + "-slash"), xianzhen(xianzhen)
	{
		events << EventPhaseProceeding;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getMark(xianzhen + "_slash-Clear") > 0;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *gaoshun, QVariant &) const
	{
		if (gaoshun->getPhase() != Player::Discard) return false;
		foreach (const Card *card, gaoshun->getHandcards()) {
			if (card->isKindOf("Slash"))
				room->ignoreCards(gaoshun, card);
		}
		return false;
	}
private:
	QString xianzhen;
};

class TenyearXianzhenTargetMod : public TargetModSkill
{
public:
	TenyearXianzhenTargetMod(const QString &xianzhen) : TargetModSkill("#" + xianzhen + "-target"), xianzhen(xianzhen)
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getResidueNum(const Player *from, const Card *, const Player *to) const
	{
		if (from->getMark(xianzhen + "_from-Clear") > 0 && to && to->getMark(xianzhen + "_to-Clear") > 0)
			return 1000;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *to) const
	{
		if (from->getMark(xianzhen + "_from-Clear") > 0 && to && to->getMark(xianzhen + "_to-Clear") > 0)
			return 1000;
		return 0;
	}
private:
	QString xianzhen;
};

class SecondTenyearJinjiu : public FilterSkill
{
public:
	SecondTenyearJinjiu() : FilterSkill("secondtenyearjinjiu")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->objectName() == "analeptic";
	}

	const Card *viewAs(const Card *original) const
	{
		Slash *slash = new Slash(original->getSuit(), 13);
		slash->setSkillName(objectName());/*
		WrappedCard *card = Sanguosha->getWrappedCard(original->getId());
		card->takeOver(slash);*/
		return slash;
	}
};

class SecondTenyearJinjiuLimit : public CardLimitSkill
{
public:
	SecondTenyearJinjiuLimit() : CardLimitSkill("#secondtenyearjinjiu-limit")
	{
	}

	bool gaoshun(const Player *target) const
	{
		foreach (const Player *p, target->getAliveSiblings()) {
			if (p->hasFlag("CurrentPlayer") && p->hasSkill("secondtenyearjinjiu"))
				return true;
		}
		return false;
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (gaoshun(target))
			return "Analeptic";
		return "";
	}
};

TenyearZishouCard::TenyearZishouCard()
{
	target_fixed = true;
}

void TenyearZishouCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->drawCards(subcardsLength(), "tenyearzishou");
}

class TenyearZishouVS : public ViewAsSkill
{
public:
	TenyearZishouVS() : ViewAsSkill("tenyearzishou")
	{
		response_pattern = "@@tenyearzishou";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (Self->isJilei(to_select) || to_select->isEquipped()) return false;
		foreach (const Card *c, selected) {
			if (to_select->getSuit() == c->getSuit())
				return false;
		}
		return true;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;

		TenyearZishouCard *c = new TenyearZishouCard;
		c->addSubcards(cards);
		return c;
	}
};

class TenyearZishou : public TriggerSkill
{
public:
	TenyearZishou() : TriggerSkill("tenyearzishou")
	{
		events << DrawNCards << DamageCaused << EventPhaseStart << TargetSpecified;
		view_as_skill = new TenyearZishouVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DrawNCards) {
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase"||!player->hasSkill(objectName())) return false;
			QSet<QString> kingdomSet;
			foreach(ServerPlayer *p, room->getAlivePlayers())
				kingdomSet.insert(p->getKingdom());

			int n = kingdomSet.count();
			if (!player->askForSkillInvoke(this, QString("tenyearzishou:" + QString::number(n)))) return false;
			room->broadcastSkillInvoke(objectName());
			room->setPlayerFlag(player, "tenyearzishou");
			draw.num += n;
			data = QVariant::fromValue(draw);
		} else if (event == DamageCaused) {
			if (!player->hasFlag(objectName())) return false;
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.to->isAlive() && damage.to != player) {
				LogMessage log;
				log.type = "#OLzishouPrevent";
				log.from = player;
				log.to << damage.to;
				log.arg = objectName();
				log.arg2 = QString::number(damage.damage);
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(player, objectName());
				return true;
			}
		} else if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId() != Card::TypeSkill) {
				foreach (ServerPlayer *p, use.to) {
					if (p != player)
						player->addMark("qieting-Clear");
				}
			}
		} else {
			if (!player->hasFlag(objectName()) || player->getPhase() != Player::Finish) return false;
			if (player->getMark("qieting-Clear") > 0 || !player->canDiscard(player, "h")) return false;
			room->askForUseCard(player, "@@tenyearzishou", "@tenyearzishou", -1, Card::MethodDiscard);
		}
		return false;
	}
};

class TenyearZongshi : public MaxCardsSkill
{
public:
	TenyearZongshi() : MaxCardsSkill("tenyearzongshi")
	{
	}

	int getExtra(const Player *target) const
	{
		if (target->hasSkill(objectName())){
			QSet<QString> kingdom_set;
			if (target->parent()) {
				foreach(const Player *player, target->parent()->findChildren<const Player *>()) {
					if (player->isAlive()) kingdom_set << player->getKingdom();
				}
			}
			return kingdom_set.size();
		}
		return 0;
	}
};

class TenyearZongshiProtect : public TriggerSkill
{
public:
	TenyearZongshiProtect() : TriggerSkill("#tenyearzongshi-protect")
	{
		events << TargetConfirmed;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->getHandcardNum() >= target->getMaxCards() && target->hasSkill("tenyearzongshi");
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.to.contains(player)||use.card->getTypeId()<1) return false;
		if (use.card->isKindOf("DelayedTrick") || (!use.card->isBlack() && !use.card->isRed())) {
			LogMessage log;
			log.type = "#TenyearzongshiAvoid";
			log.from = player;
			log.arg = "tenyearzongshi";
			log.card_str = use.card->toString();
			room->sendLog(log);
			room->notifySkillInvoked(player, "tenyearzongshi");
			use.nullified_list << player->objectName();
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class TenyearXuanfeng : public TriggerSkill
{
public:
	TenyearXuanfeng() : TriggerSkill("tenyearxuanfeng")
	{
		events << CardsMoveOneTime << EventPhaseEnd << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	void perform(Room *room, ServerPlayer *lingtong) const
	{
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *target, room->getOtherPlayers(lingtong)) {
			if (lingtong->canDiscard(target, "he"))
				targets << target;
		}
		if (targets.isEmpty())
			return;

		if (lingtong->askForSkillInvoke(this)) {
			room->broadcastSkillInvoke(objectName());

			ServerPlayer *first = room->askForPlayerChosen(lingtong, targets, "tenyearxuanfeng");
			room->doAnimate(1, lingtong->objectName(), first->objectName());
			ServerPlayer *second = nullptr;
			int first_id = -1;
			int second_id = -1;
			if (first != nullptr) {
				first_id = room->askForCardChosen(lingtong, first, "he", "tenyearxuanfeng", false, Card::MethodDiscard);
				room->throwCard(first_id, first, lingtong);
			}
			if (!lingtong->isAlive())
				return;
			targets.clear();
			foreach (ServerPlayer *target, room->getOtherPlayers(lingtong)) {
				if (lingtong->canDiscard(target, "he"))
					targets << target;
			}
			if (!targets.isEmpty()) {
				second = room->askForPlayerChosen(lingtong, targets, "tenyearxuanfeng");
				room->doAnimate(1, lingtong->objectName(), second->objectName());
			}
			if (second != nullptr) {
				second_id = room->askForCardChosen(lingtong, second, "he", "tenyearxuanfeng", false, Card::MethodDiscard);
				room->throwCard(second_id, second, lingtong);
			}
			if (lingtong->isDead() || !lingtong->hasFlag("CurrentPlayer")) return;
			QList<ServerPlayer *> victims;
			if (first->isAlive())
				victims << first;
			if (second != nullptr && second->isAlive() && second != first)
				victims << second;
			if (victims.isEmpty()) return;
			ServerPlayer *victim = room->askForPlayerChosen(lingtong, victims, "tenyearxuanfeng_damage", "@tenyearxuanfeng-invoke", true);
			if (!victim) return;
			room->doAnimate(1, lingtong->objectName(), victim->objectName());
			room->damage(DamageStruct("tenyearxuanfeng", lingtong, victim));
		}
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *lingtong, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			lingtong->setMark("tenyearxuanfeng", 0);
		} else if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from != lingtong)
				return false;

			if (lingtong->getPhase() == Player::Discard
				&& (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
				lingtong->addMark("tenyearxuanfeng", move.card_ids.length());

			if (move.from_places.contains(Player::PlaceEquip) && TriggerSkill::triggerable(lingtong))
				perform(room, lingtong);
		} else if (triggerEvent == EventPhaseEnd && TriggerSkill::triggerable(lingtong)
			&& lingtong->getPhase() == Player::Discard && lingtong->getMark("tenyearxuanfeng") >= 2) {
			perform(room, lingtong);
		}

		return false;
	}
};

TenyearyongjinCard::TenyearyongjinCard()
{
	target_fixed = true;
}

void TenyearyongjinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->doSuperLightbox(source, "tenyearyongjin");
	room->removePlayerMark(source, "@tenyearyongjinMark");

	for (int i = 1; i <= 3; i++) {
		if (source->isDead()) break;
		if (!room->moveField(source, "tenyearyongjin", i==1, "e"))
			break;
	}
}

class Tenyearyongjin : public ZeroCardViewAsSkill
{
public:
	Tenyearyongjin() : ZeroCardViewAsSkill("tenyearyongjin")
	{
		frequency = Limited;
		limit_mark = "@tenyearyongjinMark";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (player->getMark("@tenyearyongjinMark") <= 0) return false;
		QList<const Player *> as = player->getAliveSiblings();
		as << player;
		foreach (const Player *p, as) {
			if (!p->getEquips().isEmpty())
				return true;
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new TenyearyongjinCard;
	}
};


class TenyearEnyuan : public TriggerSkill
{
public:
	TenyearEnyuan() : TriggerSkill("tenyearenyuan")
	{
		events << CardsMoveOneTime << Damaged;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to == player && move.from && move.from->isAlive() && move.from != move.to && move.card_ids.size() >= 2 && move.reason.m_reason != CardMoveReason::S_REASON_PREVIEWGIVE
					&& (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip)) {
				move.from->setFlags("TenyearEnyuanDrawTarget");
				bool invoke = room->askForSkillInvoke(player, objectName(), data);
				move.from->setFlags("-TenyearEnyuanDrawTarget");
				if (invoke) {
					room->broadcastSkillInvoke(objectName());
					room->drawCards((ServerPlayer *)move.from, 1, objectName());
				}
			}
		} else if (triggerEvent == Damaged) {
			DamageStruct damage = data.value<DamageStruct>();
			ServerPlayer *source = damage.from;
			if (!source || source == player) return false;
			int x = damage.damage;
			for (int i = 0; i < x; i++) {
				if (source->isAlive() && player->isAlive() && room->askForSkillInvoke(player, objectName(), data)) {
					room->broadcastSkillInvoke(objectName());
					const Card *card = nullptr;
					if (!source->isKongcheng()) {
						source->tag["tenyearenyuan_data"] = data;
						card = room->askForExchange(source, objectName(), 1, 1, false, "EnyuanGive::" + player->objectName(), true);
						source->tag.remove("tenyearenyuan_data");
					}
					if (card) {
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(),
							player->objectName(), objectName(), "");
						reason.m_playerId = player->objectName();
						room->moveCardTo(card, source, player, Player::PlaceHand, reason, true);
						if (card->getSuit() != Card::Heart)
							player->drawCards(1, objectName());
					} else {
						room->loseHp(HpLostStruct(source, 1, objectName(), player));
					}
				} else {
					break;
				}
			}
		}
		return false;
	}
};

TenyearXuanhuoCard::TenyearXuanhuoCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void TenyearXuanhuoCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	CardMoveReason r(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "tenyearxuanhuo", "");
	room->obtainCard(effect.to, this, r, false);

	if (effect.from->isDead()) return;
	QList<int> ava = room->getAvailableCardList(effect.to, "basic,trick", "tenyearxuanhuo");

	QStringList names;
	foreach (int id, ava) {
		const Card *card = Sanguosha->getEngineCard(id);
		if ((card->isKindOf("Slash") || card->isKindOf("Duel")) && !names.contains(card->objectName()))
			names << card->objectName();
	}

	DummyCard *handcards = effect.to->wholeHandCards();
	if (names.isEmpty() || room->alivePlayerCount() <= 2) {
		if (!effect.to->isKongcheng()) {
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "tenyearxuanhuo", "");
			room->obtainCard(effect.from, handcards, reason, false);
		}
	} else {
		QList<ServerPlayer *> targets;
		QList<ServerPlayer *> all_targets = room->getOtherPlayers(effect.to);
		if (all_targets.contains(effect.from))
			all_targets.removeOne(effect.from);
		foreach (QString name, names) {
			Card *card = Sanguosha->cloneCard(name);
			card->setSkillName("_tenyearxuanhuo");
			card->deleteLater();
			foreach (ServerPlayer *p, all_targets) {
				QList<ServerPlayer *> player_list;
				player_list << p;
				if (effect.to->canUse(card, player_list) && !targets.contains(p))
					targets << p;
			}
		}
		if (targets.isEmpty()) {
			if (!effect.to->isKongcheng()) {
				CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "tenyearxuanhuo", "");
				room->obtainCard(effect.from, handcards, reason, false);
			}
		} else {
			if (room->askForChoice(effect.to, "tenyearxuanhuo", "use+give", QVariant::fromValue(effect.from)) == "give") {
				if (!effect.to->isKongcheng()) {
					CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "tenyearxuanhuo", "");
					room->obtainCard(effect.from, handcards, reason, false);
				}
			} else {
				effect.from->tag["tenyearxuanhuo_target"] = QVariant::fromValue(effect.to);
				ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "tenyearxuanhuo", "@tenyearxuanhuo-target:" + effect.to->objectName());
				effect.from->tag.remove("tenyearxuanhuo_target");

				LogMessage log;
				log.type = "#TenyearxuanhuoTarget";
				log.from = effect.from;
				log.to << target;
				log.arg = "tenyearxuanhuo";
				room->sendLog(log);
				room->doAnimate(1, effect.from->objectName(), target->objectName());

				QStringList choices;
				foreach (QString name, names) {
					Card *card = Sanguosha->cloneCard(name);
					card->setSkillName("_tenyearxuanhuo");
					card->deleteLater();
					QList<ServerPlayer *> player_list;
					player_list << target;
					if (effect.to->canUse(card, player_list))
						choices << name;
				}
				if (choices.isEmpty()) {
					if (!effect.to->isKongcheng()) {
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "tenyearxuanhuo", "");
						room->obtainCard(effect.from, handcards, reason, false);
					}
				} else {
					QString choice = room->askForChoice(effect.to, "tenyearxuanhuo", choices.join("+"));
					Card *card = Sanguosha->cloneCard(choice);
					card->setSkillName("_tenyearxuanhuo");
					card->deleteLater();
					room->useCard(CardUseStruct(card, effect.to, target));
				}
			}
		}
	}
}

class TenyearXuanhuoVS : public ViewAsSkill
{
public:
	TenyearXuanhuoVS() : ViewAsSkill("tenyearxuanhuo")
	{
		response_pattern = "@@tenyearxuanhuo";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const {
		return selected.length() < 2 && !to_select->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != 2)
			return nullptr;

		TenyearXuanhuoCard *c = new TenyearXuanhuoCard;
		c->addSubcards(cards);
		return c;
	}
};

class TenyearXuanhuo : public TriggerSkill
{
public:
	TenyearXuanhuo() : TriggerSkill("tenyearxuanhuo")
	{
		events << EventPhaseEnd;
		view_as_skill = new TenyearXuanhuoVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Draw) return false;
		if (player->getHandcardNum() < 2 || room->alivePlayerCount() < 3) return false;
		room->askForUseCard(player, "@@tenyearxuanhuo", "@tenyearxuanhuo");
		return false;
	}
};

class TenyearZhuikong : public TriggerSkill
{
public:
	TenyearZhuikong() : TriggerSkill("tenyearzhuikong")
	{
		events << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::RoundStart)
			return false;

		foreach (ServerPlayer *fuhuanghou, room->getOtherPlayers(player)) {
			if (TriggerSkill::triggerable(fuhuanghou)
				&& fuhuanghou->isWounded() && fuhuanghou->canPindian(player)
				&& fuhuanghou->askForSkillInvoke(objectName(), player)) {
				room->broadcastSkillInvoke(objectName());
				PindianStruct *pindian = fuhuanghou->PinDian(player, objectName());
				if (pindian->success) {
					room->setPlayerFlag(player, "tenyearzhuikong");
				} else {
					int to_card_id = pindian->to_card->getEffectiveId();
					if (room->getCardPlace(to_card_id) != Player::DiscardPile) return false;
					room->obtainCard(fuhuanghou, to_card_id);
					Slash *slash = new Slash(Card::NoSuit, 0);
					slash->setSkillName("_tenyearzhuikong");
					slash->deleteLater();
					if (!player->canSlash(fuhuanghou, slash, false)) return false;
					room->useCard(CardUseStruct(slash, player, fuhuanghou));
				}
			}
		}
		return false;
	}
};

class TenyearZhuikongProhibit : public ProhibitSkill
{
public:
	TenyearZhuikongProhibit() : ProhibitSkill("#tenyearzhuikong")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		if (card->getTypeId() != Card::TypeSkill && from->hasFlag("tenyearzhuikong"))
			return to != from;
		return false;
	}
};

class TenyearQiuyuan : public TriggerSkill
{
public:
	TenyearQiuyuan() : TriggerSkill("tenyearqiuyuan")
	{
		events << TargetConfirming;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		QList<ServerPlayer *> targets = room->getOtherPlayers(use.from);
		foreach (ServerPlayer *p, targets) {
			if(p==player||use.to.contains(p))
				targets.removeOne(p);
		}

		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@tenyearqiuyuan-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());

		const Card *card = nullptr;
		if (!target->isKongcheng()) {
			target->tag["tenyearqiuyuan_from"] = QVariant::fromValue(player);
			card = room->askForCard(target, "BasicCard+^Slash", "@tenyearqiuyuan-give:" + player->objectName(), data, Card::MethodNone);
		}
		if (card) {
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "tenyearqiuyuan", "");
			room->obtainCard(player, card, reason);
		} else {
			if (!use.from->canSlash(target, use.card, false)) return false;
			LogMessage log;
			log.type = "#BecomeTarget";
			log.from = target;
			log.card_str = use.card->toString();
			room->sendLog(log);
			use.to.append(target);
			room->sortByActionOrder(use.to);
			use.no_respond_list << target->objectName();
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

TenyearSidiCard::TenyearSidiCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void TenyearSidiCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &) const
{
	CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "tenyearsidi", "");
	room->throwCard(this, reason, nullptr);
}

class TenyearSidiVS : public OneCardViewAsSkill
{
public:
	TenyearSidiVS() : OneCardViewAsSkill("tenyearsidi")
	{
		expand_pile = "sidi";
		filter_pattern = ".|.|.|sidi";
		response_pattern = "@@tenyearsidi";
	}

	const Card *viewAs(const Card *card) const
	{
		TenyearSidiCard *c = new TenyearSidiCard;
		c->addSubcard(card);
		return c;
	}
};

class TenyearSidi : public TriggerSkill
{
public:
	TenyearSidi() : TriggerSkill("tenyearsidi")
	{
		events << EventPhaseStart << EventPhaseEnd << PreCardUsed;
		view_as_skill = new TenyearSidiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->isDead()) return false;
			if (player->getPhase()==Player::Finish&&player->hasSkill(objectName())&&!player->isNude()) {
				const Card *card = room->askForCard(player, "^BasicCard", "@tenyearsidi-put", data, Card::MethodNone, nullptr, false, objectName());
				if (card){
					room->broadcastSkillInvoke(objectName());
					player->addToPile("sidi", card);
				}
			}
			if (player->getPhase() == Player::Play) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (!p->hasSkill(objectName()) || p->getPile("sidi").isEmpty()) continue;
					const Card *card = room->askForUseCard(p, "@@tenyearsidi", "@tenyearsidi:" + player->objectName(), -1, Card::MethodNone);
					if (!card) continue;
					room->addPlayerMark(player, "&tenyearsidi+" + card->getColorString() + "-PlayClear");
					p->addMark(player->objectName()+"tenyearsidiUse-PlayClear");
				}
			}
		} else if (player->getPhase() == Player::Play){
			if (event == PreCardUsed){
				const Card *card = data.value<CardUseStruct>().card;
				if(card->isKindOf("TrickCard"))
					player->addMark("tenyearsidiTrick-PlayClear");
				else if(card->isKindOf("Slash"))
					player->addMark("tenyearsidiSlash-PlayClear");
			}else{
				bool tenyearsidiTrick=player->getMark("tenyearsidiTrick-PlayClear")<1,tenyearsidiSlash=player->getMark("tenyearsidiSlash-PlayClear")<1;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->getMark(player->objectName()+"tenyearsidiUse-PlayClear")>0){
						bool send = false;
						if (tenyearsidiTrick) {
							Slash *slash = new Slash(Card::NoSuit, 0);
							slash->setSkillName("_tenyearsidi");
							if (p->canSlash(player, slash, false)){
								room->sendCompulsoryTriggerLog(p, objectName());
								room->useCard(CardUseStruct(slash, p, player));
								send = true;
							}
							slash->deleteLater();
						}
						if (tenyearsidiSlash) {
							if (p->isDead()) continue;
							if (!send) room->sendCompulsoryTriggerLog(p, objectName());
							p->drawCards(2, objectName());
						}
					}
				}
			}
		}
		return false;
	}
};

class TenyearSidiLimit : public CardLimitSkill
{
public:
	TenyearSidiLimit() : CardLimitSkill("#tenyearsidi-limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use,response";
	}

	QString limitPattern(const Player *target,const Card *card) const
	{
		if (target->getMark("&tenyearsidi+"+card->getColorString()+"-PlayClear")>0)
			return card->toString();
		return "";
	}
};

TenyearHuaiyiCard::TenyearHuaiyiCard()
{
	target_fixed = true;
}

void TenyearHuaiyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->isKongcheng()) return;
	room->showAllCards(source);

	QList<int> blacks;
	QList<int> reds;
	foreach (const Card *c, source->getHandcards()) {
		if (c->isRed())
			reds << c->getId();
		else
			blacks << c->getId();
	}

	if (reds.isEmpty() || blacks.isEmpty()) {
		source->drawCards(1, "tenyearhuaiyi");
		room->addPlayerMark(source, "tenyearhuaiyi-PlayClear");
		return;
	}

	QString to_discard = room->askForChoice(source, "tenyearhuaiyi", "black+red");
	QList<int> *pile = nullptr;
	if (to_discard == "black")
		pile = &blacks;
	else
		pile = &reds;

	int n = pile->length();

	room->setPlayerMark(source, "tenyearhuaiyi_num-PlayClear", n);

	DummyCard dm(*pile);
	room->throwCard(&dm, source);

	room->askForUseCard(source, "@@tenyearhuaiyi", "@tenyearhuaiyi:" + QString::number(n), -1, Card::MethodNone);
}

TenyearHuaiyiSnatchCard::TenyearHuaiyiSnatchCard()
{
	handling_method = Card::MethodNone;
	m_skillName = "tenyearhuaiyi";
}

bool TenyearHuaiyiSnatchCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	int n = Self->getMark("tenyearhuaiyi_num-PlayClear");
	if (targets.length() >= n)
		return false;

	if (to_select == Self)
		return false;

	if (to_select->isNude())
		return false;

	return true;
}

void TenyearHuaiyiSnatchCard::onUse(Room *room, CardUseStruct &card_use) const
{
	ServerPlayer *player = card_use.from;

	QList<ServerPlayer *> to = card_use.to;

	room->sortByActionOrder(to);

	int get = 0;
	foreach (ServerPlayer *p, to) {
		if (player->isDead()) return;
		if (p->isDead() || p->isNude()) continue;
		int id = room->askForCardChosen(player, p, "he", "tenyearhuaiyi");
		player->obtainCard(Sanguosha->getCard(id), false);
		get++;
	}

	if (get >= 2)
		room->loseHp(HpLostStruct(player, 1, "tenyearhuaiyi", player));
}

class TenyearHuaiyi : public ZeroCardViewAsSkill
{
public:
	TenyearHuaiyi() : ZeroCardViewAsSkill("tenyearhuaiyi")
	{

	}

	const Card *viewAs() const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@tenyearhuaiyi")
			return new TenyearHuaiyiSnatchCard;
		else
			return new TenyearHuaiyiCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (player->isKongcheng()) return false;
		if (player->getMark("tenyearhuaiyi-PlayClear") <= 0)
			return !player->hasUsed("TenyearHuaiyiCard");
		return player->usedTimes("TenyearHuaiyiCard") < 2;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@tenyearhuaiyi";
	}
};

class Tenyearjueqing : public TriggerSkill
{
public:
	Tenyearjueqing() : TriggerSkill("tenyearjueqing")
	{
		events << DamageCaused << Predamage;
	}

	Frequency getFrequency(const Player *target) const
	{
		if (target != nullptr) {
			return target->getMark("tenyearjueqing") <= 0 ? NotFrequent : Compulsory;
		}
		return Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (event == DamageCaused) {
			if (player->getMark(objectName()) > 0) return false;
			if (damage.to->isDead()) return false;
			player->tag["tenyearjueqing_data"] = data;
			if (!player->askForSkillInvoke(objectName(), damage.to)) {
				player->tag.remove("tenyearjueqing_data");
				return false;
			};
			player->tag.remove("tenyearjueqing_data");
			room->broadcastSkillInvoke(objectName());
			room->loseHp(HpLostStruct(player, damage.damage, "tenyearjueqing", player));

			damage.damage = 2 * damage.damage;
			damage.tips << "tenyearjueqing_" + damage.to->objectName();
			data = QVariant::fromValue(damage);
		} else {
			if (player->getMark(objectName()) <= 0) return false;
			if (damage.to->isDead()) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			//room->loseHp(HpLostStruct(damage.to, damage.damage, "tenyearjueqing", player, damage.ignore_hujia));
			room->loseHp(HpLostStruct(damage.to, damage.damage, "tenyearjueqing", player));
			return true;
		}
		return false;
	}
};

class TenyearjueqingComplete : public TriggerSkill
{
public:
	TenyearjueqingComplete() : TriggerSkill("#tenyearjueqing")
	{
		events << DamageComplete;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.from || damage.from->isDead() || !damage.from->hasSkill("tenyearjueqing", true) ||
				!damage.tips.contains("tenyearjueqing_" + damage.to->objectName()) || damage.from->getMark("tenyearjueqing") > 0) return false;
		room->setPlayerMark(damage.from, "tenyearjueqing", 1);
		QString translate = Sanguosha->translate(":tenyearjueqing2");
		room->changeTranslation(damage.from, "tenyearjueqing", translate);
		return false;
	}
};

TenyearGongqiCard::TenyearGongqiCard()
{
	target_fixed = true;
	handling_method = Card::MethodDiscard;
}

void TenyearGongqiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->setPlayerFlag(source, "InfinityAttackRange");
	const Card *cd = Sanguosha->getCard(subcards.first());
	room->setPlayerMark(source, "tenyeargongqi_slash_" + cd->getSuitString() + "-Clear", 1);
	if (cd->isKindOf("EquipCard")) {
		QList<ServerPlayer *> targets;
		foreach(ServerPlayer *p, room->getOtherPlayers(source))
			if (source->canDiscard(p, "he")) targets << p;
		if (!targets.isEmpty()) {
			ServerPlayer *to_discard = room->askForPlayerChosen(source, targets, "tenyeargongqi", "@gongqi-discard", true);
			if (to_discard)
				room->throwCard(room->askForCardChosen(source, to_discard, "he", "tenyeargongqi", false, Card::MethodDiscard),
								to_discard, source);
		}
	}
}

class TenyearGongqi : public OneCardViewAsSkill
{
public:
	TenyearGongqi() : OneCardViewAsSkill("tenyeargongqi")
	{
		filter_pattern = ".!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearGongqiCard");
	}

	const Card *viewAs(const Card *originalcard) const
	{
		TenyearGongqiCard *card = new TenyearGongqiCard;
		card->addSubcard(originalcard->getId());
		card->setSkillName(objectName());
		return card;
	}
};

class TenyearGongqiTargetMod : public TargetModSkill
{
public:
	TenyearGongqiTargetMod() : TargetModSkill("#tenyeargongqi-target")
	{
		frequency = NotFrequent;
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const
	{
		if (from->getMark("tenyeargongqi_slash_" + card->getSuitString() + "-Clear") > 0)
			return 1000;
		return 0;
	}
};

TenyearJiefanCard::TenyearJiefanCard()
{
}

bool TenyearJiefanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void TenyearJiefanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if (room->getTag("TurnLengthCount").toInt() == 1)
		room->addPlayerMark(source, "tenyearjiefan_reflash-Clear");

	room->removePlayerMark(source, "@tenyearjiefanMark");
	ServerPlayer *target = targets.first();
	source->tag["TenyearJiefanTarget"] = QVariant::fromValue(target);
	room->doSuperLightbox(source, "tenyearjiefan");

	foreach (ServerPlayer *player, room->getAllPlayers()) {
		if (player->isAlive() && player->inMyAttackRange(target))
			room->cardEffect(this, source, player);
	}
	source->tag.remove("TenyearJiefanTarget");
}

void TenyearJiefanCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();

	ServerPlayer *target = effect.from->tag["TenyearJiefanTarget"].value<ServerPlayer *>();
	QVariant data = effect.from->tag["TenyearJiefanTarget"];
	if (target && !room->askForCard(effect.to, ".Weapon", "@jiefan-discard::" + target->objectName(), data))
		target->drawCards(1, "tenyearjiefan");
}

class TenyearJiefanVS : public ZeroCardViewAsSkill
{
public:
	TenyearJiefanVS() : ZeroCardViewAsSkill("tenyearjiefan")
	{
		frequency = Limited;
		limit_mark = "@tenyearjiefanMark";
	}

	const Card *viewAs() const
	{
		return new TenyearJiefanCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@tenyearjiefanMark") >= 1;
	}
};

class TenyearJiefan : public TriggerSkill
{
public:
	TenyearJiefan() : TriggerSkill("tenyearjiefan")
	{
		events << EventPhaseChanging;
		frequency = Limited;
		limit_mark = "@tenyearjiefanMark";
		view_as_skill = new TenyearJiefanVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		if (player->getMark("tenyearjiefan_reflash-Clear") <= 0) return false;
		//if (player->getMark("@tenyearjiefanMark") > 0) return false;
		room->addPlayerMark(player, "@tenyearjiefanMark");
		return false;
	}
};

class TenyearZhongyong : public TriggerSkill
{
public:
	TenyearZhongyong() : TriggerSkill("tenyearzhongyong")
	{
		events << CardOffset << CardFinished;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardOffset) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (!effect.card->isKindOf("Slash")) return false;
			QVariantList slash = effect.from->tag["tenyearzhongyong_slash" + effect.card->toString()].toList();
			if (effect.card->isVirtualCard() && effect.card->subcardsLength() > 0) {
				foreach (int id, effect.card->getSubcards()) {
					if (slash.contains(QVariant(id))) continue;
					slash << id;
				}
			} else if (!effect.card->isVirtualCard()) {
				if (!slash.contains(QVariant(effect.card->getEffectiveId())))
					slash << effect.card->getEffectiveId();
			}
			player->tag["tenyearzhongyong_slash" + effect.card->toString()] = slash;

			if (!effect.offset_card) return false;
			QVariantList jink = player->tag["tenyearzhongyong_jink" + effect.card->toString()].toList();
			if (effect.offset_card->isVirtualCard() && effect.offset_card->subcardsLength() > 0) {
				foreach (int id, effect.offset_card->getSubcards()) {
					if (jink.contains(QVariant(id))) continue;
					jink << id;
				}
			} else if (!effect.offset_card->isVirtualCard()) {
				if (!jink.contains(QVariant(effect.offset_card->getEffectiveId())))
					jink << effect.offset_card->getEffectiveId();
			}
			effect.from->tag["tenyearzhongyong_jink" + effect.card->toString()] = jink;
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash")) return false;

			QVariantList slash = player->tag["tenyearzhongyong_slash" + use.card->toString()].toList();
			QVariantList jink = player->tag["tenyearzhongyong_jink" + use.card->toString()].toList();
			QList<int> slash_ids = ListV2I(slash);
			QList<int> jink_ids = ListV2I(jink);

			foreach (int id, slash_ids) {
				if (room->getCardPlace(id) != Player::DiscardPile)
					slash_ids.removeOne(id);
			}
			foreach (int id, jink_ids) {
				if (room->getCardPlace(id) != Player::DiscardPile)
					jink_ids.removeOne(id);
			}

			QList<int> give_list = slash_ids + jink_ids;
			if (give_list.isEmpty()) return false;

			room->fillAG(give_list, player);
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@tenyearzhongyong-invoke",
								true, true);
			room->clearAG(player);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());

			room->giveCard(player, target, give_list, objectName(), true);

			if (target->isDead()) return false;
			bool red = false, black = false;
			foreach (int id, give_list) {
				const Card *card = Sanguosha->getCard(id);
				if (card->isRed())
					red = true;
				else if (card->isBlack())
					black = true;
				if (red && black)
					break;
			}

			if (red) {
				QList<ServerPlayer *> tos;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (player->inMyAttackRange(p) && target->canSlash(p, nullptr, true))
						tos << p;
				}
				if (!tos.isEmpty())
					room->askForUseSlashTo(target, tos, "@newzhongyong-slash");
			}

			if (black && target->isAlive())
				target->drawCards(1, objectName());
		}
		return false;
	}
};

class TenyearJigong : public PhaseChangeSkill
{
public:
	TenyearJigong() : PhaseChangeSkill("tenyearjigong")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play || !player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());

		QString choice = room->askForChoice(player, objectName(), "1+2+3");
		int num = choice.toInt();

		room->setPlayerMark(player, "tenyearjigong_maxcards-Clear", num);
		player->drawCards(num, objectName());
		return false;
	}
};

class TenyearJigongMax : public MaxCardsSkill
{
public:
	TenyearJigongMax() : MaxCardsSkill("#tenyearjigong")
	{
	}

	int getFixed(const Player *target) const
	{
		if (target->getMark("tenyearjigong_maxcards-Clear") > 0)
			return target->getMark("damage_point_play_phase");

		return -1;
	}
};

class TenyearJigongRecover : public PhaseChangeSkill
{
public:
	TenyearJigongRecover() : PhaseChangeSkill("#tenyearjigong-recover")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		if (target == nullptr || !target->isAlive()) return false;
		int draw = target->getMark("tenyearjigong_maxcards-Clear");
		int damage = target->getMark("damage_point_play_phase");
		return damage >= draw && draw > 0 && target->getPhase() == Player::Discard && target->isWounded();
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		room->sendCompulsoryTriggerLog(player, "tenyearjigong", true, true);
		room->recover(player, RecoverStruct("tenyearjigong", player));
		return false;
	}
};

class TenyearJiezhong : public PhaseChangeSkill
{
public:
	TenyearJiezhong() : PhaseChangeSkill("tenyearjiezhong")
	{
		frequency = Limited;
		limit_mark = "@tenyearjiezhongMark";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive()&&target->getMark("@tenyearjiezhongMark")>0
		&&target->getPhase()==Player::Play &&target->getMaxHp()>target->getHandcardNum()&&target->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke("tenyearjiezhong");
		room->doSuperLightbox(player, "tenyearjiezhong");
		room->removePlayerMark(player, "@tenyearjiezhongMark");
		player->drawCards(player->getMaxHp() - player->getHandcardNum(), objectName());
		return false;
	}
};

class TenyearLongyin : public TriggerSkill
{
public:
	TenyearLongyin() : TriggerSkill("tenyearlongyin")
	{
		events << CardUsed;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getPhase() == Player::Play;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("Slash")) {
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead() || !p->hasSkill(objectName()) || !p->canDiscard(p, "he")) continue;
				const Card *card = room->askForCard(p, "..", "@tenyearlongyin:" + use.from->objectName(), data, objectName());
				if (!card) continue;
				room->broadcastSkillInvoke(objectName());
				use.m_addHistory = false;
				data = QVariant::fromValue(use);
				if (use.card->isRed())
					p->drawCards(1, objectName());
				if (card->getNumber() == use.card->getNumber() && p->hasSkill("tenyearjiezhong", true) &&
						p->getMark("@tenyearjiezhongMark") <= 0) {
					LogMessage log;
					log.type = "#TenyearLongyinReset";
					log.from = p;
					log.arg = objectName();
					log.arg2 = "tenyearjiezhong";
					room->sendLog(log);
					room->addPlayerMark(p, "@tenyearjiezhongMark");
				}
			}
		}
		return false;
	}
};

class TenyearQieting : public TriggerSkill
{
public:
	TenyearQieting() : TriggerSkill("tenyearqieting")
	{
		events << EventPhaseChanging << TargetSpecified;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId() != Card::TypeSkill) {
				foreach (ServerPlayer *p, use.to) {
					if (p != player)
						player->addMark("qieting-Clear");
				}
			}
			return false;
		}
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		int damage = player->getMark("damage_point_round"), qieting = player->getMark("qieting-Clear");
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			if (damage <= 0) {
				QList<int> disable_ids;
				for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
					if (player->getEquip(i) && (p->getEquip(i) || !p->hasEquipArea(i)))
						disable_ids << player->getEquip(i)->getEffectiveId();
				}
				if (player->getEquips().length() > disable_ids.length() && p->askForSkillInvoke(this, "move:" + player->objectName())) {
					room->broadcastSkillInvoke(this);
					int id = room->askForCardChosen(p, player, "e", objectName(), false, Card::MethodNone, disable_ids);
					if (id >= 0)
						room->moveCardTo(Sanguosha->getCard(id), p, Player::PlaceEquip);
				}
			}
			if (qieting <= 0 && p->isAlive() && p->hasSkill(objectName())) {
				room->sendCompulsoryTriggerLog(p, objectName());
				p->drawCards(1, objectName());
			}
		}
		return false;
	}
};

TenyearXianzhouDamageCard::TenyearXianzhouDamageCard()
{
	mute = true;
	m_skillName = "tenyearxianzhou";
}

void TenyearXianzhouDamageCard::onUse(Room *room, CardUseStruct &card_use) const
{
	//room->sortByActionOrder(card_use.to);
	CardUseStruct use = card_use;
	room->sortByActionOrder(use.to);
	foreach (ServerPlayer *p, use.to)
		room->damage(DamageStruct("tenyearxianzhou", use.from->isAlive() ? use.from : nullptr, p));
}

bool TenyearXianzhouDamageCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	QString name = Self->property("tenyearxianzhou_target").toString();
	if (name.isEmpty()) return false;
	//const Player *target = Self->findChild<const Player *>(name);

	const Player *target = nullptr;
	QList<const Player *> as = Self->getAliveSiblings();
	as << Self;
	foreach (const Player *p, as) {
		if (p->objectName() == name) {
			target = p;
			break;
		}
	}

	if (!target) return false;
	return targets.length() < Self->getMark("tenyearxianzhou") && target->inMyAttackRange(to_select);
}

TenyearXianzhouCard::TenyearXianzhouCard()
{
}

bool TenyearXianzhouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}

void TenyearXianzhouCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->removePlayerMark(effect.from, "@tenyearxianzhouMark");
	room->doSuperLightbox(effect.from, "tenyearxianzhou");

	DummyCard *dummy = new DummyCard(effect.from->getEquipsId());
	int len = dummy->subcardsLength();
	room->setPlayerMark(effect.from, "tenyearxianzhou", len);
	effect.to->obtainCard(dummy);
	delete dummy;

	room->recover(effect.from, RecoverStruct(effect.from, nullptr, qMin(len, effect.from->getMaxHp() - effect.from->getHp()), "tenyearxianzhou"));
	if (effect.from->isDead() || effect.to->isDead()) return;
	bool attack = false;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (effect.to->inMyAttackRange(p)) {
			attack = true;
			break;
		}
	}
	if (!attack) return;

	room->setPlayerProperty(effect.from, "tenyearxianzhou_target", effect.to->objectName());
	room->askForUseCard(effect.from, "@@tenyearxianzhou", "@tenyearxianzhou:" + effect.to->objectName() + "::" + QString::number(len));
}

class TenyearXianzhou : public ZeroCardViewAsSkill
{
public:
	TenyearXianzhou() : ZeroCardViewAsSkill("tenyearxianzhou")
	{
		frequency = Skill::Limited;
		limit_mark = "@tenyearxianzhouMark";
		response_pattern = "@@tenyearxianzhou";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@tenyearxianzhouMark") > 0 && player->getEquips().length() > 0;
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@tenyearxianzhou") {
			return new TenyearXianzhouDamageCard;
		} else {
			return new TenyearXianzhouCard;
		}
	}
};

TenyearShenxingCard::TenyearShenxingCard()
{
	target_fixed = true;
}

void TenyearShenxingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->isAlive())
		room->drawCards(source, 1, "tenyearshenxing");
}

class TenyearShenxing : public ViewAsSkill
{
public:
	TenyearShenxing() : ViewAsSkill("tenyearshenxing")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return selected.length() < qMin(2, Self->usedTimes("TenyearShenxingCard")) && !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != qMin(2, Self->usedTimes("TenyearShenxingCard")))
			return nullptr;

		TenyearShenxingCard *card = new TenyearShenxingCard;
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return true;
	}
};

TenyearBingyiCard::TenyearBingyiCard()
{
}

bool TenyearBingyiCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card::Color color = Card::Colorless;
	foreach (const Card *c, Self->getHandcards()) {
		if (color == Card::Colorless)
			color = c->getColor();
		else if (c->getColor() != color)
			return targets.isEmpty();
	}
	return targets.length() <= Self->getHandcardNum();
}

bool TenyearBingyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
	Card::Color color = Card::Colorless;
	foreach (const Card *c, Self->getHandcards()) {
		if (color == Card::Colorless)
			color = c->getColor();
		else if (c->getColor() != color)
			return false;
	}
	return targets.length() < Self->getHandcardNum();
}

void TenyearBingyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->showAllCards(source);

	bool same_number = true;
	QList<const Card *>cards = source->getHandcards();
	int num = cards.first()->getNumber();
	foreach (const Card *c, source->getHandcards()) {
		if (c->getNumber() != num) {
			same_number = false;
			break;
		}
	}

	foreach(ServerPlayer *p, targets)
		room->drawCards(p, 1, "tenyearbingyi");
	if (same_number && source->isAlive())
		source->drawCards(1, "tenyearbingyi");
}

class TenyearBingyiViewAsSkill : public ZeroCardViewAsSkill
{
public:
	TenyearBingyiViewAsSkill() : ZeroCardViewAsSkill("tenyearbingyi")
	{
		response_pattern = "@@tenyearbingyi";
	}

	const Card *viewAs() const
	{
		return new TenyearBingyiCard;
	}
};

class TenyearBingyi : public PhaseChangeSkill
{
public:
	TenyearBingyi() : PhaseChangeSkill("tenyearbingyi")
	{
		view_as_skill = new TenyearBingyiViewAsSkill;
	}

	bool onPhaseChange(ServerPlayer *target, Room *room) const
	{
		if (target->getPhase() != Player::Finish || target->isKongcheng()) return false;
		room->askForUseCard(target, "@@tenyearbingyi", "@tenyearbingyi");
		return false;
	}
};

#include "mobile.h"
#include "yjcm2014.h"

TenyearZongxuanCard::TenyearZongxuanCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

void TenyearZongxuanCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const
{
}

class TenyearZongxuanVS : public ViewAsSkill
{
public:
	TenyearZongxuanVS() : ViewAsSkill("tenyearzongxuan")
	{
		expand_pile = "#tenyearzongxuan";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return Self->getPile("#tenyearzongxuan").contains(to_select->getEffectiveId());
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@tenyearzongxuan");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;

		TenyearZongxuanCard *card = new TenyearZongxuanCard;
		card->addSubcards(cards);
		return card;
	}
};

class TenyearZongxuan : public TriggerSkill
{
public:
	TenyearZongxuan() : TriggerSkill("tenyearzongxuan")
	{
		events << CardsMoveOneTime;
		view_as_skill = new TenyearZongxuanVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!move.from || move.from != player)
			return false;
		if (move.to_place == Player::DiscardPile
			&& ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {

			int i = 0;
			QList<int> zongxuan_card, trick_card;
			foreach (int card_id, move.card_ids) {
				if (room->getCardPlace(card_id) == Player::DiscardPile
					&& (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip)) {
					zongxuan_card << card_id;
					if (Sanguosha->getCard(card_id)->isKindOf("TrickCard"))
						trick_card << card_id;
				}
				i++;
			}
			if (zongxuan_card.isEmpty())
				return false;

			QString pattern = "@@tenyearzongxuan";
			if (!trick_card.isEmpty()) {
				ServerPlayer *geter = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@tenyearzongxuan-trick", true, true);
				if (geter) {
					room->broadcastSkillInvoke(objectName());
					pattern = pattern + "!";

					room->fillAG(trick_card, geter);  //偷懒用AG
					int id = room->askForAG(geter, trick_card, false, objectName());
					zongxuan_card.removeOne(id);
					room->clearAG(geter);
					room->obtainCard(geter, id);
				}
			}

			if (player->isDead() || zongxuan_card.isEmpty()) return false;

			room->notifyMoveToPile(player, zongxuan_card, objectName(), Player::DiscardPile, true);

			try {
				const Card *c = room->askForUseCard(player, pattern, pattern.endsWith("!") ? "@tenyearzongxuan" : "@mobilezongxuan");
				if (c) {
					QList<int> subcards = c->getSubcards();
					foreach (int id, subcards) {
						if (zongxuan_card.contains(id))
							zongxuan_card.removeOne(id);
					}
					LogMessage log;
					log.type = "$YinshicaiPut";
					log.from = player;
					log.card_str = ListI2S(subcards).join("+");
					room->sendLog(log);

					room->notifyMoveToPile(player, subcards, objectName(), Player::DiscardPile, false);

					CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), "tenyearzongxuan", "");
					room->moveCardTo(c, nullptr, Player::DrawPile, reason, true, true);
				} else {
					if (pattern.endsWith("!")) {
						int id = zongxuan_card.at(qrand() % zongxuan_card.length());
						CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), "tenyearzongxuan", "");
						room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPile, reason, true);
					}
				}
			}
			catch (TriggerEvent triggerEvent) {
				if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
					if (!zongxuan_card.isEmpty())
						room->notifyMoveToPile(player, zongxuan_card, objectName(), Player::DiscardPile, false);
				}
				throw triggerEvent;
			}
			if (!zongxuan_card.isEmpty())
				room->notifyMoveToPile(player, zongxuan_card, objectName(), Player::DiscardPile, false);
		}
		return false;
	}
};

class TenyearZhiyan : public PhaseChangeSkill
{
public:
	TenyearZhiyan() : PhaseChangeSkill("tenyearzhiyan")
	{
	}

	bool onPhaseChange(ServerPlayer *target, Room *room) const
	{
		if (target->getPhase() != Player::Finish)
			return false;

		ServerPlayer *to = room->askForPlayerChosen(target, room->getAlivePlayers(), objectName(), "@zhiyan-invoke", true, true);
		if (to) {
			room->broadcastSkillInvoke(objectName());
			QList<int> ids = room->drawCardsList(to, 1, objectName(), true, true);
			int id = ids.first();
			const Card *card = Sanguosha->getCard(id);
			if (!to->isAlive())
				return false;
			room->showCard(to, id);

			if (card->isKindOf("EquipCard")) {
				room->recover(to, RecoverStruct("tenyearzhiyan", target));
				if (to->isAlive() && to->canUse(card) && !to->getEquipsId().contains(id))
					room->useCard(CardUseStruct(card, to));
			} else if (card->isKindOf("BasicCard"))
				target->drawCards(1, objectName());
		}
		return false;
	}
};

class TenyearQiaoshi : public PhaseChangeSkill
{
public:
	TenyearQiaoshi() : PhaseChangeSkill("tenyearqiaoshi")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getPhase() == Player::Finish;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!TriggerSkill::triggerable(p) || p->getHandcardNum() != player->getHandcardNum())
				continue;

			int i = 0;
			Card::Suit suit1 = Card::NoSuit, suit2 = Card::NoSuit;
			while (suit1 == suit2) {
				if (p->isDead() || player->isDead()) break;
				//if (p->getHandcardNum() != player->getHandcardNum()) break;
				if (!p->askForSkillInvoke(this, player)) break;

				if (i == 0)
					room->broadcastSkillInvoke(this);
				i++;

				QList<ServerPlayer *> l;
				l << p << player;
				room->sortByActionOrder(l);

				int id1 = room->drawCardsList(l.first(), 1, objectName()).first();
				int id2 = room->drawCardsList(l.last(), 1, objectName()).first();
				suit1 = Sanguosha->getCard(id1)->getSuit();
				suit2 = Sanguosha->getCard(id2)->getSuit();
			}
		}
		return false;
	}
};

TenyearYjYanyuCard::TenyearYjYanyuCard()
{
	will_throw = false;
	can_recast = true;
	handling_method = Card::MethodRecast;
	target_fixed = true;
}

void TenyearYjYanyuCard::onUse(Room *room, CardUseStruct &card_use) const
{
	room->broadcastSkillInvoke("tenyearyjyanyu");
	ServerPlayer *xiahou = card_use.from;

	CardMoveReason reason(CardMoveReason::S_REASON_RECAST, xiahou->objectName());
	reason.m_skillName = getSkillName();
	room->moveCardTo(this, xiahou, nullptr, Player::DiscardPile, reason);
	//xiahou->broadcastSkillInvoke("@recast");

	int id = card_use.card->getSubcards().first();

	LogMessage log;
	log.type = "#UseCard_Recast";
	log.from = xiahou;
	log.card_str = QString::number(id);
	room->sendLog(log);

	xiahou->drawCards(1, "recast");

	xiahou->addMark("tenyearyjyanyu-PlayClear");
}

class TenyearYjYanyuVS : public OneCardViewAsSkill
{
public:
	TenyearYjYanyuVS() : OneCardViewAsSkill("tenyearyjyanyu")
	{
		filter_pattern = "Slash";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (Self->isCardLimited(originalCard, Card::MethodRecast))
			return nullptr;

		TenyearYjYanyuCard *recast = new TenyearYjYanyuCard;
		recast->addSubcard(originalCard);
		return recast;
	}
};

class TenyearYjYanyu : public TriggerSkill
{
public:
	TenyearYjYanyu() : TriggerSkill("tenyearyjyanyu")
	{
		view_as_skill = new TenyearYjYanyuVS;
		events << EventPhaseEnd;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Play) return false;
		int recastNum = player->getMark("tenyearyjyanyu-PlayClear");
		if (recastNum <= 0) return false;

		QList<ServerPlayer *> malelist;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->isMale())
				malelist << p;
		}

		if (malelist.isEmpty()) return false;

		recastNum = qMin(recastNum, 3);

		ServerPlayer *male = room->askForPlayerChosen(player, malelist, objectName(), "@tenyearyjyanyu-give:" + QString::number(recastNum), true, true);

		if (male != nullptr) {
			room->broadcastSkillInvoke(objectName());
			male->drawCards(recastNum, objectName());
		}
		return false;
	}
};

class TenyearQianxi : public PhaseChangeSkill
{
public:
	TenyearQianxi() : PhaseChangeSkill("tenyearqianxi")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() == Player::NotActive){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				QString an = p->tag["tenyearqianxi"+player->objectName()].toString();
				if (an != ""){
					p->removeEquipsNullified("Armor|"+an);
					p->tag.remove("tenyearqianxi"+player->objectName());
				}
			}
		}
		if (player->getPhase() != Player::Start) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(this);
		player->drawCards(1, objectName());
		if (!player->canDiscard(player, "he")) return false;
		const Card *card = room->askForDiscard(player, objectName(), 1, 1, false, true);
		if (!card) return false;

		QList<ServerPlayer *> to_choose;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (player->distanceTo(p) == 1)
				to_choose << p;
		}
		if (to_choose.isEmpty()) return false;

		ServerPlayer *t = room->askForPlayerChosen(player, to_choose, objectName());
		room->doAnimate(1, player->objectName(), t->objectName());

		QString color = "";
		if (card->isRed())
			color = "red";
		else if (card->isBlack())
			color = "black";

		room->addPlayerMark(t, "tenyearqianxi_target_" + player->objectName() + "-Clear");
		if (!color.isEmpty()) {
			room->addPlayerMark(t, "&tenyearqianxi+" + color + "-Clear");
			t->addEquipsNullified("Armor|"+color);
			t->tag["tenyearqianxi"+player->objectName()] = color;
		}
		return false;
	}
};

class TenyearQianxiDraw : public TriggerSkill
{
public:
	TenyearQianxiDraw() : TriggerSkill("#tenyearqianxi-draw")
	{
		events << HpRecover;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (!room->hasCurrent()) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead()) continue;
			int mark = player->getMark("tenyearqianxi_target_" + p->objectName() + "-Clear");
			for (int i = 0; i < mark; i++) {
				room->sendCompulsoryTriggerLog(p, "tenyearqianxi", true, true);
				p->drawCards(2, "tenyearqianxi");
			}
		}
		return false;
	}
};

class TenyearQianxiLimit : public CardLimitSkill
{
public:
	TenyearQianxiLimit() : CardLimitSkill("#tenyearqianxi-limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use,response";
	}

	QString limitPattern(const Player *target) const
	{
		QStringList colors;
		if (target->getMark("&tenyearqianxi+red-Clear") > 0) colors << "red";
		if (target->getMark("&tenyearqianxi+black-Clear") > 0) colors << "black";
		if (!colors.isEmpty()) return ".|" + colors.join(",") + "|.|hand";
		return "";
	}
};

TenyearJiaozhaoCard::TenyearJiaozhaoCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
	mute = true;
}

void TenyearJiaozhaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->setPlayerMark(source, "ViewAsSkill_tenyearjiaozhaoEffect", 1);

	int selfcardid = getSubcards().first();
	room->showCard(source, selfcardid);

	int level = source->property("tenyearjiaozhao_level").toInt();
	ServerPlayer *target;
	if (level >= 1)
		target = source;
	else {
		int distance = source->distanceTo(source->getNextAlive());
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			if (source->distanceTo(p) < distance)
				distance = source->distanceTo(p);
		}
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			if (source->distanceTo(p) == distance)
				targets << p;
		}
		if (targets.isEmpty()) return;
		target = room->askForPlayerChosen(source, targets, "tenyearjiaozhao", "@jiaozhao-target");
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, source->objectName(), target->objectName());
	}

	QStringList alllist;
	QList<int> ids;
	bool basic = source->getMark("tenyearjiaozhao_basic-Clear") > 0;
	bool trick = source->getMark("tenyearjiaozhao_trick-Clear") > 0;
	foreach(int id, Sanguosha->getRandomCards()) {
		const Card *c = Sanguosha->getEngineCard(id);
		if (c->isKindOf("EquipCard") || c->isKindOf("DelayedTrick")) continue;
		if (basic && c->isKindOf("BasicCard")) continue;
		if (trick && c->isKindOf("TrickCard")) continue;
		if (alllist.contains(c->objectName())) continue;
		alllist << c->objectName();
		ids << id;
	}
	if (ids.isEmpty()) return;
	room->broadcastSkillInvoke(getSkillName(),1);

	room->fillAG(ids, target);
	int id = room->askForAG(target, ids, false, "tenyearjiaozhao");
	room->clearAG(target);

	const Card *card = Sanguosha->getEngineCard(id);
	QString name = card->objectName();

	LogMessage log;
	log.type = "#ShouxiChoice";
	log.from = target;
	log.arg = name;
	room->sendLog(log);

	room->setPlayerProperty(source, "tenyearjiaozhao_name", name);
	if (card->isKindOf("BasicCard")) {
		room->setPlayerProperty(source, "tenyearjiaozhao_basic_name", name);
		room->setPlayerMark(source, "tenyearjiaozhao_basic-Clear", selfcardid + 1);
	} else if (card->isKindOf("TrickCard")) {
		room->setPlayerProperty(source, "tenyearjiaozhao_trick_name", name);
		room->setPlayerMark(source, "tenyearjiaozhao_trick-Clear", selfcardid + 1);
	}
}

class TenyearJiaozhaoVS : public OneCardViewAsSkill
{
public:
	TenyearJiaozhaoVS() : OneCardViewAsSkill("tenyearjiaozhao")
	{
		response_or_use = true;
	}

	bool viewFilter(const Card *to_select) const
	{
		QString choice = Self->tag["tenyearjiaozhao"].toString();
		if (choice == "show") return !to_select->isEquipped();
		if (choice.startsWith("use")) {
			int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
			int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
			return to_select->getEffectiveId() == basic || to_select->getEffectiveId() == trick;
		} else if (choice.startsWith("basic")) {
			int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
			return to_select->getEffectiveId() == basic;
		} else if (choice.startsWith("trick")) {
			int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
			return to_select->getEffectiveId() == trick;
		}
		return false;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			QString choice = Self->tag["tenyearjiaozhao"].toString();
			if (choice == "show") {
				TenyearJiaozhaoCard *card = new TenyearJiaozhaoCard;
				card->addSubcard(originalCard);
				return card;
			} else if (choice.startsWith("use")) {
				int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
				int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
				if (basic > -1) {
					QString bname = Self->property("tenyearjiaozhao_basic_name").toString();
					Card *use_card = Sanguosha->cloneCard(bname);
					if (!use_card) return nullptr;
					use_card->setCanRecast(false);
					use_card->addSubcard(originalCard);
					use_card->setSkillName("tenyearjiaozhao");
					return use_card;
				} else if (trick > -1) {
					QString tname = Self->property("tenyearjiaozhao_trick_name").toString();
					Card *use_card = Sanguosha->cloneCard(tname);
					if (!use_card) return nullptr;
					use_card->setCanRecast(false);
					use_card->addSubcard(originalCard);
					use_card->setSkillName("tenyearjiaozhao");
					return use_card;
				}
			} else if (choice.startsWith("basic")) {
				int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
				if (basic < 0) return nullptr;
				QString bname = Self->property("tenyearjiaozhao_basic_name").toString();
				Card *use_card = Sanguosha->cloneCard(bname);
				if (!use_card) return nullptr;
				use_card->setCanRecast(false);
				use_card->addSubcard(originalCard);
				use_card->setSkillName("tenyearjiaozhao");
				return use_card;
			} else if (choice.startsWith("trick")) {
				int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
				if (trick < 0) return nullptr;
				QString tname = Self->property("tenyearjiaozhao_trick_name").toString();
				Card *use_card = Sanguosha->cloneCard(tname);
				if (!use_card) return nullptr;
				use_card->setCanRecast(false);
				use_card->addSubcard(originalCard);
				use_card->setSkillName("tenyearjiaozhao");
				return use_card;
			}
		} else {
			QString pattern = Sanguosha->getCurrentCardUsePattern();
			if (pattern == "nullification") {
				int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
				if (trick < 0) return nullptr;
				QString tname = Self->property("tenyearjiaozhao_trick_name").toString();
				if (tname != "nullification") return nullptr;
				Card *use_card = Sanguosha->cloneCard(tname);
				if (!use_card) return nullptr;
				use_card->setCanRecast(false);
				use_card->addSubcard(trick);
				use_card->setSkillName("tenyearjiaozhao");
				return use_card;
			} else {
				int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
				if (basic < 0) return nullptr;
				QString bname = Self->property("tenyearjiaozhao_basic_name").toString();
				Card *use_card = Sanguosha->cloneCard(bname);
				if (!use_card) return nullptr;
				use_card->setCanRecast(false);
				use_card->addSubcard(basic);
				use_card->setSkillName("tenyearjiaozhao");
				return use_card;
			}
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (!player->hasUsed("JiaozhaoCard")) return true;
		int level = player->property("tenyearjiaozhao_level").toInt();

		if (level < 2 && player->hasUsed("JiaozhaoCard")) {
			Card *use_card = nullptr;
			QString bname = player->property("tenyearjiaozhao_basic_name").toString();
			QString tname = player->property("tenyearjiaozhao_trick_name").toString();
			int basic = player->getMark("tenyearjiaozhao_basic-Clear") - 1;
			int trick = player->getMark("tenyearjiaozhao_trick-Clear") - 1;

			if (!bname.isEmpty() && basic > -1) {
				use_card = Sanguosha->cloneCard(bname);
				if (!use_card) return false;
				use_card->addSubcard(basic);
			} else if (!tname.isEmpty() && trick > -1) {
				use_card = Sanguosha->cloneCard(tname);
				if (!use_card) return false;
				use_card->addSubcard(trick);
			}

			use_card->setCanRecast(false);
			use_card->setSkillName("tenyearjiaozhao");
			use_card->deleteLater();
			return use_card->isAvailable(player);
		}
		return level >= 2;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return false;
		if (pattern.startsWith(".") || pattern.startsWith("@"))
			return false;

		QString bname = player->property("tenyearjiaozhao_basic_name").toString();
		QString tname = player->property("tenyearjiaozhao_trick_name").toString();
		if (bname.isEmpty() && tname.isEmpty()) return false;

		int basic = player->getMark("tenyearjiaozhao_basic-Clear") - 1;
		int trick = player->getMark("tenyearjiaozhao_trick-Clear") - 1;
		if (!bname.isEmpty() && basic < 0) return false;
		if (!tname.isEmpty() && trick < 0) return false;

		if (pattern == "nullification")
			return tname == "nullification";

		int level = player->property("tenyearjiaozhao_level").toInt();
		QString pattern_names = pattern;
		if (pattern.contains("slash") || pattern.contains("Slash"))
			pattern_names = Sanguosha->getSlashNames().join("+");
		else if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0)
			return false;
		else if (pattern == "peach+analeptic")
			return level >= 2 && pattern_names.split("+").contains(bname);
		return pattern_names.split("+").contains(bname);
	}

	bool isEnabledAtNullification(const ServerPlayer *player) const
	{
		QString tname = player->property("tenyearjiaozhao_trick_name").toString();
		if (tname.isEmpty()) return false;
		int trick = player->getMark("tenyearjiaozhao_trick-Clear") - 1;
		return trick > -1 && tname == "nullification";
	}
};

class TenyearJiaozhao : public TriggerSkill
{
public:
	TenyearJiaozhao() : TriggerSkill("tenyearjiaozhao")
	{
		events << EventPhaseChanging;
		view_as_skill = new TenyearJiaozhaoVS;
	}

	QDialog *getDialog() const
	{
		return TiansuanDialog::getInstance("tenyearjiaozhao");
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to == Player::NotActive) {
			room->setPlayerMark(player, "ViewAsSkill_tenyearjiaozhaoEffect", 0);
			room->setPlayerProperty(player, "tenyearjiaozhao_basic_name", "");
			room->setPlayerProperty(player, "tenyearjiaozhao_trick_name", "");
		}
		return false;
	}
};

class TenyearJiaozhaoPro : public ProhibitSkill
{
public:
	TenyearJiaozhaoPro() : ProhibitSkill("#tenyearjiaozhao")
	{
		frequency = NotFrequent;
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return from == to && card->getSkillName() == "tenyearjiaozhao" && from->property("tenyearjiaozhao_level").toInt() < 2;
	}
};

class TenyearDanxin : public MasochismSkill
{
public:
	TenyearDanxin() : MasochismSkill("tenyeardanxin")
	{
		frequency = Frequent;
	}

	void onDamaged(ServerPlayer *target, const DamageStruct &) const
	{
		if (target->askForSkillInvoke(objectName())){
			Room *room = target->getRoom();
			room->broadcastSkillInvoke(objectName());

			target->drawCards(1, objectName());

			int level = target->property("tenyearjiaozhao_level").toInt();
			if (level<2) {
				LogMessage log;
				log.type = "#JiexunChange";
				log.from = target;
				log.arg = "tenyearjiaozhao";
				room->sendLog(log);
				level++;
				room->setPlayerProperty(target, "tenyearjiaozhao_level", level);
				room->setPlayerMark(target, "&tenyearjiaozhao_level", level);
				room->changeTranslation(target, "tenyearjiaozhao", level);
			}
		}
	}
};

TenyearGanluCard::TenyearGanluCard()
{
}

void TenyearGanluCard::swapEquip(ServerPlayer *first, ServerPlayer *second) const
{
	Room *room = first->getRoom();

	QList<int> equips1, equips2;
	foreach(const Card *equip, first->getEquips())
		equips1.append(equip->getId());
	foreach(const Card *equip, second->getEquips())
		equips2.append(equip->getId());

	QList<CardsMoveStruct> exchangeMove;
	CardsMoveStruct move1(equips1, second, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_SWAP, first->objectName(), second->objectName(), "ganlu", ""));
	CardsMoveStruct move2(equips2, first, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_SWAP, second->objectName(), first->objectName(), "ganlu", ""));
	exchangeMove.push_back(move2);
	exchangeMove.push_back(move1);
	room->moveCardsAtomic(exchangeMove, false);
}

bool TenyearGanluCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

bool TenyearGanluCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	if (targets.isEmpty()) return true;
	if (targets.length() == 1) {
		if (targets.first()->getEquips().isEmpty())
			return !to_select->getEquips().isEmpty();
		else
			return true;
	}
	return false;
}

void TenyearGanluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	LogMessage log;
	log.type = "#GanluSwap";
	log.from = source;
	log.to = targets;
	room->sendLog(log);

	ServerPlayer *first = targets.first(), *last = targets[1];

	swapEquip(first, last);

	if (source->isAlive() && first->isAlive() && last->isAlive() &&
			qAbs(first->getEquips().length() - last->getEquips().length()) > source->getLostHp())
		room->askForDiscard(source, "tenyearganlu", 2, 2);
}

class TenyearGanlu : public ZeroCardViewAsSkill
{
public:
	TenyearGanlu() : ZeroCardViewAsSkill("tenyearganlu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearGanluCard");
	}

	const Card *viewAs() const
	{
		return new TenyearGanluCard;
	}
};

class TenyearBuyi : public TriggerSkill
{
public:
	TenyearBuyi() : TriggerSkill("tenyearbuyi")
	{
		events << Dying << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *wuguotai, QVariant &data) const
	{
		if (event == Dying) {
			DyingStruct dying = data.value<DyingStruct>();
			ServerPlayer *player = dying.who;
			if (player->isKongcheng()) return false;
			if (player->getHp() < 1 && wuguotai->askForSkillInvoke(this, data)) {
				wuguotai->peiyin(this);
				const Card *card = nullptr;
				if (player == wuguotai)
					card = room->askForCardShow(player, wuguotai, objectName());
				else {
					int card_id = room->askForCardChosen(wuguotai, player, "h", "tenyearbuyi");
					card = Sanguosha->getCard(card_id);
				}

				room->showCard(player, card->getEffectiveId());

				if (card->getTypeId()!=Card::TypeBasic&&!player->isJilei(card)) {
					room->throwCard(card, objectName(), player);
					room->recover(player, RecoverStruct("tenyearbuyi", wuguotai));
				}
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from && move.from->isAlive() && move.is_last_handcard && move.reason.m_skillName == objectName()) {
				room->sendCompulsoryTriggerLog(wuguotai, this);
				ServerPlayer *from = (ServerPlayer *)move.from;
				from->drawCards(1, objectName());
			}
		}
		return false;
	}
};

class TenyearZhuhai : public PhaseChangeSkill
{
public:
	TenyearZhuhai() : PhaseChangeSkill("tenyearzhuhai")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getPhase() == Player::Finish && target->getMark("damage_point_round") > 0;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (player->isDead()) return false;
			if (p->isDead() || !p->hasSkill(objectName()) || (p->isKongcheng() && p->getHandPile().isEmpty())) continue;

			QStringList hand_pile_names, cards;
			foreach (QString pile, p->getPileNames()) {
				if (pile.startsWith("&") || pile == "wooden_ox")
					hand_pile_names << pile;
			}
			foreach (int id, p->handCards() + p->getHandPile()) {
				const Card *c = Sanguosha->getCard(id);

				Slash *slash = new Slash(c->getSuit(), c->getNumber());
				slash->addSubcard(c);
				slash->setSkillName(objectName());
				slash->deleteLater();

				Dismantlement *dismantlement = new Dismantlement(c->getSuit(), c->getNumber());
				dismantlement->addSubcard(c);
				dismantlement->setSkillName(objectName());
				dismantlement->deleteLater();

				if (p->canSlash(player, slash, false))
					cards << QString::number(id);
				else if (p->canUse(dismantlement, player, true))
					cards << QString::number(id);
			}
			if (cards.isEmpty()) continue;

			const Card *card = room->askForCard(p, "" + cards.join(",") + "|.|.|hand," + hand_pile_names.join(","), "@tenyearzhuhai:" + player->objectName(),
							QVariant::fromValue(player), Card::MethodResponse, player, true);
			if (!card) continue;

			Slash *slash = new Slash(card->getSuit(), card->getNumber());
			slash->addSubcard(card);
			slash->setSkillName(objectName());
			slash->deleteLater();

			Dismantlement *dismantlement = new Dismantlement(card->getSuit(), card->getNumber());
			dismantlement->addSubcard(card);
			dismantlement->setSkillName(objectName());
			dismantlement->deleteLater();

			QStringList choices;
			if (p->canSlash(player, slash, false))
				choices << "slash=" + player->objectName();
			if (p->canUse(dismantlement, player, true))
				choices << "dismantlement=" + player->objectName();
			if (choices.isEmpty()) continue;

			QString choice = room->askForChoice(p, objectName(), choices.join("+"), QVariant::fromValue(player));
			if (choice.startsWith("slash"))
				room->useCard(CardUseStruct(slash, p, player), true);
			else
				room->useCard(CardUseStruct(dismantlement, p, player), true);
		}
		return false;
	}
};

class TenyearQianxin : public TriggerSkill
{
public:
	TenyearQianxin() : TriggerSkill("tenyearqianxin")
	{
		events << Damage;
		frequency = Wake;
		waked_skills = "tenyearjianyan";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->isWounded()) {
			LogMessage log;
			log.type = "#QianxinWake";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());

		room->doSuperLightbox(player, "tenyearqianxin");

		room->setPlayerMark(player, "tenyearqianxin", 1);
		if (room->changeMaxHpForAwakenSkill(player, -1, objectName()))
			room->acquireSkill(player, "tenyearjianyan");
		return false;
	}
};

TenyearJianyanCard::TenyearJianyanCard()
{
	target_fixed = true;
}

void TenyearJianyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QStringList choice_list, pattern_list;

	if (source->getMark("tenyearjianyan_type-PlayClear") <= 0) {
		choice_list << "basic" << "trick" << "equip";
		pattern_list << "BasicCard" << "TrickCard" << "EquipCard";
	}
	if (source->getMark("tenyearjianyan_color-PlayClear") <= 0) {
		choice_list << "red" << "black";
		pattern_list << ".|red" << ".|black";
	}
	if (choice_list.isEmpty()) return;

	QString choice = room->askForChoice(source, "tenyearjianyan", choice_list.join("+"));
	int index = choice_list.indexOf(choice);
	QString pattern = pattern_list.at(index);

	if (index <= 2 && choice_list.contains("basic"))
		room->addPlayerMark(source, "tenyearjianyan_type-PlayClear");
	else
		room->addPlayerMark(source, "tenyearjianyan_color-PlayClear");

	LogMessage log;
	log.type = "#JianyanChoice";
	log.from = source;
	log.arg = choice;
	room->sendLog(log);

	int card_id = -1;
	foreach (int id, room->getDrawPile()) {
		const Card *card = Sanguosha->getCard(id);
		if (Sanguosha->matchExpPattern(pattern, nullptr, card)) {
			card_id = id;
			break;
		}
	}
	if (card_id > -1) {
		CardsMoveStruct move(card_id, nullptr, Player::PlaceTable,
			CardMoveReason(CardMoveReason::S_REASON_TURNOVER, source->objectName(), "tenyearjianyan", ""));
		room->moveCardsAtomic(move, true);
		room->getThread()->delay();

		QList<ServerPlayer *> males;
		foreach (ServerPlayer *player, room->getAlivePlayers()) {
			if (player->isMale())
				males << player;
		}
		if (males.isEmpty() || source->isDead()) {
			DummyCard *dummy = new DummyCard();
			dummy->addSubcard(card_id);
			CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), "tenyearjianyan", "");
			room->throwCard(dummy, reason, nullptr);
			delete dummy;
		} else {
			const Card *card = Sanguosha->getCard(card_id);
			if (!room->CardInTable(card)) return;

			room->fillAG(QList<int>() << card_id, source);
			source->setMark("tenyearjianyan", card_id); // For AI
			ServerPlayer *target = room->askForPlayerChosen(source, males, "tenyearjianyan",
				QString("@jianyan-give:::%1:%2\\%3").arg(card->objectName())
				.arg(card->getSuitString() + "_char")
				.arg(card->getNumberString()));
			room->clearAG(source);
			room->giveCard(source, target, card, "tenyearjianyan", true);
		}
	} else {
		LogMessage log;
		log.type = "#TenyearjianyanSwapPile";
		room->sendLog(log);
		room->swapPile();
	}
}

class TenyearJianyan : public ZeroCardViewAsSkill
{
public:
	TenyearJianyan() : ZeroCardViewAsSkill("tenyearjianyan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("tenyearjianyan_type-PlayClear") <= 0 || player->getMark("tenyearjianyan_color-PlayClear") <= 0;
	}

	const Card *viewAs() const
	{
		return new TenyearJianyanCard;
	}
};

TenyearAnxuCard::TenyearAnxuCard()
{
}

bool TenyearAnxuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
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

bool TenyearAnxuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

void TenyearAnxuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	QList<ServerPlayer *> selecteds = targets;
	ServerPlayer *from = selecteds.first()->getHandcardNum() < selecteds.last()->getHandcardNum() ? selecteds.takeFirst() : selecteds.takeLast();
	ServerPlayer *to = selecteds.takeFirst();
	int id = room->askForCardChosen(from, to, "h", "tenyearanxu");
	const Card *cd = Sanguosha->getCard(id);
	from->obtainCard(cd);
	room->showCard(from, id);
	if (cd->getSuit() != Card::Spade)
		source->drawCards(1, "tenyearanxu");
	if (from->isAlive() && to->isAlive() && from->getHandcardNum() == to->getHandcardNum())
		room->recover(source, RecoverStruct("tenyearanxu", source));
}

class TenyearAnxu : public ZeroCardViewAsSkill
{
public:
	TenyearAnxu() : ZeroCardViewAsSkill("tenyearanxu")
	{
	}

	const Card *viewAs() const
	{
		return new TenyearAnxuCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearAnxuCard");
	}
};

class TenyearZhuiyi : public TriggerSkill
{
public:
	TenyearZhuiyi() : TriggerSkill("tenyearzhuiyi")
	{
		events << Death;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->hasSkill(objectName());
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who != player) return false;
		QList<ServerPlayer *> targets = (death.damage && death.damage->from) ? room->getOtherPlayers(death.damage->from) :
			room->getAlivePlayers();
		if (targets.isEmpty()) return false;
		int alive = room->alivePlayerCount();
		if (alive <= 0) return false;
		QString prompt = "@tenyearzhuiyi-invoke:" + QString::number(alive);
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), prompt, true, true);
		if (!target) return false;
		player->peiyin(this);
		target->drawCards(alive, objectName());
		room->recover(target, RecoverStruct("tenyearzhuiyi", player), true);
		return false;
	}
};

class TenyearJianying : public Jianying
{
public:
	TenyearJianying() : Jianying()
	{
		setObjectName("tenyearjianying");
		jianying = "TenyearJianying";
	}
};

class TenyearShibei : public MasochismSkill
{
public:
	TenyearShibei() : MasochismSkill("tenyearshibei")
	{
		frequency = Compulsory;
	}

	void onDamaged(ServerPlayer *player, const DamageStruct &) const
	{
		Room *room = player->getRoom();
		player->addMark("tenyearshibei-Clear");
		int shibei = player->getMark("tenyearshibei-Clear");
		if (shibei < 3) {
			room->sendCompulsoryTriggerLog(player, this, shibei == 2 ? 1 : 2);

			if (shibei == 1)
				room->recover(player, RecoverStruct("tenyearshibei", player));
			else if (shibei == 2)
				room->loseHp(HpLostStruct(player, 1, objectName(), player));
		}
	}
};

class TenyearZhanjueVS : public ZeroCardViewAsSkill
{
public:
	TenyearZhanjueVS() : ZeroCardViewAsSkill("tenyearzhanjue")
	{

	}

	const Card *viewAs() const
	{
		Duel *duel = new Duel(Card::SuitToBeDecided, -1);
		foreach (const Card *c, Self->getHandcards()) {
			if (Self->getMark("tenyearzhanjueIgnore_" + QString::number(c->getEffectiveId()) + "-Clear") <= 0)
				duel->addSubcard(c);
		}
		if (duel->subcardsLength() <= 0) return nullptr;
		duel->setSkillName("tenyearzhanjue");
		return duel;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		bool can_use = false;
		foreach (const Card *c, player->getHandcards()) {
			if (player->getMark("tenyearzhanjueIgnore_" + c->toString() + "-Clear") <= 0) {
				can_use = true;
				break;
			}
		}
		return player->getMark("tenyearzhanjuedraw") < 3 && can_use;
	}
};

class TenyearZhanjue : public TriggerSkill
{
public:
	TenyearZhanjue() : TriggerSkill("tenyearzhanjue")
	{
		view_as_skill = new TenyearZhanjueVS;
		events << CardFinished << DamageDone << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && damage.card->isKindOf("Duel") && damage.card->getSkillNames().contains("tenyearzhanjue") && damage.from) {
				QVariantMap m = room->getTag("tenyearzhanjue").toMap();
				QVariantList l = m.value(damage.card->toString(), QVariantList()).toList();
				l << QVariant::fromValue(damage.to);
				m[damage.card->toString()] = l;
				room->setTag("tenyearzhanjue", m);
			}
		} else if (triggerEvent == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card != nullptr && use.card->isKindOf("Duel") && use.card->getSkillNames().contains("tenyearzhanjue")) {
				QVariantMap m = room->getTag("tenyearzhanjue").toMap();
				QVariantList l = m.value(use.card->toString(), QVariantList()).toList();
				if (!l.isEmpty()) {
					QList<ServerPlayer *> l_copy;
					foreach (const QVariant &s, l)
						l_copy << s.value<ServerPlayer *>();
					l_copy << use.from;
					int n = l_copy.count(use.from);
					room->addPlayerMark(use.from, "tenyearzhanjuedraw", n);
					room->sortByActionOrder(l_copy);
					room->drawCards(l_copy, 1, objectName());
				}
				m.remove(use.card->toString());
				room->setTag("tenyearzhanjue", m);
			}
		} else if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive)
				room->setPlayerMark(player, "tenyearzhanjuedraw", 0);
		}
		return false;
	}
};

TenyearQinwangCard::TenyearQinwangCard()
{
	target_fixed = true;
}

void TenyearQinwangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QList<ServerPlayer *> targets;
	foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
		if (p->isDead() || p->getKingdom() != "shu" || p->isNude()) continue;
		const Card *c = room->askForCard(p, "Slash", "@tenyearqinwang-give:" + source->objectName(), QVariant::fromValue(source),
						Card::MethodNone);
		if (!c) continue;
		targets << p;
		room->giveCard(p, source, c, "tenyearqinwang", true);
		if (source->handCards().contains(c->getEffectiveId()) && room->hasCurrent())
			room->addPlayerMark(source, "tenyearzhanjueIgnore_" + QString::number(c->getEffectiveId()) + "-Clear");
	}
	if (targets.isEmpty() || !source->askForSkillInvoke("tenyearqinwang", "tenyearqinwang", false)) return;
	room->drawCards(targets, 1, "tenyearqinwang");
}

class TenyearQinwang : public ZeroCardViewAsSkill
{
public:
	TenyearQinwang() : ZeroCardViewAsSkill("tenyearqinwang$")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		bool shu = false;
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->getKingdom() == "shu") {
				shu = true;
				break;
			}
		}
		return !player->hasUsed("TenyearQinwangCard") && shu;
	}

	const Card *viewAs() const
	{
		return new TenyearQinwangCard;
	}
};

TenyearXiansiCard::TenyearXiansiCard()
{
will_throw = false;
handling_method = Card::MethodNone;
}

bool TenyearXiansiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearxiansi");
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

void TenyearXiansiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->throwCard(subcards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), "tenyearxiansi", ""), nullptr);
	if (source->isDead()) return;
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearxiansi");
	room->useCard(CardUseStruct(slash, source, targets), true);
	slash->deleteLater();
}

class TenyearXiansiViewAsSkill : public OneCardViewAsSkill
{
public:
	TenyearXiansiViewAsSkill() : OneCardViewAsSkill("tenyearxiansi")
	{
		expand_pile = "counter";
		filter_pattern = ".|.|.|counter";
	}

	const Card *viewAs(const Card *card) const
	{
		TenyearXiansiCard *c = new TenyearXiansiCard;
		c->addSubcard(card);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player) && !player->getPile("counter").isEmpty() && player->getPile("counter").length() > player->getHp();
	}
};

class TenyearXiansi : public PhaseChangeSkill
{
public:
	TenyearXiansi() : PhaseChangeSkill("tenyearxiansi")
	{
		view_as_skill = new TenyearXiansiViewAsSkill;
		waked_skills = "#tenyearxiansi-attach";
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Start) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (!p->isNude())
				targets << p;
		}
		if (targets.isEmpty()) return false;

		QList<ServerPlayer *> tos = room->askForPlayersChosen(player, targets, objectName(), 0, 2, "@tenyearxiansi-invoke", true);
		if (tos.isEmpty()) return false;
		player->peiyin(this, 2);

		foreach (ServerPlayer *p, tos) {
			if (player->isDead()) break;
			if (p->isDead() || p->isNude()) continue;
			int id = room->askForCardChosen(player, p, "he", objectName());
			player->addToPile("counter", id);
		}
		return false;
	}

	int getEffectIndex(const ServerPlayer *, const Card *card) const
	{
		int index = 2;
		if (card->isKindOf("Slash") && card->isVirtualCard() && card->subcardsLength() == 0)
			index = 1;
		return index;
	}
};

class TenyearXiansiAttach : public TriggerSkill
{
public:
	TenyearXiansiAttach() : TriggerSkill("#tenyearxiansi-attach")
	{
		events << GameStart << EventAcquireSkill << Debut;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if ((triggerEvent == GameStart && TriggerSkill::triggerable(player))
			|| triggerEvent == EventAcquireSkill) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (!p->hasSkill("tenyearxiansi_slash", true))
					room->attachSkillToPlayer(p, "tenyearxiansi_slash");
			}
		} else if (triggerEvent == Debut) {
			foreach (ServerPlayer *liufeng, room->findPlayersBySkillName("tenyearxiansi")) {
				if (player != liufeng && !player->hasSkill("tenyearxiansi_slash", true)) {
					room->attachSkillToPlayer(player, "tenyearxiansi_slash");
					break;
				}
			}
		}
		return false;
	}
};

TenyearXiansiSlashCard::TenyearXiansiSlashCard()
{
	m_skillName = "tenyearxiansi_slash";
}

bool TenyearXiansiSlashCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearxiansi");
	slash->deleteLater();
	return slash->targetsFeasible(targets, Self);
}

bool TenyearXiansiSlashCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearxiansi");
	slash->deleteLater();
	if (targets.isEmpty()) {
		return to_select->getPile("counter").length() >= 2 && to_select->hasSkill("tenyearxiansi")
		&& slash->targetFilter(targets, to_select, Self);
	}
	return slash->targetFilter(targets, to_select, Self);
}

const Card *TenyearXiansiSlashCard::validate(CardUseStruct &cardUse) const
{
	Room *room = cardUse.from->getRoom();

	room->throwCard(subcards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, cardUse.from->objectName(), "tenyearxiansi", ""), nullptr);

	Slash *slash = new Slash(Card::SuitToBeDecided, -1);
	slash->setSkillName("_tenyearxiansi");
	slash->deleteLater();

	return slash;
}

class TenyearXiansiSlashViewAsSkill : public ViewAsSkill
{
public:
	TenyearXiansiSlashViewAsSkill() : ViewAsSkill("tenyearxiansi_slash")
	{
		attached_lord_skill = true;
		expand_pile = "%counter";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player) && canSlashLiufeng(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return (pattern.contains("slash") || pattern.contains("Slash"))
			&& Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& canSlashLiufeng(player);
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (selected.length() >= 2)
			return false;

		foreach (const Player *p, Self->getAliveSiblings()) {
			if (p->hasSkill("tenyearxiansi") && p->getPile("counter").length() > 1) {
				return p->getPile("counter").contains(to_select->getId());
			}
		}

		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() == 2) {
			TenyearXiansiSlashCard *xs = new TenyearXiansiSlashCard;
			xs->addSubcards(cards);
			return xs;
		}
		return nullptr;
	}

private:
	static bool canSlashLiufeng(const Player *player)
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->getPile("counter").length()>1&&p->hasSkill("tenyearxiansi")) {
				Slash *slash = new Slash(Card::SuitToBeDecided, -1);
				slash->setSkillName("_tenyearxiansi");
				slash->deleteLater();
				if (slash->targetFilter(QList<const Player *>(), p, player))
					return true;
			}
		}
		return false;
	}
};

class TenyearFenli : public TriggerSkill
{
public:
	TenyearFenli() : TriggerSkill("tenyearfenli")
	{
		events << EventPhaseChanging;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		Player::Phase phase = data.value<PhaseChangeStruct>().to;
		if (player->isSkipped(phase)) return false;

		QList<Player::Phase> phases;

		if (phase == Player::Judge) {
			int hand = player->getHandcardNum();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getHandcardNum() > hand)
					return false;
			}
			if (!player->askForSkillInvoke(this, "judge")) return false;
			phases << phase;
			if (!player->isSkipped(Player::Draw))
				phases << Player::Draw;
		} else if (phase == Player::Play) {
			int hp = player->getHp();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getHp() > hp)
					return false;
			}
			if (!player->askForSkillInvoke(this, "play")) return false;
			phases << phase;
		} else if (phase == Player::Discard) {
			int equip = player->getEquips().length();
			if (equip <= 0) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getEquips().length() > equip)
					return false;
			}
			if (!player->askForSkillInvoke(this, "discard")) return false;
			phases << phase;
		} else
			return false;

		player->peiyin(this);
		foreach (Player::Phase pha, phases)
			player->skip(pha);
		return false;
	}
};

class TenyearPingkou : public TriggerSkill
{
public:
	TenyearPingkou() : TriggerSkill("tenyearpingkou")
	{
		events << EventPhaseChanging << EventPhaseSkipped;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event==EventPhaseSkipped){
			room->addPlayerMark(player, "tenyearpingkouSkipped-Clear", 1);
			return false;
		}
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		int max = player->getMark("tenyearpingkouSkipped-Clear");
		if (max <= 0) return false;
		QList<ServerPlayer *> targets = room->askForPlayersChosen(player, room->getOtherPlayers(player), objectName(), 0,
							max, "@tenyearpingkou-invoke:" + QString::number(max), true);
		if (targets.isEmpty()) return false;
		player->peiyin(this);

		foreach (ServerPlayer *p, targets)
			room->damage(DamageStruct("tenyearpingkou", player, p));

		if (max <= targets.length()) return false;

		QList<ServerPlayer *> tos;
		foreach (ServerPlayer *p, targets) {
			if (p->isAlive() && !p->getEquips().isEmpty())
				tos << p;
		}
		if (tos.isEmpty()) return false;

		ServerPlayer *to = room->askForPlayerChosen(player, tos, objectName(), "@tenyearpingkou-player");
		room->doAnimate(1, player->objectName(), to->objectName());

		QList<int> equips;
		foreach (int id, to->getEquipsId()) {
			if (to->canDiscard(to, id))
				equips << id;
		}
		if (equips.isEmpty()) return false;

		int equip = equips.at(qrand() % equips.length());
		room->throwCard(equip, to);
		return false;
	}
};

TenyearFenchengCard::TenyearFenchengCard()
{
}

void TenyearFenchengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->removePlayerMark(source, "@tenyearfenchengMark");
	room->doSuperLightbox(source, "tenyearfencheng");
	room->setTag("TenyearFenchengDiscard", 0);

	QList<ServerPlayer *> tos;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if(p==targets.first()||tos.contains(targets.first()))
			tos << p;
	}
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if(!tos.contains(p))
			tos << p;
	}
	source->setFlags("TenyearFenchengUsing");

	try {
		foreach (ServerPlayer *p, tos) {
			if (p->isAlive() && p != source) {
				room->cardEffect(this, source, p);
				room->getThread()->delay();
				if(source->isDead()) return;
			}
		}
		source->setFlags("-TenyearFenchengUsing");
	}
	catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken || triggerEvent == StageChange)
			source->setFlags("-TenyearFenchengUsing");
		throw triggerEvent;
	}
}

void TenyearFenchengCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();

	if(effect.to->canDiscard(effect.to, "he")){
		int length = room->getTag("TenyearFenchengDiscard").toInt() + 1;
		const Card *c = room->askForDiscard(effect.to, "tenyearfencheng", 999, length, true, true, "@fencheng:::" + QString::number(length));
		if (c){
			room->setTag("TenyearFenchengDiscard", c->subcardsLength());
			return;
		}
	}
	room->setTag("TenyearFenchengDiscard", 0);
	room->damage(DamageStruct("tenyearfencheng", effect.from, effect.to, 2, DamageStruct::Fire));
}

class TenyearFencheng : public ZeroCardViewAsSkill
{
public:
	TenyearFencheng() : ZeroCardViewAsSkill("tenyearfencheng")
	{
		frequency = Limited;
		limit_mark = "@tenyearfenchengMark";
	}

	const Card *viewAs() const
	{
		return new TenyearFenchengCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@tenyearfenchengMark") >= 1;
	}
};

TenyearMiejiCard::TenyearMiejiCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void TenyearMiejiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), "", "tenyearmieji", "");
	room->moveCardTo(this, source, nullptr, Player::DrawPile, reason, true);

	if (source->isDead()) return;

	QList<ServerPlayer *> targets;
	foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
		if (p->isKongcheng()) continue;
		targets << p;
	}
	if (targets.isEmpty()) return;

	ServerPlayer *t = room->askForPlayerChosen(source, targets, "tenyearmieji", "@tenyearmieji-target");
	room->doAnimate(1, source->objectName(), t->objectName());

	QList<const Card *> cards = t->getCards("he");
	QList<const Card *> cardsCopy = cards;

	foreach (const Card *c, cardsCopy) {
		if (t->isJilei(c))
			cards.removeOne(c);
	}

	if (cards.length() == 0)
		return;

	bool instanceDiscard = false;
	int instanceDiscardId = -1;

	if (cards.length() == 1)
		instanceDiscard = true;
	else if (cards.length() == 2) {
		bool bothTrick = true;
		int trickId = -1;

		foreach (const Card *c, cards) {
			if (c->getTypeId() != Card::TypeTrick)
				bothTrick = false;
			else
				trickId = c->getId();
		}

		instanceDiscard = !bothTrick;
		instanceDiscardId = trickId;
	}

	if (instanceDiscard) {
		DummyCard d;
		if (instanceDiscardId == -1)
			d.addSubcards(cards);
		else
			d.addSubcard(instanceDiscardId);
		room->throwCard(&d, t);
	} else if (!room->askForCard(t, "@@tenyearmiejidiscard!", "@mieji-discard")) {
		DummyCard d;
		qShuffle(cards);
		int trickId = -1;
		foreach (const Card *c, cards) {
			if (c->getTypeId() == Card::TypeTrick) {
				trickId = c->getId();
				break;
			}
		}
		if (trickId > -1)
			d.addSubcard(trickId);
		else {
			d.addSubcard(cards.first());
			d.addSubcard(cards.last());
		}

		room->throwCard(&d, t);
	}
}

class TenyearMieji: public OneCardViewAsSkill
{
public:
	TenyearMieji() : OneCardViewAsSkill("tenyearmieji")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->isKindOf("Weapon") || (to_select->isKindOf("TrickCard") && to_select->isBlack());
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearMiejiCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		TenyearMiejiCard *card = new TenyearMiejiCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class TenyearMiejiDiscard : public ViewAsSkill
{
public:
	TenyearMiejiDiscard() : ViewAsSkill("tenyearmiejidiscard")
	{

	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@tenyearmiejidiscard!";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (Self->isJilei(to_select))
			return false;

		if (selected.length() == 0)
			return true;
		else if (selected.length() == 1) {
			if (selected.first()->getTypeId() == Card::TypeTrick)
				return false;
			else
				return to_select->getTypeId() != Card::TypeTrick;
		} else
			return false;

		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		bool ok = false;
		if (cards.length() == 1)
			ok = cards.first()->getTypeId() == Card::TypeTrick;
		else if (cards.length() == 2) {
			ok = true;
			foreach (const Card *c, cards) {
				if (c->getTypeId() == Card::TypeTrick) {
					ok = false;
					break;
				}
			}
		}

		if (!ok)
			return nullptr;

		DummyCard *dummy = new DummyCard;
		dummy->addSubcards(cards);
		return dummy;
	}
};

TenyearMingceCard::TenyearMingceCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void TenyearMingceCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QList<ServerPlayer *> targets;
	if (Slash::IsAvailable(effect.to)) {
		foreach (ServerPlayer *p, room->getOtherPlayers(effect.to)) {
			if (effect.to->canSlash(p, false))
				targets << p;
		}
	}

	ServerPlayer *target = nullptr;
	QStringList choicelist;
	choicelist << "draw";
	if (!targets.isEmpty() && effect.from->isAlive()) {
		target = room->askForPlayerChosen(effect.from, targets, "tenyearmingce", "@dummy-slash2:" + effect.to->objectName());
		target->setFlags("TenyearMingceTarget"); // For AI
		room->doAnimate(1, effect.to->objectName(), target->objectName());

		LogMessage log;
		log.type = "#CollateralSlash";
		log.from = effect.from;
		log.to << target;
		room->sendLog(log);

		choicelist << "use";
	}

	try {
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "tenyearmingce", "");
		room->obtainCard(effect.to, this, reason);
	}
	catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken || triggerEvent == StageChange)
			if (target && target->hasFlag("TenyearMingceTarget")) target->setFlags("-TenyearMingceTarget");
		throw triggerEvent;
	}

	QString choice = room->askForChoice(effect.to, "tenyearmingce", choicelist.join("+"));
	if (target && target->hasFlag("TenyearMingceTarget")) target->setFlags("-TenyearMingceTarget");

	if (choice == "use") {
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("_tenyearmingce");
		slash->deleteLater();
		if (effect.to->canSlash(target, slash, false)) {
			room->setCardFlag(slash, QString("tenyearmingce_%1_%2").arg(effect.from->objectName()).arg(effect.to->objectName()));
			room->useCard(CardUseStruct(slash, effect.to, target));
		}
	} else if (choice == "draw") {
		QList<ServerPlayer *> drawers;
		drawers << effect.from << effect.to;
		room->sortByActionOrder(drawers);
		room->drawCards(drawers, 1, "tenyearmingce");
	}
}

class TenyearMingceVS : public OneCardViewAsSkill
{
public:
	TenyearMingceVS() : OneCardViewAsSkill("tenyearmingce")
	{
		filter_pattern = "EquipCard,Slash";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearMingceCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		TenyearMingceCard *mingceCard = new TenyearMingceCard;
		mingceCard->addSubcard(originalCard);

		return mingceCard;
	}
};

class TenyearMingce : public TriggerSkill
{
public:
	TenyearMingce() : TriggerSkill("tenyearmingce")
	{
		events << DamageComplete;
		view_as_skill = new TenyearMingceVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card || !damage.card->isKindOf("Slash") || !damage.card->hasFlag("DamageDone")) return false;

		foreach (ServerPlayer *p, room->getAllPlayers()) {
			foreach (ServerPlayer *q, room->getAllPlayers()) {
				QString flag = QString("tenyearmingce_%1_%2").arg(p->objectName()).arg(q->objectName());
				if (damage.card->hasFlag(flag)) {
					QList<ServerPlayer *> drawers;
					drawers << p << q;
					room->sortByActionOrder(drawers);
					room->drawCards(drawers, 1, objectName());
				}
			}
		}
		return false;
	}
};

class TenyearZhiyu : public TriggerSkill
{
public:
	TenyearZhiyu() : TriggerSkill("tenyearzhiyu")
	{
		events << EventPhaseChanging << EventPhaseStart << Damaged;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Damaged){
			if (player->askForSkillInvoke(this, data)) {
				player->drawCards(1, objectName());
				player->peiyin(this);
	
				if (!player->isKongcheng())
					room->showAllCards(player);

				const Card *card = nullptr;
				DamageStruct damage = data.value<DamageStruct>();
				if (damage.from && damage.from->isAlive() && !damage.from->isKongcheng())
					card = room->askForDiscard(damage.from, objectName(), 1, 1);
	
				if (player->isKongcheng()) return false;
	
				QList<const Card *> cards = player->getHandcards();
				foreach (const Card *h, cards) {
					if (h->getColor() != cards.first()->getColor())
						return false;
				}
	
				if (card && room->CardInPlace(card, Player::DiscardPile))
					room->obtainCard(player, card);
				room->addPlayerMark(player, "&tenyearzhiyu_buff-SelfClear");
			}
		}else if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			int mark = player->getMark("&tenyearzhiyu_buff-SelfClear");
			if (mark <= 0) return false;

			LogMessage log;
			log.type = "#TenyearZhiyuQice";
			log.from = player;
			log.arg = QString::number(mark);
			log.arg2 = "qice";
			room->sendLog(log);

			room->setPlayerMark(player, "SkillDescriptionArg1_qice", mark+1);
			player->setSkillDescriptionSwap("qice","%arg1",QString::number(mark+1));
			room->changeTranslation(player, "qice", 1);
		} else {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			room->setPlayerMark(player, "SkillDescriptionArg1_qice", 0);
			room->changeTranslation(player, "qice", 0);
		}
		return false;
	}
};

TenyearStStandardPackage::TenyearStStandardPackage()
	: Package("TenyearStStandard")
{
	General *tenyear_sunquan = new General(this, "tenyear_sunquan$", "wu", 4);
	tenyear_sunquan->addSkill(new TenyearZhiheng);
	tenyear_sunquan->addSkill(new TenyearJiuyuan);

	General *tenyear_sunshangxiang = new General(this, "tenyear_sunshangxiang", "wu", 3, false);
	tenyear_sunshangxiang->addSkill(new TenyearJieyin);
	tenyear_sunshangxiang->addSkill("xiaoji");

	General *tenyear_liubei = new General(this, "tenyear_liubei$", "shu", 4);
	tenyear_liubei->addSkill(new TenyearRende);
	tenyear_liubei->addSkill("jijiang");

	General *tenyear_guanyu = new General(this, "tenyear_guanyu", "shu", 4);
	tenyear_guanyu->addSkill(new TenyearWusheng);
	tenyear_guanyu->addSkill(new TenyearWushengMod);
	tenyear_guanyu->addSkill(new TenyearYijue);
	related_skills.insertMulti("tenyearwusheng", "#tenyearwushengmod");

	General *tenyear_zhangfei = new General(this, "tenyear_zhangfei", "shu", 4);
	tenyear_zhangfei->addSkill(new TenyearPaoxiao);
	tenyear_zhangfei->addSkill(new TenyearTishen);

	General *tenyear_zhugeliang = new General(this, "tenyear_zhugeliang", "shu", 3);
	tenyear_zhugeliang->addSkill(new TenyearGuanxing);
	tenyear_zhugeliang->addSkill("kongcheng");

	General *tenyear_zhaoyun = new General(this, "tenyear_zhaoyun", "shu", 4);
	tenyear_zhaoyun->addSkill("longdan");
	tenyear_zhaoyun->addSkill(new TenyearYajiao);

	General *tenyear_huangyueying = new General(this, "tenyear_huangyueying", "shu", 3, false);
	tenyear_huangyueying->addSkill(new TenyearJizhi);
	tenyear_huangyueying->addSkill("qicai");

	General *tenyear_caocao = new General(this, "tenyear_caocao$", "wei", 4);
	tenyear_caocao->addSkill(new TenyearJianxiong);
	tenyear_caocao->addSkill("hujia");

	General *tenyear_xiahoudun = new General(this, "tenyear_xiahoudun", "wei", 4);
	tenyear_xiahoudun->addSkill("ganglie");
	tenyear_xiahoudun->addSkill(new TenyearQingjian);

	General *tenyear_xuchu = new General(this, "tenyear_xuchu", "wei", 4);
	tenyear_xuchu->addSkill(new TenyearLuoyi);
	tenyear_xuchu->addSkill(new TenyearLuoyiBuff);
	related_skills.insertMulti("tenyearluoyi", "#tenyearluoyibuff");

	General *tenyear_guojia = new General(this, "tenyear_guojia", "wei", 3);
	tenyear_guojia->addSkill("tiandu");
	tenyear_guojia->addSkill(new TenyearYiji);

	General *tenyear_zhenji = new General(this, "tenyear_zhenji", "wei", 3, false);
	tenyear_zhenji->addSkill("qingguo");
	tenyear_zhenji->addSkill(new TenyearLuoshen);

	General *tenyear_zhangliao = new General(this, "tenyear_zhangliao", "wei");
	tenyear_zhangliao->addSkill(new TenyearTuxi);

	General *tenyear_huatuo = new General(this, "tenyear_huatuo", "qun", 3);
	tenyear_huatuo->addSkill("jijiu");
	tenyear_huatuo->addSkill(new TenyearQingnang);

	General *tenyear_lvbu = new General(this, "tenyear_lvbu", "qun", 5);
	tenyear_lvbu->addSkill("wushuang");
	tenyear_lvbu->addSkill(new TenyearLiyu);

	General *tenyear_diaochan = new General(this, "tenyear_diaochan", "qun", 3, false);
	tenyear_diaochan->addSkill("lijian");
	tenyear_diaochan->addSkill(new TenyearBiyue);

	General *tenyear_huaxiong = new General(this, "tenyear_huaxiong", "qun", 6);
	tenyear_huaxiong->addSkill(new TenyearYaowu);


	addMetaObject<TenyearZhihengCard>();
	addMetaObject<TenyearJieyinCard>();
	addMetaObject<TenyearRendeCard>();
	addMetaObject<TenyearYijueCard>();
	addMetaObject<TenyearQingjianCard>();
	addMetaObject<TenyearTuxiCard>();
	addMetaObject<TenyearQingnangCard>();
	addMetaObject<TenyearQimouCard>();
	addMetaObject<TenyearShensuCard>();
	addMetaObject<TenyearTianxiangCard>();
	addMetaObject<TenyearSanyaoCard>();
	addMetaObject<TenyearChunlaoCard>();
	addMetaObject<TenyearChunlaoWineCard>();
	addMetaObject<SecondTenyearChunlaoCard>();
	addMetaObject<SecondTenyearChunlaoWineCard>();
	addMetaObject<TenyearJiangchiCard>();
	addMetaObject<TenyearWurongCard>();
	addMetaObject<TenyearYanzhuCard>();
	addMetaObject<TenyearXingxueCard>();
	addMetaObject<TenyearShenduanCard>();
	addMetaObject<TenyearQiaoshuiCard>();
	addMetaObject<TenyearQiaoshuiTargetCard>();
	addMetaObject<TenyearXianzhenCard>();
	addMetaObject<SecondTenyearXianzhenCard>();
	addMetaObject<TenyearZishouCard>();
	addMetaObject<TenyearyongjinCard>();
	addMetaObject<TenyearXuanhuoCard>();
	addMetaObject<TenyearSidiCard>();
	addMetaObject<TenyearHuaiyiCard>();
	addMetaObject<TenyearHuaiyiSnatchCard>();
	addMetaObject<TenyearGongqiCard>();
	addMetaObject<TenyearJiefanCard>();
	addMetaObject<TenyearXianzhouDamageCard>();
	addMetaObject<TenyearXianzhouCard>();
	addMetaObject<TenyearShenxingCard>();
	addMetaObject<TenyearBingyiCard>();


	General *tenyear_guohuanghou = new General(this, "tenyear_guohuanghou", "wei", 3, false);
	tenyear_guohuanghou->addSkill(new TenyearJiaozhao);
	tenyear_guohuanghou->addSkill(new TenyearJiaozhaoPro);
	tenyear_guohuanghou->addSkill(new TenyearDanxin);
	related_skills.insertMulti("tenyearjiaozhao", "#tenyearjiaozhao");

	addMetaObject<TenyearZongxuanCard>();
	addMetaObject<TenyearYjYanyuCard>();
	addMetaObject<TenyearJiaozhaoCard>();
	addMetaObject<TenyearGanluCard>();
	addMetaObject<TenyearJianyanCard>();
	addMetaObject<TenyearAnxuCard>();
	addMetaObject<TenyearQinwangCard>();
	addMetaObject<TenyearXiansiCard>();
	addMetaObject<TenyearXiansiSlashCard>();
	addMetaObject<TenyearFenchengCard>();
	addMetaObject<TenyearMiejiCard>();
	addMetaObject<TenyearMingceCard>();

	skills << new TenyearJianyan << new TenyearXiansiSlashViewAsSkill << new TenyearMiejiDiscard;
}
ADD_PACKAGE(TenyearStStandard)

TenyearStWindPackage::TenyearStWindPackage()
	: Package("TenyearStWind")
{
	General *tenyear_huangzhong = new General(this, "tenyear_huangzhong", "shu", 4);
	tenyear_huangzhong->addSkill(new TenyearLiegong);
	tenyear_huangzhong->addSkill(new TenyearLiegongMod);
	related_skills.insertMulti("tenyearliegong", "#tenyearliegongmod");

	General *tenyear_weiyan = new General(this, "tenyear_weiyan", "shu", 4);
	tenyear_weiyan->addSkill(new TenyearKuanggu);
	tenyear_weiyan->addSkill(new TenyearQimou);

	General *tenyear_xiahouyuan = new General(this, "tenyear_xiahouyuan", "wei", 4);
	tenyear_xiahouyuan->addSkill(new TenyearShensu);
	tenyear_xiahouyuan->addSkill(new SlashNoDistanceLimitSkill("tenyearshensu"));
	related_skills.insertMulti("tenyearshensu", "#tenyearshensu-slash-ndl");

	General *tenyear_caoren = new General(this, "tenyear_caoren", "wei", 4);
	tenyear_caoren->addSkill(new TenyearJushou);
	tenyear_caoren->addSkill(new TenyearJiewei);

	General *tenyear_xiaoqiao = new General(this, "tenyear_xiaoqiao", "wu", 3, false);
	tenyear_xiaoqiao->addSkill(new TenyearTianxiang);
	tenyear_xiaoqiao->addSkill("hongyan");

}
ADD_PACKAGE(TenyearStWind)

TenyearStYJ2011Package::TenyearStYJ2011Package()
	: Package("TenyearStYJ2011")
{
	General *tenyear_xushu = new General(this, "tenyear_xushu", "shu", 4);
	tenyear_xushu->addSkill(new TenyearZhuhai);
	tenyear_xushu->addSkill(new TenyearQianxin);

	General *tenyear_fazheng = new General(this, "tenyear_fazheng", "shu", 3);
	tenyear_fazheng->addSkill(new TenyearEnyuan);
	tenyear_fazheng->addSkill(new TenyearXuanhuo);

	General *tenyear_masu = new General(this, "tenyear_masu", "shu", 3);
	tenyear_masu->addSkill(new TenyearSanyao);
	tenyear_masu->addSkill(new TenyearZhiman);

	General *tenyear_zhangchunhua = new General(this, "tenyear_zhangchunhua", "wei", 3, false);
	tenyear_zhangchunhua->addSkill(new Tenyearjueqing);
	tenyear_zhangchunhua->addSkill(new TenyearjueqingComplete);
	tenyear_zhangchunhua->addSkill("nosshangshi");
	related_skills.insertMulti("tenyearjueqing", "#tenyearjueqing");

	General *tenyear_yujin = new General(this, "tenyear_yujin", "wei", 4);
	tenyear_yujin->addSkill(new TenyearZhenjun);

	General *second_tenyear_yujin = new General(this, "second_tenyear_yujin", "wei", 4);
	second_tenyear_yujin->addSkill(new SecondTenyearZhenjun);

	General *tenyear_wuguotai = new General(this, "tenyear_wuguotai", "wu", 3, false);
	tenyear_wuguotai->addSkill(new TenyearGanlu);
	tenyear_wuguotai->addSkill(new TenyearBuyi);

	General *tenyear_lingtong = new General(this, "tenyear_lingtong", "wu", 4);
	tenyear_lingtong->addSkill(new TenyearXuanfeng);
	tenyear_lingtong->addSkill(new Tenyearyongjin);

	General *tenyear_xusheng = new General(this, "tenyear_xusheng", "wu", 4);
	tenyear_xusheng->addSkill(new TenyearPojun);

	General *tenyear_chengong = new General(this, "tenyear_chengong", "qun", 3);
	tenyear_chengong->addSkill(new TenyearMingce);
	tenyear_chengong->addSkill("zhichi");

	General *tenyear_gaoshun = new General(this, "tenyear_gaoshun", "qun", 4);
	tenyear_gaoshun->addSkill(new TenyearXianzhen("tenyearxianzhen"));
	tenyear_gaoshun->addSkill(new TenyearXianzhenSlash("tenyearxianzhen"));
	tenyear_gaoshun->addSkill(new TenyearXianzhenTargetMod("tenyearxianzhen"));
	tenyear_gaoshun->addSkill("jinjiu");
	related_skills.insertMulti("tenyearxianzhen", "#tenyearxianzhen-slash");
	related_skills.insertMulti("tenyearxianzhen", "#tenyearxianzhen-target");

	General *second_tenyear_gaoshun = new General(this, "second_tenyear_gaoshun", "qun", 4);
	second_tenyear_gaoshun->addSkill(new TenyearXianzhen("secondtenyearxianzhen"));
	second_tenyear_gaoshun->addSkill(new TenyearXianzhenSlash("secondtenyearxianzhen"));
	second_tenyear_gaoshun->addSkill(new TenyearXianzhenTargetMod("secondtenyearxianzhen"));
	second_tenyear_gaoshun->addSkill(new SecondTenyearJinjiu);
	second_tenyear_gaoshun->addSkill(new SecondTenyearJinjiuLimit);
	related_skills.insertMulti("secondtenyearxianzhen", "#secondtenyearxianzhen-slash");
	related_skills.insertMulti("secondtenyearxianzhen", "#secondtenyearxianzhen-target");
	related_skills.insertMulti("secondtenyearjinjiu", "#secondtenyearjinjiu-limit");
}
ADD_PACKAGE(TenyearStYJ2011)

TenyearStYJ2012Package::TenyearStYJ2012Package()
	: Package("TenyearStYJ2012")
{
	General *tenyear_liaohua = new General(this, "tenyear_liaohua", "shu", 4);
	tenyear_liaohua->addSkill(new TenyearDangxian);
	tenyear_liaohua->addSkill(new TenyearFuli);

	General *tenyear_madai = new General(this, "tenyear_madai", "shu", 4);
	tenyear_madai->addSkill(new TenyearQianxi);
	tenyear_madai->addSkill(new TenyearQianxiDraw);
	tenyear_madai->addSkill(new TenyearQianxiLimit);
	tenyear_madai->addSkill("mashu");
	related_skills.insertMulti("tenyearqianxi", "#tenyearqianxi-draw");
	related_skills.insertMulti("tenyearqianxi", "#tenyearqianxi-limit");

	General *tenyear_wangyi = new General(this, "tenyear_wangyi", "wei", 4, false);
	tenyear_wangyi->addSkill("zhenlie");
	tenyear_wangyi->addSkill("secondmiji");

	General *tenyear_caozhang = new General(this, "tenyear_caozhang", "wei", 4);
	tenyear_caozhang->addSkill(new TenyearJiangchi);

	General *second_tenyear_caozhang = new General(this, "second_tenyear_caozhang", "wei", 4);
	second_tenyear_caozhang->addSkill(new SecondTenyearJiangchi);
	second_tenyear_caozhang->addSkill(new SecondTenyearJiangchiClear);
	second_tenyear_caozhang->addSkill(new SecondTenyearJiangchiMod);
	related_skills.insertMulti("secondtenyearjiangchi", "#secondtenyearlihuo-clear");
	related_skills.insertMulti("secondtenyearjiangchi", "#secondtenyearjiangchi-target");

	General *tenyear_xunyou = new General(this, "tenyear_xunyou", "wei", 3);
	tenyear_xunyou->addSkill("qice");
	tenyear_xunyou->addSkill(new TenyearZhiyu);

	General *tenyear_bulianshi = new General(this, "tenyear_bulianshi", "wu", 3, false);
	tenyear_bulianshi->addSkill(new TenyearAnxu);
	tenyear_bulianshi->addSkill(new TenyearZhuiyi);

	General *tenyear_chengpu = new General(this, "tenyear_chengpu", "wu", 4);
	tenyear_chengpu->addSkill("lihuo");
	tenyear_chengpu->addSkill(new TenyearChunlao);

	General *second_tenyear_chengpu = new General(this, "second_tenyear_chengpu", "wu", 4);
	second_tenyear_chengpu->addSkill(new SecondTenyearLihuo);
	second_tenyear_chengpu->addSkill(new SecondTenyearLihuoTargetMod);
	second_tenyear_chengpu->addSkill(new SecondTenyearChunlao);
	related_skills.insertMulti("secondtenyearlihuo", "#secondtenyearlihuo-target");

	General *tenyear_handang = new General(this, "tenyear_handang", "wu", 4);
	tenyear_handang->addSkill(new TenyearGongqi);
	tenyear_handang->addSkill(new TenyearGongqiTargetMod);
	tenyear_handang->addSkill(new TenyearJiefan);
	related_skills.insertMulti("tenyeargongqi", "#tenyeargongqi-target");

	General *tenyear_liubiao = new General(this, "tenyear_liubiao", "qun", 3);
	tenyear_liubiao->addSkill(new TenyearZishou);
	tenyear_liubiao->addSkill(new TenyearZongshi);
	tenyear_liubiao->addSkill(new TenyearZongshiProtect);
	related_skills.insertMulti("tenyearzongshi", "#tenyearzongshi-protect");





}
ADD_PACKAGE(TenyearStYJ2012)

TenyearStYJ2013Package::TenyearStYJ2013Package()
	: Package("TenyearStYJ2013")
{
	General *tenyear_guanping = new General(this, "tenyear_guanping", "shu", 4);
	tenyear_guanping->addSkill(new TenyearJiezhong);
	tenyear_guanping->addSkill(new TenyearLongyin);

	General *tenyear_liufeng = new General(this, "tenyear_liufeng", "shu", 4);
	tenyear_liufeng->addSkill(new TenyearXiansi);
	tenyear_liufeng->addSkill(new TenyearXiansiAttach);

	General *tenyear_jianyong = new General(this, "tenyear_jianyong", "shu", 3);
	tenyear_jianyong->addSkill(new TenyearQiaoshui);
	tenyear_jianyong->addSkill(new TenyearQiaoshuiTargetMod);
	tenyear_jianyong->addSkill("zongshih");
	related_skills.insertMulti("tenyearqiaoshui", "#tenyearqiaoshui-target");

	General *tenyear_guohuai = new General(this, "tenyear_guohuai", "wei", 4);
	tenyear_guohuai->addSkill(new TenyearJingce);

	General *second_tenyear_guohuai = new General(this, "second_tenyear_guohuai", "wei", 4);
	second_tenyear_guohuai->addSkill(new SecondTenyearJingce);
	second_tenyear_guohuai->addSkill("#tenyearjingce-record");
	related_skills.insertMulti("secondtenyearjingce", "#tenyearjingce-record");

	General *tenyear_zhuran = new General(this, "tenyear_zhuran", "wu", 4);
	tenyear_zhuran->addSkill(new TenyearDanshou);

	General *tenyear_panzhangmazhong = new General(this, "tenyear_panzhangmazhong", "wu", 4);
	tenyear_panzhangmazhong->addSkill(new TenyearDuodao);
	tenyear_panzhangmazhong->addSkill(new TenyearAnjian);
	tenyear_panzhangmazhong->addSkill(new TenyearAnjianEffect);
	related_skills.insertMulti("tenyearanjian", "#tenyearanjian-effect");

	General *tenyear_yufan = new General(this, "tenyear_yufan", "wu", 3);
	tenyear_yufan->addSkill(new TenyearZongxuan);
	tenyear_yufan->addSkill(new TenyearZhiyan);

	General *tenyear_fuhuanghou = new General(this, "tenyear_fuhuanghou", "qun", 3, false);
	tenyear_fuhuanghou->addSkill(new TenyearZhuikong);
	tenyear_fuhuanghou->addSkill(new TenyearZhuikongProhibit);
	tenyear_fuhuanghou->addSkill(new TenyearQiuyuan);
	related_skills.insertMulti("tenyearzhuikong", "#tenyearzhuikong");

	General *tenyear_liru = new General(this, "tenyear_liru", "qun", 3);
	tenyear_liru->addSkill("juece");
	tenyear_liru->addSkill(new TenyearFencheng);
	tenyear_liru->addSkill(new TenyearMieji);






}
ADD_PACKAGE(TenyearStYJ2013)

TenyearStYJ2014Package::TenyearStYJ2014Package()
	: Package("TenyearStYJ2014")
{
	General *tenyear_wuyi = new General(this, "tenyear_wuyi", "shu", 4);
	tenyear_wuyi->addSkill(new TenyearBenxi);

	General *tenyear_zhoucang = new General(this, "tenyear_zhoucang", "shu", 4);
	tenyear_zhoucang->addSkill(new TenyearZhongyong);

	General *tenyear_caozhen = new General(this, "tenyear_caozhen", "wei", 4);
	tenyear_caozhen->addSkill(new TenyearSidi);
	tenyear_caozhen->addSkill(new TenyearSidiLimit);
	related_skills.insertMulti("tenyearsidi", "#tenyearsidi-limit");

	General *tenyear_hanhaoshihuan = new General(this, "tenyear_hanhaoshihuan", "wei", 4);
	tenyear_hanhaoshihuan->addSkill(new TenyearShenduan);
	tenyear_hanhaoshihuan->addSkill(new TenyearShenduanTargetMod);
	tenyear_hanhaoshihuan->addSkill(new TenyearYonglve);
	related_skills.insertMulti("tenyearshenduan", "#tenyearshenduan-target");

	General *tenyear_sunluban = new General(this, "tenyear_sunluban", "wu", 3, false);
	tenyear_sunluban->addSkill(new TenyearZenhui);
	tenyear_sunluban->addSkill(new TenyearJiaojin);

	General *tenyear_zhuhuan = new General(this, "tenyear_zhuhuan", "wu", 4);
	tenyear_zhuhuan->addSkill(new TenyearFenli);
	tenyear_zhuhuan->addSkill(new TenyearPingkou);

	General *tenyear_guyong = new General(this, "tenyear_guyong", "wu", 3);
	tenyear_guyong->addSkill(new TenyearShenxing);
	tenyear_guyong->addSkill(new TenyearBingyi);

	General *tenyear_caifuren = new General(this, "tenyear_caifuren", "qun", 3, false);
	tenyear_caifuren->addSkill(new TenyearQieting);
	tenyear_caifuren->addSkill(new TenyearXianzhou);

	General *tenyear_jushou = new General(this, "tenyear_jushou", "qun", 3);
	tenyear_jushou->addSkill(new TenyearJianying);
	tenyear_jushou->addSkill(new TenyearShibei);

}
ADD_PACKAGE(TenyearStYJ2014)

TenyearStYJ2015Package::TenyearStYJ2015Package()
	: Package("TenyearStYJ2015")
{
	General *tenyear_xiahoushi = new General(this, "tenyear_xiahoushi", "shu", 3, false);
	tenyear_xiahoushi->addSkill(new TenyearQiaoshi);
	tenyear_xiahoushi->addSkill(new TenyearYjYanyu);

	General *tenyear_liuchen = new General(this, "tenyear_liuchen$", "shu", 4);
	tenyear_liuchen->addSkill(new TenyearZhanjue);
	tenyear_liuchen->addSkill(new TenyearQinwang);

	General *tenyear_zhangyi = new General(this, "tenyear_zhangyi", "shu", 4);
	tenyear_zhangyi->addSkill(new TenyearWurong);
	tenyear_zhangyi->addSkill("shizhi");

	General *second_tenyear_zhangyi = new General(this, "second_tenyear_zhangyi", "shu", 5);
	second_tenyear_zhangyi->addSkill("tenyearwurong");
	second_tenyear_zhangyi->addSkill(new SecondTenyearShizhi);
	second_tenyear_zhangyi->addSkill(new SecondTenyearShizhiTrigger);
	related_skills.insertMulti("secondtenyearshizhi", "#secondtenyearshizhi");

	General *tenyear_caoxiu = new General(this, "tenyear_caoxiu", "wei", 4);
	tenyear_caoxiu->addSkill(new TenyearQingxi);
	tenyear_caoxiu->addSkill(new TenyearQingxiDamage);
	tenyear_caoxiu->addSkill("qianju");
	related_skills.insertMulti("tenyearqingxi", "#tenyearqingxi-damage");

	General *tenyear_quancong = new General(this, "tenyear_quancong", "wu", 4);
	tenyear_quancong->addSkill(new TenyearYaoming);

	General *tenyear_sunxiu = new General(this, "tenyear_sunxiu$", "wu", 3);
	tenyear_sunxiu->addSkill(new TenyearYanzhu);
	tenyear_sunxiu->addSkill(new TenyearXingxue);
	tenyear_sunxiu->addSkill("zhaofu");

	General *tenyear_gongsunyuan = new General(this, "tenyear_gongsunyuan", "qun", 4);
	tenyear_gongsunyuan->addSkill(new TenyearHuaiyi);

	General *tenyear_guotupangji = new General(this, "tenyear_guotupangji", "qun", 3);
	tenyear_guotupangji->addSkill(new TenyearJigong);
	tenyear_guotupangji->addSkill(new TenyearJigongMax);
	tenyear_guotupangji->addSkill(new TenyearJigongRecover);
	tenyear_guotupangji->addSkill("shifei");
	related_skills.insertMulti("tenyearjigong", "#tenyearjigong");
	related_skills.insertMulti("tenyearjigong", "#tenyearjigong-recover");










}
ADD_PACKAGE(TenyearStYJ2015)
