#include "wisdom.h"
//#include "skill.h"
//#include "client.h"
#include "engine.h"
#include "settings.h"
#include "room.h"
#include "maneuvering.h"
//#include "general.h"
#include "roomthread.h"

JuaoCard::JuaoCard()
{
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

class JuaoViewAsSkill : public ViewAsSkill
{
public:
	JuaoViewAsSkill() :ViewAsSkill("juao")
	{

	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("JuaoCard");
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (selected.length() >= 2)
			return false;
		return !to_select->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != 2)
			return nullptr;

		JuaoCard *card = new JuaoCard();
		card->addSubcards(cards);
		return card;
	}
};

class Juao : public PhaseChangeSkill
{
public:
	Juao() :PhaseChangeSkill("juao")
	{
		view_as_skill = new JuaoViewAsSkill;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getMark("juao") > 0;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() == Player::Start) {
			player->setMark("juao", 0);

			LogMessage log;
			log.type = "#JuaoObtain";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);

			DummyCard *dummy = new DummyCard;
			dummy->addSubcards(player->getPile("hautain"));
			player->obtainCard(dummy, false);
			delete dummy;

			player->skip(Player::Draw);
		}
		return false;
	}
};

class Tanlan : public TriggerSkill
{
public:
	Tanlan() :TriggerSkill("tanlan")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent, Room* room, ServerPlayer *xuyou, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		ServerPlayer *from = damage.from;
		if (from && xuyou->canPindian(from) && room->askForSkillInvoke(xuyou, objectName(), data)) {
			room->broadcastSkillInvoke(objectName(), 1);
			room->notifySkillInvoked(xuyou, objectName());
			PindianStruct *pindian = xuyou->PinDian(from, objectName());
			if (!pindian) return false;
			if (pindian->from_number <= pindian->to_number || !pindian->from || pindian->from->isDead() || !pindian->from->hasSkill(objectName())) return false;
			DummyCard *dummy = new DummyCard();
			int from_card_id = pindian->from_card->getEffectiveId();
			int to_card_id = pindian->to_card->getEffectiveId();
			if (room->getCardPlace(from_card_id) == Player::DiscardPile)
				dummy->addSubcard(from_card_id);
			if (room->getCardPlace(to_card_id) == Player::DiscardPile && to_card_id != from_card_id)
				dummy->addSubcard(to_card_id);
			if (!dummy->getSubcards().isEmpty())
				pindian->from->obtainCard(dummy);
			delete dummy;
		}
		return false;
	}
};

class Shicai : public TriggerSkill
{
public:
	Shicai() :TriggerSkill("shicai")
	{
		events << Pindian;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room* room, ServerPlayer *player, QVariant &data) const
	{
		if (player == nullptr) return false;
		ServerPlayer *xuyou = room->findPlayerBySkillName(objectName());
		if (!xuyou) return false;
		PindianStruct *pindian = data.value<PindianStruct *>();
		if (pindian->from != xuyou && pindian->to != xuyou)
			return false;
		ServerPlayer *winner = pindian->from_number > pindian->to_number ? pindian->from : pindian->to;
		if (winner == xuyou) {
			LogMessage log;
			log.type = "#TriggerSkill";
			log.from = xuyou;
			log.arg = objectName();
			room->sendLog(log);
			int index = 1;
			if (pindian->reason == "tanlan")
				index = 2;

			room->broadcastSkillInvoke(objectName(), index);
			room->notifySkillInvoked(xuyou, objectName());

			xuyou->drawCards(1,objectName());
		}
		return false;
	}
};

class Yicai :public TriggerSkill
{
public:
	Yicai() :TriggerSkill("yicai")
	{
		events << CardUsed;
	}

	bool trigger(TriggerEvent, Room* room, ServerPlayer *jiangwei, QVariant &data) const
	{
		const Card *card = data.value<CardUseStruct>().card;

		if (card && card->isNDTrick()) {
			//if(room->askForSkillInvoke(jiangwei, objectName(), data))
			jiangwei->setFlags("yicairesponding");
			try {
				room->askForUseCard(jiangwei, "slash", "@askforslash");
			}
			catch (TriggerEvent errorevent) {
				if (errorevent == TurnBroken || errorevent == StageChange)
					if (jiangwei->hasFlag("yicairesponding"))
						jiangwei->setFlags("-yicairesponding");

				throw errorevent;
			}
			jiangwei->setFlags("-yicairesponding");
		} else if (card && card->isKindOf("Slash") && data.value<CardUseStruct>().from->hasFlag("yicairesponding")) {
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(jiangwei, objectName());
			room->setPlayerFlag(data.value<CardUseStruct>().from, "-yicairesponding");
		}
		return false;
	}
};

