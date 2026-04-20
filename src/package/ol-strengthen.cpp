#include "ol-strengthen.h"
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
//#include "json.h"
#include "wind.h"
//#include "mountain.h"
//#include "ai.h"
#include "exppattern.h"
#include "clientstruct.h"

class OLHujia : public Hujia
{
public:
	OLHujia() : Hujia("olhujia")
	{
	}
};

class OLHujiaDraw : public TriggerSkill
{
public:
	OLHujiaDraw() : TriggerSkill("#olhujia$")
	{
		events << CardUsed << CardResponded;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->getKingdom() == "wei" && !target->hasFlag("CurrentPlayer");
	}

	QList<ServerPlayer *> getCaocaos(ServerPlayer *player) const
	{
		QList<ServerPlayer *> caocaos;
		foreach (ServerPlayer *p, player->getRoom()->getOtherPlayers(player)) {
			if (!p->hasLordSkill("olhujia") || p->getMark("olhujia-Clear") > 0) continue;
			caocaos << p;
		}
		return caocaos;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		const Card *card = nullptr;
		if (event == CardUsed)
			card = data.value<CardUseStruct>().card;
		else
			card = data.value<CardResponseStruct>().m_card;
		if (!card || !card->isKindOf("Jink")) return false;

		QList<ServerPlayer *> caocaos = getCaocaos(player);
		while (player->isAlive() && !caocaos.isEmpty()) {
			ServerPlayer *drawer = room->askForPlayerChosen(player, caocaos, objectName(), "@olhujia-draw", true);
			if (!drawer) break;
			room->doAnimate(1, player->objectName(), drawer->objectName());
			drawer->addMark("olhujia-Clear");
			LogMessage log;
			log.type = "#InvokeOthersSkill";
			log.from = player;
			log.to << drawer;
			log.arg = drawer->isWeidi() ? "weidi" : "olhujia";
			room->sendLog(log);
			if (drawer->isWeidi())
				room->broadcastSkillInvoke("weidi");
			else {
				int r = 1 + qrand() % 2;
				room->broadcastSkillInvoke("hujia", r);
			}
			room->notifySkillInvoked(drawer, log.arg);
			drawer->drawCards(1, "olhujia");
			caocaos = getCaocaos(player);
		}
		return false;
	}
};

OLJijiangCard::OLJijiangCard() : JijiangCard("oljijiang")
{
	mute = true;
}

class OLJijiangVS : public ZeroCardViewAsSkill
{
public:
	OLJijiangVS() : ZeroCardViewAsSkill("oljijiang$")
	{
	}

	bool hasShuGenerals(const Player *player) const
	{
		foreach(const Player *p, player->getAliveSiblings())
			if (p->getKingdom() == "shu")
				return true;
		return false;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return hasShuGenerals(player) && !player->hasFlag("Global_JijiangFailed") && Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return hasShuGenerals(player)
			&& (pattern.contains("slash") || pattern.contains("Slash") || pattern == "@jijiang")
			&& Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& !player->hasFlag("Global_JijiangFailed");
	}

	const Card *viewAs() const
	{
		return new OLJijiangCard;
	}
};

class OLJijiang : public TriggerSkill
{
public:
	OLJijiang() : TriggerSkill("oljijiang$")
	{
		events << CardAsked << CardUsed << CardResponded;
		view_as_skill = new OLJijiangVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	QList<ServerPlayer *> getLiubeis(ServerPlayer *liubei) const
	{
		QList<ServerPlayer *> liubeis;
		Room *room = liubei->getRoom();
		foreach (ServerPlayer *p, room->getOtherPlayers(liubei)) {
			if (p->isDead() || !p->hasLordSkill(this) || p->getMark("oljijiang-Clear") > 0) continue;
			liubeis << p;
		}
		return liubeis;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *liubei, QVariant &data) const
	{
		if (event == CardAsked) {
			if (!liubei->hasLordSkill("oljijiang")) return false;
			QStringList patterns = data.toStringList();
			if (patterns.first() != "slash" || patterns.at(1).contains("jijiang-slash:") || patterns.at(2) != "response")
				return false;
			QList<ServerPlayer *> lieges = room->getLieges("shu", liubei);
			if (lieges.isEmpty())
				return false;
			if (!liubei->hasFlag("qinwangjijiang") && !room->askForSkillInvoke(liubei, objectName(), data))
				return false;
			if (!liubei->isLord() && liubei->hasSkill("weidi"))
				room->broadcastSkillInvoke("weidi");
			else {
				int r = 1 + qrand() % 2;
				if (!liubei->hasInnateSkill("jijiang") && liubei->getMark("ruoyu") > 0)
					r += 2;
				else if (liubei->isJieGeneral())
					r += 4;
				room->broadcastSkillInvoke("jijiang", r);
			}
			foreach (ServerPlayer *liege, lieges) {
				const Card *slash = room->askForCard(liege, "slash", "@oljijiang-slash:" + liubei->objectName(),
					QVariant(), Card::MethodResponse, liubei, false, "", true);
				if (slash) {
					room->setCardFlag(slash,"YUANBEN");
					room->provide(slash);
					return true;
				}
			}
		} else {
			if (liubei->hasFlag("CurrentPlayer") || liubei->getKingdom() != "shu") return false;
			const Card *card = nullptr;
			if (event == CardUsed)
				card = data.value<CardUseStruct>().card;
			else
				card = data.value<CardResponseStruct>().m_card;
			if (!card || !card->isKindOf("Slash")) return false;
			QList<ServerPlayer *> liubeis = getLiubeis(liubei);
			while (liubei->isAlive() && !liubeis.isEmpty()) {
				ServerPlayer *drawer = room->askForPlayerChosen(liubei, liubeis, objectName(), "@oljijiang-draw", true);
				if (!drawer) break;
				room->doAnimate(1, liubei->objectName(), drawer->objectName());
				room->addPlayerMark(drawer, "oljijiang-Clear");
				LogMessage log;
				log.type = "#InvokeOthersSkill";
				log.from = liubei;
				log.to << drawer;
				log.arg = drawer->isWeidi() ? "weidi" : objectName();
				room->sendLog(log);
				if (drawer->isWeidi())
					room->broadcastSkillInvoke("weidi");
				else {
					int r = 1 + qrand() % 2;
					if (!drawer->hasInnateSkill("jijiang") && drawer->getMark("ruoyu") > 0)
						r += 2;
					else if (drawer->isJieGeneral())
						r += 4;
					room->broadcastSkillInvoke("jijiang", r);
				}
				room->notifySkillInvoked(drawer, objectName());
				drawer->drawCards(1, objectName());
				liubeis = getLiubeis(liubei);
			}
		}
		return false;
	}
};

class OLPaoxiao : public TriggerSkill
{
public:
	OLPaoxiao() : TriggerSkill("olpaoxiao")
	{
		frequency = Compulsory;
		events << DamageCaused << CardOffset;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardOffset) {
			if (!player->hasSkill(objectName())) return false;
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (!effect.card->isKindOf("Slash")) return false;
			room->addPlayerMark(player, "&olpaoxiao_missed-Clear");
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash")) return false;
			int n = player->getMark("&olpaoxiao_missed-Clear");
			if (n <= 0) return false;
			room->setPlayerMark(player, "&olpaoxiao_missed-Clear", 0);
			LogMessage log;
			log.type = "#OLpaoxiaoDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = QString::number(damage.damage);
			damage.damage += n;
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class OLPaoxiaoMod : public TargetModSkill
{
public:
	OLPaoxiaoMod() : TargetModSkill("#olpaoxiaomod")
	{
	}

	int getResidueNum(const Player *from, const Card *, const Player *) const
	{
		if (from->hasSkill("olpaoxiao"))
			return 1000;
		return 0;
	}/*
	int getDistanceLimit(const Player *from, const Card *card, const Player *) const
	{
		if (from->getMark(""))
			return 1000;
		return 0;
	}*/
};

class OLTishen : public PhaseChangeSkill
{
public:
	OLTishen() : PhaseChangeSkill("oltishen")
	{
		frequency = Limited;
		limit_mark = "@oltishenMark";
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Start || player->getMark("@oltishenMark") <= 0) return false;
		if (player->getLostHp() <= 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "oltishen");
		room->removePlayerMark(player, "@oltishenMark");
		int x = player->getMaxHp() - player->getHp();
		if (x > 0) {
			room->recover(player, RecoverStruct(player, nullptr, x, "oltishen"));
			player->drawCards(x, objectName());
		}
		return false;
	}
};

class OLLongdan : public OneCardViewAsSkill
{
public:
	OLLongdan() : OneCardViewAsSkill("ollongdan")
	{
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->isWounded() || Slash::IsAvailable(player) || Analeptic::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return (pattern.contains("slash") || pattern.contains("Slash"))
				|| pattern == "jink"
				|| (pattern.contains("peach") && player->getMark("Global_PreventPeach") == 0)
				|| pattern == "analeptic";
	}

	bool viewFilter(const Card *to_select) const
	{
		const Card *card = to_select;

		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			if (Slash::IsAvailable(Self) && Analeptic::IsAvailable(Self) && Self->isWounded())
				return card->isKindOf("Jink") || card->isKindOf("Peach") || card->isKindOf("Analeptic");
			if (Slash::IsAvailable(Self) && Analeptic::IsAvailable(Self))
				return card->isKindOf("Jink") || card->isKindOf("Peach");
			if (Slash::IsAvailable(Self) && Self->isWounded())
				return card->isKindOf("Jink") || card->isKindOf("Analeptic");
			if (Analeptic::IsAvailable(Self) && Self->isWounded())
				return card->isKindOf("Peach") || card->isKindOf("Analeptic");
			if (Analeptic::IsAvailable(Self))
				return card->isKindOf("Peach");
			if (Slash::IsAvailable(Self))
				return card->isKindOf("Jink");
			if (Self->isWounded())
				return card->isKindOf("Analeptic");
		} else {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if (pattern.contains("slash") || pattern.contains("Slash"))
				return card->isKindOf("Jink");
			else if (pattern == "peach+analeptic") {
				if (Self->getMark("Global_PreventPeach") > 0)
					return card->isKindOf("Peach");
				return card->isKindOf("Peach") || card->isKindOf("Analeptic");
			} else if (pattern == "peach") {
				if (Self->getMark("Global_PreventPeach") == 0)
					return card->isKindOf("Analeptic");
			} else if(pattern == "analeptic")
				return card->isKindOf("Peach");
			else if (pattern == "jink")
				return card->isKindOf("Slash");
		}
		return false;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (originalCard->isKindOf("Slash")) {
			Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
			jink->addSubcard(originalCard);
			jink->setSkillName(objectName());
			return jink;
		} else if (originalCard->isKindOf("Jink")) {
			Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
			slash->addSubcard(originalCard);
			slash->setSkillName(objectName());
			return slash;
		} else if (originalCard->isKindOf("Peach")) {
			Analeptic *ana = new Analeptic(originalCard->getSuit(), originalCard->getNumber());
			ana->addSubcard(originalCard);
			ana->setSkillName(objectName());
			return ana;
		} else if (originalCard->isKindOf("Analeptic")) {
			Peach *peach = new Peach(originalCard->getSuit(), originalCard->getNumber());
			peach->addSubcard(originalCard);
			peach->setSkillName(objectName());
			return peach;
		} else
			return nullptr;
	}
};

class OLYajiao : public TriggerSkill
{
public:
	OLYajiao() : TriggerSkill("olyajiao")
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
		if (cardstar->getTypeId()>0 && isHandcard && room->askForSkillInvoke(player, objectName(), data)) {
			room->broadcastSkillInvoke(objectName());
			QList<int> ids = room->getNCards(1, false);
			CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
				CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), "olyajiao", ""));
			room->moveCardsAtomic(move, true);
			int id = ids.first();
			if (room->getCardPlace(id) == Player::PlaceTable)
				room->returnToTopDrawPile(ids);

			const Card *card = Sanguosha->getCard(id);
			player->setMark("olyajiao", id); // For AI
			if (card->getTypeId() == cardstar->getTypeId()) {
				room->fillAG(ids, player);
				ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@olyajiao-give", true, true);
				room->clearAG(player);
				if (!target) return false;
				room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
				CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "olyajiao", "");
				room->obtainCard(target, card, reason, true);
			} else {
				QList<ServerPlayer *> targets;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (p->inMyAttackRange(player) && player->canDiscard(p, "hej"))
						targets << p;
				}
				ServerPlayer *target = room->askForPlayerChosen(player, targets, "olyajiao_discard", "@olyajiao-discard", true, true);
				if (!target) return false;
				room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
				int card_id = room->askForCardChosen(player, target, "hej", objectName(), false, Card::MethodDiscard);
				room->throwCard(Sanguosha->getCard(card_id), room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? nullptr : target, player);
			}
		}
		return false;
	}
};

class OLLeiji : public TriggerSkill
{
public:
	OLLeiji() : TriggerSkill("olleiji")
	{
		events << CardResponded << CardUsed << FinishJudge;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == FinishJudge) {
			JudgeStruct *judge = data.value<JudgeStruct *>();
			if (Sanguosha->translate(judge->reason) == "暴虐") return false;
			if (judge->card->getSuit() != Card::Club && judge->card->getSuit() != Card::Spade) return false;
			int n = 2;
			if (judge->card->getSuit() == Card::Club) {
				room->recover(player, RecoverStruct("olleiji", player));
				n = 1;
			}
			if (player->isDead()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@olleiji-invoke:" + QString::number(n),
										false, true);
			room->damage(DamageStruct(objectName(), player, target, n, DamageStruct::Thunder));
		} else {
			const Card *card = nullptr;
			if (event == CardUsed)
				card = data.value<CardUseStruct>().card;
			else
				card = data.value<CardResponseStruct>().m_card;
			if (card && (card->isKindOf("Jink") || card->isKindOf("Lightning"))) {
				if (!player->askForSkillInvoke(this)) return false;
				room->broadcastSkillInvoke(objectName());
				JudgeStruct judge;
				judge.pattern = ".|black";
				judge.good = true;
				judge.reason = objectName();
				judge.who = player;
				room->judge(judge);
			}
		}
		return false;
	}
};

class OLGuidao : public TriggerSkill
{
public:
	OLGuidao() : TriggerSkill("olguidao")
	{
		events << AskForRetrial;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		if (!TriggerSkill::triggerable(target))
			return false;

		if (target->isKongcheng()) {
			bool has_black = false;
			for (int i = 0; i < 5; i++) {
				const EquipCard *equip = target->getEquip(i);
				if (equip && equip->isBlack()) {
					has_black = true;
					break;
				}
			}
			return has_black;
		} else
			return true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		JudgeStruct *judge = data.value<JudgeStruct *>();
		QStringList prompt_list;
		prompt_list << "@olguidao-card" << judge->who->objectName()
			<< objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
		QString prompt = prompt_list.join(":");

		const Card *card = room->askForCard(player, ".|black", prompt, QVariant::fromValue(judge), Card::MethodResponse, judge->who, true, objectName());
		if (!card) return false;
		room->broadcastSkillInvoke(objectName());
		room->retrial(card, player, judge, objectName(), true);
		if (card->getSuit() == Card::Spade && card->getNumber() >= 2 && card->getNumber() <= 9)
			player->drawCards(1, objectName());
		return false;
	}
};

OLHuangtianCard::OLHuangtianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	mute = true;
}

void OLHuangtianCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *zhangjiao, use.to) {
		if (zhangjiao->isWeidi()){
			room->broadcastSkillInvoke("weidi",-1,zhangjiao);
			room->notifySkillInvoked(zhangjiao, "weidi");
		}else
			room->broadcastSkillInvoke("huangtian",qrand()%2+5,zhangjiao);
		use.from->skillInvoked("olhuangtian",0,zhangjiao);

		room->giveCard(use.from, zhangjiao, this, "olhuangtian", true);
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

bool OLHuangtianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&& to_select != Self && to_select->hasLordSkill("olhuangtian");
}

class OLHuangtianViewAsSkill : public OneCardViewAsSkill
{
public:
	OLHuangtianViewAsSkill() :OneCardViewAsSkill("olhuangtian_attach")
	{
		attached_lord_skill = true;
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->isKindOf("Jink") || (to_select->getSuit() == Card::Spade && !to_select->isEquipped());
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getKingdom() == "qun"&&player->usedTimes("OLHuangtianCard")<1;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		OLHuangtianCard *card = new OLHuangtianCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class OLHuangtian : public TriggerSkill
{
public:
	OLHuangtian() : TriggerSkill("olhuangtian$")
	{
		events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (triggerEvent == EventAcquireSkill&&player->hasLordSkill(this,true)) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("olhuangtian_attach",true)){
					room->attachSkillToPlayer(p, "olhuangtian_attach");
				}
			}
		}else if(player->getPhase()!=Player::Play)
			return false;
		if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasLordSkill(this,true)){
					room->attachSkillToPlayer(player, "olhuangtian_attach");
					break;
				}
			}
		}else{
			if (player->hasSkill("olhuangtian_attach",true))
				room->detachSkillFromPlayer(player, "olhuangtian_attach", true);
		}
		return false;
	}
};

OLGuhuoCard::OLGuhuoCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool OLGuhuoCard::olguhuo(ServerPlayer *yuji) const
{
	Room *room = yuji->getRoom();
	room->setTag("OLGuhuoType", user_string);

	QList<ServerPlayer *> questioned,aps = room->getOtherPlayers(yuji);
	foreach (ServerPlayer *player, aps) {
		QString choice = "noquestion+question";
		if (player->hasSkill("chanyuan")) {
			room->sendCompulsoryTriggerLog(player, "chanyuan", true, true);
			choice = "noquestion";
		}
		choice = room->askForChoice(player, "olguhuo", choice, QVariant::fromValue(yuji));
		if (choice == "question"){
			room->setEmotion(player, "question");
			questioned << player;
		}else
			room->setEmotion(player, "no-question");
		LogMessage log;
		log.type = "#GuhuoQuery";
		log.from = player;
		log.arg = choice;
		room->sendLog(log);
	}

	LogMessage log;
	log.type = "$GuhuoResult";
	log.from = yuji;
	log.card_str = QString::number(subcards.first());
	room->sendLog(log);

	QList<CardsMoveStruct> moves;
	bool success = false;
	if (questioned.isEmpty()) {
		success = true;
		CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "olguhuo");
		CardsMoveStruct move(subcards, yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
		moves.append(move);
		room->moveCardsAtomic(moves, true);
	} else {
		const Card *card = Sanguosha->getCard(subcards.first());
		if (user_string == "peach+analeptic")
			success = card->objectName() == yuji->getTag("OLGuhuoSaveSelf").toString();
		else if (user_string == "slash")
			success = card->objectName().contains("slash");
		else if (user_string == "normal_slash")
			success = card->objectName() == "slash";
		else
			success = card->match(user_string);
		if (success) {
			CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "olguhuo");
			CardsMoveStruct move(subcards, yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
			moves.append(move);
			room->moveCardsAtomic(moves, true);
		} else {
			room->moveCardTo(this, yuji, nullptr, Player::DiscardPile,
				CardMoveReason(CardMoveReason::S_REASON_PUT, yuji->objectName(), "", "olguhuo"), true);
		}
	}
	foreach (ServerPlayer *player, aps) {
		room->setEmotion(player, ".");
		if(questioned.contains(player)){
			if(success){
				if (!player->canDiscard(player, "he")||!room->askForDiscard(player, "olguhuo", 1, 1, true, true, "olguhuo-discard"))
					room->loseHp(HpLostStruct(player, 1, "olguhuo", yuji));
				if (player->isAlive())
					room->acquireSkill(player, "chanyuan");
			}else{
				player->drawCards(1, "olguhuo");
			}
		}
	}
	yuji->removeTag("OLGuhuoSaveSelf");
	yuji->removeTag("OLGuhuoSlash");
	room->addPlayerMark(yuji, "olguhuo-Clear");
	return success;
}

bool OLGuhuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}

	const Card *_card = Self->getTag("olguhuo").value<const Card *>();
	if (_card == nullptr)
		return false;

	card = Sanguosha->cloneCard(_card);
	card->setCanRecast(false);
	card->deleteLater();
	return card->targetFilter(targets, to_select, Self);
}

bool OLGuhuoCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetFixed();
	}

	const Card *_card = Self->getTag("olguhuo").value<const Card *>();
	if (_card == nullptr)
		return false;

	card = Sanguosha->cloneCard(_card);
	card->setCanRecast(false);
	card->deleteLater();
	return card->targetFixed();
}

bool OLGuhuoCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}

	const Card *_card = Self->getTag("olguhuo").value<const Card *>();
	if (_card == nullptr)
		return false;

	card = Sanguosha->cloneCard(_card);
	card->setCanRecast(false);
	card->deleteLater();
	return card->targetsFeasible(targets, Self);
}