class Beifa : public TriggerSkill
{
public:
	Beifa() :TriggerSkill("beifa")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room* room, ServerPlayer *jiangwei, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from == jiangwei && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard) {
			Slash *slash = new Slash(Card::NoSuit, 0);
			slash->setSkillName(objectName());
			QList<ServerPlayer *> players;
			foreach (ServerPlayer *player, room->getOtherPlayers(jiangwei)) {
				if (jiangwei->canSlash(player, slash, false))
					players << player;
			}
			ServerPlayer *target = room->askForPlayerChosen(jiangwei, players, objectName());

			if (target == nullptr && !jiangwei->isProhibited(jiangwei, slash))
				target = jiangwei;
			slash->deleteLater();
			if(target)
				room->useCard(CardUseStruct(slash,jiangwei,target));
		}
		return false;
	}
};

HouyuanCard::HouyuanCard()
{
}

void HouyuanCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->drawCards(2);
}

class Houyuan : public ViewAsSkill
{
public:
	Houyuan() :ViewAsSkill("houyuan")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return !to_select->isEquipped() && selected.length() < 2;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("HouyuanCard");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != 2)
			return nullptr;
		HouyuanCard *card = new HouyuanCard;
		card->addSubcards(cards);
		return card;
	}
};

class Chouliang : public PhaseChangeSkill
{
public:
	Chouliang() :PhaseChangeSkill("chouliang")
	{
		frequency = Frequent;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		int handcardnum = player->getHandcardNum();
		if (player->getPhase() == Player::Finish && handcardnum < 3
			&& room->askForSkillInvoke(player, objectName())) {
			room->notifySkillInvoked(player, objectName());
			int x = 4 - handcardnum;
			QList<int> ids = room->getNCards(x, false);
			CardsMoveStruct move;
			move.card_ids = ids;
			move.to_place = Player::PlaceTable;
			move.reason = CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), "");
			room->moveCardsAtomic(move, true);
			room->getThread()->delay(2 * Config.AIDelay);

			QList<int> card_to_throw;
			QList<int> card_to_gotback;
			for (int i = 0; i < x; i++) {
				if (!Sanguosha->getCard(ids[i])->isKindOf("BasicCard"))
					card_to_throw << ids[i];
				else
					card_to_gotback << ids[i];
			}
			if (!card_to_gotback.isEmpty()) {
				room->broadcastSkillInvoke(objectName());
				DummyCard *dummy2 = new DummyCard;
				foreach(int id, card_to_gotback)
					dummy2->addSubcard(id);

				CardMoveReason reason(CardMoveReason::S_REASON_GOTBACK, player->objectName());
				room->obtainCard(player, dummy2, reason);
				dummy2->deleteLater();
			}
			if (!card_to_throw.isEmpty()) {
				DummyCard *dummy = new DummyCard;
				foreach(int id, card_to_throw)
					dummy->addSubcard(id);

				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), "");
				room->throwCard(dummy, reason, nullptr);
				dummy->deleteLater();
			}

		}
		return false;
	}
};