const Card *OLGuhuoCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer *yuji = card_use.from;
	Room *room = yuji->getRoom();

	QString to_guhuo = user_string;
	if ((user_string.contains("slash") || (user_string.contains("Slash")))
		&& Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		QStringList guhuo_list;
		static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
		foreach (const Slash *slash, slashs) {
			QString name = slash->objectName();
			if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
			guhuo_list << name;
		}

		if (guhuo_list.isEmpty())
			guhuo_list << "slash";
		to_guhuo = room->askForChoice(yuji, "olguhuo_slash", guhuo_list.join("+"));
		yuji->setTag("OLGuhuoSlash", QVariant(to_guhuo));
	}
	room->broadcastSkillInvoke("olguhuo");

	LogMessage log;
	log.type = card_use.to.isEmpty() ? "#GuhuoNoTarget" : "#Guhuo";
	log.from = yuji;
	log.to = card_use.to;
	log.arg = to_guhuo;
	log.arg2 = "olguhuo";

	room->sendLog(log);

	if (olguhuo(card_use.from)) {
		Card *card = Sanguosha->getCard(subcards.first());
		Card *use_card;
		if (to_guhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_guhuo = card->objectName();
		} else if (to_guhuo == "normal_slash")
			to_guhuo = "slash";
		if (to_guhuo.startsWith(card->objectName()))
			use_card = card;
		else{
			use_card = Sanguosha->cloneCard(to_guhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("olguhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
		}

		foreach (ServerPlayer *to, card_use.to) {
			const Skill *skill = room->isProhibited(card_use.from, to, use_card);
			if (skill) {
				log.from = to;
				log.type = "#SkillAvoid";
				if (skill->isVisible()) {
					log.arg = skill->objectName();
					log.arg2 = use_card->objectName();
					room->sendLog(log);

					room->broadcastSkillInvoke(skill->objectName());
					room->notifySkillInvoked(to, skill->objectName());
				} else {
					skill = Sanguosha->getMainSkill(skill->objectName());
					if (skill && skill->isVisible()) {
						log.arg = skill->objectName();
						if (to->hasSkill(skill)) {
							log.arg2 = objectName();
							room->sendLog(log);

							room->broadcastSkillInvoke(skill->objectName());
							room->notifySkillInvoked(to, skill->objectName());
						} else if (yuji->hasSkill(skill)) {
							log.type = "#SkillAvoidFrom";
							log.from = yuji;
							log.to.clear();
							log.to << to;
							log.arg2 = objectName();
							room->sendLog(log);

							room->broadcastSkillInvoke(skill->objectName());
							room->notifySkillInvoked(yuji, skill->objectName());
						}
					}
				}
				card_use.to.removeOne(to);
			}
		}
		return use_card;
	}
	return nullptr;
}

const Card *OLGuhuoCard::validateInResponse(ServerPlayer *yuji) const
{
	Room *room = yuji->getRoom();
	room->broadcastSkillInvoke("olguhuo");

	QString to_guhuo;
	if (user_string == "peach+analeptic") {
		QStringList guhuo_list;
		static QList<const Peach *> peachs = Sanguosha->findChildren<const Peach *>();
		foreach (const Peach *peach, peachs) {
			QString name = peach->objectName();
			if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(peach->getPackage())) continue;
			guhuo_list << name;
			break;
		}
		static QList<const Analeptic *> anas = Sanguosha->findChildren<const Analeptic *>();
		foreach (const Analeptic *ana, anas) {
			QString name = ana->objectName();
			if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(ana->getPackage())) continue;
			guhuo_list << name;
			break;
		}

		if (guhuo_list.isEmpty())
			guhuo_list << "peach";
		to_guhuo = room->askForChoice(yuji, "olguhuo_saveself", guhuo_list.join("+"));
		yuji->setTag("OLGuhuoSaveSelf", QVariant(to_guhuo));
	} else if (user_string.contains("slash") || user_string.contains("Slash")) {
		QStringList guhuo_list;
		static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
		foreach (const Slash *slash, slashs) {
			QString name = slash->objectName();
			if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
			guhuo_list << name;
		}

		if (guhuo_list.isEmpty())
			guhuo_list << "slash";
		to_guhuo = room->askForChoice(yuji, "olguhuo_slash", guhuo_list.join("+"));
		yuji->setTag("OLGuhuoSlash", QVariant(to_guhuo));
	} else
		to_guhuo = user_string;

	LogMessage log;
	log.type = "#GuhuoNoTarget";
	log.from = yuji;
	log.arg = to_guhuo;
	log.arg2 = "olguhuo";
	room->sendLog(log);

	if (olguhuo(yuji)) {
		Card *card = Sanguosha->getCard(subcards.first());
		if (to_guhuo == "slash" && card->isKindOf("Slash"))
			to_guhuo = card->objectName();
		else if (to_guhuo == "normal_slash")
			to_guhuo = "slash";
		
		if (to_guhuo.startsWith(card->objectName())||card->objectName().startsWith(to_guhuo))
			return card;
		else{
			Card *use_card = Sanguosha->cloneCard(to_guhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("olguhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
			return use_card;
		}
	}
	return nullptr;
}

class OLGuhuo : public OneCardViewAsSkill
{
public:
	OLGuhuo() : OneCardViewAsSkill("olguhuo")
	{
		filter_pattern = ".|.|.|hand";
		response_or_use = true;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		bool current = false;
		QList<const Player *> players = player->getAliveSiblings();
		players.append(player);
		foreach (const Player *p, players) {
			if (p->getPhase() != Player::NotActive) {
				current = true;
				break;
			}
		}
		if (!current) return false;

		if (player->isKongcheng() || player->getMark("olguhuo-Clear") > 0
			|| pattern.startsWith(".") || pattern.startsWith("@"))
			return false;
		if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
		for (int i = 0; i < pattern.length(); i++) {
			QChar ch = pattern[i];
			if (ch.isUpper() || ch.isDigit()) return false; // This is an extremely dirty hack!! For we need to prevent patterns like 'BasicCard'
		}
		return true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		bool current = false;
		foreach (const Player *p, player->getAliveSiblings(true)) {
			if (p->getPhase() != Player::NotActive) {
				current = true;
				break;
			}
		}
		if (!current) return false;
		return !player->isKongcheng() && player->getMark("olguhuo-Clear") <= 0;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
			|| Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
			OLGuhuoCard *card = new OLGuhuoCard;
			card->setUserString(Sanguosha->currentRoomState()->getCurrentCardUsePattern());
			card->addSubcard(originalCard);
			return card;
		}

		const Card *c = Self->getTag("olguhuo").value<const Card *>();
		if (c) {
			OLGuhuoCard *card = new OLGuhuoCard;
			if (!c->objectName().contains("slash"))
				card->setUserString(c->objectName());
			else
				card->setUserString(Self->getTag("OLGuhuoSlash").toString());
			card->addSubcard(originalCard);
			return card;
		}
		return nullptr;
	}

	QDialog *getDialog() const
	{
		return GuhuoDialog::getInstance("olguhuo");
	}

	bool isEnabledAtNullification(const ServerPlayer *player) const
	{
		ServerPlayer *current = player->getRoom()->getCurrent();
		if (!current || current->isDead() || current->getPhase() == Player::NotActive) return false;
		return (!player->isKongcheng() || !player->getHandPile().isEmpty()) && player->getMark("olguhuo-Clear") <= 0;
	}
};

class OLShebian : public TriggerSkill
{
public:
	OLShebian() : TriggerSkill("olshebian")
	{
		events << TurnedOver;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (!room->canMoveField("e")) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		room->moveField(player, objectName(), false, "e");
		return false;
	}
};

OLQimouCard::OLQimouCard()
{
	target_fixed = true;
}

void OLQimouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@olqimouMark");
	room->doSuperLightbox(source, "olqimou");
	QStringList choices;
	for (int i = 1; i <= source->getHp(); i++) {
		choices << QString::number(i);
	}
	QString choice = room->askForChoice(source, "olqimou", choices.join("+"));
	int n = choice.toInt();
	room->loseHp(HpLostStruct(source, n, "olqimou", source));
	if (source->isAlive()) {
		source->drawCards(n, "olqimou");
		room->addDistance(source, -n);
		room->addSlashCishu(source, n);
	}
}

class OLQimou : public ZeroCardViewAsSkill
{
public:
	OLQimou() : ZeroCardViewAsSkill("olqimou")
	{
		frequency = Limited;
		limit_mark = "@olqimouMark";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getHp() > 0 && player->getMark("@olqimouMark") > 0;
	}

	const Card *viewAs() const
	{
		return new OLQimouCard;
	}
};

OLTianxiangCard::OLTianxiangCard() : TenyearTianxiangCard("oltianxiang")
{
		handling_method = Card::MethodDiscard;
}

class OLTianxiangVS : public OneCardViewAsSkill
{
public:
	OLTianxiangVS() : OneCardViewAsSkill("oltianxiang")
	{
		filter_pattern = ".|heart";
		response_pattern = "@@oltianxiang";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		OLTianxiangCard *tianxiangCard = new OLTianxiangCard;
		tianxiangCard->addSubcard(originalCard);
		return tianxiangCard;
	}
};

class OLTianxiang : public TriggerSkill
{
public:
	OLTianxiang() : TriggerSkill("oltianxiang")
	{
		events << DamageInflicted;
		view_as_skill = new OLTianxiangVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *xiaoqiao, QVariant &) const
	{
		if (xiaoqiao->canDiscard(xiaoqiao, "he")) {
			return room->askForUseCard(xiaoqiao, "@@oltianxiang", "@oltianxiang", -1, Card::MethodDiscard);
		}
		return false;
	}

	int getEffectIndex(const ServerPlayer *player, const Card *) const
	{
		int index = qrand() % 2 + 1;
		if (!player->hasInnateSkill(this) && player->hasSkill("olluoyan"))
			index += 2;

		return index;
	}
};

class OLHongyan : public FilterSkill
{
public:
	OLHongyan() : FilterSkill("olhongyan")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->getSuit() == Card::Spade;
	}

	const Card *viewAs(const Card *original) const
	{
		Card *new_card = Sanguosha->cloneCard(original->objectName(),Card::Heart,original->getNumber());
		new_card->setSkillName("olhongyan");
		return new_card;
	}

	int getEffectIndex(const ServerPlayer *, const Card *) const
	{
		return -2;
	}
};

class OLHongyanKeep : public MaxCardsSkill
{
public:
	OLHongyanKeep() : MaxCardsSkill("#olhongyan-keep")
	{
	}

	int getFixed(const Player *target) const
	{
		if (target->hasSkill("olhongyan")) {
			foreach (const Card *c, target->getEquips()) {
				if (c->getSuit() == Card::Heart)
					return target->getMaxHp();
			}
		}
		return -1;
	}
};

class OLPiaoling : public TriggerSkill
{
public:
	OLPiaoling() : TriggerSkill("olpiaoling")
	{
		events << EventPhaseChanging;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to != Player::NotActive) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		JudgeStruct judge;
		judge.pattern = ".|heart";
		judge.who = player;
		judge.reason = objectName();
		room->judge(judge);

		if (judge.isBad() || player->isDead()) return false;
		if (!room->CardInPlace(judge.card, Player::DiscardPile)) return false;
		player->setTag("olpiaoling", QVariant::fromValue(judge.card)); // For AI
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
			"@olpiaoling-invoke:" + judge.card->objectName(), true);
		if (target) {
			room->doAnimate(1, player->objectName(), target->objectName());
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "olpiaoling", "");
			room->obtainCard(target, judge.card, reason, true);
			if (player->isDead() || target != player || !player->canDiscard(player, "he")) return false;
			room->askForDiscard(player, objectName(), 1, 1, false, true);
		} else {
			LogMessage log;
			log.type = "$PutCard2";
			log.from = player;
			log.card_str = judge.card->toString();
			room->sendLog(log);
			CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), "olpiaoling", "");
			CardsMoveStruct move(judge.card->getEffectiveId(), nullptr, nullptr, Player::PlaceJudge, Player::DrawPile, reason);
			room->moveCardsAtomic(move, true);
		}
		return false;
	}
};

class OLLuanjiVS : public ViewAsSkill
{
public:
	OLLuanjiVS() : ViewAsSkill("olluanji")
	{
		response_or_use = true;
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (selected.isEmpty())
			return !to_select->isEquipped();
		else if (selected.length() == 1) {
			const Card *card = selected.first();
			return !to_select->isEquipped() && to_select->getSuit() == card->getSuit();
		} else
			return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() == 2) {
			ArcheryAttack *aa = new ArcheryAttack(Card::SuitToBeDecided, 0);
			aa->addSubcards(cards);
			aa->setSkillName(objectName());
			return aa;
		} else
			return nullptr;
	}
};

class OLLuanji : public TriggerSkill
{
public:
	OLLuanji() : TriggerSkill("olluanji")
	{
		events << CardUsed;
		view_as_skill = new OLLuanjiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("ArcheryAttack") || use.to.length() < 2) return false;
		player->setTag("olluanji_data", data);
		ServerPlayer *remove = room->askForPlayerChosen(player, use.to, objectName(), "@olluanji-remove", true);
		player->removeTag("olluanji_data");
		if (!remove) return false;
		room->broadcastSkillInvoke(objectName());
		LogMessage log;
		log.type = "#QiaoshuiRemove";
		log.from = player;
		log.to << remove;
		log.card_str = use.card->toString();
		log.arg = "olluanji";
		room->sendLog(log);
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), remove->objectName());
		room->notifySkillInvoked(player, objectName());
		use.to.removeOne(remove);
		data = QVariant::fromValue(use);
		return false;
	}
};

class OLXueyi : public TriggerSkill
{
public:
	OLXueyi(const QString &xueyi) : TriggerSkill(xueyi + "$"), xueyi(xueyi)
	{
		events << GameStart << EventPhaseStart;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (!player->hasLordSkill(this)) return false;
		if (event == GameStart) {
			int qun = 0;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (p->getKingdom() == "qun")
					qun++;
			}
			if (qun == 0) return false;
			QString name;
			if (!player->isWeidi())
				name = objectName();
			else
				name = "weidi";
			room->sendCompulsoryTriggerLog(player, name, true, true);
			player->gainMark("&olyi", xueyi == "olxueyi" ? qun : 2 * qun);
		} else {
			if ((xueyi == "olxueyi" && player->getPhase() == Player::RoundStart) ||
					(xueyi == "secondolxueyi" && player->getPhase() == Player::Play)) {
				if (player->getMark("&olyi") <= 0 || !player->askForSkillInvoke(this)) return false;
				room->broadcastSkillInvoke(objectName());
				player->loseMark("&olyi");
				player->drawCards(1, objectName());
			}

		}
		return false;
	}
private:
	QString xueyi;
};

class OLXueyiKeep : public MaxCardsSkill
{
public:
	OLXueyiKeep(const QString &xueyi) : MaxCardsSkill("#" + xueyi + "-keep$"), xueyi(xueyi)
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player *target) const
	{
		int n = 0;
		if (target->hasLordSkill(xueyi))
			n += xueyi == "olxueyi" ? 2 * target->getMark("&olyi") : target->getMark("&olyi");
		if(target->hasSkill("olzongshi")){
			QStringList kingdoms;
			foreach (const Player *p, target->getAliveSiblings(true)) {
				if (kingdoms.contains(p->getKingdom())) continue;
				kingdoms << p->getKingdom();
				n++;
			}
		}
		return n;
	}
private:
	QString xueyi;
};

class OLHuojiVs : public OneCardViewAsSkill
{
public:
	OLHuojiVs() : OneCardViewAsSkill("olhuoji")
	{
		filter_pattern = ".|red";
		response_or_use = true;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		FireAttack *fire_attack = new FireAttack(originalCard->getSuit(), originalCard->getNumber());
		fire_attack->addSubcard(originalCard->getId());
		fire_attack->setSkillName(objectName());
		return fire_attack;
	}
};

class OLHuoji : public TriggerSkill
{
public:
	OLHuoji() : TriggerSkill("olhuoji")
	{
		events << CardEffected;
		view_as_skill = new OLHuojiVs;
	}

	int getPriority(TriggerEvent) const
	{
		return 0;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	int getEffectIndex(const ServerPlayer *player, const Card *) const
	{
		int index = qrand() % 2 + 1;
		if (player->getGeneralName().contains("pangtong") || player->getGeneral2Name().contains("pangtong"))
			index += 2;
		return index;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardEffected) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (effect.card->isKindOf("FireAttack")&&effect.from->isAlive()&&effect.from->hasSkill(objectName())){
				if (effect.nullified) {
					LogMessage log;
					log.type = "#CardNullified";
					log.from = player;
					log.card_str = effect.card->toString();
					room->sendLog(log);
					return true;
				}
				player->setFlags("Global_NonSkillNullify");
				if (!effect.offset_card)
					effect.offset_card = room->isCanceled(effect);
				if (effect.offset_card) {
					data.setValue(effect);
					if (!room->getThread()->trigger(CardOffset, room, effect.from, data))
						return true;
				}
				room->getThread()->trigger(CardOnEffect, room, player, data);
				if (player->getHandcardNum()>0&&effect.from->isAlive()){
					room->sendCompulsoryTriggerLog(effect.from,objectName(),true,!effect.card->getSkillName().contains("huoji"));
					const Card*c = player->getRandomHandCard();
					room->showCard(player,c->getId());
					if(room->askForCard(effect.from,".|"+c->getColorString()+"|.|hand","olhuoji0:"+player->objectName()+"::"+c->getColorString(),data)){
						room->damage(DamageStruct(effect.card,effect.from,player,1,DamageStruct::Fire));
					}else
						effect.from->setFlags("FireAttackFailed_" + player->objectName()); // For AI
				}
				return true;
			}
		}
		return false;
	}
};

class OLKanpoVs : public OneCardViewAsSkill
{
public:
	OLKanpoVs() : OneCardViewAsSkill("olkanpo")
	{
		filter_pattern = ".|black";
		response_pattern = "nullification";
		response_or_use = true;
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
		bool black = false;
		if (player->isKongcheng()) {
			foreach (const Card *c, player->getCards("e")) {
				if (c->isBlack()) {
					black = true;
					break;
				}
			}
		} else
			black = true;
		return black || !player->getHandPile().isEmpty();
	}
};

class OLKanpo : public TriggerSkill
{
public:
	OLKanpo() : TriggerSkill("olkanpo")
	{
		events << CardUsed;
		view_as_skill = new OLKanpoVs;
	}

	int getEffectIndex(const ServerPlayer *player, const Card *) const
	{
		int index = qrand() % 2 + 1;
		if (player->getGeneralName().contains("pangtong") || player->getGeneral2Name().contains("pangtong"))
			index += 2;
		return index;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Nullification")){
				int n = getEffectIndex(player,use.card);
				if(use.card->getSkillName().contains("olkanpo")) n = 0;
				room->sendCompulsoryTriggerLog(player,this,n);
				use.no_respond_list << "_ALL_TARGETS";
				data.setValue(use);
			}
		}
		return false;
	}
};

class OLCangzhuo : public TriggerSkill
{
public:
	OLCangzhuo() : TriggerSkill("olcangzhuo")
	{
		events << EventPhaseProceeding;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Discard) return false;
		if (player->getMark("olcangzhuo_usedtrick-Clear") > 0) return false;
		QList<int> tricks;
		foreach (const Card *c, player->getCards("h")) {
			if (c->isKindOf("TrickCard"))
				tricks << c->getEffectiveId();
		}
		if (tricks.isEmpty()) return false;
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		room->ignoreCards(player, tricks);
		return false;
	}
};

class OLLianhuan : public OneCardViewAsSkill
{
public:
	OLLianhuan() : OneCardViewAsSkill("ollianhuan")
	{
		filter_pattern = ".|club";
		response_or_use = true;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		IronChain *chain = new IronChain(originalCard->getSuit(), originalCard->getNumber());
		chain->addSubcard(originalCard);
		chain->setSkillName(objectName());
		return chain;
	}
};

class OLLianhuanMod : public TargetModSkill
{
public:
	OLLianhuanMod() : TargetModSkill("#ollianhuanmod")
	{
		frequency = NotFrequent;
		pattern = "IronChain";
	}

	int getExtraTargetNum(const Player *from, const Card *) const
	{
		if (from->hasSkill("ollianhuan"))
			return 1;
		return 0;
	}
};

class OLNiepan : public TriggerSkill
{
public:
	OLNiepan() : TriggerSkill("olniepan")
	{
		events << AskForPeaches;
		frequency = Limited;
		limit_mark = "@olniepanMark";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return TriggerSkill::triggerable(target) && target->getMark("@olniepanMark") > 0;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *pangtong, QVariant &data) const
	{
		DyingStruct dying_data = data.value<DyingStruct>();
		if (dying_data.who != pangtong)
			return false;

		if (pangtong->askForSkillInvoke(this, data)) {
			room->broadcastSkillInvoke(objectName());
			room->doSuperLightbox(pangtong, "olniepan");

			room->removePlayerMark(pangtong, "@olniepanMark");

			pangtong->throwAllHandCardsAndEquips();
			/*QList<const Card *> tricks = pangtong->getJudgingArea();
			foreach (const Card *trick, tricks) {
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, pangtong->objectName());
				room->throwCard(trick, reason, nullptr);
			}*/

			if (pangtong->isChained())
				room->setPlayerChained(pangtong);

			if (!pangtong->faceUp())
				pangtong->turnOver();

			pangtong->drawCards(3, objectName());

			int n = qMin(3 - pangtong->getHp(), pangtong->getMaxHp() - pangtong->getHp());
			if (n > 0)
				room->recover(pangtong, RecoverStruct(pangtong, nullptr, n, "olniepan"));

			QStringList skills;
			if (!pangtong->hasSkill("bazhen", true))
				skills << "bazhen";
			if (!pangtong->hasSkill("olhuoji", true))
				skills << "olhuoji";
			if (!pangtong->hasSkill("olkanpo", true))
				skills << "olkanpo";
			if (skills.isEmpty()) return false;
			QString skill = room->askForChoice(pangtong, objectName(), skills.join("+"));
			room->handleAcquireDetachSkills(pangtong, skill);
		}
		return false;
	}
};