BawangCard::BawangCard()
{
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

class BawangViewAsSkill : public ZeroCardViewAsSkill
{
public:
	BawangViewAsSkill() :ZeroCardViewAsSkill("bawang")
	{
		response_pattern = "@@bawang";
	}

	const Card *viewAs() const
	{
		return new BawangCard;
	}
};

class Bawang : public TriggerSkill
{
public:
	Bawang() :TriggerSkill("bawang")
	{
		view_as_skill = new BawangViewAsSkill;
		events << CardOffset;
	}

	bool trigger(TriggerEvent, Room* room, ServerPlayer *sunce, QVariant &data) const
	{
		CardEffectStruct effect = data.value<CardEffectStruct>();
		if (effect.card->isKindOf("Slash")||(!effect.to->isNude()&&!sunce->isKongcheng()&&!effect.to->isKongcheng())) {
			if (room->askForSkillInvoke(sunce, objectName(), data)) {
				room->broadcastSkillInvoke(objectName(), 1);
				room->notifySkillInvoked(sunce, objectName());
				bool success = sunce->pindian(effect.to, objectName(), nullptr);
				if (success) {
					if (sunce->hasFlag("drank"))
						room->setPlayerFlag(sunce, "-drank");
					room->askForUseCard(sunce, "@@bawang", "@bawang");
				} else
					room->broadcastSkillInvoke(objectName(), 3);
			}
		}
		return false;
	}

	int getEffectIndex(const ServerPlayer *, const Card *card) const
	{
		if (!card->isKindOf("Slash"))
			return 2;
		return 0;
	}
};

WeidaiCard::WeidaiCard()
{
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

class Weidai :public ZeroCardViewAsSkill
{
public:
	Weidai() :ZeroCardViewAsSkill("weidai$")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return hasWuGenerals(player) && player->hasLordSkill("weidai")
			&& !player->hasFlag("Global_WeidaiFailed") && Analeptic::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return hasWuGenerals(player) && pattern == "peach+analeptic" && !player->hasFlag("Global_WeidaiFailed");
	}

	const Card *viewAs() const
	{
		return new WeidaiCard;
	}

private:
	static bool hasWuGenerals(const Player *player)
	{
		foreach(const Player *p, player->getSiblings())
			if (p->isAlive() && p->getKingdom() == "wu")
				return true;
		return false;
	}
};

class Longluo : public TriggerSkill
{
public:
	Longluo() : TriggerSkill("longluo")
	{
		events << CardsMoveOneTime << EventPhaseStart;
	}

	bool trigger(TriggerEvent triggerEvent, Room* room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase() == Player::Finish) {
				int drawnum = player->getMark(objectName());
				if (drawnum > 0) {
					ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@longluo", true, true);
					if (target != nullptr) {
						room->broadcastSkillInvoke(objectName());
						target->drawCards(drawnum);
					}
				}
			} else if (player->getPhase() == Player::NotActive) {
				room->setPlayerMark(player, objectName(), 0);
			}
		} else if (player->getPhase() == Player::Discard) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
				room->setPlayerMark(player, objectName(), player->getMark(objectName()) + move.card_ids.length());
			}
		}
		return false;
	}
};

FuzuoCard::FuzuoCard()
{
}

bool FuzuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty() && to_select->hasFlag("fuzuo_target");
}

void FuzuoCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->getRoom()->setPlayerMark(effect.to, "fuzuo", this->getNumber());
}

class FuzuoViewAsSkill : public OneCardViewAsSkill
{
public:
	FuzuoViewAsSkill() : OneCardViewAsSkill("fuzuo")
	{
		response_pattern = "@@fuzuo";
		filter_pattern = ".|.|~7|hand!";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		FuzuoCard *card = new FuzuoCard;
		card->addSubcard(originalCard->getId());
		return card;
	}
};

class Fuzuo : public TriggerSkill
{
public:
	Fuzuo() : TriggerSkill("fuzuo")
	{
		events << PindianVerifying;
		view_as_skill = new FuzuoViewAsSkill;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room* room, ServerPlayer *, QVariant &data) const
	{
		ServerPlayer *zhangzhao = room->findPlayerBySkillName(objectName());
		if (!zhangzhao) return false;
		PindianStruct * pindian = data.value<PindianStruct *>();
		room->setPlayerFlag(pindian->from, "fuzuo_target");
		room->setPlayerFlag(pindian->to, "fuzuo_target");
		room->setTag("FuzuoPindianData", data);
		if (room->askForUseCard(zhangzhao, "@@fuzuo", "@fuzuo-pindian", -1, Card::MethodDiscard)) {
			bool isFrom = (pindian->from->getMark(objectName()) > 0);

			LogMessage log;
			log.type = "$Fuzuo";

			if (isFrom) {
				int to_add = pindian->from->getMark(objectName()) / 2;
				room->setPlayerMark(pindian->from, objectName(), 0);
				pindian->from_number += to_add;

				log.from = pindian->from;
				log.arg = QString::number(pindian->from_number);
			} else {
				int to_add = pindian->to->getMark(objectName()) / 2;
				room->setPlayerMark(pindian->to, objectName(), 0);
				pindian->to_number += to_add;

				log.from = pindian->to;
				log.arg = QString::number(pindian->to_number);
			}

			room->sendLog(log);
		}

		room->setPlayerFlag(pindian->from, "-fuzuo_target");
		room->setPlayerFlag(pindian->to, "-fuzuo_target");

		room->removeTag("FuzuoPindianData");
		return false;
	}
};