class OLJianchu : public TriggerSkill
{
public:
	OLJianchu() : TriggerSkill("oljianchu")
	{
		events << TargetSpecified;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		foreach (ServerPlayer *p, use.to) {
			if (player->isDead() || !player->hasSkill(objectName())) break;
			if (p->isDead()) continue;
			if (!player->canDiscard(p, "he") || !player->askForSkillInvoke(this, QVariant::fromValue(p))) continue;
			room->broadcastSkillInvoke(objectName());
			int to_throw = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
			const Card *card = Sanguosha->getCard(to_throw);
			room->throwCard(card, p, player);
			if (card->isKindOf("BasicCard")) {
				if (!room->CardInTable(use.card)||p->isDead()) continue;
				p->obtainCard(use.card, true);
			} else {
				LogMessage log;
				log.type = "#NoJink";
				log.from = p;
				room->sendLog(log);
				use.no_respond_list << p->objectName();
				data = QVariant::fromValue(use);
				room->addSlashCishu(player, 1);
			}
		}
		return false;
	}
};

class OLHanzhan : public TriggerSkill
{
public:
	OLHanzhan() : TriggerSkill("olhanzhan")
	{
		events << AskforPindianCard;
	}

	int getPriority(TriggerEvent) const
	{
		return 3;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		PindianStruct *pindian = data.value<PindianStruct *>();
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead()) continue;
			if (pindian->from != p && pindian->to != p) continue;
			if (!p->hasSkill(objectName())) continue;
			if (pindian->from == p) {
				if (pindian->to_card) continue;
				if (pindian->to->isDead() || pindian->to->isKongcheng()) continue;
				if (!p->askForSkillInvoke(this, QVariant::fromValue(pindian->to))) continue;
				room->broadcastSkillInvoke(objectName());
				pindian->to_card = pindian->to->getRandomHandCard();
			}
			if (pindian->to == p) {
				if (pindian->from_card) continue;
				if (pindian->from->isDead() || pindian->from->isKongcheng()) continue;
				if (!p->askForSkillInvoke(this, QVariant::fromValue(pindian->from))) continue;
				room->broadcastSkillInvoke(objectName());
				pindian->from_card = pindian->from->getRandomHandCard();
			}
		}
		return false;
	}
};

SecondOLHanzhanCard::SecondOLHanzhanCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

void SecondOLHanzhanCard::onUse(Room *, CardUseStruct &) const
{
}

class SecondOLHanzhanVS : public OneCardViewAsSkill
{
public:
	SecondOLHanzhanVS() : OneCardViewAsSkill("secondolhanzhan")
	{
		//filter_pattern = ".|.|.|#secondolhanzhan";
		expand_pile = "#secondolhanzhan";
		response_pattern = "@@secondolhanzhan";
	}

	bool viewFilter(const Card *to_select) const
	{
		return Self->getPile("#secondolhanzhan").contains(to_select->getEffectiveId());
	}

	const Card *viewAs(const Card *originalCard) const
	{
		SecondOLHanzhanCard *c = new SecondOLHanzhanCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class SecondOLHanzhan : public TriggerSkill
{
public:
	SecondOLHanzhan() : TriggerSkill("secondolhanzhan")
	{
		events << AskforPindianCard << Pindian;
		view_as_skill = new SecondOLHanzhanVS;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == AskforPindianCard)
			return 3;
		return TriggerSkill::getPriority(event);
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		PindianStruct *pindian = data.value<PindianStruct *>();
		if (event == AskforPindianCard) {
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead()) continue;
				if (pindian->from != p && pindian->to != p) continue;
				if (!p->hasSkill(objectName())) continue;
				if (pindian->from == p) {
					if (pindian->to_card) continue;
					if (pindian->to->isDead() || pindian->to->isKongcheng()) continue;
					if (!p->askForSkillInvoke(this, QVariant::fromValue(pindian->to))) continue;
					room->broadcastSkillInvoke(objectName());
					pindian->to_card = pindian->to->getRandomHandCard();
				}
				if (pindian->to == p) {
					if (pindian->from_card) continue;
					if (pindian->from->isDead() || pindian->from->isKongcheng()) continue;
					if (!p->askForSkillInvoke(this, QVariant::fromValue(pindian->from))) continue;
					room->broadcastSkillInvoke(objectName());
					pindian->from_card = pindian->from->getRandomHandCard();
				}
			}
		} else {
			QList<ServerPlayer *> pd;
			pd << pindian->from << pindian->to;
			room->sortByActionOrder(pd);
			foreach (ServerPlayer *p, pd) {
				if (p->isDead() || !p->hasSkill(objectName())) continue;

				QList<int> slash;
				if (pindian->from_number == pindian->to_number) {
					if (pindian->from_card->isKindOf("Slash") && room->CardInTable(pindian->from_card))
						slash << pindian->from_card->getEffectiveId();
					if (pindian->to_card->isKindOf("Slash") && !slash.contains(pindian->to_card->getEffectiveId()) &&
							room->CardInTable(pindian->to_card))
						slash << pindian->to_card->getEffectiveId();
				} else {
					if (pindian->from_card->isKindOf("Slash") && pindian->from_number > pindian->to_number &&
							room->CardInTable(pindian->from_card))
						slash << pindian->from_card->getEffectiveId();
					else if (pindian->to_card->isKindOf("Slash") && pindian->to_number > pindian->from_number &&
							room->CardInTable(pindian->to_card))
						slash << pindian->to_card->getEffectiveId();
					else if (!pindian->from_card->isKindOf("Slash") && pindian->to_card->isKindOf("Slash") &&
							room->CardInTable(pindian->to_card))
						slash << pindian->to_card->getEffectiveId();
					else if (pindian->from_card->isKindOf("Slash") && !pindian->to_card->isKindOf("Slash") &&
							room->CardInTable(pindian->from_card))
						slash << pindian->from_card->getEffectiveId();
				}
				if (slash.isEmpty()) return false;

				room->notifyMoveToPile(p, slash, objectName(), Player::PlaceTable, true);
				const Card *c = room->askForUseCard(p, "@@secondolhanzhan", "@secondolhanzhan");
				room->notifyMoveToPile(p, slash, objectName(), Player::PlaceTable, false);
				if (!c) continue;
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = p;
				log.arg = objectName();
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(p, objectName());
				room->obtainCard(p, c, true);
			}
		}

		return false;
	}
};

OLWulieCard::OLWulieCard()
{
	mute = true;
}

bool OLWulieCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.length() < Self->getHp() && to_select != Self;
}

void OLWulieCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->removePlayerMark(source, "@olwulieMark");
	room->broadcastSkillInvoke("olwulie");
	room->doSuperLightbox(source, "olwulie");
	if (targets.isEmpty()) return;
	room->loseHp(HpLostStruct(source, targets.length(), "olwulie", source));
	foreach (ServerPlayer *p, targets) {
		if (p->isAlive())
			room->cardEffect(this, source, p);
	}
}

void OLWulieCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->gainMark("&ollie");
}

class OLWulieVS : public ZeroCardViewAsSkill
{
public:
	OLWulieVS() : ZeroCardViewAsSkill("olwulie")
	{
		response_pattern = "@@olwulie";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs() const
	{
		return new OLWulieCard;
	}
};

class OLWulie : public TriggerSkill
{
public:
	OLWulie() : TriggerSkill("olwulie")
	{
		events << EventPhaseStart << DamageInflicted;
		frequency = Limited;
		limit_mark = "@olwulieMark";
		view_as_skill = new OLWulieVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Finish || !player->hasSkill(objectName())) return false;
			if (player->getMark("@olwulieMark") <= 0) return false;
			room->askForUseCard(player, "@@olwulie", "@olwulie");
		} else {
			if (player->getMark("&ollie") <= 0) return false;
			DamageStruct damage = data.value<DamageStruct>();
			LogMessage log;
			log.type = "#OLwuliePrevent";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			player->loseMark("&ollie");
			return true;
		}
		return false;
	}
};

OLFangquanCard::OLFangquanCard()
{
	mute = true;
}

void OLFangquanCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	ServerPlayer *liushan = effect.from, *player = effect.to;

	LogMessage log;
	log.type = "#Fangquan";
	log.from = liushan;
	log.to << player;
	room->sendLog(log);

	room->setTag("OLFangquanTarget", QVariant::fromValue(player));
}

class OLFangquanViewAsSkill : public OneCardViewAsSkill
{
public:
	OLFangquanViewAsSkill() : OneCardViewAsSkill("olfangquan")
	{
		filter_pattern = ".|.|.|hand";
		response_pattern = "@@olfangquan";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		OLFangquanCard *fangquan = new OLFangquanCard;
		fangquan->addSubcard(originalCard);
		return fangquan;
	}
};

class OLFangquan : public TriggerSkill
{
public:
	OLFangquan() : TriggerSkill("olfangquan")
	{
		events << EventPhaseChanging << EventPhaseStart;
		view_as_skill = new OLFangquanViewAsSkill;
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == EventPhaseStart)
			return 1;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *liushan, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::Play) return false;
			if (!TriggerSkill::triggerable(liushan) || liushan->isSkipped(Player::Play)) return false;
			if (!liushan->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			liushan->setFlags(objectName());
			liushan->skip(Player::Play, true);
		} else if (triggerEvent == EventPhaseStart) {
			if (liushan->getPhase() == Player::Discard) {
				if (liushan->hasFlag(objectName())) {
					room->setPlayerFlag(liushan, "-olfangquan");
					if (!liushan->canDiscard(liushan, "h"))
						return false;
					room->askForUseCard(liushan, "@@olfangquan", "@olfangquan-give", -1, Card::MethodDiscard);
				}
			} else if (liushan->getPhase() == Player::NotActive) {
				if (!room->getTag("OLFangquanTarget").isNull()) {
					ServerPlayer *target = room->getTag("OLFangquanTarget").value<ServerPlayer *>();
					room->removeTag("OLFangquanTarget");
					if (target->isAlive())
						target->gainAnExtraTurn();
				}
			}
		}
		return false;
	}
};

class OLRuoyu : public PhaseChangeSkill
{
public:
	OLRuoyu() : PhaseChangeSkill("olruoyu$")
	{
		frequency = Wake;
		waked_skills = "oljijiang,olsishu";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasLordSkill(this);
	}

	bool onPhaseChange(ServerPlayer *liushan, Room *room) const
	{
		if (liushan->isLowestHpPlayer()) {
			LogMessage log;
			log.type = "#RuoyuWake";
			log.from = liushan;
			log.arg = QString::number(liushan->getHp());
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!liushan->canWake(objectName()))
			return false;

		if (liushan->isWeidi())
			room->broadcastSkillInvoke("weidi");
		else
			room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(liushan, objectName());
		room->doSuperLightbox(liushan, "olruoyu");
		room->setPlayerMark(liushan, "olruoyu", 1);
		if (room->changeMaxHpForAwakenSkill(liushan, 1, objectName())) {
			int recover = qMin(3, liushan->getMaxHp()) - liushan->getHp();
			room->recover(liushan, RecoverStruct(liushan, nullptr, recover, "olruoyu"));
			QStringList skills;
			skills << "oljijiang" << "olsishu";
			room->handleAcquireDetachSkills(liushan, skills);
		}
		return false;
	}
};

class OLSishu : public TriggerSkill
{
public:
	OLSishu() : TriggerSkill("olsishu")
	{
		events << StartJudge << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == StartJudge) {
			if (player->getMark("&olsishu") <= 0) return false;
			JudgeStruct *judge = data.value<JudgeStruct *>();
			if (judge->reason != "indulgence") return false;
			LogMessage log;
			log.type = "#OLsishuEffect";
			log.from = player;
			log.arg = "olsishu";
			room->sendLog(log);
			judge->good = false;
		} else {
			if (!player->hasSkill(objectName()) || player->getPhase() != Player::Play) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@olsishu-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			room->setPlayerMark(target, "&olsishu", 1);
		}
		return false;
	}
};

OLZhibaCard::OLZhibaCard()
{
	mute = true;
}

bool OLZhibaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->getKingdom() == "wu" && Self->canPindian(to_select) && Self != to_select;
}

void OLZhibaCard::onEffect(CardEffectStruct &effect) const
{
	if (!effect.from->canPindian(effect.to)) return;
	Room *room = effect.from->getRoom();
	if (effect.from->isWeidi())
		room->broadcastSkillInvoke("weidi");
	else
		room->broadcastSkillInvoke("olzhiba");
	PindianStruct *pindian = effect.from->PinDian(effect.to, "olzhiba", nullptr);
	if (!pindian) return;
	if (pindian->from_number < pindian->to_number) return;
	if (pindian->from && !pindian->from->isDead() && pindian->from->hasLordSkill("olzhiba")) {
		DummyCard *dummy = new DummyCard();
		int from_card_id = pindian->from_card->getEffectiveId();
		int to_card_id = pindian->to_card->getEffectiveId();
		if (room->getCardPlace(from_card_id) == Player::DiscardPile)
			dummy->addSubcard(from_card_id);
		if (room->getCardPlace(to_card_id) == Player::DiscardPile && from_card_id != to_card_id)
			dummy->addSubcard(to_card_id);
		if (!dummy->getSubcards().isEmpty() && room->askForChoice(pindian->from, "olzhiba_pindian_obtain", "obtainPindianCards+reject") == "obtainPindianCards")
			pindian->from->obtainCard(dummy);
		delete dummy;
	}
}

class OLZhibaVS : public ZeroCardViewAsSkill
{
public:
	OLZhibaVS() : ZeroCardViewAsSkill("olzhiba$")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return hasTarget(player) && !player->hasUsed("OLZhibaCard") && player->canPindian();
	}

	bool hasTarget(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->getKingdom() == "wu")
				return true;
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new OLZhibaCard;
	}
};

class OLZhiba : public TriggerSkill
{
public:
	OLZhiba() : TriggerSkill("olzhiba$")
	{
		view_as_skill = new OLZhibaVS;
		events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (triggerEvent == EventAcquireSkill&&player->hasLordSkill(this,true)) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("olzhiba_pindian",true)){
					room->attachSkillToPlayer(p, "olzhiba_pindian");
				}
			}
		}else if(player->getPhase()!=Player::Play)
			return false;
		if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasLordSkill(this,true)){
					room->attachSkillToPlayer(player, "olzhiba_pindian");
					break;
				}
			}
		}else{
			if (player->hasSkill("olzhiba_pindian",true))
				room->detachSkillFromPlayer(player, "olzhiba_pindian", true);
		}
		return false;
	}
};

OLZhibaPindianCard::OLZhibaPindianCard()
{
	m_skillName = "olzhiba_pindian";
	mute = true;
}

bool OLZhibaPindianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self != to_select && Self->canPindian(to_select)
	&& to_select->hasLordSkill("olzhiba");
}

void OLZhibaPindianCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *p, use.to) {
		if (p->isWeidi()) {
			room->broadcastSkillInvoke("weidi",-1,p);
			room->notifySkillInvoked(p, "weidi");
		}else
			room->broadcastSkillInvoke("olzhiba",-1,p);
		use.from->skillInvoked("olzhiba",0,p);
		if (room->askForChoice(p, "olzhiba_pindian", "accept+reject") == "reject") {
			LogMessage log;
			log.type = "#ZhibaReject";
			log.from = p;
			log.to << use.from;
			log.arg = "olzhiba_pindian";
			room->sendLog(log);
			continue;
		}
		PindianStruct *pindian = use.from->PinDian(p, "olzhiba_pindian");
		if (!pindian) continue;
		if (pindian->from_number > pindian->to_number) continue;
		if (pindian->to->isAlive() && pindian->to->hasLordSkill("olzhiba")) {
			DummyCard *dummy = new DummyCard();
			int from_card_id = pindian->from_card->getEffectiveId();
			int to_card_id = pindian->to_card->getEffectiveId();
			if (room->getCardPlace(from_card_id) == Player::DiscardPile)
				dummy->addSubcard(from_card_id);
			if (room->getCardPlace(to_card_id) == Player::DiscardPile && from_card_id != to_card_id)
				dummy->addSubcard(to_card_id);
			if (dummy->subcardsLength()>0 && room->askForChoice(pindian->to, "olzhiba_pindian_obtain", "obtainPindianCards+reject") != "reject")
				pindian->to->obtainCard(dummy);
			delete dummy;
		}
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class OLZhibaPindian : public ZeroCardViewAsSkill
{
public:
	OLZhibaPindian() : ZeroCardViewAsSkill("olzhiba_pindian")
	{
		attached_lord_skill = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getKingdom() == "wu"&&hasTarget(player);
	}

	bool hasTarget(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->hasLordSkill("olzhiba") && player->usedTimes("OLZhibaPindianCard") <= 0 && player->canPindian(p))
				return true;
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new OLZhibaPindianCard;
	}
};

class OLQiaomeng : public TriggerSkill
{
public:
	OLQiaomeng() : TriggerSkill("olqiaomeng")
	{
		events << Damage;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to->isAlive() && !damage.to->hasFlag("Global_DebutFlag")
			&& damage.card && damage.card->isKindOf("Slash")
			&& player->canDiscard(damage.to, "hej") && room->askForSkillInvoke(player, objectName(), QVariant::fromValue(damage.to))) {
			room->broadcastSkillInvoke(objectName());
			int id = room->askForCardChosen(player, damage.to, "hej", objectName(), false, Card::MethodDiscard);
			CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, player->objectName(), damage.to->objectName(),
				objectName(), "");
			const Card *c = Sanguosha->getCard(id);
			room->throwCard(c, reason, damage.to, player);
			if (c->isKindOf("Horse") && player->isAlive())
				room->obtainCard(player, c);
		}
		return false;
	}
};

class OLYicong : public DistanceSkill
{
public:
	OLYicong() : DistanceSkill("olyicong")
	{
	}

	int getCorrect(const Player *from, const Player *to) const
	{
		int correct = 0;
		if (from->hasSkill(objectName()))
			correct--;
		if (to->getHp()<=2&&to->hasSkill(objectName()))
			correct++;

		return correct;
	}
};

class OLYicongEffect : public TriggerSkill
{
public:
	OLYicongEffect() : TriggerSkill("#olyicong-effect")
	{
		events << HpChanged;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		int hp = player->getHp();
		int index = 0;
		int reduce = 0;
		if (data.canConvert<RecoverStruct>()) {
			int rec = data.value<RecoverStruct>().recover;
			if (hp > 2 && hp - rec <= 2)
				index = 1;
		} else {
			if (data.canConvert<DamageStruct>()) {
				DamageStruct damage = data.value<DamageStruct>();
				reduce = damage.damage;
			} else if (!data.isNull()) {
				reduce = data.toInt();
			}
			if (hp <= 2 && hp + reduce > 2)
				index = 2;
		}

		if (index > 0) {
			if (player->getGeneralName().contains("sp_")
				|| (!player->getGeneralName().contains("sp_") && player->getGeneral2Name().contains("sp_")))
				index += 2;
			room->broadcastSkillInvoke("olyicong", index);
		}
		return false;
	}
};

class OLHuashen : public GameStartSkill
{
public:
	OLHuashen() : GameStartSkill("olhuashen")
	{
	}