class Jincui : public TriggerSkill
{
public:
	Jincui() :TriggerSkill("jincui")
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
		if (death.who != player)
			return false;
		QList<ServerPlayer *> targets = room->getAlivePlayers();
		if (targets.isEmpty())
			return false;

		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@jincui", true, true);
		if (target == nullptr)
			return false;
		//room->broadcastSkillInvoke(objectName());

		QVariant t_data = QVariant::fromValue((ServerPlayer *)target);
		if (room->askForChoice(player, objectName(), "draw+throw", t_data) == "draw") {
			room->broadcastSkillInvoke(objectName(), 1);
			target->drawCards(3);
		} else {
			room->broadcastSkillInvoke(objectName(), 2);
			room->askForDiscard(target, objectName(), 3, 3, false, true);
		}
		return false;
	}
};

class Badao : public TriggerSkill
{
public:
	Badao() :TriggerSkill("badao")
	{
		events << TargetConfirmed << CardUsed;
	}

	bool trigger(TriggerEvent triggerEvent, Room* room, ServerPlayer *hua, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (triggerEvent == TargetConfirmed) {
			if (use.card->isKindOf("Slash") && use.card->isBlack() && use.to.contains(hua)) {
				hua->setFlags("badaoresponding");
				try {
					room->askForUseCard(hua, "slash", "@askforslash");
				}
				catch (TriggerEvent errorevent) {
					if (errorevent == TurnBroken || errorevent == StageChange) {
						if (hua->hasFlag("badaoresponding"))
							hua->setFlags("-badaoresponding");
					}
					throw errorevent;
				}
				hua->setFlags("-badaoresponding");
			}
		} else {
			if (use.card->isKindOf("Slash") && use.from->hasFlag("badaoresponding")) {
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(hua, "badao");
				room->setPlayerFlag(use.from, "-badaoresponding");
			}
		}

		return false;
	}
};

class Wenjiu : public TriggerSkill
{
public:
	Wenjiu() :TriggerSkill("wenjiu")
	{
		events << ConfirmDamage << TargetConfirmed;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent triggerEvent, Room* room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && damage.card->isKindOf("Slash") && damage.card->isBlack()) {
				room->sendCompulsoryTriggerLog(player,this, 1);
				LogMessage log;
				log.type = "#Wenjiu2";
				log.from = player;
				log.to << damage.to;
				log.arg = QString::number(damage.damage);
				log.arg2 = QString::number(++damage.damage);
				room->sendLog(log);
				data = QVariant::fromValue(damage);
			}
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash") && use.card->isRed() && use.to.contains(player)) {
				QVariantList jink_list = use.from->tag["Jink_" + use.card->toString()].toList();
				room->sendCompulsoryTriggerLog(player,this, 2);
				int index = use.to.indexOf(player);
				LogMessage log;
				log.type = "#NoJink";
				log.from = player;
				room->sendLog(log);
				jink_list.replace(index, QVariant(0));
				use.from->tag["Jink_" + use.card->toString()] = jink_list;
			}
		}
		return false;
	}
};

class Shipo : public TriggerSkill
{
public:
	Shipo() :TriggerSkill("shipo")
	{
		events << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room* room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Judge || player->getJudgingArea().length() == 0)
			return false;
		foreach (ServerPlayer *tianfeng, room->findPlayersBySkillName(objectName())) {
			if (tianfeng->getCardCount(true) >= 2
			&& room->askForDiscard(tianfeng, objectName(), 2, 2, true, true, "@shipo:"+player->objectName(),".",objectName())) {
				room->broadcastSkillInvoke(objectName());
				DummyCard *dummy = new DummyCard;
				dummy->addSubcards(player->getJudgingArea());
				tianfeng->obtainCard(dummy);
				delete dummy;
				break;
			}
		}
		return false;
	}
};

class Gushou :public TriggerSkill
{
public:
	Gushou() :TriggerSkill("gushou")
	{
		frequency = Frequent;
		events << CardUsed << CardResponded;
	}