	static void AcquireGenerals(ServerPlayer *zuoci, int n, QStringList remove_list)
	{
		Room *room = zuoci->getRoom();
		QStringList list = GetAvailableGenerals(zuoci, remove_list);
		if (list.isEmpty()) return;
		qShuffle(list);
		n = qMin(n, list.length());
		QStringList huashens = zuoci->property("Huashens").toString().split("+");
		if(zuoci->property("Huashens").toString().isEmpty()) huashens.clear();

		QStringList acquired = list.mid(0, n);
		foreach (QString name, acquired) {
			huashens << name;
			const General *general = Sanguosha->getGeneral(name);
			if (general) {
				foreach (const TriggerSkill *skill, general->getTriggerSkills())
					room->getThread()->addTriggerSkill(skill);
			}
		}
		room->setPlayerProperty(zuoci,"Huashens",huashens.join("+"));

		QStringList hidden;
		for (int i = 0; i < n; i++) hidden << "unknown";
		room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), hidden.join(":"), room->getOtherPlayers(zuoci));
		room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, zuoci->objectName(), acquired.join(":"), QList<ServerPlayer *>() << zuoci);

		LogMessage log;
		log.type = "#GetHuashen";
		log.from = zuoci;
		log.arg = QString::number(n);
		log.arg2 = QString::number(huashens.length());
		room->sendLog(log);

		log.type = "#GetHuashenDetail";
		log.arg = acquired.join("\\, \\");
		room->sendLog(log, zuoci);

		room->setPlayerMark(zuoci, "@huashen", huashens.length());
	}

	static QStringList GetAvailableGenerals(ServerPlayer *zuoci, QStringList remove_list)
	{
		QStringList all = Sanguosha->getLimitedGeneralNames();
		Room *room = zuoci->getRoom();
		if (room->getMode() == "06_XMode") {
			foreach(ServerPlayer *p, room->getAlivePlayers())
				all << p->getTag("XModeBackup").toStringList();
		} else if (room->getMode() == "02_1v1") {
			foreach(ServerPlayer *p, room->getAlivePlayers())
				all << p->getTag("1v1Arrange").toStringList();
		}
		QSet<QString> huashen_set, room_set;
		foreach(QString huashen, zuoci->property("Huashens").toString().split("+")){
			if(huashen.isEmpty()) continue;
			huashen_set << huashen;
		}
		foreach (ServerPlayer *player, room->getAlivePlayers()) {
			QString name = player->getGeneralName();/*
			if (Sanguosha->isGeneralHidden(name)) {
				QString fname = Sanguosha->findConvertFrom(name);
				if (!fname.isEmpty()) name = fname;
			}*/
			room_set << name;

			if (!player->getGeneral2()) continue;

			name = player->getGeneral2Name();/*
			if (Sanguosha->isGeneralHidden(name)) {
				QString fname = Sanguosha->findConvertFrom(name);
				if (!fname.isEmpty()) name = fname;
			}*/
			room_set << name;
		}

		static QSet<QString> banned;
		if (banned.isEmpty()) {
			banned << "zuoci" << "guzhielai" << "dengshizai" << "yt_caochong" << "jiangboyue" << "ol_zuoci";
		}
		QSet<QString> remove_set = QSet<QString>(remove_list.begin(), remove_list.end());
		return (QSet<QString>(all.begin(), all.end()) - banned - huashen_set - room_set - remove_set).values();
	}

	static void SelectSkill(ServerPlayer *zuoci)
	{
		Room *room = zuoci->getRoom();

		QStringList huashens = zuoci->property("Huashens").toString().split("+");
		if(zuoci->property("Huashens").toString().isEmpty()) huashens.clear();
		if (huashens.isEmpty()) return;
		QStringList ac_dt_list;
		QString huashen_skill = zuoci->getTag("OLHuashenSkill").toString();
		if (!huashen_skill.isEmpty())
			ac_dt_list.append("-" + huashen_skill);

		QString skill_name;
		QStringList skill_names;
		const General *general = nullptr;
		AI* ai = zuoci->getAI();
		if (ai) {
			QHash<QString, const General *> hash;
			foreach (QString general_name, huashens) {
				const General *general = Sanguosha->getGeneral(general_name);
				foreach (const Skill *skill, general->getVisibleSkillList()) {
					if (skill->isLordSkill()
						|| skill->isLimitedSkill()
						|| skill->getFrequency() == Skill::Wake)
						continue;
					hash[skill->objectName()] = general;
					skill_names << skill->objectName();
				}
			}
			if (skill_names.isEmpty()) return;
			skill_name = ai->askForChoice("olhuashen", skill_names.join("+"), QVariant());
			general = hash[skill_name];
		} else {
			QString general_name = room->askForGeneral(zuoci, huashens);
			general = Sanguosha->getGeneral(general_name);
			foreach (const Skill *skill, general->getVisibleSkillList()) {
				if (skill->isLordSkill()
					|| skill->isLimitedSkill()
					|| skill->getFrequency() == Skill::Wake)
					continue;
				skill_names << skill->objectName();
			}
			if (skill_names.isEmpty()) return;
			skill_name = room->askForChoice(zuoci, "olhuashen", skill_names.join("+"));
		}

		QString kingdom = general->getKingdom();
		if (general->getKingdoms().contains("+")) {
			room->setPlayerProperty(zuoci, "kingdom", room->askForKingdom(zuoci, general->objectName() + "_ChooseKingdom"));
		} else if (zuoci->getKingdom() != kingdom) {
			if (kingdom == "god")
				kingdom = room->askForKingdom(zuoci);
			room->setPlayerProperty(zuoci, "kingdom", kingdom);
		}

		if (zuoci->getGender() != general->getGender())
			zuoci->setGender(general->getGender());

		JsonArray arg;
		arg << QSanProtocol::S_GAME_EVENT_HUASHEN << zuoci->objectName() << general->objectName() << skill_name;
		room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

		ac_dt_list.append(skill_name);
		zuoci->setTag("OLHuashenSkill", skill_name);
		room->handleAcquireDetachSkills(zuoci, ac_dt_list, true);
	}

	void onGameStart(ServerPlayer *zuoci) const
	{
		zuoci->getRoom()->broadcastSkillInvoke(objectName(), zuoci->isMale(), -1);
		zuoci->getRoom()->sendCompulsoryTriggerLog(zuoci, objectName());
		AcquireGenerals(zuoci, 3, QStringList());
		SelectSkill(zuoci);
	}

	QDialog *getDialog() const
	{
		static HuashenDialog *dialog;

		if (dialog == nullptr)
			dialog = new HuashenDialog;

		return dialog;
	}
};

class OLHuashenSelect : public PhaseChangeSkill
{
public:
	OLHuashenSelect() : PhaseChangeSkill("#olhuashen-select")
	{
	}

	int getPriority(TriggerEvent) const
	{
		return 4;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return PhaseChangeSkill::triggerable(target)
			&& (target->getPhase() == Player::RoundStart || target->getPhase() == Player::NotActive);
	}

	bool onPhaseChange(ServerPlayer *zuoci, Room *room) const
	{
		if (zuoci->hasSkill("olhuashen") && zuoci->askForSkillInvoke("olhuashen")) {
			QStringList choices;
			choices << "change" << "exchangeone";
			QStringList huashens = zuoci->property("Huashens").toString().split("+");
			if(zuoci->property("Huashens").toString().isEmpty()) huashens.clear();
			if (huashens.length() > 1)
				choices << "exchangetwo";
			QString choice = room->askForChoice(zuoci, "olhuashen", choices.join("+"));
			if (choice == "change")
				OLHuashen::SelectSkill(zuoci);
			else {
				int n = 1;
				if (choice == "exchangetwo")
					n = 2;
				QStringList remove_list;
				for (int i = 0; i < n; i++) {
					if (huashens.isEmpty()) break;
					QString general_name = room->askForGeneral(zuoci, huashens);
					remove_list << general_name;
					huashens.removeOne(general_name);
				}
				room->setPlayerProperty(zuoci,"Huashens",huashens.join("+"));

				LogMessage log;
				log.type = "#RemoveHuashenDetail";
				log.from = zuoci;
				log.arg = QString::number(remove_list.length());
				log.arg2 = remove_list.join("\\, \\");
				room->sendLog(log);

				OLHuashen::AcquireGenerals(zuoci, remove_list.length(), remove_list);
			}
		}

		return false;
	}
};

class OLHuashenClear : public DetachEffectSkill
{
public:
	OLHuashenClear() : DetachEffectSkill("olhuashen")
	{
	}

	void onSkillDetached(Room *room, ServerPlayer *player) const
	{
		if (player->getKingdom() != player->getGeneral()->getKingdom() && player->getGeneral()->getKingdom() != "god")
			room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());
		if (player->getGender() != player->getGeneral()->getGender())
			player->setGender(player->getGeneral()->getGender());
		QString huashen_skill = player->getTag("OLHuashenSkill").toString();
		if (player->hasSkill(huashen_skill,true))
			room->detachSkillFromPlayer(player, huashen_skill);
		room->setPlayerProperty(player,"Huashens","");
		room->setPlayerMark(player, "@huashen", 0);
	}
};

class OLJiuchiVS : public OneCardViewAsSkill
{
public:
	OLJiuchiVS() : OneCardViewAsSkill("oljiuchi")
	{
		filter_pattern = ".|spade|.|hand";
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Analeptic::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return  pattern.contains("analeptic");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Analeptic *analeptic = new Analeptic(originalCard->getSuit(), originalCard->getNumber());
		analeptic->setSkillName(objectName());
		analeptic->addSubcard(originalCard->getId());
		return analeptic;
	}
};

class OLJiuchi : public TriggerSkill
{
public:
	OLJiuchi() : TriggerSkill("oljiuchi")
	{
		events << Damage;
		view_as_skill = new OLJiuchiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.card && damage.card->isKindOf("Slash") && damage.card->hasFlag("drank")) {
			if (player->hasSkill("benghuai")) {
				LogMessage log;
				log.type = "#BenghuaiNullification";
				log.from = player;
				log.arg = objectName();
				log.arg2 = "benghuai";
				room->sendLog(log);
				room->broadcastSkillInvoke("oljiuchi");
				room->notifySkillInvoked(player, "oljiuchi");
			}
			room->addPlayerMark(player, "benghuai_nullification-Clear");
		}
		return false;
	}
};

class OLJiuchiTargetMod : public TargetModSkill
{
public:
	OLJiuchiTargetMod() : TargetModSkill("#oljiuchi-target")
	{
		frequency = NotFrequent;
		pattern = "Analeptic";
	}

	int getResidueNum(const Player *from, const Card *, const Player *) const
	{
		if (from->hasSkill("oljiuchi"))
			return 999;
		return 0;
	}
};

class OLBaonue : public TriggerSkill
{
public:
	OLBaonue() : TriggerSkill("olbaonue$")
	{
		events << Damage;
		frequency = Frequent;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getKingdom() == "qun";
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		int damage = data.value<DamageStruct>().damage;

		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->isDead() || !p->hasLordSkill(this)) continue;
			for (int i = 1; i <= damage; i++) {
				if (p->isDead() || !p->hasLordSkill(this)) break;
				if (!p->askForSkillInvoke(this)) break;

				if (p->isWeidi()) {
					room->broadcastSkillInvoke("weidi");
					room->notifySkillInvoked(p, "weidi");
				} else {
					room->broadcastSkillInvoke(objectName());
					room->notifySkillInvoked(p, objectName());
				}

				JudgeStruct judge;
				judge.pattern = ".|spade";
				judge.reason = objectName();
				judge.good = true;
				judge.who = p;
				room->judge(judge);

				if (judge.isGood() && p->isAlive()) {
					room->recover(p, RecoverStruct("olbaonue", p));
					if (p->isAlive() && room->getCardPlace(judge.card->getEffectiveId()) == Player::DiscardPile)
						p->obtainCard(judge.card);
				}
			}
		}
		return false;
	}
};

class OLJiuyuan : public TriggerSkill
{
public:
	OLJiuyuan() : TriggerSkill("oljiuyuan$")
	{
		events << PreHpRecover;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getKingdom() == "wu" && target->hasFlag("CurrentPlayer");
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		QList<ServerPlayer *> sunquans;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->hasLordSkill(this)) continue;
			if (player->getHp() >= p->getHp() && p->getLostHp() > 0)
				sunquans << p;
		}
		if (sunquans.isEmpty()) return false;
		ServerPlayer *sunquan = room->askForPlayerChosen(player, sunquans, objectName(), "@tenyearjiuyuan-invoke", true);
		if (!sunquan) return false;
		LogMessage log;
		log.type = "#InvokeOthersSkill";
		log.from = player;
		log.to << sunquan;
		log.arg = "oljiuyuan";
		room->sendLog(log);
		if (sunquan->isWeidi()) {
			room->broadcastSkillInvoke("weidi");
			room->notifySkillInvoked(sunquan, "weidi");
		} else {
			room->broadcastSkillInvoke("jiuyuan");
			room->notifySkillInvoked(sunquan, objectName());
		}
		room->recover(sunquan, RecoverStruct("oljiuyuan", player));
		player->drawCards(1, objectName());
		return true;
	}
};

class OLBotu : public PhaseChangeSkill
{
public:
	OLBotu() : PhaseChangeSkill("olbotu")
	{
		frequency = Frequent;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::NotActive) return false;
		QStringList botu_str = player->property("olbotu_suit").toStringList();
		if (botu_str.length() < 4) return false;
		if (player->askForSkillInvoke(this)){
			room->broadcastSkillInvoke(objectName());
			player->gainAnExtraTurn();
		}
		return false;
	}
};

class OLBotuMark : public TriggerSkill
{
public:
	OLBotuMark() : TriggerSkill("#olbotu-mark")
	{
		events << CardFinished << EventPhaseStart;
        global = true;
	}
	int getPriority(TriggerEvent) const
	{
		return 1;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			if (player->getPhase() != Player::Play) return false;

			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;

			QString suit = use.card->getSuitString() + "_char";
			if (use.card->getSuit() == Card::NoSuit || use.card->getSuit() == Card::NoSuitRed || use.card->getSuit() == Card::NoSuitBlack)
				suit = "no_suit_char";
			QStringList botu_str = player->property("olbotu_suit").toStringList();
			if (botu_str.contains(suit)) return false;
			botu_str << suit;
			room->safeSetPlayerProperty(player,"olbotu_suit", botu_str);

			if (player->hasSkill("olbotu", true)){
				foreach (QString m, player->getMarkNames()) {
					if (m.startsWith("&olbotu"))
						room->setPlayerMark(player, m, 0);
				}
				botu_str.prepend("&olbotu");
				room->setPlayerMark(player, botu_str.join("+"), 1);
			}
		} else {
			if (player->getPhase() != Player::NotActive) return false;
			room->setPlayerProperty(player, "olbotu_suit", QStringList());
			foreach (QString m, player->getMarkNames()) {
				if (m.startsWith("&olbotu"))
					room->setPlayerMark(player, m, 0);
			}
		}
		return false;
	}
};

class OLTuntian : public TriggerSkill
{
public:
	OLTuntian() : TriggerSkill("oltuntian")
	{
		events << CardsMoveOneTime << FinishJudge;
		frequency = Frequent;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return TriggerSkill::triggerable(target);
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player) {
				bool invoke = false;
				if (!player->hasFlag("CurrentPlayer") && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
				&& !(move.to == player && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip))
				&& player->askForSkillInvoke("oltuntian", data))
					invoke = true;
				else if (player->hasFlag("CurrentPlayer")
				&& (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
					bool can_invoke = false;
					foreach (int id, move.card_ids) {
						if (Sanguosha->getCard(id)->isKindOf("Slash")) {
							can_invoke = true;
							break;
						}
					}
					if (can_invoke && player->askForSkillInvoke("oltuntian", data))
						invoke = true;
				}
				if (!invoke) return false;
				room->broadcastSkillInvoke("oltuntian");
				JudgeStruct judge;
				judge.pattern = ".|heart";
				judge.good = false;
				judge.reason = "oltuntian";
				judge.who = player;
				room->judge(judge);
			}
		} else if (triggerEvent == FinishJudge) {
			JudgeStruct *judge = data.value<JudgeStruct *>();
			if (judge->reason == "oltuntian" && judge->isGood() && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
				player->addToPile("field", judge->card->getEffectiveId());
		}
		return false;
	}
};

class OLTuntianDistance : public DistanceSkill
{
public:
	OLTuntianDistance() : DistanceSkill("#oltuntian-dist")
	{
	}

	int getCorrect(const Player *from, const Player *) const
	{
		int n = from->getPile("field").length();
		if (n>0&&from->hasSkill("oltuntian"))
			return -n;
		return 0;
	}
};

class OLZaoxian : public PhaseChangeSkill
{
public:
	OLZaoxian() : PhaseChangeSkill("olzaoxian")
	{
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *dengai, Room *room) const
	{
		if (dengai->getPile("field").length() >= 3) {
			LogMessage log;
			log.type = "#ZaoxianWake";
			log.from = dengai;
			log.arg = QString::number(dengai->getPile("field").length());
			log.arg2 = objectName();
			log.arg3 = "field";
			room->sendLog(log);
		}else if(!dengai->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(dengai, objectName());

		room->doSuperLightbox(dengai, "olzaoxian");

		room->setPlayerMark(dengai, "olzaoxian", 1);
		if (room->changeMaxHpForAwakenSkill(dengai, -1, objectName()))
			room->acquireSkill(dengai, "jixi");

		int n = dengai->getTag("OLzaoxianExtraTurn").toInt();
		dengai->setTag("OLzaoxianExtraTurn", ++n);
		return false;
	}
};

class OLZaoxianExtraTurn : public PhaseChangeSkill
{
public:
	OLZaoxianExtraTurn() : PhaseChangeSkill("#olzaoxian-extra-turn")
	{
		frequency = Wake;
	}

	int getPriority(TriggerEvent) const
	{
		return 1;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target->isAlive() && target->getPhase() == Player::NotActive;
	}

	bool onPhaseChange(ServerPlayer *dengai, Room *) const
	{
		int n = dengai->getTag("OLzaoxianExtraTurn").toInt();
		if (n <= 0) return false;
		dengai->setTag("OLzaoxianExtraTurn", --n);
		dengai->gainAnExtraTurn();
		return false;
	}
};

OLChangbiaoCard::OLChangbiaoCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool OLChangbiaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Slash *slash = new Slash(Card::SuitToBeDecided, 0);
	slash->addSubcards(subcards);
	slash->setSkillName("olchangbiao");
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

void OLChangbiaoCard::onUse(Room *room, CardUseStruct &card_use) const
{
	Slash *slash = new Slash(Card::SuitToBeDecided, 0);
	slash->addSubcards(subcards);
	slash->setSkillName("olchangbiao");
	room->setCardFlag(slash, "olchangbiao");
	slash->deleteLater();

	room->useCard(CardUseStruct(slash, card_use.from, card_use.to), true);
}

class OLChangbiaoVS : public ViewAsSkill
{
public:
	OLChangbiaoVS() : ViewAsSkill("olchangbiao")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (to_select->isEquipped()) return false;
		Slash *slash = new Slash(Card::SuitToBeDecided, 0);
		slash->addSubcards(selected);
		slash->addSubcard(to_select);
		slash->setSkillName("olchangbiao");
		slash->deleteLater();
		return !Self->isCardLimited(slash, Card::MethodUse, true);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		Slash *slash = new Slash(Card::SuitToBeDecided, 0);
		slash->addSubcards(cards);
		slash->setSkillName("olchangbiao");
		slash->deleteLater();
		if (Self->isCardLimited(slash, Card::MethodUse, true)) return nullptr;

		OLChangbiaoCard *card = new OLChangbiaoCard;
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("OLChangbiaoCard") && Slash::IsAvailable(player);
	}
};

class OLChangbiao : public TriggerSkill
{
public:
	OLChangbiao() : TriggerSkill("olchangbiao")
	{
		events << DamageDone << EventPhaseEnd;
		view_as_skill = new OLChangbiaoVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash") || !damage.card->hasFlag("olchangbiao")) return false;
			int mark = 0;
			if (!damage.card->isVirtualCard())
				mark = 1;
			else
				mark = damage.card->subcardsLength();
			if (mark > 0 && damage.from && damage.from->isAlive())
				room->addPlayerMark(damage.from, "olchangbiao_draw-PlayClear", mark);
		} else {
			if (player->getPhase() != Player::Play) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead() || p->getMark("olchangbiao_draw-PlayClear") <= 0) continue;
				room->sendCompulsoryTriggerLog(p, objectName(), true, true);
				p->drawCards(p->getMark("olchangbiao_draw-PlayClear"), objectName());
			}
		}
		return false;
	}
};

OLTiaoxinCard::OLTiaoxinCard()
{
}

bool OLTiaoxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->inMyAttackRange(Self);
}

void OLTiaoxinCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	const Card *use_slash = nullptr;
	if (effect.to->canSlash(effect.from, nullptr, false)) {
		try {
			use_slash = room->askForUseSlashTo(effect.to, effect.from, "@oltiaoxin-slash:" + effect.from->objectName(), true, false, false,
				effect.from, this, "oltiaoxin_slash");
		}
		catch (TriggerEvent triggerEvent) {
			if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (p->getMark("oltiaoxin_damage") > 0)
						room->setPlayerMark(p, "oltiaoxin_damage", 0);
				}
			}
			throw triggerEvent;
		}
	}

	bool hasMark = effect.from->getMark("oltiaoxin_damage") > 0;

	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p->getMark("oltiaoxin_damage") > 0)
			room->setPlayerMark(p, "oltiaoxin_damage", 0);
	}

	if ((!use_slash || !hasMark) && effect.from->canDiscard(effect.to, "he")) {
		room->throwCard(room->askForCardChosen(effect.from, effect.to, "he", "oltiaoxin", false, Card::MethodDiscard), effect.to, effect.from);
		room->addPlayerMark(effect.from, "oltiaoxin_extra-Clear");
	}
}

class OLTiaoxinVS : public ZeroCardViewAsSkill
{
public:
	OLTiaoxinVS() : ZeroCardViewAsSkill("oltiaoxin")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (player->getMark("oltiaoxin_extra-Clear") <= 0)
			return !player->hasUsed("OLTiaoxinCard");
		else
			return player->usedTimes("OLTiaoxinCard") < 2;
	}

	const Card *viewAs() const
	{
		return new OLTiaoxinCard;
	}
};

class OLTiaoxin : public TriggerSkill
{
public:
	OLTiaoxin() : TriggerSkill("oltiaoxin")
	{
		events << DamageDone;
		view_as_skill = new OLTiaoxinVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.card && damage.card->isKindOf("Slash") && damage.card->hasFlag("oltiaoxin_slash"))
			room->addPlayerMark(player, "oltiaoxin_damage");
		return false;
	}
};

class OLZhiji : public PhaseChangeSkill
{
public:
	OLZhiji() : PhaseChangeSkill("olzhiji")
	{
		frequency = Wake;
		waked_skills = "tenyearguanxing";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&(player->getPhase() == Player::Start||player->getPhase() == Player::Finish)
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *jiangwei, Room *room) const
	{
		if (jiangwei->isKongcheng()) {
			LogMessage log;
			log.type = "#ZhijiWake";
			log.from = jiangwei;
			log.arg = objectName();
			room->sendLog(log);
		}else if(!jiangwei->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(jiangwei, objectName());

		room->doSuperLightbox(jiangwei, "olzhiji");

		room->setPlayerMark(jiangwei, "olzhiji", 1);
		if (jiangwei->isWounded() && room->askForChoice(jiangwei, objectName(), "recover+draw") == "recover")
			room->recover(jiangwei, RecoverStruct("olzhiji", jiangwei));
		else
			room->drawCards(jiangwei, 2, objectName());
		if (room->changeMaxHpForAwakenSkill(jiangwei, -1, objectName()))
			room->acquireSkill(jiangwei, "tenyearguanxing");
		return false;
	}
};

OLZaiqiCard::OLZaiqiCard()
{
}

bool OLZaiqiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
	return targets.length() < Self->getMark("olzaiqi-Clear");
}

void OLZaiqiCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isDead()) return;
	QStringList choices;
	choices << "draw";
	if (effect.from->isAlive() && effect.from->isWounded())
		choices << "recover=" + effect.from->objectName();
	Room *room = effect.from->getRoom();
	QString choice = room->askForChoice(effect.to, "olzaiqi", choices.join("+"), QVariant::fromValue(effect.from));
	if (choice == "draw")
		effect.to->drawCards(1, "olzaiqi");
	else {
		room->recover(effect.from, RecoverStruct("olzaiqi", effect.to));
	}
}

class OLZaiqiVS : public ZeroCardViewAsSkill
{
public:
	OLZaiqiVS() : ZeroCardViewAsSkill("olzaiqi")
	{
		response_pattern = "@@olzaiqi";
	}

	const Card *viewAs() const
	{
		return new OLZaiqiCard;
	}
};

class OLZaiqi : public TriggerSkill
{
public:
	OLZaiqi() : TriggerSkill("olzaiqi")
	{
		events << CardsMoveOneTime << EventPhaseEnd;
		view_as_skill = new OLZaiqiVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardsMoveOneTime) {
			if (!room->getCurrent()) return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place != Player::DiscardPile) return false;
			foreach (int id, move.card_ids) {
				if (Sanguosha->getCard(id)->isRed())
					room->addPlayerMark(player, "olzaiqi-Clear");
			}
		} else {
			if (player->getPhase() != Player::Discard) return false;
			int n = player->getMark("olzaiqi-Clear");
			if (n <= 0) return false;
			room->askForUseCard(player, "@@olzaiqi", "@mobilezaiqi:" + QString::number(n));
		}

		return false;
	}
};

class OLDuanliang : public OneCardViewAsSkill
{
public:
	OLDuanliang() : OneCardViewAsSkill("olduanliang")
	{
		filter_pattern = "BasicCard,EquipCard|black";
		response_or_use = true;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		SupplyShortage *shortage = new SupplyShortage(originalCard->getSuit(), originalCard->getNumber());
		shortage->setSkillName(objectName());
		shortage->addSubcard(originalCard);
		return shortage;
	}
};

class OLDuanliangTargetMod : public TargetModSkill
{
public:
	OLDuanliangTargetMod() : TargetModSkill("#olduanliang-target")
	{
		frequency = NotFrequent;
		pattern = "SupplyShortage";
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *) const
	{
		if (from->getMark("damage_point_round")<1&&from->hasSkill("olduanliang"))
			return 999;
		return 0;
	}
};

class OLJiezi : public TriggerSkill
{
public:
	OLJiezi() : TriggerSkill("oljiezi")
	{
		events << EventPhaseSkipped << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getPhase() == Player::Draw;
	}

	bool isLeastHandcard(Room *room, ServerPlayer *player) const
	{
		if (player->getMark("&olxhzi") > 0) return false;
		int hand = player->getHandcardNum();
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->getHandcardNum() < hand)
				return false;
		}
		return true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == EventPhaseSkipped) {
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isAlive() && p->hasSkill(objectName())) {
					ServerPlayer *t = room->askForPlayerChosen(p, room->getAlivePlayers(), objectName(), "@oljiezi-target", true, true);
					if (!t) continue;
					room->broadcastSkillInvoke(this);
					if (isLeastHandcard(room, t))
						t->gainMark("&olxhzi");
					else
						t->drawCards(1, objectName());
				}
			}
		} else {
			if (player->isDead() || player->getMark("&olxhzi") <= 0) return false;

			LogMessage log;
			log.type = "#ZhenguEffect";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->broadcastSkillInvoke(this);

			player->loseAllMarks("&olxhzi");
			player->insertPhase(Player::Draw);/*

			RoomThread *thread = room->getThread();
			if (!thread->trigger(EventPhaseStart, room, player))
				thread->trigger(EventPhaseProceeding, room, player);
			thread->trigger(EventPhaseEnd, room, player);

			player->setPhase(Player::Draw);
			room->broadcastProperty(player, "phase");*/
		}
		return false;
	}
};

OLQiaobianCard::OLQiaobianCard() : QiaobianCard()
{
	mute = true;
	m_skillName = "olqiaobian";
}

class OLQiaobianVS : public ZeroCardViewAsSkill
{
public:
	OLQiaobianVS() : ZeroCardViewAsSkill("olqiaobian")
	{
		response_pattern = "@@olqiaobian";
	}

	const Card *viewAs() const
	{
		return new OLQiaobianCard;
	}
};

class OLQiaobian : public TriggerSkill
{
public:
	OLQiaobian() : TriggerSkill("olqiaobian")
	{
		events << EventPhaseChanging;
		view_as_skill = new OLQiaobianVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *zhanghe, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		room->setPlayerMark(zhanghe, "olqiaobianPhase", (int)change.to);
		int index = 0;
		switch (change.to) {
		case Player::RoundStart:
		case Player::Start:
		case Player::Finish:
		case Player::NotActive: return false;

		case Player::Judge: index = 1; break;
		case Player::Draw: index = 2; break;
		case Player::Play: index = 3; break;
		case Player::Discard: index = 4; break;
		case Player::PhaseNone: Q_ASSERT(false);
		}

		if (index <= 0) return false;

		QStringList phases;
		phases << "judge" << "draw" << "play" << "discard";
		QString phase = phases.at(index - 1);

		QStringList choices;
		if (zhanghe->canDiscard(zhanghe, "he"))
			choices << "card=" + phase;
		if (zhanghe->getMark("&olzhbian") > 0)
			choices << "mark=" + phase;
		if (choices.isEmpty()) return false;
		choices << "cancel";

		QString choice = room->askForChoice(zhanghe, objectName(), choices.join("+"), data);
		if (choice == "cancel") return false;

		LogMessage log;
		log.type = "#InvokeSkill";
		log.from = zhanghe;
		log.arg = objectName();
		room->sendLog(log);
		zhanghe->peiyin(this);
		room->notifySkillInvoked(zhanghe, objectName());

		if (choice.startsWith("mark"))
			zhanghe->loseMark("&olzhbian");
		else {
			QString discard_prompt = QString("#olqiaobian-%1").arg(index);
			room->askForDiscard(zhanghe, objectName(), 1, 1, false, true, discard_prompt);
		}

		if (!zhanghe->isAlive()) return false;

		QString use_prompt = QString("@olqiaobian-%1").arg(index);
		if (!zhanghe->isSkipped(change.to) && (index == 2 || index == 3))
			room->askForUseCard(zhanghe, "@@olqiaobian", use_prompt, index);
		zhanghe->skip(change.to, true);
		return false;
	}
};

class OLQiaobianGameStart : public GameStartSkill
{
public:
	OLQiaobianGameStart() : GameStartSkill("#olqiaobian")
	{
	}

	void onGameStart(ServerPlayer *player) const
	{
		if (!player->hasSkill("olqiaobian")) return;
		Room *room = player->getRoom();
		room->sendCompulsoryTriggerLog(player, "olqiaobian", true, true);
		player->gainMark("&olzhbian", 2);
	}
};

class OLQiaobianMark : public PhaseChangeSkill
{
public:
	OLQiaobianMark() : PhaseChangeSkill("#olqiaobian-mark")
	{
        global = true;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		QStringList records = player->property("SkillDescriptionRecord_olqiaobian").toStringList();
		QString hand = QString::number(player->getHandcardNum());
		if (records.contains(hand)) return false;
		bool empty = records.isEmpty();
		records << hand;
		room->safeSetPlayerProperty(player,"SkillDescriptionRecord_olqiaobian", records);
		player->setSkillDescriptionSwap("olqiaobian","%arg11",records.join(","));
		room->changeTranslation(player, "olqiaobian", 1);
		if (!empty && player->hasSkill("olqiaobian")) {
			room->sendCompulsoryTriggerLog(player, "olqiaobian");
			player->gainMark("&olzhbian");
		}
		return false;
	}
};

class OLBeige : public TriggerSkill
{
public:
	OLBeige() : TriggerSkill("olbeige")
	{
		events << Damaged;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Damaged) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash"))
				return false;

			foreach (ServerPlayer *caiwenji, room->getAllPlayers()) {
				if (player->isDead()) return false;
				if (!TriggerSkill::triggerable(caiwenji)) continue;
				if (!caiwenji->isNude() && caiwenji->askForSkillInvoke(this, player)) {
					caiwenji->peiyin(this);

					JudgeStruct judge;
					judge.good = true;
					judge.play_animation = false;
					judge.who = player;
					judge.reason = objectName();
					room->judge(judge);

					QString suit = judge.card->getSuitString();
					QString number = judge.card->getNumberString();
					caiwenji->setTag("OLbeigeTag", QVariant::fromValue(suit+"+"+number));
					const Card *card = room->askForDiscard(caiwenji, objectName(), 1, 1, true, true,
						"@olbeige-discard:" + player->objectName() + ":" + number + ":" + "<img src='image/system/cardsuit/" + suit + ".png' height=17/>");
					caiwenji->removeTag("OLbeigeTag");
					if (!card) continue;

					if (suit == "heart")
						room->recover(player, RecoverStruct("olbeige", caiwenji));
					else if (suit == "diamond")
						player->drawCards(2, objectName());
					else if (suit == "club") {
						if (damage.from && damage.from->isAlive())
							room->askForDiscard(damage.from, objectName(), 2, 2, false, true);
					} else if (suit == "spade") {
						if (damage.from && damage.from->isAlive())
							damage.from->turnOver();
					}

					if (caiwenji->isAlive()) {
						if (card->getNumberString() == number) {
							if (room->CardInPlace(card, Player::DiscardPile))
								room->obtainCard(caiwenji, card);
						}
						if (card->getSuitString() == suit) {
							if (room->CardInPlace(judge.card, Player::DiscardPile))
								room->obtainCard(caiwenji, judge.card);
						}
					}
				}
			}
		}
		return false;
	}
};

class OLJieming : public MasochismSkill
{
public:
	OLJieming() : MasochismSkill("oljieming")
	{
		waked_skills = "#oljieming";
	}

	void onDamaged(ServerPlayer *xunyu, const DamageStruct &damage) const
	{
		Room *room = xunyu->getRoom();
		for (int i = 0; i < damage.damage; i++) {
			ServerPlayer *to = room->askForPlayerChosen(xunyu, room->getAlivePlayers(), objectName(), "@oljieming-invoke", true, true);
			if (!to) break;
			xunyu->peiyin(this);

			int upper = qMin(5, to->getMaxHp());
			to->drawCards(upper, objectName());

			if (to->isAlive() && to->canDiscard(to, "h") && to->getHandcardNum() > upper)
				room->askForDiscard(to, objectName(), to->getHandcardNum() - upper, to->getHandcardNum() - upper);

			if (!xunyu->isAlive())
				break;
		}
	}
};

class OLJiemingDeath : public TriggerSkill
{
public:
	OLJiemingDeath() : TriggerSkill("#oljieming")
	{
		events << Death;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->hasSkill("oljieming");
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *xunyu, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who != xunyu) return false;
		ServerPlayer *to = room->askForPlayerChosen(xunyu, room->getAlivePlayers(), "oljieming", "@oljieming-invoke", true, true);
		if (!to) return false;
		xunyu->peiyin("oljieming");

		int upper = qMin(5, to->getMaxHp());
		to->drawCards(upper, "oljieming");

		if (to->isAlive() && to->canDiscard(to, "h") && to->getHandcardNum() > upper)
			room->askForDiscard(to, "oljieming", to->getHandcardNum() - upper, to->getHandcardNum() - upper);
		return false;
	}
};

OLQiangxiCard::OLQiangxiCard()
{
}

bool OLQiangxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getMark("olqiangxiTarget-Clear") <= 0;
}

void OLQiangxiCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = to->getRoom();

	room->addPlayerMark(to, "olqiangxiTarget-Clear");

	if (subcards.isEmpty())
		room->damage(DamageStruct("olqiangxi", nullptr, from));
	room->damage(DamageStruct("olqiangxi", from, to));
}

class OLQiangxi : public ViewAsSkill
{
public:
	OLQiangxi() : ViewAsSkill("olqiangxi")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("OLQiangxiCard") < 2;
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return selected.isEmpty() && to_select->isKindOf("Weapon") && !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return new OLQiangxiCard;
		else if (cards.length() == 1) {
			OLQiangxiCard *card = new OLQiangxiCard;
			card->addSubcards(cards);
			return card;
		}
		return nullptr;
	}
};

class OLNinge : public MasochismSkill
{
public:
	OLNinge() : MasochismSkill("olninge")
	{
		frequency = Compulsory;
        global = true;
	}

	void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
	{
		Room *room = player->getRoom();
		if (!room->hasCurrent()) return;
		player->addMark("olningeTimes-Clear");

		int mark = player->getMark("olningeTimes-Clear");
		if (mark != 2) return;

		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			if (p == player || damage.from == p) {
				room->sendCompulsoryTriggerLog(p, this);
				p->drawCards(1, objectName());
				if (p->isAlive() && player->isAlive() && p->canDiscard(player, "ej")) {
					int id = room->askForCardChosen(p, player, "ej", objectName(), false, Card::MethodDiscard);
					room->throwCard(id, player, p);
				}
			}
		}
	}
};

OLJianmieCard::OLJianmieCard()
{
}

bool OLJianmieCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty() && to!=Self;
}

void OLJianmieCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	QString choice1 = room->askForChoice(effect.from, "oljianmie", "red+black", QVariant::fromValue(effect));
	QString choice2 = room->askForChoice(effect.to, "oljianmie", "red+black", QVariant::fromValue(effect));
	const Card*dc = room->askForDiscard(effect.from, "oljianmie", 999,999,false,false,"",".|"+choice1);
	int n = 0, x = 0;
	if(dc) n = dc->subcardsLength();
	dc = room->askForDiscard(effect.to, "oljianmie", 999,999,false,false,"",".|"+choice2);
	if(dc) x = dc->subcardsLength();
	Card*duel = Sanguosha->cloneCard("duel");
	duel->setSkillName("_oljianmie");
	if(n>x&&effect.from->canUse(duel,effect.to))
		room->useCard(CardUseStruct(duel,effect.from,effect.to));
	else if(x>n&&effect.to->canUse(duel,effect.from))
		room->useCard(CardUseStruct(duel,effect.to,effect.from));
	duel->deleteLater();
}

class OLJianmie : public ZeroCardViewAsSkill
{
public:
	OLJianmie() : ZeroCardViewAsSkill("oljianmie")
	{
	}
	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("OLJianmieCard") < 1;
	}

	const Card *viewAs() const
	{
		return new OLJianmieCard;
	}
};

OLMiejiCard::OLMiejiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool OLMiejiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void OLMiejiCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	CardMoveReason reason(CardMoveReason::S_REASON_PUT, effect.from->objectName(), "olmieji", "");
	room->moveCardTo(this, effect.from, nullptr, Player::DrawPile, reason, true);

	QList<const Card *> cards = effect.to->getCards("he");
	foreach (const Card *c, cards) {
		if (effect.to->isJilei(c))
			cards.removeOne(c);
	}

	if (cards.isEmpty())
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
		if (instanceDiscardId == -1) d.addSubcards(cards);
		else d.addSubcard(instanceDiscardId);
		room->throwCard(&d, effect.to);
	} else if (!room->askForCard(effect.to, "@@olmiejidiscard!", "@mieji-discard")) {
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
		room->throwCard(&d, effect.to);
	}
}

class OLMieji : public OneCardViewAsSkill
{
public:
	OLMieji() : OneCardViewAsSkill("olmieji")
	{
		filter_pattern = "TrickCard";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("OLMiejiCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		OLMiejiCard *card = new OLMiejiCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class OLMiejiDiscard : public ViewAsSkill
{
public:
	OLMiejiDiscard() : ViewAsSkill("OLMiejidiscard")
	{

	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@olmiejidiscard!";
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
		if (!ok) return nullptr;
		DummyCard *dummy = new DummyCard;
		dummy->addSubcards(cards);
		return dummy;
	}
};

class OLLihuo : public TriggerSkill
{
public:
	OLLihuo() : TriggerSkill("ollihuo")
	{
		events << CardFinished << ChangeSlash;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == ChangeSlash) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->objectName() != "slash") return false;
			FireSlash *fire_slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
			fire_slash->addSubcard(use.card);
			fire_slash->setSkillName("ollihuo");
			fire_slash->deleteLater();
			foreach (ServerPlayer *p, use.to) {
				if (!player->canSlash(p, fire_slash, false))
					return false;
			}
			if (player->askForSkillInvoke(this, data, false)) {
				room->setCardFlag(fire_slash,"ollihuoUse");
				use.changeCard(fire_slash);
				data = QVariant::fromValue(use);
			}
		} else if (triggerEvent == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->hasFlag("ollihuoUse")&&use.card->hasFlag("DamageDone")) {
				room->sendCompulsoryTriggerLog(player, this);
				if(!room->askForDiscard(player,objectName(),1,1,true,true,"ollihuo0:"))
					room->loseHp(HpLostStruct(player, 1, objectName(), player));
			}
		}
		return false;
	}
};

OLChunlaoCard::OLChunlaoCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void OLChunlaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
	if(pattern.contains("olchunlao")){
		source->obtainCard(this);
	}else{
		ServerPlayer *who = room->getCurrentDyingPlayer();
		if (who&&subcards.length()>0) {
			room->throwCard(subcards, "olchunlao", nullptr);
			Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
			analeptic->setSkillName("_olchunlao");
			room->useCard(CardUseStruct(analeptic, who, who, false));
			analeptic->deleteLater();
		}
	}
}

class OLChunlaoViewAsSkill : public ViewAsSkill
{
public:
	OLChunlaoViewAsSkill() : ViewAsSkill("olchunlao")
	{
		expand_pile = "wine";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if(pattern.contains("olchunlao")) return true;
		return pattern.contains("peach") && !player->getPile("wine").isEmpty();
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.contains("olchunlao")){
			if(selected.length()>1) return false;
		}else if(selected.length()>0)
			return false;
		ExpPattern p(".|.|.|wine");
		return p.match(Self, to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.contains("olchunlao")){
			if(cards.length()<2) return nullptr;
		}else if(cards.length()<1)
			return nullptr;
		Card *wine = new OLChunlaoCard;
		wine->setSkillName(objectName());
		wine->addSubcards(cards);
		return wine;
	}
};

class OLChunlao : public TriggerSkill
{
public:
	OLChunlao() : TriggerSkill("olchunlao")
	{
		events << CardsMoveOneTime << HpLost;
		view_as_skill = new OLChunlaoViewAsSkill;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (!move.reason.m_playerId.isEmpty()&&move.from&&move.from->isAlive()&&(move.from==player||player->isAdjacentTo(move.from))
				&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD) {
				QList<int>ids;
				foreach (int id, move.card_ids) {
					if (Sanguosha->getCard(id)->isKindOf("Slash")&&room->getCardPlace(id)==Player::DiscardPile)
						ids << id;
				}
				if (ids.length()>0&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					player->addToPile("wine",ids);
				}
			}
		}else{
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if(p->getPile("wine").length()>0&&p->hasSkill(objectName())){
					room->askForUseCard(p,"@@olchunlao","olchunlao0:");
				}
			}
		}
		return false;
	}
};

class OLRenxin : public TriggerSkill
{
public:
	OLRenxin() : TriggerSkill("olrenxin")
	{
		events << Dying;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Dying) {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.who!=player&&room->askForCard(player,"EquipCard","olrenxin0:"+dying.who->objectName(),data,objectName())) {
				player->peiyin(this);
				player->turnOver();
				room->recover(dying.who,RecoverStruct(objectName(),player,1-dying.who->getHp()));
			}
		}
		return false;
	}
};