	bool trigger(TriggerEvent triggerEvent, Room* room, ServerPlayer *tianfeng, QVariant &data) const
	{
		if (room->getCurrent() == tianfeng)
			return false;
		const Card * card = nullptr;
		if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			card = use.card;
		} else if (triggerEvent == CardResponded)
			card = data.value<CardResponseStruct>().m_card;

		if (card && card->isKindOf("BasicCard")) {
			if (room->askForSkillInvoke(tianfeng, objectName(), data)) {
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(tianfeng, objectName());
				tianfeng->drawCards(1,objectName());
			}
		}

		return false;
	}
};

class Yuwen : public TriggerSkill
{
public:
	Yuwen() :TriggerSkill("yuwen")
	{
		events << GameOverJudge;
		frequency = Compulsory;
	}

	int getPriority(TriggerEvent) const
	{
		return 4;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->hasSkill(objectName());
	}

	bool trigger(TriggerEvent, Room* room, ServerPlayer *tianfeng, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();

		if (death.damage) {
			if (death.damage->from == tianfeng)
				return false;
		} else {
			death.damage = new DamageStruct;
			death.damage->to = tianfeng;
			data = QVariant::fromValue(death);
		}

		death.damage->from = tianfeng;

		room->broadcastSkillInvoke(objectName());

		LogMessage log;
		log.type = "#TriggerSkill";
		log.from = tianfeng;
		log.arg = objectName();
		room->sendLog(log);
		room->notifySkillInvoked(tianfeng, objectName());

		return false;
	}
};

ShouyeCard::ShouyeCard()
{
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

class Shouye : public OneCardViewAsSkill
{
public:
	Shouye() :OneCardViewAsSkill("shouye")
	{
		filter_pattern = ".|red|.|hand!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("jiehuo")<1||!player->hasUsed("ShouyeCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		ShouyeCard *shouye_card = new ShouyeCard;
		shouye_card->addSubcard(originalCard->getId());

		return shouye_card;
	}
};

class Jiehuo : public TriggerSkill
{
public:
	Jiehuo() :TriggerSkill("jiehuo")
	{
		events << CardFinished;
		frequency = Wake;
		waked_skills = "shien";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool trigger(TriggerEvent, Room* room, ServerPlayer *player, QVariant &) const
	{
		if (player->getMark("@shouye") >= 7) {
			LogMessage log;
			log.type = "#JiehuoWake";
			log.from = player;
			log.arg = objectName();
			log.arg2 = "shouye";
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		//room->doLightbox("$JiehuoAnimate", 3000);
		room->doSuperLightbox(player, "jiehuo");

		room->setPlayerMark(player, "jiehuo", 1);

		if (room->changeMaxHpForAwakenSkill(player, -1, objectName())) {
			room->acquireSkill(player, "shien");
		}
		room->setPlayerMark(player, "@shouye", 0);
		return false;
	}
};

class Shien :public TriggerSkill
{
public:
	Shien() :TriggerSkill("shien")
	{
		events << CardUsed << CardResponded;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && !target->hasSkill(objectName());
	}

	bool trigger(TriggerEvent triggerEvent, Room* room, ServerPlayer *player, QVariant &data) const
	{
		if (player == nullptr) return false;
		if (player->getMark("forbid_shien") > 0 || player->hasFlag("forbid_shien"))
			return false;
		const Card * card = nullptr;
		if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			card = use.card;
		} else if (triggerEvent == CardResponded)
			card = data.value<CardResponseStruct>().m_card;

		if (card && card->isNDTrick()) {
			ServerPlayer *shuijing = room->findPlayerBySkillName(objectName());
			if (!shuijing)
				return false;

			if (room->askForSkillInvoke(player, objectName(), QVariant::fromValue((ServerPlayer *)shuijing))) {
				room->broadcastSkillInvoke(objectName());
				LogMessage log;
				log.type = "#InvokeOthersSkill";
				log.from = player;
				log.to << shuijing;
				log.arg = objectName();
				room->sendLog(log);
				room->notifySkillInvoked(shuijing, objectName());
				shuijing->drawCards(1,objectName());
			} else {
				QString choice = room->askForChoice(player, "forbid_shien", "yes+no+maybe");
				if (choice == "yes")
					room->setPlayerMark(player, "forbid_shien", 1);
				else if (choice == "maybe")
					room->setPlayerFlag(player, "forbid_shien");
			}
		}

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