class OLChengxiang : public TriggerSkill
{
public:
	OLChengxiang() : TriggerSkill("olchengxiang")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		for (int i = 0; i < damage.damage; i++) {
			if (!player->askForSkillInvoke(this, QVariant::fromValue(damage))) continue;
			room->broadcastSkillInvoke(objectName());
			QList<int> card_ids = room->getNCards(4);
			room->fillAG(card_ids);
			QList<int> to_get, to_throw;
			while (!card_ids.isEmpty()) {
				int sum = 0;
				foreach(int id, to_get)
					sum += Sanguosha->getCard(id)->getNumber();
				foreach (int id, card_ids) {
					if (sum + Sanguosha->getCard(id)->getNumber() > 13) {
						room->takeAG(nullptr, id, false);
						card_ids.removeOne(id);
						to_throw << id;
					}
				}
				if (card_ids.isEmpty()) break;
				sum = room->askForAG(player, card_ids, card_ids.length() < 4, objectName());
				if (sum == -1) break;
				card_ids.removeOne(sum);
				to_get << sum;
				room->takeAG(player, sum, false);
			}
			room->getThread()->delay();
			room->clearAG();
			DummyCard *dummy = new DummyCard(to_get);
			if (dummy->subcardsLength()>0)
				player->obtainCard(dummy);
			dummy->clearSubcards();
			dummy->addSubcards(to_throw + card_ids);
			if (dummy->subcardsLength()>0) {
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), "");
				room->throwCard(dummy, reason, nullptr);
			}
			delete dummy;
			if (player->isDead()) break;
		}
		return false;
	}
};

OLGanluCard::OLGanluCard()
{
	will_throw = false;
}

void OLGanluCard::swapEquip(ServerPlayer *first, ServerPlayer *second) const
{
	Room *room = first->getRoom();
	room->swapEquips(first, second, "olganlu");
}

bool OLGanluCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	foreach(const Player *to, targets){
		if(to->hasEquip())
			return targets.length() == 2;
	}
	return false;
}

bool OLGanluCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.length()<2;
}

void OLGanluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if(qAbs(targets.first()->getEquips().length() - targets.last()->getEquips().length()) > Self->getLostHp()){
		room->throwCard(this,getSkillName(),source);
	}
	LogMessage log;
	log.type = "#GanluSwap";
	log.from = source;
	log.to = targets;
	room->sendLog(log);
	swapEquip(targets.first(), targets.last());
}

class OLGanlu : public ViewAsSkill
{
public:
	OLGanlu() : ViewAsSkill("olganlu")
	{
	}
	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return !Self->isJilei(to_select);
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("OLGanluCard");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		OLGanluCard *sc = new OLGanluCard;
		sc->addSubcards(cards);
		return sc;
	}
};

class OLBuyi : public TriggerSkill
{
public:
	OLBuyi() : TriggerSkill("olbuyi")
	{
		events << Dying;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *wuguotai, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		ServerPlayer *who = dying.who;
		if (who->isNude()) return false;
		if (who->getHp() < 1 && wuguotai->askForSkillInvoke(this, who)) {
			int card_id = room->askForCardChosen(wuguotai, who, "he", "buyi");

			room->showCard(who, card_id);
			const Card *card = Sanguosha->getCard(card_id);

			if (card->getTypeId() != Card::TypeBasic&&!who->isJilei(card)) {
				room->throwCard(card, objectName(), who);
				room->broadcastSkillInvoke(objectName());
				room->recover(who, RecoverStruct(objectName(), wuguotai));
			}
		}
		return false;
	}
};

class OLQieting : public TriggerSkill
{
public:
	OLQieting() : TriggerSkill("olqieting")
	{
		events << EventPhaseChanging << DamageDone << TargetSpecified;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive || player->getMark("qietingDamageDone-Clear") > 0) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (!TriggerSkill::triggerable(p)||!p->askForSkillInvoke(this,player)) continue;
				room->broadcastSkillInvoke(objectName());
				QStringList choices;
				for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
					if (player->getEquip(i) && !p->getEquip(i) && p->hasEquipArea(i))
						choices << QString::number(i);
				}
				choices << "draw";
				QString choice = room->askForChoice(p, objectName(), choices.join("+"), QVariant::fromValue(player));
				choices.removeOne(choice);
				if (choice == "draw") {
					p->drawCards(1, objectName());
				} else {
					const Card *card = player->getEquip(choice.toInt());
					room->moveCardTo(card, p, Player::PlaceEquip);
					foreach (QString ch, choices) {
						if(ch != "draw")
							choices.removeOne(ch);
					}
				}
				if(player->isDead()||p->isDead()||player->getMark("qietingTo-Clear")>0) continue;
				choices << "cancel";
				choice = room->askForChoice(p, objectName(), choices.join("+"), QVariant::fromValue(player));
				if(choice=="cancel") continue;
				if (choice == "draw") {
					p->drawCards(1, objectName());
				} else {
					const Card *card = player->getEquip(choice.toInt());
					room->moveCardTo(card, p, Player::PlaceEquip);
				}
			}
		}else if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from)
				damage.from->addMark("qietingDamageDone-Clear");
		}else if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId() != Card::TypeSkill) {
				foreach (ServerPlayer *p, use.to) {
					if (p != player) {
						player->addMark("qietingTo-Clear");
					}
				}
			}
		}
		return false;
	}
};

OLXianzhouCard::OLXianzhouCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void OLXianzhouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
	foreach (ServerPlayer *p, targets) {
		if (pattern == "@xianzhou") {
			room->damage(DamageStruct(getSkillName(),source,p));
		} else {
			room->removePlayerMark(source, "@olxianzhou");
			room->doSuperLightbox(source, "olxianzhou");
			room->setPlayerMark(p, "olxianzhou", source->getEquips().length());
			room->giveCard(source,p,source->getEquipsId(),getSkillName());
		}
	}
}

bool OLXianzhouCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	return targets.length() == Self->getMark("olxianzhou");
}

bool OLXianzhouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < Self->getMark("olxianzhou") && Self->inMyAttackRange(to_select);
}

class OLXianzhou : public ZeroCardViewAsSkill
{
public:
	OLXianzhou() : ZeroCardViewAsSkill("olxianzhou")
	{
		frequency = Skill::Limited;
		limit_mark = "@olxianzhou";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@olxianzhou") > 0 && player->getEquips().length() > 0;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@olxianzhou";
	}

	const Card *viewAs() const
	{
		return new OLXianzhouCard;
	}
};

OLZhijianCard::OLZhijianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool OLZhijianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (!targets.isEmpty() || to_select == Self) return false;
	const Card *card = Sanguosha->getCard(getEffectiveId());
	return !Self->isProhibited(to_select, card);
}

void OLZhijianCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QList<CardsMoveStruct> exchangeMove;
	const Card *card = Sanguosha->getCard(getEffectiveId());
	const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
	int equip_index = equip->location();
	if (effect.to->getEquips(equip_index).length()>=effect.to->getEquipArea(equip_index)){
		CardsMoveStruct move2(effect.to->getEquip(equip_index)->getEffectiveId(), nullptr, Player::DiscardPile,
			CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, effect.to->objectName(), getSkillName(), ""));
		exchangeMove.append(move2);
	}
	CardsMoveStruct move1(getEffectiveId(), effect.to, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_USE, effect.to->objectName(), getSkillName(), ""));
	exchangeMove.append(move1);
	room->moveCardsAtomic(exchangeMove, true);
	effect.from->drawCards(1, getSkillName());
}

class OLZhijian : public OneCardViewAsSkill
{
public:
	OLZhijian() :OneCardViewAsSkill("olzhijian")
	{
		filter_pattern = "EquipCard";
	}
	const Card *viewAs(const Card *originalCard) const
	{
		OLZhijianCard *olzhijian_card = new OLZhijianCard();
		olzhijian_card->addSubcard(originalCard);
		return olzhijian_card;
	}
};

class OLGuzheng : public TriggerSkill
{
public:
	OLGuzheng() : TriggerSkill("olguzheng")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (!move.from || player == move.from || player->getMark("olguzheng-"+QString::number(move.from->getPhase())+"Clear")>0
				|| (move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
				return false;
			int i = 0;
			QList<int>ids;
			foreach (int id, move.card_ids) {
				if (move.from_places[i] == Player::PlaceHand||move.from_places[i] == Player::PlaceEquip){
					if(room->getCardPlace(id)==Player::DiscardPile)
						ids << id;
				}
				i++;
			}
			player->setMark("olguzhengNum",ids.length());
			if (move.from->isAlive()&&ids.length()>1&&player->askForSkillInvoke(this,(ServerPlayer*)move.from)) {
				player->peiyin(this);
				player->addMark("olguzheng-"+QString::number(move.from->getPhase())+"Clear");
				room->fillAG(ids,player);
				i = room->askForAG(player, ids, false, objectName());
				room->takeAG(player, i, false ,QList<ServerPlayer *>()<<player);
				room->obtainCard((ServerPlayer *)move.from,i);
				ids.removeOne(i);
				if(player->askForSkillInvoke(this,"olguzheng0",false)){
					DummyCard*dc = new DummyCard(ids);
					room->obtainCard(player,dc);
					dc->deleteLater();
				}
				room->clearAG(player);
			}
		}
		return false;
	}
};

class OLJiushiVs : public ZeroCardViewAsSkill
{
public:
	OLJiushiVs() : ZeroCardViewAsSkill("oljiushi")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->faceUp()&&Analeptic::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern.contains("analeptic") && player->faceUp();
	}

	const Card *viewAs() const
	{
		Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
		analeptic->setSkillName(objectName());
		return analeptic;
	}
};

class OLJiushi : public TriggerSkill
{
public:
	OLJiushi() : TriggerSkill("oljiushi")
	{
		events << PreCardUsed << DamageDone << DamageComplete << CardsMoveOneTime;
		view_as_skill = new OLJiushiVs;
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == PreCardUsed)
			return 5;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getSkillNames().contains(objectName()))
				player->turnOver();
			if(!player->faceUp()&&use.card->hasTip("luoying")){
				use.no_respond_list << "_ALL_TARGETS";
				data.setValue(use);
			}
		} else if (triggerEvent == DamageDone) {
			player->setTag("PredamagedFace", !player->faceUp());
		} else if (triggerEvent == DamageComplete) {
			bool facedown = player->getTag("PredamagedFace").toBool();
			player->removeTag("PredamagedFace");
			if (facedown && !player->faceUp() && player->askForSkillInvoke(this, data)) {
				room->broadcastSkillInvoke(objectName(), 3);
				player->turnOver();
			}
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place == Player::PlaceHand&&move.to==player&&move.reason.m_skillName=="luoying"){
				foreach (int id, move.card_ids) {
					player->addMark("luoyingNum-SelfClear");
					if(player->hasCard(id))
						room->setCardTip(id,"luoying");
				}
				if(!player->faceUp()&&player->getMark("luoyingNum-SelfClear")>=player->getMaxHp()
					&&!player->hasFlag("CurrentPlayer")&&player->askForSkillInvoke(this, data)){
					player->setMark("luoyingNum-SelfClear",0);
					room->broadcastSkillInvoke(objectName(), 3);
					player->turnOver();
				}
			}
		}
		return false;
	}
};

class OLJizhi : public TriggerSkill
{
public:
	OLJizhi() : TriggerSkill("oljizhi")
	{
		events << CardUsed;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("TrickCard")&&!use.card->isVirtualCard()&&player->askForSkillInvoke(this,data)){
				player->peiyin("jizhi");
				int id = player->drawCardsList(1,objectName()).first();
				if(Sanguosha->getCard(id)->isKindOf("BasicCard")&&player->hasCard(id)&&room->askForCard(player,QString::number(id),"oljizhi0:"))
					room->addMaxCards(player,1);
			}
		}
		return false;
	}
};

class OLQicai : public TargetModSkill
{
public:
	OLQicai() : TargetModSkill("olqicai")
	{
		pattern = ".";
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const
	{
		if (card->isKindOf("TrickCard")&&from->hasSkill(objectName()))
			return 999;
		if (card->hasTip("luoying")&&!from->faceUp()&&from->hasSkill("oljiushi"))
			return 999;
		return 0;
	}
};

class OLQicaiLimit : public CardLimitSkill
{
public:
	OLQicaiLimit() : CardLimitSkill("#olqicai-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "discard";//设置为限制弃置
	}

	QString limitPattern(const Player *target, const Card *card) const
	{
		if(card->isKindOf("Armor")||card->isKindOf("Treasure")){
			foreach (const Player *p, target->getAliveSiblings()) {//获取其他角色
				if (p->getEquipsId().contains(card->getId())//这张牌在他的装备区
					&&p->hasSkill("olqicai"))//且这个角色拥有奇才
					return card->toString();//则这张牌不能被target弃置
			}
		}
		return "";
	}
};

class OL2HuojiVs : public OneCardViewAsSkill
{
public:
	OL2HuojiVs() : OneCardViewAsSkill("ol2huoji")
	{
		filter_pattern = ".|red";
		response_or_use = true;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		FireAttack *fire_attack = new FireAttack(originalCard->getSuit(), originalCard->getNumber());
		fire_attack->addSubcard(originalCard->getId());
		fire_attack->setSkillName(objectName());
		return fire_attack;
	}
};

class OL2Huoji : public TriggerSkill
{
public:
	OL2Huoji() : TriggerSkill("ol2huoji")
	{
		events << CardEffected;
		view_as_skill = new OL2HuojiVs;
	}
	int getEffectIndex(const ServerPlayer *player, const Card *) const
	{
		int index = qrand() % 2 + 1;
		if (player->getGeneralName().contains("pangtong") || player->getGeneral2Name().contains("pangtong"))
			index += 2;
		return index;
	}
	int getPriority(TriggerEvent) const
	{
		return 0;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (triggerEvent == CardEffected) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (effect.card->isKindOf("FireAttack")&&effect.from->isAlive()&&effect.from->hasSkill(objectName())){
				if (effect.nullified) {
					LogMessage log;
					log.type = "#CardNullified";
					log.from = effect.to;
					log.card_str = effect.card->toString();
					room->sendLog(log);
					return true;
				}
				effect.to->setFlags("Global_NonSkillNullify");
				if (!effect.offset_card)
					effect.offset_card = room->isCanceled(effect);
				if (effect.offset_card) {
					data.setValue(effect);
					if (!room->getThread()->trigger(CardOffset, room, effect.from, data)){
						return true;
					}
				}
				room->getThread()->trigger(CardOnEffect, room, effect.to, data);
				if (effect.to->getHandcardNum()>0&&effect.from->isAlive()){
					room->sendCompulsoryTriggerLog(effect.from,objectName(),true,!effect.card->getSkillName().contains("huoji"));
					const Card*c = effect.to->getRandomHandCard();
					room->showCard(effect.to,c->getId());
					if(room->askForCard(effect.from,".|"+c->getColorString()+"|.|hand","ol2huoji0:"+effect.to->objectName()+"::"+c->getColorString())){
						room->damage(DamageStruct(effect.card,effect.from,effect.to));
					}
				}
				return true;
			}
		}
		return false;
	}
};

class OL2KanpoVs : public OneCardViewAsSkill
{
public:
	OL2KanpoVs() : OneCardViewAsSkill("ol2kanpo")
	{
		filter_pattern = ".|black";
		response_or_use = true;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Card *ncard = new Nullification(originalCard->getSuit(), originalCard->getNumber());
		ncard->setSkillName(objectName());
		ncard->addSubcard(originalCard);
		return ncard;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (!pattern.contains("nullification")) return false;
		if (player->isKongcheng()) {
			foreach (const Card *c, player->getEquips()) {
				if (c->isBlack()) return true;
			}
			return !player->getHandPile().isEmpty();
		}
		return true;
	}

	bool isEnabledAtNullification(const ServerPlayer *player) const
	{
		if (player->isKongcheng()) {
			foreach (const Card *c, player->getEquips()) {
				if (c->isBlack()) return true;
			}
			return !player->getHandPile().isEmpty();
		}
		return true;
	}
};

class OL2Kanpo : public TriggerSkill
{
public:
	OL2Kanpo() : TriggerSkill("ol2kanpo")
	{
		events << CardUsed << TrickCardCanceling;
		view_as_skill = new OL2KanpoVs;
	}
	int getEffectIndex(const ServerPlayer *player, const Card *) const
	{
		int index = qrand() % 2 + 1;
		if (player->getGeneralName().contains("pangtong") || player->getGeneral2Name().contains("pangtong"))
			index += 2;
		return index;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Nullification")&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(use.from,objectName(),true,!use.card->getSkillName().contains("kanpo"));
				room->setCardFlag(use.card,"ol2kanpobf");
			}
		}else{
			CardEffectStruct effect = data.value<CardEffectStruct>();
			return effect.card->hasFlag("ol2kanpobf");
		}
		return false;
	}
};

class OLJiang : public TriggerSkill
{
public:
	OLJiang() : TriggerSkill("oljiang")
	{
		events << TargetSpecified << TargetConfirmed << CardsMoveOneTime;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *sunce, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place!=Player::DiscardPile) return false;
			foreach (int id, move.card_ids) {
				const Card*c = Sanguosha->getCard(id);
				if(c->isKindOf("Duel") || (c->isKindOf("Slash") && c->isRed())){
					sunce->addMark("oljiang-Clear");
					if(sunce->getMark("oljiang-Clear")==1&&room->getCardPlace(id)==Player::DiscardPile){
						if(sunce->askForSkillInvoke(this, data)){
							room->broadcastSkillInvoke(objectName());
							room->loseHp(sunce,1,true,sunce,objectName());
							if(sunce->isAlive()) sunce->obtainCard(c);
						}
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if (triggerEvent == TargetSpecified || (triggerEvent == TargetConfirmed && use.to.contains(sunce))) {
				if (use.card->isKindOf("Duel") || (use.card->isKindOf("Slash") && use.card->isRed())) {
					if (sunce->askForSkillInvoke(this, data)) {
						room->broadcastSkillInvoke(objectName());
						sunce->drawCards(1, objectName());
					}
				}
			}
		}
		return false;
	}
};

class OLHunzi : public PhaseChangeSkill
{
public:
	OLHunzi() : PhaseChangeSkill("olhunzi")
	{
		frequency = Wake;
		waked_skills = "yingzi,yinghun";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		if (player&&player->getPhase() == Player::Finish)
			return player->getMark("olhunzi-Clear") > 0;
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *sunce, Room *room) const
	{
		if(sunce->getPhase() == Player::Start){
			if (sunce->getHp() == 1) {
				LogMessage log;
				log.type = "#HunziWake";
				log.from = sunce;
				log.arg = "1";
				log.arg2 = objectName();
				room->sendLog(log);
			}else if(!sunce->canWake(objectName()))
				return false;
			room->sendCompulsoryTriggerLog(sunce, this);
			room->doSuperLightbox(sunce, objectName());
			room->setPlayerMark(sunce, objectName(), 1);
			if (room->changeMaxHpForAwakenSkill(sunce, -1, objectName()))
				room->handleAcquireDetachSkills(sunce, "yingzi|yinghun");
			sunce->addMark("olhunzi-Clear");
		}else if(sunce->getPhase() == Player::Finish){
			if(sunce->isWounded()&&room->askForChoice(sunce,objectName(),"draw+recover")=="recover")
				room->recover(sunce,RecoverStruct(objectName()));
			else
				sunce->drawCards(2,objectName());
		}
		return false;
	}
};

class OLEnyuan : public TriggerSkill
{
public:
	OLEnyuan() : TriggerSkill("olenyuan")
	{
		events << CardsMoveOneTime << Damaged;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to == player && move.from && move.from->isAlive() && move.from != move.to && move.card_ids.size() >= 2
				&& move.reason.m_reason != CardMoveReason::S_REASON_PREVIEWGIVE
				&& (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip)) {
				if (room->askForSkillInvoke(player, objectName(), data)) {
					player->peiyin(this,1);
					room->drawCards((ServerPlayer *)move.from, 1, objectName());
				}
			}
		} else if (triggerEvent == Damaged) {
			DamageStruct damage = data.value<DamageStruct>();
			ServerPlayer *source = damage.from;
			if (!source || source == player) return false;
			for (int i = 0; i < damage.damage; i++) {
				if (source->isAlive() && player->isAlive() && player->askForSkillInvoke(this, data)) {
					player->peiyin(this,2);
					if (!source->isKongcheng()){
						const Card *card = room->askForCard(source, ".|red|.|hand","olenyuan0:" + player->objectName(), data,Card::MethodNone);
						if (card){
							room->giveCard(source, player, card, objectName());
							continue;
						}
					}
					room->loseHp(HpLostStruct(source, 1, objectName(), player));
				}
			}
		}
		return false;
	}
};

OLXuanhuoCard::OLXuanhuoCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void OLXuanhuoCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	CardMoveReason r(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "olxuanhuo", "");
	room->obtainCard(effect.to, this, r, false);

	if (effect.from->isDead()) return;
	QList<ServerPlayer *> targets;
	foreach (ServerPlayer *p, room->getOtherPlayers(effect.to)) {
		if(effect.to->canSlash(p,false))
			targets << p;
	}
	ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "olxuanhuo", "olxuanhuo1:" + effect.to->objectName());
	if(target){
		room->doAnimate(1, effect.from->objectName(), target->objectName());
		if(!room->askForUseSlashTo(effect.to, target, "olxuanhuo2:"+target->objectName()+":"+effect.from->objectName(),false)){
			room->doGongxin(effect.from,effect.to,effect.to->handCards(),"olxuanhuo");
			Card*dc = new DummyCard;
			for (int i = 0; i < 2; i++) {
				int id = room->askForCardChosen(effect.from,effect.to,"he","olxuanhuo",true,Card::MethodNone,dc->getSubcards());
				if(id>-1) dc->addSubcard(id);
				else break;
			}
			effect.from->obtainCard(dc,false);
			dc->deleteLater();
		}
	}
}

class OLXuanhuoVS : public ViewAsSkill
{
public:
	OLXuanhuoVS() : ViewAsSkill("olxuanhuo")
	{
		response_pattern = "@@olxuanhuo";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const {
		return selected.length() < 2 && !to_select->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != 2)
			return nullptr;

		OLXuanhuoCard *c = new OLXuanhuoCard;
		c->addSubcards(cards);
		return c;
	}
};

class OLXuanhuo : public TriggerSkill
{
public:
	OLXuanhuo() : TriggerSkill("olxuanhuo")
	{
		events << EventPhaseEnd;
		view_as_skill = new OLXuanhuoVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Draw) return false;
		if (player->getHandcardNum() < 2) return false;
		room->askForUseCard(player, "@@olxuanhuo", "olxuanhuo0");
		return false;
	}
};

class OLDangxian : public TriggerSkill
{
public:
	OLDangxian() : TriggerSkill("oldangxian")
	{
		events << EventPhaseStart << EventPhaseEnd << DamageDone;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseEnd) {
			if(player->getPhase() == Player::Play&&player->getMark("oldangxiandraw-PlayClear")>0){
				if(player->getMark("oldangxianDamage-PlayClear")<1){
					room->sendCompulsoryTriggerLog(player, objectName());
					room->damage(DamageStruct(objectName(), player,player));
				}
			}
		}else if (event == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.from->getMark("oldangxiandraw-PlayClear")>0)
				damage.from->addMark("oldangxianDamage-PlayClear");
		}else if (player->getPhase() == Player::RoundStart&&player->hasSkill(objectName())) {
			room->sendCompulsoryTriggerLog(player, this);

			player->setFlags("oldangxian");
			player->insertPhase(Player::Play);/*
			room->broadcastProperty(player, "phase");
			if (!room->getThread()->trigger(EventPhaseStart, room, player))
				room->getThread()->trigger(EventPhaseProceeding, room, player);
			room->getThread()->trigger(EventPhaseEnd, room, player);
			player->setFlags("-oldangxian");*/

			player->setPhase(Player::RoundStart);
			room->broadcastProperty(player, "phase");
		} else if (player->getPhase() == Player::Play&&player->hasFlag("oldangxian")){
			player->setFlags("-oldangxian");
			if(player->askForSkillInvoke(this,"draw",false)) {
				player->addMark("oldangxiandraw-PlayClear");
				QList<int> ids = room->getDiscardPile();
				ids << room->getDrawPile();
				qShuffle(ids);
				foreach (int id, ids) {
					if (Sanguosha->getCard(id)->isKindOf("Slash")){
						room->obtainCard(player, id,true);
						break;
					}
				}
			}
		}
		return false;
	}
};

class OLFuli : public TriggerSkill
{
public:
	OLFuli() : TriggerSkill("olfuli")
	{
		events << AskForPeaches << Damage;
		frequency = Limited;
		limit_mark = "@olfuli";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	int getKingdoms(Room *room) const
	{
		QSet<QString> kingdom_set;
		foreach(ServerPlayer *p, room->getAlivePlayers())
			kingdom_set << p->getKingdom();
		return kingdom_set.size();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *liaohua, QVariant &data) const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			liaohua->addMark("olfuliDamage",damage.damage);
			return false;
		}
		DyingStruct dying_data = data.value<DyingStruct>();
		if (dying_data.who != liaohua || liaohua->getMark("@olfuli")<1||!liaohua->hasSkill(objectName())) return false;
		if (liaohua->askForSkillInvoke(this, data)) {
			room->broadcastSkillInvoke(objectName());

			room->doSuperLightbox(liaohua, "olfuli");

			room->removePlayerMark(liaohua, "@olfuli");
			int x = getKingdoms(room);
			int n = qMin(x - liaohua->getHp(), liaohua->getMaxHp() - liaohua->getHp());
			if (n > 0) room->recover(liaohua, RecoverStruct(liaohua, nullptr, n, objectName()));
			if (liaohua->getHandcardNum() < x) liaohua->drawCards(x - liaohua->getHandcardNum(), objectName());
			if (x > liaohua->getMark("olfuliDamage")) liaohua->turnOver();
		}
		return false;
	}
};

class OLJiangchi : public TriggerSkill
{
public:
	OLJiangchi() : TriggerSkill("oljiangchi")
	{
		events << EventPhaseEnd;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseEnd) {
			if(player->getPhase()==Player::Draw&&player->getCardCount()>0&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				const Card*c = room->askForCard(player,"..","oljiangchi0",data,Card::MethodRecast);
				if(c){
					LogMessage log;
					log.type = "$RecastCard";
					log.from = player;
					log.card_str = c->toString();
					room->sendLog(log);
					player->broadcastSkillInvoke("@recast");
					room->moveCardTo(c, player, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, player->objectName(), "oljiangchi", ""));
					player->drawCards(1, "recast");
					room->addSlashJuli(player,999);
					room->addSlashCishu(player,1);
				}else{
					player->drawCards(1,objectName());
					room->setPlayerCardLimitation(player,"ignore","Slash",true);
					room->addSlashCishu(player,-1);
				}
			}
		}
		return false;
	}
};

OLZongxuanCard::OLZongxuanCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

void OLZongxuanCard::use(Room *room, ServerPlayer *player, QList<ServerPlayer *> &) const
{
	CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), "olzongxuan", "");
	room->moveCardTo(this, nullptr, Player::DrawPile, reason, true, true);
}

class OLZongxuanVS : public ViewAsSkill
{
public:
	OLZongxuanVS() : ViewAsSkill("olzongxuan")
	{
		expand_pile = "#olzongxuan";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return Self->getPile("#olzongxuan").contains(to_select->getEffectiveId());
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@olzongxuan");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		OLZongxuanCard *card = new OLZongxuanCard;
		card->addSubcards(cards);
		return card;
	}
};

class OLZongxuan : public TriggerSkill
{
public:
	OLZongxuan() : TriggerSkill("olzongxuan")
	{
		events << CardsMoveOneTime;
		view_as_skill = new OLZongxuanVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place == Player::DiscardPile&&((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {
			if(move.from == player->getNextAlive(player->aliveCount()-1)){
				if(move.from->getMark("olzongxuanUse-Clear")>0)
					return false;
				move.from->addMark("olzongxuanUse-Clear");
			}else if(move.from != player)
				return false;
			int i = 0;
			QList<int> ids;
			foreach (int id, move.card_ids) {
				if (room->getCardPlace(id) == Player::DiscardPile
					&& (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip)) {
					ids << id;
				}
				i++;
			}
			if (ids.isEmpty())
				return false;

			room->notifyMoveToPile(player, ids, objectName(), Player::DiscardPile, true);
			room->askForUseCard(player, "@@olzongxuan", "olzongxuan0");
		}
		return false;
	}
};

class OLZhiyan : public PhaseChangeSkill
{
public:
	OLZhiyan() : PhaseChangeSkill("olzhiyan")
	{
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool onPhaseChange(ServerPlayer *target, Room *room) const
	{
		if (target->getPhase() != Player::Finish)
			return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if(!p->isAlive()||!p->hasSkill(objectName())) continue;
			if(target!=p&&target!=p->getNextAlive(p->aliveCount()-1)) continue;
			ServerPlayer *to = room->askForPlayerChosen(p, room->getAlivePlayers(), objectName(), "olzhiyan0", true, true);
			if (to) {
				room->broadcastSkillInvoke(objectName());
				int id = to->drawCardsList(1, objectName()).first();
				if (!to->isAlive()) return false;
				room->showCard(to, id);
				const Card *card = Sanguosha->getCard(id);
	
				if (card->isKindOf("EquipCard")) {
					if (to->isAlive() && card->isAvailable(to) && !to->getEquipsId().contains(id)){
						room->useCard(CardUseStruct(card, to));
						room->recover(to, RecoverStruct("olzhiyan", p));
					}
				} else if (to->getHp()!=p->getHp())
					room->loseHp(to,1,true,p,objectName());
			}
		}
		return false;
	}
};

class OLJieZishou : public TriggerSkill
{
public:
	OLJieZishou() : TriggerSkill("oljiezishou")
	{
		events << DrawNCards << DamageDone << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DrawNCards) {
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase"||!player->hasSkill(objectName())||!player->askForSkillInvoke(this,data)) return false;
			player->addMark("oljiezishouUse-Clear");
			player->peiyin(this);
			QStringList kingdoms;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(kingdoms.contains(p->getKingdom())) continue;
				kingdoms << p->getKingdom();
			}
			draw.num += kingdoms.length();
			data.setValue(draw);
		}else if (event == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.from!=player)
				damage.from->addMark("oljiezishouDamage-Clear");
		}else if(player->getPhase()==Player::Finish&&player->getMark("oljiezishouUse-Clear")>0
			&&player->getMark("oljiezishouDamage-Clear")>0){
			QStringList kingdoms;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(kingdoms.contains(p->getKingdom())) continue;
				kingdoms << p->getKingdom();
			}
			room->askForDiscard(player,objectName(),kingdoms.length(),kingdoms.length(),false,true);
		}
		return false;
	}
};

class OLZongshi : public TriggerSkill
{
public:
	OLZongshi() : TriggerSkill("olzongshi")
	{
		events << DamageCaused;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.to->hasSkill(objectName())&&damage.to->getMark("olzongshi"+player->getKingdom())<1){
				room->sendCompulsoryTriggerLog(damage.to,this);
				damage.to->addMark("olzongshi"+player->getKingdom());
				damage.to->damageRevises(data,-damage.damage);
				player->drawCards(1,objectName());
				return true;
			}
		}
		return false;
	}
};

class OLZhuikong : public TriggerSkill
{
public:
	OLZhuikong() : TriggerSkill("olzhuikong")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase()==Player::RoundStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->isWounded()&&p->hasSkill(objectName())&&p->canPindian(player)&&p->askForSkillInvoke(this,player)){
					p->peiyin(this);
					PindianStruct *pd = p->PinDian(player,objectName());
					if(pd->success)
						room->setPlayerFlag(player,"mobilezhuikong");
					else{
						if(p->isAlive()&&!room->getCardOwner(pd->to_card->getEffectiveId()))
							p->obtainCard(pd->to_card);
						Card*dc = Sanguosha->cloneCard("slash");
						dc->setSkillName("_olzhuikong");
						if(player->canSlash(p,dc,false))
							room->useCard(CardUseStruct(dc,player,p));
						dc->deleteLater();
					}
				}
			}
		}
		return false;
	}
};

class OLQiuyuan : public TriggerSkill
{
public:
	OLQiuyuan() : TriggerSkill("olqiuyuan")
	{
		events << TargetConfirming;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash")||(use.card->isKindOf("TrickCard")&&use.card->isDamageCard())) {
				QList<ServerPlayer *>tps = room->getOtherPlayers(use.from);
				foreach (ServerPlayer *p, tps) {
					if(p==player||use.to.contains(p))
						tps.removeOne(p);
				}
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"olqiuyuan0:"+use.card->objectName(),true,true);
				if(tp){
					player->peiyin(this);
					const Card*dc = room->askForCard(tp,use.card->getType()+"+^%"+use.card->objectName(),"olqiuyuan1:"+use.card->objectName(),data,Card::MethodNone);
					if(dc){
						player->obtainCard(dc);
					}else{
						use.to << tp;
						room->sortByActionOrder(use.to);
						data.setValue(use);
					}
				}
			}
		}
		return false;
	}
};

class OLJieJingce : public TriggerSkill
{
public:
	OLJieJingce() : TriggerSkill("oljiejingce")
	{
		events << CardUsed << EventPhaseEnd;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0) {
				if(player->getMark(use.card->getSuitString()+"oljiejingceSuit-Clear")<1
				&&player->hasFlag("CurrentPlayer")&&player->hasSkill(objectName())){
					player->addMark(use.card->getSuitString()+"oljiejingceSuit-Clear");
					room->sendCompulsoryTriggerLog(player,objectName());
					room->addMaxCards(player,1);
				}
				if(player->getMark(use.card->getType()+"oljiejingceType-Clear")<1&&player->hasSkill(objectName(),true)){
					player->addMark(use.card->getType()+"oljiejingceType-Clear");
					foreach (QString m, player->getMarkNames()) {
						if(m.contains("&oljiejingce+:+")&&player->getMark(m)>0){
							room->setPlayerMark(player,m,0);
							m.remove("-Clear");
							QStringList ms = m.split("+");
							ms << use.card->getType()+"_char";
							room->setPlayerMark(player,ms.join("+")+"-Clear",1);
							return false;
						}
					}
					room->setPlayerMark(player,"&oljiejingce+:+"+use.card->getType()+"_char-Clear",1);
				}
			}
		}else if(player->getPhase()==Player::Play&&player->hasSkill(objectName())){
			foreach (QString m, player->getMarkNames()) {
				if(m.contains("&oljiejingce+:+")&&player->getMark(m)>0){
					if(player->askForSkillInvoke(this,data)){
						player->peiyin(this);
						QStringList ms = m.split("+");
						player->drawCards(ms.length()-2,objectName());
					}
					break;
				}
			}
		}
		return false;
	}
};

OLQingjianCard::OLQingjianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void OLQingjianCard::onUse(Room *, CardUseStruct &card_use) const
{
	card_use.to.first()->obtainCard(this);
}

class OLQingjianVS : public ViewAsSkill
{
public:
	OLQingjianVS() : ViewAsSkill("olqingjian")
	{
		expand_pile = "olqingjian";
		response_pattern = "@@olqingjian!";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return Self->getPile("olqingjian").contains(to_select->getId());
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		OLQingjianCard *qj = new OLQingjianCard;
		qj->addSubcards(cards);
		return qj;
	}
};

class OLQingjian : public TriggerSkill
{
public:
	OLQingjian() : TriggerSkill("olqingjian")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
		view_as_skill = new OLQingjianVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place == Player::PlaceHand&&move.to == player&&player->getPhase() != Player::Draw
			&&!room->getTag("FirstRound").toBool()&&player->isAlive()&&player->hasSkill(objectName())) {
				QList<int> ids;
				foreach (int id, move.card_ids) {
					if (player->handCards().contains(id))
						ids << id;
				}
				if (ids.isEmpty()) return false;
				player->setTag("olqingjian", ListI2V(ids));
				const Card *c = room->askForExchange(player, "olqingjian", ids.length(), 1, false, "@olqingjian", true, ListI2S(ids).join(","));
				if (c){
					player->peiyin("olqingjian");
					player->addToPile("olqingjian", c);
					player->setFlags("olqingjian");
				}
			}
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if (p->hasFlag("olqingjian")) {
						p->setFlags("-olqingjian");
						while (p->isAlive()&&!p->getPile("olqingjian").isEmpty()) { // cannot cancel!!!!!!!! must have AI to make program continue
							if (!room->askForUseCard(p, "@@olqingjian!", "@olqingjian-distribute", -1, Card::MethodNone))
								break;
						}
					}
				}
			}
		}
		return false;
	}
};

class OLFuhunVS : public ViewAsSkill
{
public:
	OLFuhunVS() : ViewAsSkill("olfuhun")
	{
		response_pattern = "slash";
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *) const
	{
		return cards.length()<2;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length()<2)
			return nullptr;

		Card *sc = Sanguosha->cloneCard("slash");
		sc->setSkillName("olfuhun");
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player);
	}
};

class OLFuhun : public TriggerSkill
{
public:
	OLFuhun() : TriggerSkill("olfuhun")
	{
		events << TargetSpecified << Damage << PostCardEffected;
		view_as_skill = new OLFuhunVS;
		waked_skills = "wusheng,paoxiao";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash")&&use.card->isVirtualCard()&&player->isAlive()
				&&use.card->getEffectiveId()>=0&&player->hasSkill(objectName())) {
				room->sendCompulsoryTriggerLog(player,objectName());
				QString color = ".|^"+use.card->getColorString()+"|.|hand";
				foreach (ServerPlayer *p, use.to) {
					p->setTag("olfuhunBan", color);
					room->setPlayerCardLimitation(p,"use",color,false);
				}
				use.card->setFlags("olfuhunUse");
			}
		} else if(triggerEvent == PostCardEffected){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->hasFlag("olfuhunUse")){
				QString color = player->getTag("olfuhunBan").toString();
				room->removePlayerCardLimitation(player,"use",color);
			}
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card&&damage.card->isKindOf("Slash") &&player->getPhase()==Player::Play&&player->isAlive()
				&&!player->hasSkills("wusheng+paoxiao",true)&&player->hasSkill(objectName())) {
				room->sendCompulsoryTriggerLog(player,objectName());
				room->acquireOneTurnSkills(player,objectName(),"wusheng|paoxiao");
			}
		}
		return false;
	}
};

class OLZenhui : public TriggerSkill
{
public:
	OLZenhui() : TriggerSkill("olzenhui")
	{
		events << TargetSpecifying;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive()&&target->getPhase()==Player::Play&&target->hasSkill(objectName());
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetSpecifying) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash")||(use.card->isNDTrick()&&!use.card->isKindOf("Collateral"))) {
				if(player->getMark("olzenhui1-PlayClear")>0&&player->getMark("olzenhui2-PlayClear")>0) return false;
				QList<ServerPlayer *>tps = room->getCardTargets(player,use.card,use.to);
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"olzenhui0",true,true);
				if(tp){
					player->peiyin(objectName());
                    const Card*dc = nullptr;
					if(player->getMark("olzenhui1-PlayClear")<1)
						dc = room->askForExchange(tp,objectName(),1,1,true,"olzenhui1:"+player->objectName(),true);
					if(dc){
						player->addMark("olzenhui1-PlayClear");
						player->obtainCard(dc,false);
						use.from = tp;
					}else{
						player->addMark("olzenhui2-PlayClear");
						use.to << tp;
						room->sortByActionOrder(use.to);
					}
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class OLJiaojin : public TriggerSkill
{
public:
	OLJiaojin() : TriggerSkill("oljiaojin")
	{
		events << DamageInflicted;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==DamageInflicted){
			if (player->canDiscard(player,"he")&&room->askForCard(player,"EquipCard","oljiaojin0",data,objectName())) {
				DamageStruct damage = data.value<DamageStruct>();
				player->damageRevises(data,-damage.damage);
				if(damage.card&&!room->getCardOwner(damage.card->getEffectiveId()))
					player->obtainCard(damage.card);
                return true;
			}
		}
		return false;
	}
};

class OLJieQianxi : public TriggerSkill
{
public:
	OLJieQianxi() : TriggerSkill("oljieqianxi")
	{
		events << EventPhaseStart << ConfirmDamage;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent == ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&player->getMark(damage.card->toString()+"oljieqianxiBf-Clear")>0){
				player->setMark(damage.card->toString()+"oljieqianxiBf-Clear",0);
				player->damageRevises(data,1);
			}
		}else if (player->getPhase()==Player::Play&&player->getCardCount()>0&&player->hasSkill(objectName())) {
			const Card*c = room->askForCard(player,"..","oljieqianxi",data,Card::MethodNone);
			if (c) {
				player->skillInvoked(objectName());
				room->showCard(player,c->getEffectiveId());
				player->addMark(c->toString()+"oljieqianxiBf-Clear");
                room->setPlayerMark(player,"&oljieqianxi+"+c->getColorString()+"-Clear",1);
				room->setCardTip(c->getEffectiveId(),"oljieqianxi-Clear");
			}
		}
		return false;
	}
};

class OLJieQianxiLimit : public CardLimitSkill
{
public:
	OLJieQianxiLimit() : CardLimitSkill("#OLJieQianxiLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use,response";
	}

	QString limitPattern(const Player *target, const Card *card) const
	{
		foreach (const Player *p, target->getAliveSiblings()) {//获取其他角色
			if(p->distanceTo(target)!=1) continue;
			foreach (QString m, p->getMarkNames()) {
				if(m.contains("&oljieqianxi+")&&m.contains(card->getColorString()))
					return card->toString();
			}
		}
		return "";
	}
};

OLJiaozhaoCard::OLJiaozhaoCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

void OLJiaozhaoCard::use(Room *room, ServerPlayer *player, QList<ServerPlayer *> &) const
{
	int x = player->property("oljiaozhaoUp").toInt();
	if(x<2) room->showCard(player,getEffectiveId());
	QList<int>ids = room->getAvailableCardList(player,"basic,trick","oljiaozhao",this);
	if(x<1){
		foreach (int id, ids) {
			const Card*c = Sanguosha->getEngineCard(id);
			if(player->getMark("oljiaozhaoBan"+c->objectName())>0)
				ids.removeOne(id);
		}
	}
	if(ids.isEmpty()) return;
	if(x<2) room->setCardTip(getEffectiveId(),"oljiaozhao-Clear");
	room->fillAG(ids,player);
	int id = room->askForAG(player,ids,true,"oljiaozhao","oljiaozhao0");
	room->clearAG(player);
	if(id<0) id = ids.first();
	const Card*c = Sanguosha->getEngineCard(id);
	LogMessage log;
	log.type = "#ShouxiChoice";
	log.from = player;
	log.arg = c->objectName();
	room->setPlayerProperty(player,"oljiaozhaoCn",c->objectName());
	player->addMark("oljiaozhaoBan"+c->objectName());
	if(x>1) room->askForUseCard(player,"@@oljiaozhao","oljiaozhao1:"+c->objectName());
	else room->sendLog(log);
}

class OLJiaozhaoVS : public ViewAsSkill
{
public:
	OLJiaozhaoVS() : ViewAsSkill("oljiaozhao")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern=="@@oljiaozhao") return false;
		if(pattern.isEmpty()){
			foreach (const Card *h, Self->getHandcards()) {
				if(h->hasTip("oljiaozhao"))
					return cards.isEmpty()&&to_select==h;
			}
			return cards.isEmpty();
		}
		return cards.isEmpty()&&to_select->hasTip("oljiaozhao");
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (const Card *h, player->getHandcards()) {
			if(h->hasTip("oljiaozhao")){
				Card*dc = Sanguosha->cloneCard(player->property("oljiaozhaoCn").toString());
				dc->setSkillName("oljiaozhao");
				dc->addSubcard(h);
				dc->deleteLater();
				if(dc->isAvailable(player))
					return true;
			}
		}
		return player->usedTimes("OLJiaozhaoCard")<1;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		foreach (const Card *h, player->getHandcards()) {
			if(h->hasTip("oljiaozhao")){
				foreach (QString p, pattern.split("+")) {
					if(player->property("oljiaozhaoCn").toString().contains(p))
						return true;
				}
			}
		}
		return pattern=="@@oljiaozhao";
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()){
			if(Self->property("oljiaozhaoUp").toInt()>1){
				Card*dc = Sanguosha->cloneCard(Self->property("oljiaozhaoCn").toString());
				dc->setSkillName(objectName());
				return dc;
			}
			return nullptr;
		}
		if(cards[0]->hasTip("oljiaozhao")){
			Card*dc = Sanguosha->cloneCard(Self->property("oljiaozhaoCn").toString());
			dc->setSkillName(objectName());
			dc->addSubcards(cards);
			return dc;
		}
		OLJiaozhaoCard *card = new OLJiaozhaoCard;
		card->addSubcards(cards);
		return card;
	}
};

class OLJiaozhao : public TriggerSkill
{
public:
	OLJiaozhao() : TriggerSkill("oljiaozhao")
	{
		//events << PreCardUsed;
		view_as_skill = new OLJiaozhaoVS;
		waked_skills = "#OLJiaozhaoProhibit";
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const
	{
		return false;
	}
};

class OLJiaozhaoProhibit : public ProhibitSkill
{
public:
	OLJiaozhaoProhibit() : ProhibitSkill("#OLJiaozhaoProhibit")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		if (card->getSkillName()=="oljiaozhao" && to == from)
			return from->hasSkill("oljiaozhao");
		return false;
	}
};

class OLDanxin : public TriggerSkill
{
public:
	OLDanxin() : TriggerSkill("oldanxin")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==Damaged){
			if (player->askForSkillInvoke(objectName(),data)) {
				player->peiyin(objectName());
				player->drawCards(1,objectName());
				int x = player->property("oljiaozhaoUp").toInt()+1;
				room->setPlayerProperty(player,"oljiaozhaoUp",x);
				if(x<3) room->changeTranslation(player,"oljiaozhao",x);
			}
		}
		return false;
	}
};









OLStStandardPackage::OLStStandardPackage()
	: Package("OLStStandard")
{

	General *ol_caocao = new General(this, "ol_caocao$", "wei", 4);
	ol_caocao->addSkill("tenyearjianxiong");
	ol_caocao->addSkill(new OLHujia);
	ol_caocao->addSkill(new OLHujiaDraw);
	related_skills.insertMulti("olhujia", "#olhujia");

	General *ol_xiahd = new General(this, "ol_xiahoudun", "wei");
	ol_xiahd->addSkill("ganglie");
	ol_xiahd->addSkill(new OLQingjian);
	addMetaObject<OLQingjianCard>();

	General *ol_liubei = new General(this, "ol_liubei$", "shu", 4);
	ol_liubei->addSkill("tenyearrende");
	ol_liubei->addSkill(new OLJijiang);

	General *ol_zhangfei = new General(this, "ol_zhangfei", "shu", 4);
	ol_zhangfei->addSkill(new OLPaoxiao);
	ol_zhangfei->addSkill(new OLPaoxiaoMod);
	ol_zhangfei->addSkill(new OLTishen);
	related_skills.insertMulti("olpaoxiao", "#olpaoxiaomod");

	General *ol_zhaoyun = new General(this, "ol_zhaoyun", "shu", 4);
	ol_zhaoyun->addSkill(new OLLongdan);
	ol_zhaoyun->addSkill(new OLYajiao);

	General *oljie_huangyueying = new General(this, "oljie_huangyueying", "shu", 3, false);
	oljie_huangyueying->addSkill(new OLJizhi);
	oljie_huangyueying->addSkill(new OLQicai);
	oljie_huangyueying->addSkill(new OLQicaiLimit);
	related_skills.insertMulti("olqicai", "#olqicai-limit");

	General *ol_sunquan = new General(this, "ol_sunquan$", "wu", 4);
	ol_sunquan->addSkill("tenyearzhiheng");
	ol_sunquan->addSkill(new OLJiuyuan);

	General *ol_lvmeng = new General(this, "ol_lvmeng", "wu", 4);
	ol_lvmeng->addSkill("keji");
	ol_lvmeng->addSkill("qinxue");
	ol_lvmeng->addSkill(new OLBotu);
	ol_lvmeng->addSkill(new OLBotuMark);
	related_skills.insertMulti("olbotu", "#olbotu-mark");

	General *ol_gongsunzan = new General(this, "ol_gongsunzan", "qun", 4);
	ol_gongsunzan->addSkill(new OLQiaomeng);
	ol_gongsunzan->addSkill(new OLYicong);
	ol_gongsunzan->addSkill(new OLYicongEffect);
	related_skills.insertMulti("olyicong", "#olyicong-effect");


	addMetaObject<OLJijiangCard>();
	addMetaObject<OLHuangtianCard>();
	addMetaObject<OLGuhuoCard>();
	addMetaObject<OLQimouCard>();
	addMetaObject<OLTianxiangCard>();
	addMetaObject<OLWulieCard>();
	addMetaObject<OLFangquanCard>();
	addMetaObject<OLZhibaCard>();
	addMetaObject<OLZhibaPindianCard>();
	addMetaObject<SecondOLHanzhanCard>();
	addMetaObject<OLChangbiaoCard>();
	addMetaObject<OLTiaoxinCard>();
	addMetaObject<OLZaiqiCard>();
	addMetaObject<OLQiaobianCard>();
	addMetaObject<OLQiangxiCard>();

	skills << new OLHuangtianViewAsSkill << new OLSishu << new OLZhibaPindian;
}
ADD_PACKAGE(OLStStandard)

OLStWindPackage::OLStWindPackage()
	: Package("OLStWind")
{
	General *ol_weiyan = new General(this, "ol_weiyan", "shu", 4);
	ol_weiyan->addSkill("tenyearkuanggu");
	ol_weiyan->addSkill(new OLQimou);

	General *ol_xiahouyuan = new General(this, "ol_xiahouyuan", "wei", 4);
	ol_xiahouyuan->addSkill("tenyearshensu");
	ol_xiahouyuan->addSkill(new OLShebian);

	General *ol_xiaoqiao = new General(this, "ol_xiaoqiao", "wu", 3, false);
	ol_xiaoqiao->addSkill(new OLTianxiang);
	ol_xiaoqiao->addSkill(new OLHongyan);
	ol_xiaoqiao->addSkill(new OLHongyanKeep);
	ol_xiaoqiao->addSkill(new OLPiaoling);
	related_skills.insertMulti("olhongyan", "#olhongyan-keep");

	General *ol_zhangjiao = new General(this, "ol_zhangjiao$", "qun", 3);
	ol_zhangjiao->addSkill(new OLLeiji);
	ol_zhangjiao->addSkill(new OLGuidao);
	ol_zhangjiao->addSkill("huangtian");

	General *second_ol_zhangjiao = new General(this, "second_ol_zhangjiao$", "qun", 3);
	second_ol_zhangjiao->addSkill("olleiji");
	second_ol_zhangjiao->addSkill("olguidao");
	second_ol_zhangjiao->addSkill(new OLHuangtian);

	General *ol_yuji = new General(this, "ol_yuji", "qun", 3);
	ol_yuji->addSkill(new OLGuhuo);


}
ADD_PACKAGE(OLStWind)

OLStThicketPackage::OLStThicketPackage()
	: Package("OLStThicket")
{
	General *ol_zhurong = new General(this, "ol_zhurong", "shu", 4, false);
	ol_zhurong->addSkill("juxiang");
	ol_zhurong->addSkill("lieren");
	ol_zhurong->addSkill(new OLChangbiao);
	ol_zhurong->addSkill(new SlashNoDistanceLimitSkill("olchangbiao"));
	related_skills.insertMulti("olchangbiao", "#olchangbiao-slash-ndl");

	General *ol_menghuo = new General(this, "ol_menghuo", "shu", 4);
	ol_menghuo->addSkill("huoshou");
	ol_menghuo->addSkill(new OLZaiqi);

	General *ol_xuhuang = new General(this, "ol_xuhuang", "wei", 4);
	ol_xuhuang->addSkill(new OLDuanliang);
	ol_xuhuang->addSkill(new OLDuanliangTargetMod);
	ol_xuhuang->addSkill(new OLJiezi);

	General *ol_sunjian = new General(this, "ol_sunjian", "wu", 4);
	ol_sunjian->addSkill("yinghun");
	ol_sunjian->addSkill(new OLWulie);

	General *second_ol_sunjian = new General(this, "second_ol_sunjian", "wu", 5, true, false, false, 4);
	second_ol_sunjian->addSkill("yinghun");
	second_ol_sunjian->addSkill("olwulie");

	General *ol_dongzhuo = new General(this, "ol_dongzhuo$", "qun", 8);
	ol_dongzhuo->addSkill(new OLJiuchi);
	ol_dongzhuo->addSkill(new OLJiuchiTargetMod);
	ol_dongzhuo->addSkill("roulin");
	ol_dongzhuo->addSkill("benghuai");
	ol_dongzhuo->addSkill(new OLBaonue);
	related_skills.insertMulti("oljiuchi", "#oljiuchi-target");





}
ADD_PACKAGE(OLStThicket)

OLStFirePackage::OLStFirePackage()
	: Package("OLStFire")
{
	General *ol_wolong = new General(this, "ol_wolong", "shu", 3);
	ol_wolong->addSkill("bazhen");
	ol_wolong->addSkill(new OLHuoji);
	ol_wolong->addSkill(new OLKanpo);
	ol_wolong->addSkill(new OLCangzhuo);/*

	General *ol2_wolong = new General(this, "ol2_wolong", "shu", 3);
	ol2_wolong->addSkill("bazhen");
	ol2_wolong->addSkill(new OL2Huoji);
	ol2_wolong->addSkill(new OL2Kanpo);
	ol2_wolong->addSkill("olcangzhuo");*/

	General *ol_pangtong = new General(this, "ol_pangtong", "shu", 3);
	ol_pangtong->addSkill(new OLLianhuan);
	ol_pangtong->addSkill(new OLLianhuanMod);
	ol_pangtong->addSkill(new OLNiepan);
	related_skills.insertMulti("ollianhuan", "#ollianhuanmod");

	General *ol_xunyu = new General(this, "ol_xunyu", "wei", 3);
	ol_xunyu->addSkill("quhu");
	ol_xunyu->addSkill(new OLJieming);
	ol_xunyu->addSkill(new OLJiemingDeath);

	General *ol_dianwei = new General(this, "ol_dianwei", "wei", 4);
	ol_dianwei->addSkill(new OLQiangxi);
	ol_dianwei->addSkill(new OLNinge);

	General *ol_taishici = new General(this, "ol_taishici", "wu", 4);
	ol_taishici->addSkill("tianyi");
	ol_taishici->addSkill(new OLHanzhan);

	General *second_ol_taishici = new General(this, "second_ol_taishici", "wu", 4);
	second_ol_taishici->addSkill("tianyi");
	second_ol_taishici->addSkill(new SecondOLHanzhan);

	General *ol_pangde = new General(this, "ol_pangde", "qun", 4);
	ol_pangde->addSkill(new OLJianchu);
	ol_pangde->addSkill("mashu");

	General *ol_yuanshao = new General(this, "ol_yuanshao$", "qun", 4);
	ol_yuanshao->addSkill(new OLLuanji);
	ol_yuanshao->addSkill(new OLXueyi("olxueyi"));
	ol_yuanshao->addSkill(new OLXueyiKeep("olxueyi"));
	related_skills.insertMulti("olxueyi", "#olxueyi-keep");

	General *second_ol_yuanshao = new General(this, "second_ol_yuanshao$", "qun", 4);
	second_ol_yuanshao->addSkill("olluanji");
	second_ol_yuanshao->addSkill(new OLXueyi("secondolxueyi"));
	second_ol_yuanshao->addSkill(new OLXueyiKeep("secondolxueyi"));
	related_skills.insertMulti("secondolxueyi", "#secondolxueyi-keep");




}
ADD_PACKAGE(OLStFire)

OLStMountainPackage::OLStMountainPackage()
	: Package("OLStMountain")
{
	General *ol_jiangwei = new General(this, "ol_jiangwei", "shu", 4);
	ol_jiangwei->addSkill(new OLTiaoxin);
	ol_jiangwei->addSkill(new OLZhiji);

	General *ol_zhanghe = new General(this, "ol_zhanghe", "wei", 4);
	ol_zhanghe->addSkill(new OLQiaobian);
	ol_zhanghe->addSkill(new OLQiaobianGameStart);
	ol_zhanghe->addSkill(new OLQiaobianMark);
	related_skills.insertMulti("olqiaobian", "#olqiaobian");
	related_skills.insertMulti("olqiaobian", "#olqiaobian-mark");

	General *ol_caiwenji = new General(this, "ol_jie_caiwenji", "qun", 3, false);
	ol_caiwenji->addSkill(new OLBeige);
	ol_caiwenji->addSkill("duanchang");

	General *ol_zuoci = new General(this, "ol_zuoci", "qun", 3);
	ol_zuoci->addSkill(new OLHuashen);
	ol_zuoci->addSkill(new OLHuashenSelect);
	ol_zuoci->addSkill(new OLHuashenClear);
	ol_zuoci->addSkill("xinsheng");
	related_skills.insertMulti("olhuashen", "#olhuashen-select");
	related_skills.insertMulti("olhuashen", "#olhuashen-clear");

	General *ol_liushan = new General(this, "ol_liushan$", "shu", 3);
	ol_liushan->addSkill("xiangle");
	ol_liushan->addSkill(new OLFangquan);
	ol_liushan->addSkill(new OLRuoyu);
	ol_liushan->addRelateSkill("oljijiang");
	ol_liushan->addRelateSkill("olsishu");

	General *ol_sunce = new General(this, "ol_sunce$", "wu", 4);
	ol_sunce->addSkill(new OLJiang);
	ol_sunce->addSkill(new OLHunzi);
	ol_sunce->addSkill(new OLZhiba);

	General *ol_dengai = new General(this, "ol_dengai", "wei", 4);
	ol_dengai->addSkill(new OLTuntian);
	ol_dengai->addSkill(new OLTuntianDistance);
	ol_dengai->addSkill(new OLZaoxian);
	ol_dengai->addSkill(new OLZaoxianExtraTurn);
	ol_dengai->addRelateSkill("jixi");
	related_skills.insertMulti("oltuntian", "#oltuntian-dist");
	related_skills.insertMulti("olzaoxian", "#olzaoxian-extra-turn");

	General *oljie_erzhang = new General(this, "oljie_erzhang", "wu", 3);
	oljie_erzhang->addSkill(new OLZhijian);
	oljie_erzhang->addSkill(new OLGuzheng);
	addMetaObject<OLZhijianCard>();




}
ADD_PACKAGE(OLStMountain)

OLStYJ2011Package::OLStYJ2011Package()
	: Package("OLStYJ2011")
{
	General *oljie_caozhi = new General(this, "oljie_caozhi", "wei", 3);
	oljie_caozhi->addSkill("luoying");
	oljie_caozhi->addSkill(new OLJiushi);

	General *oljie_fazheng = new General(this, "oljie_fazheng", "shu", 3);
	oljie_fazheng->addSkill(new OLXuanhuo);
	oljie_fazheng->addSkill(new OLEnyuan);
	addMetaObject<OLXuanhuoCard>();

	General *oljie_zhangchunhua = new General(this, "oljie_zhangchunhua", "wei", 3,false);
	oljie_zhangchunhua->addSkill("jueqing");
	oljie_zhangchunhua->addSkill("nosshangshi");
	oljie_zhangchunhua->addSkill(new OLJianmie);
	addMetaObject<OLJianmieCard>();

	General *oljie_wuguotai = new General(this, "oljie_wuguotai", "wu", 3,false);
	oljie_wuguotai->addSkill(new OLGanlu);
	oljie_wuguotai->addSkill(new OLBuyi);
	addMetaObject<OLGanluCard>();

}
ADD_PACKAGE(OLStYJ2011)

OLStYJ2012Package::OLStYJ2012Package()
	: Package("OLStYJ2012")
{
	General *oljie_liaohua = new General(this, "oljie_liaohua", "shu", 4);
	oljie_liaohua->addSkill(new OLDangxian);
	oljie_liaohua->addSkill(new OLFuli);

	General *oljie_caozhang = new General(this, "oljie_caozhang", "wei", 4);
	oljie_caozhang->addSkill(new OLJiangchi);

	General *oljie_chengpu = new General(this, "oljie_chengpu", "wu", 4);
	oljie_chengpu->addSkill(new OLLihuo);
	oljie_chengpu->addSkill(new OLChunlao);
	addMetaObject<OLChunlaoCard>();

	General *oljie_liubiao = new General(this, "oljie_liubiao", "qun", 3);
	oljie_liubiao->addSkill(new OLJieZishou);
	oljie_liubiao->addSkill(new OLZongshi);

	General *oljie_guanxingzhangbao = new General(this, "oljie_guanxingzhangbao", "shu", 4);
	oljie_guanxingzhangbao->addSkill(new OLFuhun);

	General *oljie_madai = new General(this, "oljie_madai", "qun", 4);
	oljie_madai->addSkill("mashu");
	oljie_madai->addSkill(new OLJieQianxi);
	oljie_madai->addSkill(new OLJieQianxiLimit);

}
ADD_PACKAGE(OLStYJ2012)

OLStYJ2013Package::OLStYJ2013Package()
	: Package("OLStYJ2013")
{
	General *oljie_caochong = new General(this, "oljie_caochong", "wei", 3);
	oljie_caochong->addSkill(new OLChengxiang);
	oljie_caochong->addSkill(new OLRenxin);

	General *oljie_guohuai = new General(this, "oljie_guohuai", "wei", 3);
	oljie_guohuai->addSkill(new OLJieJingce);

	General *oljie_yufan = new General(this, "oljie_yufan", "wu", 3);
	oljie_yufan->addSkill(new OLZongxuan);
	oljie_yufan->addSkill(new OLZhiyan);
	addMetaObject<OLZongxuanCard>();

	General *oljie_fuhuanghou = new General(this, "oljie_fuhuanghou", "qun", 3,false);
	oljie_fuhuanghou->addSkill(new OLZhuikong);
	oljie_fuhuanghou->addSkill(new OLQiuyuan);

	General *oljie_liru = new General(this, "oljie_liru", "qun", 3);
	oljie_liru->addSkill("juece");
	oljie_liru->addSkill(new OLMieji);
	oljie_liru->addSkill("tenyearfencheng");
	addMetaObject<OLMiejiCard>();


}
ADD_PACKAGE(OLStYJ2013)

OLStYJ2014Package::OLStYJ2014Package()
	: Package("OLStYJ2014")
{
	General *oljie_caifuren = new General(this, "oljie_caifuren", "qun", 3,false);
	oljie_caifuren->addSkill(new OLQieting);
	oljie_caifuren->addSkill("xianzhou");

	General *oljie_sunluban = new General(this, "oljie_sunluban", "wu", 3,false);
	oljie_sunluban->addSkill(new OLZenhui);
	oljie_sunluban->addSkill(new OLJiaojin);






}
ADD_PACKAGE(OLStYJ2014)

OLStYC2016Package::OLStYC2016Package()
	: Package("OLStYC2016")
{

	General *oljie_guohuanghou = new General(this, "oljie_guohuanghou", "wei", 3,false);
	oljie_guohuanghou->addSkill(new OLJiaozhao);
	oljie_guohuanghou->addSkill(new OLDanxin);
	addMetaObject<OLJiaozhaoCard>();






}
ADD_PACKAGE(OLStYC2016)
