#include "ol.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
#include "yjcm2013.h"
#include "engine.h"
#include "maneuvering.h"
//#include "json.h"
#include "settings.h"
#include "clientplayer.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "clientstruct.h"

TunanCard::TunanCard()
{
}

void TunanCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.from->getRoom();
	QList<int> ids = room->getNCards(1);

	LogMessage log;
	log.type = "$ViewDrawPile";
	log.from = effect.to;
	log.arg = QString::number(1);
	log.card_str = ListI2S(ids).join("+");
	room->sendLog(log, effect.to);

	room->fillAG(ids, effect.to);
	room->askForAG(effect.to, ids, true, "tunan");

	QStringList choices;
	QList<ServerPlayer*> players, slash_to;
	const Card*card = Sanguosha->getCard(ids.first());
	room->setCardFlag(card,"tunan_distance");

	Slash*slash = new Slash(card->getSuit(), card->getNumber());
	slash->addSubcard(card);
	slash->setSkillName("_tunan");
	slash->deleteLater();

	foreach (ServerPlayer*p, room->getAlivePlayers()) {
		if (effect.to->canUse(card, p))
			players << p;
		if (effect.to->canSlash(p, slash, true))
			slash_to << p;
	}
	if (!players.isEmpty())
		choices << "use";
	if (!slash_to.isEmpty())
		choices << "slash";

	room->returnToTopDrawPile(ids);
	if (choices.isEmpty()) {
		room->clearAG(effect.to);
		room->setCardFlag(card,"-tunan_distance");
		return;
	}

	QString choice = room->askForChoice(effect.to, "tunan", choices.join("+"), QVariant::fromValue(card));
	room->clearAG(effect.to);
	room->addPlayerMark(effect.to, "tunan_id-PlayClear", ids.first() + 1);

	ServerPlayer*target = nullptr;
	if (choice == "use") {
		if (card->targetFixed())
			room->useCard(CardUseStruct(card, effect.to));
		else {
			if (!room->askForUseCard(effect.to, "@@tunan1!", "@tunan1:" + card->objectName(), 1)) {
				if (card->targetFixed())
					target = effect.to;
				else
					target = players.at(qrand() % players.length());

				if (target)
					room->useCard(CardUseStruct(card, effect.to, target));
			}
		}
	} else {
		if (!room->askForUseCard(effect.to, "@@tunan2!", "@tunan2", 2)) {
			target = slash_to.at(qrand() % slash_to.length());
			if (target)
				room->useCard(CardUseStruct(slash, effect.to, target));
		}
	}
	room->setPlayerMark(effect.to, "tunan_id-PlayClear", 0);
}

class Tunan : public ZeroCardViewAsSkill
{
public:
	Tunan() : ZeroCardViewAsSkill("tunan")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("TunanCard");
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern == "@@tunan1!" || pattern == "@@tunan2!";
	}

	const Card*viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@tunan1!") {
			return Sanguosha->getCard(Self->getMark("tunan_id-PlayClear") - 1);
		} else if (pattern == "@@tunan2!") {
			const Card*card = Sanguosha->getEngineCard(Self->getMark("tunan_id-PlayClear") - 1);
			Slash*slash = new Slash(card->getSuit(), card->getNumber());
			slash->setSkillName("_tunan");
			slash->addSubcard(card);
			return slash;
		}
		return new TunanCard;
	}
};

class Bijing : public TriggerSkill
{
public:
	Bijing() : TriggerSkill("bijing")
	{
		events << EventPhaseStart << CardsMoveOneTime << EventLoseSkill;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() == Player::Finish && player->hasSkill(objectName())) {
				if (player->isKongcheng()) return false;
				const Card*card = room->askForCard(player, ".|.|.|hand", "bijing-invoke", data, Card::MethodNone);
				if (!card) return false;
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);
				player->peiyin(this);
				room->notifySkillInvoked(player, objectName());

				int id = card->getSubcards().first();
				if (!player->handCards().contains(id)) return false;
				QVariantList bijing = player->getTag("BijingIds").toList();
				if (!bijing.contains(QVariant(id))) {
					bijing << id;
					room->setCardTip(id, objectName());
				}
				player->setTag("BijingIds", bijing);
			} else if (player->getPhase() == Player::Start && player->hasSkill(objectName())) {
				QVariantList bijing = player->getTag("BijingIds").toList();
				QList<int> ids = ListV2I(bijing);
				player->removeTag("BijingIds");
				if (ids.isEmpty()) return false;

				DummyCard*dummy = new DummyCard();
				foreach (int id, player->handCards()) {
					if (!ids.contains(id)) continue;
					room->setCardTip(id, "-bijing");
					if (player->canDiscard(player, id))
						dummy->addSubcard(id);
				}
				if (dummy->subcardsLength() > 0) {
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
					room->throwCard(dummy, objectName(), player);
				}
				delete dummy;
			} else if (player->getPhase() == Player::Discard) {
				int n = player->getMark("bijing_lose-Clear");
				if (n <= 0) return false;
				room->setPlayerMark(player, "bijing_lose-Clear", 0);
				QList<ServerPlayer*> losers;
				foreach (ServerPlayer*p, room->getAlivePlayers()) {
					if (p->hasSkill(objectName())) {
						int n = p->getMark("bijing_lose_from-Clear");
						if (n > 0) {
							for (int i = 0; i < n; i++) {
								losers << p;
							}
							room->setPlayerMark(p, "bijing_lose_from-Clear", 0);
						}
					}
				}
				if (losers.isEmpty()) return false;

				int num = qMin(n, losers.length());
				for (int i = 0; i < num; i++) {
					if (player->isDead()) return false;
					foreach (ServerPlayer*p, losers) {
						if (p->isDead() || !p->hasSkill(objectName()))
							losers.removeOne(p);
					}
					if (losers.isEmpty()) return false;
					if (player->isNude()) {
						LogMessage log;
						log.type = "#BijingKongcheng";
						log.from = losers.first();
						log.to << player;
						log.arg = objectName();
						room->sendLog(log);
						room->broadcastSkillInvoke(objectName());
						room->notifySkillInvoked(losers.first(), objectName());
						return false;
					}
					room->sendCompulsoryTriggerLog(losers.first(), objectName(), true, true);
					losers.removeFirst();
					room->askForDiscard(player, objectName(), 2, 2, false, true);
					if (!player->canDiscard(player, "he")) return false;
				}
		}
		} else if (event == EventLoseSkill) {
			if (data.toString() != objectName()) return false;
			player->removeTag("BijingIds");
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player && player->hasSkill(objectName()) && !player->hasFlag("CurrentPlayer")
				&& move.from_places.contains(Player::PlaceHand)) {
				ServerPlayer*current = room->getCurrent();
				if (!current || current->isDead()) return false;
				QVariantList bijing = player->getTag("BijingIds").toList();
				QList<int> ids = ListV2I(bijing);
				foreach (int id, move.card_ids) {
					if (ids.contains(id)) {
						ids.removeOne(id);
						room->addPlayerMark(player, "bijing_lose_from-Clear");
						room->addPlayerMark(current, "bijing_lose-Clear");
					}
				}
				QVariantList new_bijing = ListI2V(ids);
				player->setTag("BijingIds", new_bijing);
			}
		}
		return false;
	}
};

class OLShenxian : public TriggerSkill
{
public:
	OLShenxian() : TriggerSkill("olshenxian")
	{
		events << CardsMoveOneTime;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!player->hasFlag("CurrentPlayer") && player->getMark("olshenxian-Clear")<1
			&& (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
			&& move.from->isAlive() && move.from != player
			&& (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD){
			foreach(int id, move.card_ids){
				if (Sanguosha->getCard(id)->getTypeId() == Card::TypeBasic){
					if (room->askForSkillInvoke(player, objectName(), data)){
						room->broadcastSkillInvoke("shenxian");
						player->drawCards(1, objectName());
						player->addMark("olshenxian-Clear");
					}
					break;
				}
			}
		}
		return false;
	}
};

class OLMeibu : public TriggerSkill
{
public:
	OLMeibu() : TriggerSkill("olmeibu")
	{
		events << EventPhaseStart << EventPhaseChanging << CardUsed;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == CardUsed)
			return 6;

		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play){
			foreach(ServerPlayer*sunluyu, room->getOtherPlayers(player)){
				if (!player->inMyAttackRange(sunluyu) && TriggerSkill::triggerable(sunluyu) && sunluyu->askForSkillInvoke(this)){
					room->broadcastSkillInvoke(objectName());
					if (!player->hasSkill("#olmeibu-filter", true)){
						room->acquireSkill(player, "#olmeibu-filter", false);
						room->filterCards(player, player->getCards("he"), false);
					}
					QVariantList sunluyus = player->getTag(objectName()).toList();
					sunluyus << QVariant::fromValue(sunluyu);
					player->setTag(objectName(), QVariant::fromValue(sunluyus));
					room->insertAttackRangePair(player, sunluyu);
				}
			}
		} else if (triggerEvent == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;

			QVariantList sunluyus = player->getTag(objectName()).toList();
			foreach(const QVariant &sunluyu, sunluyus){
				ServerPlayer*s = sunluyu.value<ServerPlayer*>();
				room->removeAttackRangePair(player, s);
			}

			player->setTag(objectName(), QVariantList());

			if (player->hasSkill("#olmeibu-filter", true)){
				room->detachSkillFromPlayer(player, "#olmeibu-filter");
				room->filterCards(player, player->getCards("he"), true);
			}
		} else if (triggerEvent == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (player->hasSkill("#olmeibu-filter", true) && use.card != nullptr && use.card->getSkillNames().contains("olmeibu")){
				room->detachSkillFromPlayer(player, "#olmeibu-filter");
				room->filterCards(player, player->getCards("he"), true);
			}
		}
		return false;
	}

	int getEffectIndex(const ServerPlayer*, const Card*card) const
	{
		if (card->isKindOf("Slash"))
			return -2;

		return -1;
	}
};

OLMumuCard::OLMumuCard()
{

}

bool OLMumuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	if (targets.isEmpty()){
		if (to_select->getWeapon() && Self->canDiscard(to_select, to_select->getWeapon()->getEffectiveId()))
			return true;
		if (to_select != Self && to_select->getArmor() && Self->hasArmorArea())
			return true;
	}

	return false;
}

void OLMumuCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*target = effect.to;
	ServerPlayer*player = effect.from;

	QStringList choices;
	if (target->getWeapon() && player->canDiscard(target, target->getWeapon()->getEffectiveId()))
		choices << "weapon";
	if (target != player && target->getArmor() && player->hasArmorArea())
		choices << "armor";

	if (choices.length() == 0)
		return;

	Room*r = target->getRoom();
	QString choice = choices.length() == 1 ? choices.first() : r->askForChoice(player, "olmumu", choices.join("+"), QVariant::fromValue(target));

	if (choice == "weapon"){
		r->throwCard(target->getWeapon(), target, player == target ? nullptr : player);
		player->drawCards(1, "olmumu");
	} else {
		int equip = target->getArmor()->getEffectiveId();
		QList<CardsMoveStruct> exchangeMove;
		CardsMoveStruct move1(equip, player, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_ROB, player->objectName()));
		exchangeMove.push_back(move1);
		if (player->getArmor()){
			CardsMoveStruct move2(player->getArmor()->getEffectiveId(), nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, player->objectName()));
			exchangeMove.push_back(move2);
		}
		r->moveCardsAtomic(exchangeMove, true);
	}
}

class OLMumu : public OneCardViewAsSkill
{
public:
	OLMumu() : OneCardViewAsSkill("olmumu")
	{
		filter_pattern = "Slash#TrickCard|black!";
	}

	const Card*viewAs(const Card*originalCard) const
	{
		OLMumuCard*mm = new OLMumuCard;
		mm->addSubcard(originalCard);
		return mm;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("OLMumuCard");
	}
};

class OLZhixi : public TriggerSkill
{
public:
	OLZhixi() : TriggerSkill("olzhixi")
	{
		events << CardUsed << EventLoseSkill;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == CardUsed)
			return 6;

		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card != nullptr && use.card->isKindOf("TrickCard") && TriggerSkill::triggerable(player)){
				if (!player->hasSkill("#olzhixi-filter", true)){
					room->acquireSkill(player, "#olzhixi-filter", false);
					room->filterCards(player, player->getCards("he"), true);
				}
			}
		} else if (triggerEvent == EventLoseSkill){
			QString name = data.toString();
			if (name == objectName()){
				if (player->hasSkill("#olzhixi-filter", true)){
					room->detachSkillFromPlayer(player, "#olzhixi-filter");
					room->filterCards(player, player->getCards("he"), true);
				}
			}
		}
		return false;
	}

	int getEffectIndex(const ServerPlayer*, const Card*card)
	{
		if (card->isKindOf("Slash"))
			return -2;

		return -1;
	}
};

class OLMeibu2 : public TriggerSkill
{
public:
	OLMeibu2() : TriggerSkill("olmeibu2")
	{
		events << EventPhaseStart << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play){
			foreach(ServerPlayer*sunluyu, room->getOtherPlayers(player)){
				if (!player->inMyAttackRange(sunluyu) && TriggerSkill::triggerable(sunluyu) && sunluyu->askForSkillInvoke(this)){
					room->broadcastSkillInvoke(objectName());
					if (!player->hasSkill("olzhixi", true))
						room->acquireSkill(player, "olzhixi");
					if (sunluyu->getMark("olmumu2") == 0){
						QVariantList sunluyus = player->getTag(objectName()).toList();
						sunluyus << QVariant::fromValue(sunluyu);
						player->setTag(objectName(), QVariant::fromValue(sunluyus));
						room->insertAttackRangePair(player, sunluyu);
					}
				}
			}
		} else if (triggerEvent == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;

			QVariantList sunluyus = player->getTag(objectName()).toList();
			foreach(const QVariant &sunluyu, sunluyus){
				ServerPlayer*s = sunluyu.value<ServerPlayer*>();
				room->removeAttackRangePair(player, s);
			}

			player->setTag(objectName(), QVariantList());

			if (player->hasSkill("olzhixi", true))
				room->detachSkillFromPlayer(player, "olzhixi");
		}
		return false;
	}

	int getEffectIndex(const ServerPlayer*, const Card*card) const
	{
		if (card->isKindOf("Slash"))
			return -2;

		return -1;
	}
};

OLMumu2Card::OLMumu2Card()
{
}

bool OLMumu2Card::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	if (targets.isEmpty() && !to_select->getEquips().isEmpty()){
		QList<const Card*> equips = to_select->getEquips();
		foreach(const Card*e, equips){
			if (to_select->getArmor() != nullptr && to_select->getArmor()->getRealCard() == e->getRealCard())
				return true;

			if (Self->canDiscard(to_select, e->getEffectiveId()))
				return true;
		}
	}

	return false;
}

void OLMumu2Card::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*target = effect.to;
	ServerPlayer*player = effect.from;

	Room*r = target->getRoom();

	QList<int> disabled;
	foreach(const Card*e, target->getEquips()){
		if (target->getArmor() != nullptr && target->getArmor()->getRealCard() == e->getRealCard())
			continue;

		if (!player->canDiscard(target, e->getEffectiveId()))
			disabled << e->getEffectiveId();
	}

	int id = r->askForCardChosen(player, target, "e", "olmumu2", false, Card::MethodNone, disabled);

	QString choice = "discard";
	if (target->getArmor() != nullptr && id == target->getArmor()->getEffectiveId()){
		if (!player->canDiscard(target, id))
			choice = "obtain";
		else
			choice = r->askForChoice(player, "olmumu2", "discard+obtain", id);
	}

	if (choice == "discard"){
		r->throwCard(Sanguosha->getCard(id), target, player == target ? nullptr : player);
		player->drawCards(1, "olmumu2");
	} else
		r->obtainCard(player, id);

	const Card*c = Sanguosha->getCard(subcards.first());
	if (c->isKindOf("Slash") || (c->isBlack() && c->isKindOf("TrickCard")))
		player->addMark("olmumu2");
}

class OLMumu2VS : public OneCardViewAsSkill
{
public:
	OLMumu2VS() : OneCardViewAsSkill("olmumu2")
	{
		filter_pattern = ".!";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("OLMumu2Card");
	}

	const Card*viewAs(const Card*originalCard) const
	{
		OLMumu2Card*mm = new OLMumu2Card;
		mm->addSubcard(originalCard);
		return mm;
	}
};

class OLMumu2 : public PhaseChangeSkill
{
public:
	OLMumu2() : PhaseChangeSkill("olmumu2")
	{
		view_as_skill = new OLMumu2VS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->getPhase() == Player::RoundStart;
	}

	bool onPhaseChange(ServerPlayer*target, Room*) const
	{
		target->setMark("olmumu2", 0);

		return false;
	}
};

class Chenqing : public TriggerSkill
{
public:
	Chenqing() : TriggerSkill("chenqing")
	{
		events << AskForPeaches;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getMark("chenqing_used-Clear")>0) return false;

		DyingStruct dying = data.value<DyingStruct>();

		QList<ServerPlayer*> players = room->getOtherPlayers(player);
		players.removeAll(dying.who);
		ServerPlayer*target = room->askForPlayerChosen(player, players, objectName(), "@ChenqingAsk", true, true);
		if (target){
			player->addMark("chenqing_used-Clear");
			room->broadcastSkillInvoke(objectName());
			target->drawCards(4, objectName());
			QSet<Card::Suit> suit;
			const Card*card = room->askForDiscard(target, "Chenqing", 4, 4, false, true,"ChenqingDiscard");
			if(card){
				foreach(int id, card->getSubcards())
					suit.insert(Sanguosha->getCard(id)->getSuit());
			}
			if (suit.count() == 4){
				Peach*peach = new Peach(Card::NoSuit, 0);
				peach->setSkillName("_chenqing");
				peach->deleteLater();
				if (target->isCardLimited(peach, Card::MethodUse) || target->isProhibited(dying.who, peach)) return false;
				room->useCard(CardUseStruct(peach, target, dying.who, false), true);
			}
		}
		return false;
	}
};

class MozhiViewAsSkill : public OneCardViewAsSkill
{
public:
	MozhiViewAsSkill() : OneCardViewAsSkill("mozhi")
	{
		response_or_use = true;
		response_pattern = "@@mozhi";
	}

	bool viewFilter(const Card*to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card*viewAs(const Card*originalCard) const
	{
		QString ori = Self->property("mozhiCn").toString();
		Card*a = Sanguosha->cloneCard(ori);
		a->addSubcard(originalCard);
		a->setSkillName(objectName());
		return a;
	}
};

class Mozhi : public TriggerSkill
{
public:
	Mozhi() : TriggerSkill("mozhi")
	{
		events << EventPhaseStart << PreCardUsed;
		view_as_skill = new MozhiViewAsSkill;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("BasicCard") || use.card->isNDTrick()){
				QStringList list = player->getTag("MozhiRecord").toStringList();
				list.append(use.card->objectName());
				player->setTag("MozhiRecord", list);
			}
		}else if (player->getPhase()==Player::Finish&&player->getTag("MozhiRecord").isValid()){
			QStringList list = player->getTag("MozhiRecord").toStringList();
			player->removeTag("MozhiRecord");
			if(!player->hasSkill(objectName())) return false;
			for (int i = 0; i < 2; i++){
				room->setPlayerProperty(player, "mozhiCn", list.first());
				if(room->askForUseCard(player, "@@mozhi", "@mozhi_ask:"+list.takeFirst())){
					if(list.isEmpty()||player->isDead()) break;
				}else
					break;
			}
		}
		return false;
	}
};

class Fengpo : public TriggerSkill
{
public:
	Fengpo() : TriggerSkill("fengpo")
	{
		events << TargetSpecified << DamageCaused;
	}

	bool trigger(TriggerEvent e, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (e == TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.to.length() != 1) return false;
			if (use.card == nullptr) return false;
			if (!use.card->isKindOf("Slash") && !use.card->isKindOf("Duel")) return false;
			if (!use.card->hasFlag("fengporecc")) return false;

			int n = 0;
			foreach(const Card*card, use.to.first()->getHandcards()){
				if (card->getSuit() == Card::Diamond)
					++n;
			}

			QStringList choices;
			choices << "drawCards" << "addDamage" << "cancel";
			QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
			if (choice == "cancel") return false;
			player->skillInvoked(objectName());
			if (choice == "drawCards"){
				if (n > 0) player->drawCards(n, objectName());
			} else if (choice == "addDamage")
				player->setTag("fengpoaddDamage" + use.card->toString(), n);
		} else if (e == DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.from) return false;
			int n = damage.from->getTag("fengpoaddDamage" + damage.card->toString()).toInt();
			if (n>0){
				damage.damage += n;
				data = QVariant::fromValue(damage);
				damage.from->removeTag("fengpoaddDamage" + damage.card->toString());
			}
		}
		return false;
	}
};

OLRendeCard::OLRendeCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool OLRendeCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	QStringList rende_prop = Self->property("olrende").toString().split("+");
	if (rende_prop.contains(to_select->objectName()))
		return false;

	return targets.isEmpty() && to_select != Self;
}

void OLRendeCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	ServerPlayer*target = targets.first();

	QDateTime dtbefore = source->getTag("olrende", QDateTime(QDate::currentDate(), QTime(0, 0, 0))).toDateTime();
	QDateTime dtafter = QDateTime::currentDateTime();

	if (dtbefore.secsTo(dtafter) > 3* Config.AIDelay / 1000)
		room->broadcastSkillInvoke("rende");

	source->setTag("olrende", QDateTime::currentDateTime());

	CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "olrende", "");
	room->obtainCard(target, this, reason, false);

	int old_value = source->getMark("olrende");
	int new_value = old_value + subcards.length();
	room->setPlayerMark(source, "olrende", new_value);

	if (old_value < 2 && new_value >= 2)
		room->recover(source, RecoverStruct("olrende", source));

	QStringList rende_prop = source->property("olrende").toString().split("+");
	rende_prop.append(target->objectName());
	room->setPlayerProperty(source, "olrende", rende_prop.join("+"));
}

class OLRendeVS : public ViewAsSkill
{
public:
	OLRendeVS() : ViewAsSkill("olrende")
	{
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (ServerInfo.GameMode == "04_1v3" && selected.length() + Self->getMark("olrende") >= 2)
			return false;
		else
			return !to_select->isEquipped();
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		if (ServerInfo.GameMode == "04_1v3" && player->getMark("olrende") >= 2)
			return false;
		QStringList rende_prop = player->property("olrende").toString().split("+");
		foreach(const Player*p, player->getAliveSiblings()){
			if (!rende_prop.contains(p->objectName()))
				return !player->isKongcheng();
		}
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		OLRendeCard*rende_card = new OLRendeCard;
		rende_card->addSubcards(cards);
		return rende_card;
	}
};

class OLRende : public TriggerSkill
{
public:
	OLRende() : TriggerSkill("olrende")
	{
		events << EventPhaseChanging;
		view_as_skill = new OLRendeVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->getMark("olrende") > 0;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to != Player::NotActive) return false;
		room->setPlayerMark(player, "olrende", 0);

		room->setPlayerProperty(player, "olrende", QVariant());
		return false;
	}
};

class OLChenqing : public TriggerSkill
{
public:
	OLChenqing() : TriggerSkill("olchenqing")
	{
		events << AskForPeaches;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(player->getMark("@advise_lun")>0) return false;
		DyingStruct dying = data.value<DyingStruct>();
		QList<ServerPlayer*> targets = room->getOtherPlayers(player);
		targets.removeOne(dying.who);

		ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@olchenqing:"+dying.who->objectName(), true, true);
		if (target){
			room->addPlayerMark(player, "@advise_lun");
			room->broadcastSkillInvoke("olchenqing");

			room->drawCards(target, 4, "olchenqing");

			QSet<Card::Suit> suit;
			const Card*to_discard = room->askForDiscard(target, objectName(), 4, 4, false, true, "@olchenqing-exchange:"+player->objectName()+":"+dying.who->objectName());
			if(to_discard){
				foreach(int id, to_discard->getSubcards())
					suit.insert(Sanguosha->getCard(id)->getSuit());
			}
			Card*peach = Sanguosha->cloneCard("peach");
			peach->setSkillName("_olchenqing");
			if (suit.count() == 4&&target->canUse(peach,dying.who))
				room->useCard(CardUseStruct(peach, target, dying.who, false), true);
			peach->deleteLater();
		}
		return false;
	}
};

class OLZhengnan : public TriggerSkill
{
public:
	OLZhengnan() : TriggerSkill("olzhengnan")
	{
		events << Death;
		frequency = Frequent;
		waked_skills = "wusheng,dangxian,zhiman";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*guansuo, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		ServerPlayer*player = death.who;
		if (guansuo == player) return false;
		if (guansuo->isAlive() && room->askForSkillInvoke(guansuo, objectName(), data)){
			room->broadcastSkillInvoke(objectName());
			guansuo->drawCards(3, objectName());
			if (guansuo->isDead()) return false;
			QStringList choices;
			if (!guansuo->hasSkill("wusheng", true)) choices << "wusheng";
			if (!guansuo->hasSkill("dangxian", true)) choices << "dangxian";
			if (!guansuo->hasSkill("zhiman", true)) choices << "zhiman";
			if (choices.isEmpty()) return false;
			QString choice = room->askForChoice(guansuo, "olzhengnan", choices.join("+"), QVariant());
			if (!guansuo->hasSkill(choice))
				room->handleAcquireDetachSkills(guansuo, choice);
		}
		return false;
	}
};

class Lingren : public TriggerSkill
{
public:
	Lingren() : TriggerSkill("lingren")
	{
		events << TargetSpecified;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getMark("lingren-PlayClear") > 0 || player->getPhase() != Player::Play) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("Slash") || (use.card->isKindOf("TrickCard") && use.card->isDamageCard())){
			QList<ServerPlayer*> targets = use.to;
			if (targets.contains(player))
				targets.removeOne(player);
			if (targets.isEmpty()) return false;
			ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@lingren-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player, "lingren-PlayClear");
			QString hasbasic = "hasnobasic", hastrick = "hasnotrick", hasequip = "hasnoequip";
			foreach(const Card*c, target->getCards("h")){
				if (c->isKindOf("BasicCard")) hasbasic = "hasbasic";
				else if (c->isKindOf("TrickCard")) hastrick = "hastrick";
				else if (c->isKindOf("EquipCard")) hasequip = "hasequip";
			}
			LogMessage log;
			log.type = "#LingrenGuess";
			log.from = player;
			log.to << target;

			QString choiceo = room->askForChoice(player, "lingren", "hasbasic+hasnobasic", QVariant::fromValue(target));
			log.arg = "lingren:" + choiceo;
			room->sendLog(log);

			QString choicet = room->askForChoice(player, "lingren", "hastrick+hasnotrick", QVariant::fromValue(target));
			log.arg = "lingren:" + choicet;
			room->sendLog(log);

			QString choiceth = room->askForChoice(player, "lingren", "hasequip+hasnoequip", QVariant::fromValue(target));
			log.arg = "lingren:" + choiceth;
			room->sendLog(log);

			int n = 0;
			if (choiceo == hasbasic)
				n++;
			if (choicet == hastrick)
				n++;
			if (choiceth == hasequip)
				n++;

			log.type = "#LingrenGuessResult";
			log.arg= QString::number(n);
			room->sendLog(log);

			if (n == 0) return false;
			if (n == 3){
				room->setPlayerFlag(target, "lingren_damage_to");
				room->setCardFlag(use.card, "lingren_damage_card");
				player->drawCards(2, objectName());
				room->acquireNextTurnSkills(player, objectName(), "jianxiong|xingshang");
			} else if (n == 2){
				room->setPlayerFlag(target, "lingren_damage_to");
				room->setCardFlag(use.card, "lingren_damage_card");
				player->drawCards(2, objectName());
			} else {
				room->setPlayerFlag(target, "lingren_damage_to");
				room->setCardFlag(use.card, "lingren_damage_card");
			}
		}
		return false;
	}
};

class LingrenEffect : public TriggerSkill
{
public:
	LingrenEffect() : TriggerSkill("#lingreneffect")
	{
		events << DamageCaused;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.card&&damage.to->isAlive()&&damage.to->hasFlag("lingren_damage_to")&&damage.card->hasFlag("lingren_damage_card")){
			room->setPlayerFlag(damage.to, "-lingren_damage_to");
			room->setCardFlag(damage.card, "-lingren_damage_card");
			LogMessage log;
			log.type = "#LingrenDamage";
			log.from = damage.from;
			log.to << damage.to;
			log.arg = "lingren";
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class Fujian : public PhaseChangeSkill
{
public:
	Fujian() : PhaseChangeSkill("fujian")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		int n = player->getHandcardNum();
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (p->getHandcardNum() < n)
				n = p->getHandcardNum();
		}
		if (n < 0) n = 0;
		ServerPlayer*target = room->getOtherPlayers(player).at(qrand() % room->getOtherPlayers(player).length());
		room->broadcastSkillInvoke(objectName());
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());;
		room->notifySkillInvoked(player, objectName());
		LogMessage log;
		log.type = "#FujianWatch";
		log.from = player;
		log.to << target;
		log.arg = "fujian";
		log.arg2 = QString::number(n);
		if (n == 0)
			room->sendLog(log);
		else {
			QList<int> handcards;
			QList<int> list;
			foreach(int id, target->handCards()){
				handcards << id;
			}
			for (int i = 1; i <= n; i++){
				if (handcards.isEmpty()) break;
				int id = handcards.at(qrand() % handcards.length());
				handcards.removeOne(id);
				list << id;
			}
			if (list.isEmpty())
				room->sendLog(log);
			else {
				QStringList slist;
				foreach(int id, list){
					slist << Sanguosha->getCard(id)->toString();
				}
				foreach(ServerPlayer*p, room->getAllPlayers(true)){
					if (p == player) continue;
					room->sendLog(log, p);
				}
				log.type = "$FujianWatch";
				log.card_str = slist.join("+");
				room->sendLog(log, player);
				room->fillAG(list, player);
				room->askForAG(player, list, true, objectName());
				room->clearAG(player);
			}
		}
		return false;
	}
};

NeifaCard::NeifaCard()
{
}

bool NeifaCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return targets.length() >= 0 && targets.length() <= 1;
}

bool NeifaCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	return targets.isEmpty() && to_select->getCardCount(true, true) > to_select->getHandcardNum();
}

void NeifaCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	if (targets.isEmpty())
		source->drawCards(2, "neifa");
	else {
		foreach(ServerPlayer*p, targets){
			if (source->isDead()) return;
			if (p->isDead() || p->getCards("ej").isEmpty()) continue;
			int card_id = room->askForCardChosen(source, p, "ej", "neifa");
			room->obtainCard(source, card_id, true);
		}
	}
}

class NeifaVS : public ZeroCardViewAsSkill
{
public:
	NeifaVS() : ZeroCardViewAsSkill("neifa")
	{
	}

	const Card*viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@neifa")
			return new NeifaCard;
		else
			return new ExtraCollateralCard;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.startsWith("@@neifa");
	}
};

class Neifa : public TriggerSkill
{
public:
	Neifa() : TriggerSkill("neifa")
	{
		events << EventPhaseStart << CardUsed << PreCardUsed;
		view_as_skill = new NeifaVS;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if (player->getPhase() != Player::Play) return false;
			if (!room->askForUseCard(player, "@@neifa", "@neifa")) return false;
			if (!player->canDiscard(player, "he")) return false;

			const Card*card = room->askForDiscard(player, objectName(), 1, 1, false, true);
			if (!card) return false;
			card = Sanguosha->getCard(card->getSubcards().first());

			if (card->isKindOf("BasicCard")){
				room->setPlayerCardLimitation(player, "use", "TrickCard,EquipCard", true);
				int x = qMin(5, NeifaX(player));
				if (x <= 0) return false;
				room->addPlayerMark(player, "&neifa+basic-Clear", x);
				room->addSlashCishu(player, x);
				room->addSlashMubiao(player, 1);
			} else {
				room->setPlayerCardLimitation(player, "use", "BasicCard", true);
				room->setPlayerFlag(player, "neifa_not_basic");
				int x = qMin(5, NeifaX(player));
				if (x <= 0) return false;
				room->addPlayerMark(player, "&neifa+notbasic-Clear", x);
			}
		} else if (event == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("EquipCard")) return false;

			int n = player->getMark("&neifa+notbasic-Clear");
			n = qMin(5, n);
			int mark = player->getMark("neifa_equip-Clear");
			room->addPlayerMark(player, "neifa_equip-Clear");
			if (n <= 0 || mark >= 2) return false;

			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			player->drawCards(n, objectName());
		} else {
			if (!player->hasFlag("neifa_not_basic")) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isNDTrick() || use.card->isKindOf("Nullification")) return false;

			QList<ServerPlayer*> ava;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (use.to.contains(p)) continue;
				if (use.from->canUse(use.card,p))
					ava << p;
			}

			QStringList choices;
			if (!ava.isEmpty()) choices << "add";
			if (use.to.length() > 1) choices << "remove";
			if (choices.isEmpty()) return false;
			choices << "cancel";

			QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
			if (choice == "cancel") return false;
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());
			LogMessage log;
			log.type = "#QiaoshuiAdd";
			log.from = player;
			log.card_str = use.card->toString();
			log.arg = "neifa";
			if (choice == "add"){
				log.to << room->askForPlayerChosen(player, ava, objectName(), "@neifa-add:" + use.card->objectName());
				use.to << log.to;
				room->sortByActionOrder(use.to);
			} else {
				log.type = "#QiaoshuiRemove";
				log.to << room->askForPlayerChosen(player, use.to, objectName(), "@neifa-remove:" + use.card->objectName());
				use.to.removeOne(log.to.first());
			}
			room->sendLog(log);
			data = QVariant::fromValue(use);
		}
		return false;
	}
private:
	int NeifaX(ServerPlayer*player) const
	{
		int x = 0;
		foreach(const Card*c, player->getCards("h")){
			if (!player->canUse(c))
				x++;
		}
		return x;
	}
};

class Tuogu : public TriggerSkill
{
public:
	Tuogu() : TriggerSkill("tuogu")
	{
		events << Death;
		frequency = Limited;
		limit_mark = "@tuoguMark";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		ServerPlayer*who = data.value<DeathStruct>().who;
		if (!who || who == player || player->getMark("@tuoguMark") <= 0) return false;

		QStringList list;

		QString name = who->getGeneralName();
		const General*g = Sanguosha->getGeneral(name);
		if (g){
			foreach(const Skill*skill, g->getSkillList()){
				if (!skill->isVisible()) continue;
				//if (skill->getFrequency() == Skill::Limited) continue;
				if (skill->isLimitedSkill()) continue;
				if (skill->getFrequency() == Skill::Wake) continue;
				if (skill->isLordSkill()) continue;
				if (!list.contains(skill->objectName()))
					list << skill->objectName();
			}
		}

		if (who->getGeneral2()){
			QString name2 = who->getGeneral2Name();
			const General*g2 = Sanguosha->getGeneral(name2);
			if (g2){
				foreach(const Skill*skill, g2->getSkillList()){
					if (!skill->isVisible()) continue;
					//if (skill->getFrequency() == Skill::Limited) continue;
					if (skill->isLimitedSkill()) continue;
					if (skill->getFrequency() == Skill::Wake) continue;
					if (skill->isLordSkill()) continue;
					if (!list.contains(skill->objectName()))
						list << skill->objectName();
				}
			}
		}

		if (list.isEmpty()) return false;
		if (!player->askForSkillInvoke(this, QVariant::fromValue(who))) return false;

		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "tuogu");
		room->removePlayerMark(player, "@tuoguMark");

		QString skill = room->askForChoice(who, objectName(), list.join("+"), QVariant::fromValue(player));
		if (player->hasSkill(skill)) return false;
		room->acquireSkill(player, skill);
		return false;
	}
};

class Shanzhuan : public TriggerSkill
{
public:
	Shanzhuan() : TriggerSkill("shanzhuan")
	{
		events << Damage << EventPhaseChanging;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == Damage){
			ServerPlayer*to = data.value<DamageStruct>().to;
			if (!to || to->isDead() || to == player || to->isNude() || !to->hasJudgeArea() || !to->getJudgingArea().isEmpty()) return false;
			if (!player->askForSkillInvoke(this, QVariant::fromValue(to))) return false;
			room->broadcastSkillInvoke(objectName());

			int id = room->askForCardChosen(player, to, "he", objectName());
			const Card*card = Sanguosha->getCard(id);

			LogMessage log;
			log.from = player;
			log.to << to;
			log.card_str = card->toString();
			CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), to->objectName(), "shanzhuan", "");
			if (card->isKindOf("DelayedTrick")){
				log.type = "$ShanzhuanPut";
			} else {
				log.type = "$ShanzhuanViewAsPut";
				WrappedCard*c = Sanguosha->getWrappedCard(id);
				room->moveCardTo(card, nullptr, Player::PlaceTable, reason, true);
				if (card->isRed()){
					Indulgence*indulgence = new Indulgence(card->getSuit(), card->getNumber());
					indulgence->setSkillName("shanzhuan");
					c->takeOver(indulgence);
				} else if (card->isBlack()){
					SupplyShortage*supply_shortage = new SupplyShortage(card->getSuit(), card->getNumber());
					supply_shortage->setSkillName("shanzhuan");
					c->takeOver(supply_shortage);
				}
				log.arg = card->objectName();
				room->broadcastUpdateCard(room->getAllPlayers(true), id, c);
			}
			room->sendLog(log);
			room->moveCardTo(card, to, Player::PlaceDelayedTrick, reason, true);
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			if (player->getMark("damage_point_round") > 0) return false;
			if (!player->askForSkillInvoke(this, "draw")) return false;
			room->broadcastSkillInvoke(objectName());
			player->drawCards(1, objectName());
		}

		return false;
	}
};

class SecondTuogu : public TriggerSkill
{
public:
	SecondTuogu() : TriggerSkill("secondtuogu")
	{
		events << Death;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		ServerPlayer*who = data.value<DeathStruct>().who;
		if (!who || who == player) return false;

		QStringList list;

		QString name = who->getGeneralName();
		const General*g = Sanguosha->getGeneral(name);
		if (g){
			foreach(const Skill*skill, g->getSkillList()){
				if (!skill->isVisible()) continue;
				//if (skill->getFrequency() == Skill::Limited) continue;
				if (skill->isLimitedSkill()) continue;
				if (skill->getFrequency() == Skill::Wake) continue;
				if (skill->isLordSkill()) continue;
				if (!list.contains(skill->objectName()))
					list << skill->objectName();
			}
		}

		if (who->getGeneral2()){
			QString name2 = who->getGeneral2Name();
			const General*g2 = Sanguosha->getGeneral(name2);
			if (g2){
				foreach(const Skill*skill, g2->getSkillList()){
					if (!skill->isVisible()) continue;
					//if (skill->getFrequency() == Skill::Limited) continue;
					if (skill->isLimitedSkill()) continue;
					if (skill->getFrequency() == Skill::Wake) continue;
					if (skill->isLordSkill()) continue;
					if (!list.contains(skill->objectName()))
						list << skill->objectName();
				}
			}
		}

		if (list.isEmpty()) return false;
		if (!player->askForSkillInvoke(this, QVariant::fromValue(who))) return false;
		room->broadcastSkillInvoke(objectName());
		QString sk = player->property("secondtuogu_skill").toString();
		QString skill = room->askForChoice(who, objectName(), list.join("+"), QVariant::fromValue(player));
		room->setPlayerProperty(player, "secondtuogu_skill", skill);
		/*QStringList sks;
		if (player->hasSkill(sk))
			sks << "-" + sk;
		if (!player->hasSkill(skill))
			sks << skill;
		if (sks.isEmpty()) return false;
		room->handleAcquireDetachSkills(player, sks);*/
		if (player->hasSkill(sk))
			room->detachSkillFromPlayer(player, sk);
		if (player->isAlive() && !player->hasSkill(skill))
			room->acquireSkill(player, skill);
		return false;
	}
};

YoulongDialog*YoulongDialog::getInstance(const QString &object)
{
	static YoulongDialog*instance;
	if (instance == nullptr || instance->objectName() != object)
		instance = new YoulongDialog(object);

	return instance;
}

YoulongDialog::YoulongDialog(const QString &object)
	: GuhuoDialog(object)
{
}

bool YoulongDialog::isButtonEnabled(const QString &button_name) const
{
	const Card*card = map[button_name];
	if (Self->getChangeSkillState("youlong") == 1 && !card->isNDTrick()) return false;
	if (Self->getChangeSkillState("youlong") == 2 && !card->isKindOf("BasicCard")) return false;
	return Self->getMark(objectName() + "_" + button_name) <= 0 && button_name != "normal_slash"
			&& !Self->isCardLimited(card, Card::MethodUse) && card->isAvailable(Self);
}

YoulongCard::YoulongCard()
{
	mute = true;
}

bool YoulongCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("youlong");
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool YoulongCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE){
		Card*card = Sanguosha->cloneCard(user_string.split("+").first());
		if(card){
			card->deleteLater();
			return card->targetFixed();
		}
	}
	return true;
}

bool YoulongCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("youlong");
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*YoulongCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer*source = card_use.from;
	Room*room = source->getRoom();

	QString tl = user_string;
	if ((user_string.contains("slash") || user_string.contains("Slash"))
		&& Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
		QStringList tl_list;
		if (source->getMark("youlong_slash") <= 0)
			tl_list << "slash";
		if (!Config.BanPackages.contains("maneuvering")){
			if (source->getMark("youlong_thunder_slash") <= 0)
				tl_list << "thunder_slash";
			if (source->getMark("youlong_fire_slash") <= 0)
				tl_list << "fire_slash";
		}
		if (tl_list.isEmpty()) return nullptr;
		tl = room->askForChoice(source, "youlong_slash", tl_list.join("+"));
	}
	if (source->getMark("youlong_" + tl) > 0) return nullptr;

	QStringList areas;
	for (int i = 0; i < 5; i++){
		if (source->hasEquipArea(i))
			areas << QString::number(i);
	}
	if (areas.isEmpty()) return nullptr;

	if (source->getChangeSkillState("youlong") == 1){
		room->addPlayerMark(source, "youlong_trick_lun");
		room->setChangeSkillState(source, "youlong", 2);
	} else {
		room->addPlayerMark(source, "youlong_basic_lun");
		room->setChangeSkillState(source, "youlong", 1);
	}

	QString area = room->askForChoice(source, "youlong", areas.join("+"));
	source->throwEquipArea(area.toInt());
	room->addPlayerMark(source, "youlong_" + tl);

	Card*use_card = Sanguosha->cloneCard(tl);
	use_card->setSkillName("youlong");
	use_card->deleteLater();
	return use_card;
}

const Card*YoulongCard::validateInResponse(ServerPlayer*source) const
{
	Room*room = source->getRoom();
	QString tl = user_string;
	if (user_string == "peach+analeptic"){
		QStringList tl_list;
		if (source->getMark("youlong_peach") <= 0)
			tl_list << "peach";
		if (!Config.BanPackages.contains("maneuvering") && source->getMark("youlong_analeptic") <= 0)
			tl_list << "analeptic";
		if (tl_list.isEmpty()) return nullptr;
		tl = room->askForChoice(source, "youlong_saveself", tl_list.join("+"));
	} else if (user_string == "slash"){
		QStringList tl_list;
		if (source->getMark("youlong_slash") <= 0)
			tl_list << "slash";
		if (!Config.BanPackages.contains("maneuvering")){
			if (source->getMark("youlong_thunder_slash") <= 0)
				tl_list << "thunder_slash";
			if (source->getMark("youlong_fire_slash") <= 0)
				tl_list << "fire_slash";
		}
		if (tl_list.isEmpty()) return nullptr;
		tl = room->askForChoice(source, "youlong_slash", tl_list.join("+"));
	}
	if (source->getMark("youlong_" + tl) > 0) return nullptr;

	QStringList areas;
	for (int i = 0; i < 5; i++){
		if (source->hasEquipArea(i))
			areas << QString::number(i);
	}
	if (areas.isEmpty()) return nullptr;

	if (source->getChangeSkillState("youlong") == 1){
		room->addPlayerMark(source, "youlong_trick_lun");
		room->setChangeSkillState(source, "youlong", 2);
	} else {
		room->addPlayerMark(source, "youlong_basic_lun");
		room->setChangeSkillState(source, "youlong", 1);
	}

	QString area = room->askForChoice(source, "youlong", areas.join("+"));
	source->throwEquipArea(area.toInt());
	room->addPlayerMark(source, "youlong_" + tl);

	Card*use_card = Sanguosha->cloneCard(tl);
	use_card->setSkillName("youlong");
	use_card->deleteLater();
	return use_card;
}

class Youlong : public ZeroCardViewAsSkill
{
public:
	Youlong() : ZeroCardViewAsSkill("youlong")
	{
		change_skill = true;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		if (!player->hasEquipArea()) return false;
		return (player->getChangeSkillState(objectName()) == 1 && player->getMark("youlong_trick_lun") <= 0)
			||(player->getChangeSkillState(objectName()) == 2 && player->getMark("youlong_basic_lun") <= 0);
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
			return false;
		if (!player->hasEquipArea()) return false;
		if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;

		bool can_use = false, trick = false, basic = false;
		QStringList patterns = pattern.split("+");
		foreach(QString name, patterns){
			name = name.toLower();
			if (player->getMark("youlong_" + name) > 0) continue;
			Card*card = Sanguosha->cloneCard(name);
			if (!card) continue;
			card->deleteLater();
			can_use = true;
			if (card->isKindOf("BasicCard"))
				basic = true;
			else if (card->isNDTrick())
				trick = true;
		}
		if (!can_use){
			patterns = pattern.split(",");
			foreach(QString name, patterns){
				name = name.toLower();
				if (player->getMark("youlong_" + name) > 0) continue;
				Card*card = Sanguosha->cloneCard(name);
				if (!card) continue;
				card->deleteLater();
				can_use = true;
				if (card->isKindOf("BasicCard"))
					basic = true;
				else if (card->isNDTrick())
					trick = true;
			}
		}
		if (!can_use) return false;
		return (player->getMark("youlong_trick_lun") <= 0 && trick && player->getChangeSkillState("youlong") == 1)
		|| (player->getMark("youlong_basic_lun") <= 0 && basic && player->getChangeSkillState("youlong") == 2);
	}

	bool isEnabledAtNullification(const ServerPlayer*player) const
	{
		if (player->getMark("youlong_trick_lun") > 0) return false;
		return player->getMark("youlong_nullification") <= 0 && player->hasEquipArea() && player->getChangeSkillState("youlong") == 1;
	}

	const Card*viewAs() const
	{
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
			YoulongCard*card = new YoulongCard;
			card->setUserString(Sanguosha->getCurrentCardUsePattern());
			return card;
		}

		const Card*c = Self->getTag("youlong").value<const Card*>();
		if (c && c->isAvailable(Self)){
			YoulongCard*card = new YoulongCard;
			card->setUserString(c->objectName());
			return card;
		}
		return nullptr;
	}

	QDialog*getDialog() const
	{
		return YoulongDialog::getInstance("youlong");
	}
};

class Luanfeng : public TriggerSkill
{
public:
	Luanfeng() : TriggerSkill("luanfeng")
	{
		events << Dying;
		frequency = Limited;
		limit_mark = "@luanfengMark";
	}


	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getMark("@luanfengMark") <= 0) return false;
		DyingStruct dying = data.value<DyingStruct>();
		if (dying.who->getMaxHp() < player->getMaxHp()) return false;
		if (!player->askForSkillInvoke(this, dying.who)) return false;

		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "luanfeng");
		room->removePlayerMark(player, "@luanfengMark");

		int recover = qMin(3, dying.who->getMaxHp()) - dying.who->getHp();
		room->recover(dying.who, RecoverStruct(player, nullptr, recover, "luanfeng"));
		QList<int> list;
		for (int i = 0; i < 5; i++){
			if (!dying.who->hasEquipArea(i))
				list << i;
		}
		if (!list.isEmpty())
			dying.who->obtainEquipArea(list);
		int n = 6 - list.length() - dying.who->getHandcardNum();
		if (n > 0)
			dying.who->drawCards(n, objectName());
		if (dying.who == player){
			foreach(QString mark, player->getMarkNames()){
				if (mark.startsWith("youlong_") && !mark.endsWith("_lun") && player->getMark(mark) > 0)
					room->setPlayerMark(player, mark, 0);
			}
		}
		return false;
	}
};

class Weiyi : public MasochismSkill
{
public:
	Weiyi() : MasochismSkill("weiyi")
	{
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	void onDamaged(ServerPlayer*player, const DamageStruct &) const
	{
		Room*room = player->getRoom();
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (player->isDead()) return;
			if (!p->isAlive() || !p->hasSkill(objectName())) continue;
			QStringList targets = p->property("weiyi_targets").toStringList();
			if (targets.contains(player->objectName())) continue;

			QStringList choices;
			if (player->getHp() >= p->getHp())
				choices << "losehp=" + player->objectName();
			if (player->getHp() <= p->getHp() && player->isWounded())
				choices << "recover=" + player->objectName();
			if (choices.isEmpty()||!p->askForSkillInvoke(this,player)) continue;
			QString choice = room->askForChoice(p, objectName(), choices.join("+"), QVariant::fromValue(player));
			targets << player->objectName();
			room->setPlayerProperty(p, "weiyi_targets", targets);
			if (choice.startsWith("losehp")){
				room->broadcastSkillInvoke(objectName(), 1);
				room->loseHp(HpLostStruct(player, 1, objectName(), p));
			} else {
				room->broadcastSkillInvoke(objectName(), 2);
				room->recover(player, RecoverStruct("weiyi", p));
			}
		}
	}
};

JinzhiCard::JinzhiCard(QString skill_name): skill_name(skill_name)
{
	handling_method = Card::MethodDiscard;
}

bool JinzhiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card){
		card->addSubcards(subcards);
		card->setSuit(Card::NoSuit);
		card->setNumber(0);
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool JinzhiCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE){
		Card*card = Sanguosha->cloneCard(user_string.split("+").first());
		if(card){
			card->deleteLater();
			return card->targetFixed();
		}
	}
	return true;
}

bool JinzhiCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*JinzhiCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer*source = card_use.from;
	Room*room = source->getRoom();

	QString tl = user_string;
	if ((user_string.contains("slash") || user_string.contains("Slash"))
		&& Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
		QStringList tl_list;
		tl_list << "slash";
		if (!Config.BanPackages.contains("maneuvering"))
			tl_list << "thunder_slash" << "fire_slash";
		if (tl_list.isEmpty()) return nullptr;
		tl = room->askForChoice(source, skill_name + "_slash", tl_list.join("+"));
	}

	room->addPlayerMark(source, "&" + skill_name + "_lun");

	LogMessage log;
	log.from = source;
	log.type = "#UseCard";
	log.card_str = toString();
	room->sendLog(log);

	room->broadcastSkillInvoke(skill_name);

	CardMoveReason reason(CardMoveReason::S_REASON_THROW, source->objectName(), "", skill_name, "");
	QList<CardsMoveStruct> moves;
	foreach(int id, subcards){
		CardsMoveStruct move(id, nullptr, Player::DiscardPile, reason);
		moves.append(move);
	}
	room->moveCardsAtomic(moves, true);

	source->drawCards(1, skill_name);

	bool same = true;
	const Card*first = Sanguosha->getCard(subcards.first());
	foreach(int id, subcards){
		const Card*card = Sanguosha->getCard(id);
		if (!card->sameColorWith(first)){
			same = false;
			break;
		}
	}

	if (!same) return nullptr;

	Card*use_card = Sanguosha->cloneCard(tl);
	use_card->setSkillName("_" + skill_name);
	use_card->deleteLater();
	return use_card;
}

const Card*JinzhiCard::validateInResponse(ServerPlayer*source) const
{
	Room*room = source->getRoom();
	QString tl;
	if (user_string == "peach+analeptic"){
		QStringList tl_list;
		tl_list << "peach";
		if (!Config.BanPackages.contains("maneuvering"))
			tl_list << "analeptic";
		if (tl_list.isEmpty()) return nullptr;
		tl = room->askForChoice(source, skill_name + "_saveself", tl_list.join("+"));
	} else if (user_string == "slash"){
		QStringList tl_list;
		tl_list << "slash";
		if (!Config.BanPackages.contains("maneuvering"))
			tl_list << "thunder_slash" << "fire_slash";
		if (tl_list.isEmpty()) return nullptr;
		tl = room->askForChoice(source, skill_name + "_slash", tl_list.join("+"));
	} else
		tl = user_string;

	room->addPlayerMark(source, "&" + skill_name + "_lun");

	LogMessage log;
	log.from = source;
	log.type = "#UseCard";
	log.card_str = toString();
	room->sendLog(log);

	room->broadcastSkillInvoke(skill_name);

	CardMoveReason reason(CardMoveReason::S_REASON_THROW, source->objectName(), "", "jinzhi", "");
	QList<CardsMoveStruct> moves;
	foreach(int id, subcards){
		CardsMoveStruct move(id, nullptr, Player::DiscardPile, reason);
		moves.append(move);
	}
	room->moveCardsAtomic(moves, true);

	source->drawCards(1, skill_name);

	bool same = true;
	const Card*first = Sanguosha->getCard(subcards.first());
	foreach(int id, subcards){
		const Card*card = Sanguosha->getCard(id);
		if (!card->sameColorWith(first)){
			same = false;
			break;
		}
	}

	if (!same) return nullptr;

	Card*use_card = Sanguosha->cloneCard(tl);
	use_card->setSkillName("_" + skill_name);
	use_card->deleteLater();
	return use_card;
}

class Jinzhi : public ViewAsSkill
{
public:
	Jinzhi(const QString &skill_name) : ViewAsSkill(skill_name), skill_name(skill_name)
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->canDiscard(player, "he");
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;

		bool basic = false;
		foreach(QString name, pattern.split("+")){
			Card*card = Sanguosha->cloneCard(name);
			if (!card) continue;
			card->deleteLater();
			if (card->isKindOf("BasicCard")){
				basic = true;
				break;
			}
		}
		if (!basic){
			foreach(QString name, pattern.split(",")){
				Card*card = Sanguosha->cloneCard(name);
				if (!card) continue;
				card->deleteLater();
				if (card->isKindOf("BasicCard")){
					basic = true;
					break;
				}
			}
		}
		return basic&&player->canDiscard(player, "he");
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		int mark = Self->getMark("&" + skill_name + "_lun") + 1;
		if (!Self->isJilei(to_select) && selected.length() < mark){
			if (skill_name == "jinzhi")
				return true;
			else if (skill_name == "secondjinzhi"){
				if (selected.isEmpty())
					return true;
				else
					return to_select->sameColorWith(selected.first());
			}
		}
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		int mark = Self->getMark("&" + skill_name + "_lun") + 1;
		if (cards.length() != mark) return nullptr;

		SkillCard*card = nullptr;
		if (skill_name == "jinzhi")
			card = new JinzhiCard;
		else if (skill_name == "secondjinzhi")
			card = new SecondJinzhiCard;
		if (!card) return nullptr;

		card->addSubcards(cards);

		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			|| Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE){
			card->setUserString(Sanguosha->getCurrentCardUsePattern());
			return card;
		}

		const Card*c = Self->getTag(skill_name).value<const Card*>();
		if (c && c->isAvailable(Self)){
			card->setUserString(c->objectName());
			return card;
		}
		return nullptr;
	}

	QDialog*getDialog() const
	{
		return GuhuoDialog::getInstance(skill_name, true, false);
	}

private:
	QString skill_name;
};

SecondJinzhiCard::SecondJinzhiCard() : JinzhiCard("secondjinzhi")
{
	handling_method = Card::MethodDiscard;
}

class Zhuangshu : public PhaseChangeSkill
{
public:
	Zhuangshu() : PhaseChangeSkill("zhuangshu")
	{
		waked_skills = "_qiongshu,_xishu,_jinshu";
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->getPhase() == Player::RoundStart;
	}

	int getBaoshu(Room*room, const QString &baoshu) const
	{
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			foreach(int id, p->getEquipsId() + p->getJudgingAreaID()){
				const Card*card = Sanguosha->getEngineCard(id);
				if (card->objectName() == baoshu)
					return id;
			}
		}
		return -1;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (!p->hasSkill(objectName()) || !p->canDiscard(p, "h")) continue;
			const Card*card = room->askForCard(p, "..", "@zhuangshu-discard:" + player->objectName(), QVariant::fromValue(player), objectName());
			if (!card) continue;
			room->broadcastSkillInvoke(this);

			if (player->isDead() || !player->hasTreasureArea() || player->getTreasure()) continue;

			QString baoshu = "_qiongshu";
			if (card->isKindOf("TrickCard"))
				baoshu = "_xishu";
			else if (card->isKindOf("EquipCard"))
				baoshu = "_jinshu";

			int id = getBaoshu(room, baoshu);
			if (id >= 0){
				CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER, p->objectName(), "zhuangshu", "");
				QList<CardsMoveStruct> moves;
				card = Sanguosha->getCard(id);
				const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
				if(!player->hasEquipArea(equip->location())) continue;
				card = player->getEquip(equip->location());
				if(card){
					reason.m_reason = CardMoveReason::S_REASON_PUT;
					CardsMoveStruct move(card->getEffectiveId(), nullptr, Player::DiscardPile, reason);
					moves << move;
				}
				CardsMoveStruct move(id, player, Player::PlaceEquip, reason);
				moves << move;
				room->moveCardsAtomic(move, true);
			} else {
				id = player->getDerivativeCard(baoshu, Player::PlaceEquip);
				if (player->getEquipsId().contains(id)){
					LogMessage log;
					log.type = "#ZhuangshuEquip";
					log.from = p;
					log.to << player;
					log.arg = baoshu;
					room->sendLog(log);
				}else if (id >= 0){
					CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER, p->objectName(), "zhuangshu", "");
					QList<CardsMoveStruct> moves;
					card = Sanguosha->getCard(id);
					const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
					if(!player->hasEquipArea(equip->location())) continue;
					card = player->getEquip(equip->location());
					if(card){
						reason.m_reason = CardMoveReason::S_REASON_PUT;
						CardsMoveStruct move(card->getEffectiveId(), nullptr, Player::DiscardPile, reason);
						moves << move;
					}
					CardsMoveStruct move(id, player, Player::PlaceEquip, reason);
					moves << move;
					room->moveCardsAtomic(move, true);
				} 
			}
		}
		return false;
	}
};

class ZhuangshuStart : public GameStartSkill
{
public:
	ZhuangshuStart() : GameStartSkill("#zhuangshu")
	{
	}

	void onGameStart(ServerPlayer*player) const
	{
		if (!player->hasSkill("zhuangshu")) return;
		if (!player->hasTreasureArea() || player->getTreasure()) return;

		QList<int> baoshus;
		QStringList baoshu_names;
		baoshu_names << "_qiongshu" << "_xishu" << "_jinshu";
		foreach(QString baoshu, baoshu_names){
			int id = player->getDerivativeCard(baoshu, Player::PlaceTable);
			if (id > -1)
				baoshus << id;
		}
		if (baoshus.isEmpty()) return;

		if (!player->askForSkillInvoke("zhuangshu")) return;
		Room*room = player->getRoom();
		room->broadcastSkillInvoke("zhuangshu");

		room->fillAG(baoshus, player);
		int id = room->askForAG(player, baoshus, false, "zhuangshu");
		room->clearAG(player);

		CardMoveReason reason(CardMoveReason::S_REASON_EXCLUSIVE, "zhuangshu");
		CardsMoveStruct move(id, nullptr, player, Player::PlaceTable, Player::PlaceEquip, reason);
		room->moveCardsAtomic(move, true);
		if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceEquip){
			LogMessage log;
			log.type = "#ZhuangshuEquip";
			log.from = player;
			log.to << player;
			log.arg = Sanguosha->getCard(id)->objectName();
			room->sendLog(log);
		}
		return;
	}
};

class ChuitiVS : public OneCardViewAsSkill
{
public:
	ChuitiVS() : OneCardViewAsSkill("chuiti")
	{
		response_pattern = "@@chuiti";
		expand_pile = "#chuiti";
	}

	bool viewFilter(const Card*to_select) const
	{
		return Self->getPile("#chuiti").contains(to_select->getEffectiveId());
	}

	const Card*viewAs(const Card*originalCard) const
	{
		return originalCard;
	}
};

class Chuiti : public TriggerSkill
{
public:
	Chuiti() : TriggerSkill("chuiti")
	{
		events << CardsMoveOneTime << PreCardUsed;
		view_as_skill = new ChuitiVS;
	}

	bool hasBaoshu(Player*player) const
	{
		QStringList baoshu_names;
		baoshu_names << "_qiongshu" << "_xishu" << "_jinshu";
		foreach(const Card*card, player->getEquips()){
			if (baoshu_names.contains(card->objectName()))
				return true;
		}
		return false;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->hasFlag("chuiti_use_card")) return false;
	
			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = use.from;
			log.arg = "chuiti";
			room->sendLog(log);
			room->broadcastSkillInvoke("chuiti");
			room->notifySkillInvoked(use.from, "chuiti");

			if (room->hasCurrent())
				room->addPlayerMark(use.from, "chuiti-Clear");
			return false;
		}
		if (!room->hasCurrent() || player->getMark("chuiti-Clear") > 0) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place != Player::DiscardPile || !move.from) return false;
		if (move.from != player && !hasBaoshu(move.from)) return false;

		if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD){
			QList<int> ids;
			for (int i = 0; i < move.card_ids.length(); i++){
				if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip){
					int id = move.card_ids.at(i);
					if (room->getCardPlace(id) == Player::DiscardPile){
						const Card*card = Sanguosha->getCard(id);
						if (player->canUse(card))
							ids << id;
					}
				}
			}
			if (ids.isEmpty()) return false;
			room->notifyMoveToPile(player, ids, objectName(), Player::DiscardPile, true);
			try {
				room->askForUseCard(player, "@@chuiti", "@chuiti", -1, Card::MethodUse, true, nullptr, nullptr, "chuiti_use_card");
			}catch (TriggerEvent triggerEvent){
				if (triggerEvent == TurnBroken || triggerEvent == StageChange)
					room->notifyMoveToPile(player, ids, objectName(), Player::DiscardPile, false);
				throw triggerEvent;
			}
			room->notifyMoveToPile(player, ids, objectName(), Player::DiscardPile, false);
		}
		return false;
	}
};

class OLWuniang : public TriggerSkill
{
public:
	OLWuniang() : TriggerSkill("olwuniang")
	{
		events << CardFinished;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getMark("olwuniang-Clear") > 0 || !player->hasFlag("CurrentPlayer")) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash") || use.to.length() != 1) return false;
		ServerPlayer*to = use.to.first();
		if (to->isDead() || !to->canSlash(player, false) || !player->askForSkillInvoke(this, to)) return false;
		room->addPlayerMark(player, "olwuniang-Clear");
		room->broadcastSkillInvoke(objectName());
		room->askForUseSlashTo(to, player, "@olwuniang-slash:" + player->objectName(), false);
		if (player->isDead()) return false;
		player->drawCards(1, objectName());
		room->addSlashCishu(player, 1);
		return false;
	}
};

class OLXushen : public TriggerSkill
{
public:
	OLXushen() : TriggerSkill("olxushen")
	{
		events << Dying;
		frequency = Limited;
		limit_mark = "@olxushenMark";
		waked_skills = "olzhennan";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (player != dying.who || player->getMark("@olxushenMark") <= 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "olxushen");
		room->removePlayerMark(player, "@olxushenMark");
		room->recover(player, RecoverStruct(player, nullptr, qMin(1 - player->getHp(), player->getMaxHp() - player->getHp()), "olxushen"));
		if (player->isDead()) return false;
		room->acquireSkill(player, "olzhennan");
		if (player->isDead()) return false;
		bool guansuo = false;
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo")){
				guansuo = true;
				break;
			}
		}
		if (guansuo) return false;
		QList<ServerPlayer*> males;
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (p->isMale())
				males << p;
		}
		if (males.isEmpty()) return false;
		ServerPlayer*target = room->askForPlayerChosen(player, males, objectName(), "@olxushen-invoke", true);
		if (!target) return false;
		room->doAnimate(1, player->objectName(), target->objectName());
		if (target->askForSkillInvoke("tenyearxushenChange", "guansuo"))
			room->changeHero(target, "ol_guansuo", false, false);
		return false;
	}
};

class Shajue : public TriggerSkill
{
public:
	Shajue() : TriggerSkill("shajue")
	{
		events << Dying;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (dying.who == player || dying.who->getHp() >= 0) return false;
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		player->gainMark("&brutal");
		if (!dying.damage || !dying.damage->card) return false;
		if (dying.damage->card->isKindOf("SkillCard") || !room->CardInTable(dying.damage->card)) return false;
		room->obtainCard(player, dying.damage->card);
		return false;
	}
};

XionghuoCard::XionghuoCard()
{
}

bool XionghuoCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select->getMark("&brutal") <= 0 && to_select != Self;
}

void XionghuoCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->getMark("&brutal") < 0) return;
	effect.from->loseMark("&brutal");
	effect.to->gainMark("&brutal");
}

class XionghuoViewAsSkill : public ZeroCardViewAsSkill
{
public:
	XionghuoViewAsSkill() : ZeroCardViewAsSkill("xionghuo")
	{
	}

	const Card*viewAs() const
	{
		return new XionghuoCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("&brutal") > 0;
	}
};

class Xionghuo : public TriggerSkill
{
public:
	Xionghuo() : TriggerSkill("xionghuo")
	{
		events << DamageCaused << EventPhaseStart << GameStart;
		view_as_skill = new XionghuoViewAsSkill;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart){
			if (player->getPhase() != Player::Play) return false;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->hasSkill(objectName()) && player->isAlive() && player->getMark("&brutal") > 0){
					room->sendCompulsoryTriggerLog(p, "xionghuo", true, true);
					player->loseAllMarks("&brutal");
					int i = qrand() % 3;
					LogMessage log;
					log.type = "#XionghuoEffect";
					log.from = player;
					log.arg = "xionghuo";
					if (i == 0){
						log.arg2 = "xionghuo_choice0";
						room->sendLog(log);
						room->damage(DamageStruct(objectName(), p, player, 1, DamageStruct::Fire));
						if (player->isAlive()){
							room->setPlayerMark(player, "xionghuo_from-Clear", 1);
							room->setPlayerMark(p, "xionghuo_to-Clear", 1);
						}
					} else if (i == 1){
						log.arg2 = "xionghuo_choice1";
						room->sendLog(log);
						room->loseHp(HpLostStruct(player, 1, objectName(), p));
						if (player->isAlive())
							room->addMaxCards(player, -1);
					} else {
						log.arg2 = "xionghuo_choice2";
						room->sendLog(log);
						DummyCard*dummy = new DummyCard;
						if (player->hasEquip()){
							int i = qrand() % player->getEquips().length();
							dummy->addSubcard(player->getEquips().at(i));
						}
						if (!player->isKongcheng())
							dummy->addSubcard(player->getRandomHandCardId());
						if (dummy->subcardsLength() > 0)
							room->obtainCard(p, dummy, false);
						delete dummy;
					}
				}
			}
		}else if (triggerEvent == GameStart){
			if (!player->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->gainMark("&brutal", 3);
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.from->hasSkill(objectName()) || damage.from == damage.to || damage.to->getMark("&brutal") <= 0) return false;
			LogMessage log;
			log.type = "#XionghuoDamage";
			log.from = damage.from;
			log.to << damage.to;
			log.arg = "xionghuo";
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class XionghuoPro : public ProhibitSkill
{
public:
	XionghuoPro() : ProhibitSkill("#xionghuopro")
	{
	}

	bool isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*> &) const
	{
		return card->isKindOf("Slash") && from->getMark("xionghuo_from-Clear") > 0 && to->getMark("xionghuo_to-Clear") > 0;
	}
};

class Falu : public TriggerSkill
{
public:
	Falu() : TriggerSkill("falu")
	{
		events << GameStart << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == GameStart){
			bool send = false;
			if (player->getMark("@flziwei") <= 0){
				if (!send){
					send = true;
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				}
				player->gainMark("@flziwei");
			}
			if (player->getMark("@flhoutu") <= 0){
				if (!send){
					send = true;
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				}
				player->gainMark("@flhoutu");
			}
			if (player->getMark("@flyuqing") <= 0){
				if (!send){
					send = true;
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				}
				player->gainMark("@flyuqing");
			}
			if (player->getMark("@flgouchen") <= 0){
				if (!send){
					send = true;
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				}
				player->gainMark("@flgouchen");
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from && move.from == player && move.to_place == Player::DiscardPile &&
			(move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD &&
					(move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))){
				bool send = false;
				for (int i = 0; i < move.card_ids.length(); i++){
					if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip){
						const Card*c = Sanguosha->getCard(move.card_ids.at(i));
						if (c->getSuit() == Card::Spade){
							if (player->getMark("@flziwei") <= 0){
								if (!send){
									send = true;
									room->sendCompulsoryTriggerLog(player, objectName(), true, true);
								}
								player->gainMark("@flziwei");
							}
						} else if (c->getSuit() == Card::Club){
							if (player->getMark("@flhoutu") <= 0){
								if (!send){
									send = true;
									room->sendCompulsoryTriggerLog(player, objectName(), true, true);
								}
								player->gainMark("@flhoutu");
							}
						} else if (c->getSuit() == Card::Heart){
							if (player->getMark("@flyuqing") <= 0){
								if (!send){
									send = true;
									room->sendCompulsoryTriggerLog(player, objectName(), true, true);
								}
								player->gainMark("@flyuqing");
							}
						} else if (c->getSuit() == Card::Diamond){
							if (player->getMark("@flgouchen") <= 0){
								if (!send){
									send = true;
									room->sendCompulsoryTriggerLog(player, objectName(), true, true);
								}
								player->gainMark("@flgouchen");
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class ZhenyiVS : public OneCardViewAsSkill
{
public:
	ZhenyiVS() : OneCardViewAsSkill("zhenyi")
	{
		filter_pattern = ".|.|.|hand";
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		return pattern == "peach+analeptic" && player->getMark("Global_PreventPeach")<1 && player->getMark("@flhoutu") > 0;
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Peach*peach = new Peach(originalCard->getSuit(), originalCard->getNumber());
		peach->addSubcard(originalCard->getId());
		peach->setSkillName(objectName());
		return peach;
	}
};

class Zhenyi : public TriggerSkill
{
public:
	Zhenyi() : TriggerSkill("zhenyi")
	{
		events << AskForRetrial << DamageCaused << Damaged << PreCardUsed;
		view_as_skill = new ZhenyiVS;
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == PreCardUsed)
			return 5;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == DamageCaused){
			if (player->getMark("@flyuqing") > 0){
				player->setTag("flyuqing", data);
				DamageStruct damage = data.value<DamageStruct>();
				bool invoke = player->askForSkillInvoke(this, QString("flyuqing:%1").arg(damage.to->objectName()));
				player->removeTag("flyuqing");
				if (!invoke) return false;
				room->broadcastSkillInvoke(objectName());
				player->loseMark("@flyuqing");
				JudgeStruct judge;
				judge.pattern = ".|black";
				judge.who = player;
				judge.reason = objectName();
				judge.good = true;
				room->judge(judge);
				if (judge.isGood()){
					++damage.damage;
					data = QVariant::fromValue(damage);
				}
			}
		} else if (event == AskForRetrial){
			JudgeStruct*judge = data.value<JudgeStruct*>();
			if (player->getMark("@flziwei") > 0){
				player->setTag("flziwei", data);
				bool invoke = player->askForSkillInvoke(this, QString("flziwei:%1").arg(judge->who->objectName()));
				player->removeTag("flziwei");
				if (!invoke) return false;
				room->broadcastSkillInvoke(objectName());
				player->loseMark("@flziwei");
				QString choice = room->askForChoice(player, objectName(), "spade+heart", data);

				WrappedCard*new_card = Sanguosha->getWrappedCard(judge->card->getId());
				new_card->setSkillName("zhenyi");
				new_card->setNumber(5);
				new_card->setModified(true);
				//new_card->deleteLater();

				if (choice == "spade")
					new_card->setSuit(Card::Spade);
				else
					new_card->setSuit(Card::Heart);

				LogMessage log;
				log.type = "#ZhenyiRetrial";
				log.from = player;
				log.to << judge->who;
				log.arg2 = QString::number(5);
				log.arg = new_card->getSuitString();
				room->sendLog(log);
				room->broadcastUpdateCard(room->getAllPlayers(true), judge->card->getId(), new_card);
				judge->updateResult();
			}
		} else if (event == PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getSkillNames().contains(objectName()))
				player->loseMark("@flhoutu");
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature == DamageStruct::Normal) return false;
			if (player->getMark("@flgouchen") <= 0 || !player->askForSkillInvoke(this, QString("flgouchen"))) return false;
			room->broadcastSkillInvoke(objectName());
			player->loseMark("@flgouchen");

			QList<int> basic, equip, trick;
			foreach(int id, room->getDrawPile()){
				const Card*c = Sanguosha->getCard(id);
				if (c->isKindOf("BasicCard"))
					basic << id;
				else if (c->isKindOf("EquipCard"))
					equip << id;
				else if (c->isKindOf("TrickCard"))
					trick << id;
			}

			DummyCard*dummy = new DummyCard;
			if (!basic.isEmpty())
				dummy->addSubcard(basic.at(qrand() % basic.length()));
			if (!equip.isEmpty())
				dummy->addSubcard(equip.at(qrand() % equip.length()));
			if (!trick.isEmpty())
				dummy->addSubcard(trick.at(qrand() % trick.length()));

			if (dummy->subcardsLength() > 0)
				room->obtainCard(player, dummy, false);
			delete dummy;
		}
		return false;
	}
};

class Dianhua : public PhaseChangeSkill
{
public:
	Dianhua() : PhaseChangeSkill("dianhua")
	{
		frequency = Frequent;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Start && player->getPhase() != Player::Finish) return false;
		int x = 0;
		if (player->getMark("@flziwei") > 0)
			x++;
		if (player->getMark("@flhoutu") > 0)
			x++;
		if (player->getMark("@flyuqing") > 0)
			x++;
		if (player->getMark("@flgouchen") > 0)
			x++;
		if (x == 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		room->askForGuanxing(player, room->getNCards(x), Room::GuanxingUpOnly);
		return false;
	}
};

OLZhennanCard::OLZhennanCard()
{
	will_throw = false;
	mute = true;
	handling_method = Card::MethodUse;
}

bool OLZhennanCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	SavageAssault*sa = new SavageAssault(Card::SuitToBeDecided, -1);
	sa->addSubcards(subcards);
	sa->setSkillName("olzhennan");
	sa->deleteLater();

	if (subcardsLength() >= Self->getAliveSiblings().length())
		return !Self->isLocked(sa, true);

	return !Self->isLocked(sa) && targets.length() < subcardsLength() && sa->targetFilter(targets, to_select, Self);
}

bool OLZhennanCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	if (subcardsLength() >= Self->getAliveSiblings().length())
		return true;
	return !targets.isEmpty();
}

void OLZhennanCard::onUse(Room*room, CardUseStruct &card_use) const
{
	room->addPlayerMark(card_use.from, "olzhennan-PlayClear");

	SavageAssault*sa = new SavageAssault(Card::SuitToBeDecided, -1);
	sa->addSubcards(subcards);
	sa->setSkillName("olzhennan");
	sa->deleteLater();

	if (card_use.from->isLocked(sa)) return;

	foreach(ServerPlayer*p, card_use.to)
		room->addPlayerMark(p, "olzhennan_target-PlayClear");
	room->useCard(CardUseStruct(sa, card_use.from, card_use.to), true);
}

class OLZhennanVS : public ViewAsSkill
{
public:
	OLZhennanVS() : ViewAsSkill("olzhennan")
	{
		response_or_use = true;
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (to_select->isEquipped() || selected.length() >= Self->getAliveSiblings().length()) return false;
		SavageAssault*sa = new SavageAssault(Card::SuitToBeDecided, -1);
		sa->addSubcards(selected);
		sa->addSubcard(to_select);
		sa->setSkillName("olzhennan");
		sa->deleteLater();
		return !Self->isLocked(sa);
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty() || cards.length() > Self->getAliveSiblings().length()) return nullptr;
		OLZhennanCard*zhennan = new OLZhennanCard;
		zhennan->addSubcards(cards);
		return zhennan;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("olzhennan-PlayClear") <= 0;
	}
};

class OLZhennan : public TriggerSkill
{
public:
	OLZhennan() : TriggerSkill("olzhennan")
	{
		events << PreCardUsed;
		view_as_skill = new OLZhennanVS;
	}

	int getPriority(TriggerEvent) const
	{
		return 7;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("SavageAssault") || !use.card->getSkillNames().contains(objectName())) return false;
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (p->getMark("olzhennan_target-PlayClear") > 0){
				room->setPlayerMark(p, "olzhennan_target-PlayClear", 0);
				targets << p;
			}
		}
		if (targets.isEmpty()) return false;
		room->sortByActionOrder(targets);
		use.to = targets;
		data = QVariant::fromValue(use);
		return false;
	}
};

class OLZhennanWuxiao : public TriggerSkill
{
public:
	OLZhennanWuxiao() : TriggerSkill("#olzhennan-wuxiao")
	{
		events << CardEffected;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		CardEffectStruct effect = data.value<CardEffectStruct>();
		if (effect.to->hasSkill("olzhennan") && effect.card->isKindOf("SavageAssault")){
			LogMessage log;
			log.type = "#OLZhennanWuxiao";
			log.from = effect.to;
			log.arg = effect.card->objectName();
			log.arg2 = "olzhennan";
			room->sendLog(log);
			room->broadcastSkillInvoke("olzhennan");
			room->notifySkillInvoked(effect.to, "olzhennan");
			return true;
		}
		return false;
	}
};

class Ziruo : public TriggerSkill
{
public:
	Ziruo() : TriggerSkill("ziruo")
	{
		events << PreCardUsed << CardUsed << GameStart << EventAcquireSkill;
		frequency = Compulsory;
		change_skill = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==GameStart||event==EventAcquireSkill){
			room->setPlayerProperty(player,"NotSortHands",true);
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->getTypeId()<1) return false;
		int n = player->getChangeSkillState(objectName());
		if(event==PreCardUsed){
			QList<int> ids = player->handCards();
			if(ids.isEmpty()) return false;
			if(n==1){
				if(use.card->getSubcards().contains(ids.first()))
					room->setCardFlag(use.card,"ziruo");
			}else{
				if(use.card->getSubcards().contains(ids.last()))
					room->setCardFlag(use.card,"ziruo");
			}
		}else if(use.card->hasFlag("ziruo")){
			room->sendCompulsoryTriggerLog(player,this);
			if(n==1){
				player->drawCards(1,objectName());
				room->setChangeSkillState(player, objectName(), 2);
			}else{
				player->drawCards(1,objectName());
				room->setChangeSkillState(player, objectName(), 1);
			}
		}
		return false;
	}
};

XufaCard::XufaCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void XufaCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	if(source->handCards().contains(getEffectiveId())){
		source->addToPile("xufa",this);
		room->addPlayerMark(source,"xufa1-PlayClear");
	}else{
		room->throwCard(this,"xufa",nullptr);
		room->addPlayerMark(source,"xufa2-PlayClear");
	}
	if(source->isAlive()){
		QStringList choices;
		foreach(int id, getSubcards()){
			const Card*c = Sanguosha->getCard(id);
			if(c->isNDTrick()&&!choices.contains(c->objectName())){
				choices << c->objectName();
			}
		}
		if(choices.isEmpty()) return;
		QString choice = room->askForChoice(source,"xufa",choices.join("+"));
		room->setPlayerProperty(source,"xufaUse",choice);
		room->askForUseCard(source,"@@xufa","xufa0:"+choice);
	}
}

class Xufa : public ViewAsSkill
{
public:
	Xufa() : ViewAsSkill("xufa")
	{
		response_or_use = true;
		expand_pile = "xufa";
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (to_select->isEquipped()) return false;
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		QList<int> ids = Self->getPile("xufa");
		if(pattern=="@@xufa"){
			if (selected.length()>0||ids.contains(to_select->getEffectiveId())) return false;
			pattern = Self->property("xufaUse").toString();
			Card*c = Sanguosha->cloneCard(pattern);
			c->setSkillName("_"+objectName());
			c->addSubcard(to_select);
			c->deleteLater();
			return c->isAvailable(Self);
		}
		if(Self->getMark("xufa1-PlayClear")>0){
			if (!ids.contains(to_select->getEffectiveId())) return false;
		}
		if(Self->getMark("xufa2-PlayClear")>0){
			if (ids.contains(to_select->getEffectiveId())) return false;
		}
		if(selected.length()>0){
			if(ids.contains(selected[0]->getEffectiveId()))
				return ids.contains(to_select->getEffectiveId());
			return !ids.contains(to_select->getEffectiveId());
		}
		return true;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern == "@@xufa";
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern=="@@xufa"){
			pattern = Self->property("xufaUse").toString();
			Card*c = Sanguosha->cloneCard(pattern);
			c->setSkillName("_"+objectName());
			c->addSubcards(cards);
			return c;
		}
		QList<int> ids = Self->getPile("xufa");
		if(ids.contains(cards[0]->getEffectiveId())){
			if (cards.length()<ids.length()/2.0) return nullptr;
		}else{
			if (cards.length()<Self->getHandcardNum()/2.0) return nullptr;
		}
		XufaCard*sc = new XufaCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("xufa1-PlayClear")<1||player->getMark("xufa2-PlayClear")<1;
	}
};

OL2ShanjiaCard::OL2ShanjiaCard()
{
	target_fixed = true;
	will_throw = false;
}

void OL2ShanjiaCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	source->drawCards(3,"ol2shanjia");
	room->askForUseCard(source,"Slash","ol2shanjia0");
	source->setMark("sgsol2shanjiaDis-PlayClear",source->getMark("&sjbaijia"));
	source->addMark("sgsol2shanjiaUse-PlayClear");
}

class OL2ShanjiaVs : public ZeroCardViewAsSkill
{
public:
	OL2ShanjiaVs() : ZeroCardViewAsSkill("ol2shanjia")
	{
		response_pattern = "@@ol2shanjia";
	}

	const Card*viewAs() const
	{
		if(Sanguosha->getCurrentCardUsePattern().isEmpty())
			return new OL2ShanjiaCard;
		Card*dc = Sanguosha->cloneCard("slash");
		dc->setSkillName("_ol2shanjia");
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("OL2ShanjiaCard")<1;
	}
};

class OL2Shanjia : public TriggerSkill
{
public:
	OL2Shanjia() : TriggerSkill("ol2shanjia")
	{
		events << GameStart << CardsMoveOneTime << EventPhaseEnd << CardFinished;
		view_as_skill = new OL2ShanjiaVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == GameStart){
			if(player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player, this);
				player->gainMark("&sjbaijia",3);
			}
		} else if (event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(player->getMark("sgsol2shanjiaDis-PlayClear")>0){
				player->removeMark("sgsol2shanjiaDis-PlayClear");
				room->askForDiscard(player,objectName(),1,1,false,true);
			}
		} else if (event == EventPhaseEnd){
			if(player->getPhase()==Player::Play&&player->getMark("sgsol2shanjiaUse-PlayClear")>0
			&&player->getMark("sgsol2shanjiaMove-PlayClear")<1){
				room->askForUseCard(player,"@@ol2shanjia","ol2shanjia1");
			}
		} else if (event == CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player&&player->hasSkill(objectName(),true)){
				bool send = true;
				for (int i = 0; i < move.card_ids.length(); i++){
					if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip){
						const Card*c = Sanguosha->getCard(move.card_ids.at(i));
						if (c->isKindOf("EquipCard")){
							if (send&&player->getMark("&sjbaijia")>0){
								send = false;
								room->sendCompulsoryTriggerLog(player, objectName());
							}
							player->loseMark("&sjbaijia");
						}else if(move.reason.m_skillName==objectName())
							player->addMark("sgsol2shanjiaMove-PlayClear");
					}
				}
			}
		}
		return false;
	}
};

QiwuCard::QiwuCard()
{
	handling_method = Card::MethodDiscard;
}

bool QiwuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("qiwu");
	dc->deleteLater();
	return targets.length()<=Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget,Self,this,to_select)&&Self->canSlash(to_select,dc,false);
}

void QiwuCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	QStringList suits;
	foreach(int id, subcards){
		QString suit = Sanguosha->getCard(id)->getSuitString();
		if(suits.contains(suit)) continue;
		suits.append(suit);
	}
	QList<int>ids = subcards;
	foreach(ServerPlayer*p, targets){
		Card*dc = room->askForDiscard(p,"qiwu",suits.length(),suits.length(),false,true);
		if(dc) ids << dc->getSubcards();
	}
	Card*dc = dummyCard();
	foreach(int id, ids){
		if(Sanguosha->getCard(id)->isKindOf("Jink")){
			suits.clear();
			if(room->getCardOwner(id)) continue;
			dc->addSubcard(id);
		}
	}
	if(suits.isEmpty()){
		source->obtainCard(dc);
		foreach(int id, dc->getSubcards()){
			if(source->hasCard(id)){
				room->ignoreCards(source,id);
				room->setCardTip(id,"qiwu");
			}
		}
	}
	foreach(ServerPlayer*p, targets){
		if(p->isDead()) targets.removeOne(p);
	}
	if(source->isDead()||targets.isEmpty())
		return;
	dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("_qiwu");
	CardUseStruct use(dc,source,targets);
	if(suits.length()>0) use.no_respond_list << "_ALL_TARGETS";
	room->useCard(use,true);
}

class Qiwu : public ViewAsSkill
{
public:
	Qiwu() : ViewAsSkill("qiwu")
	{
	}

	bool viewFilter(const QList<const Card*> &, const Card*to_select) const
	{
		return !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		QiwuCard*sc = new QiwuCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("QiwuCard")<1;
	}
};

class OLZhuangrong : public TriggerSkill
{
public:
	OLZhuangrong() : TriggerSkill("olzhuangrong")
	{
		events << EventPhaseChanging << ChoiceMade;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->getMark("olzhuangrongNum")>1&&player->hasSkill(objectName())){
						if(p->getMark("olzhuangrongUse")>0) continue;
						p->addMark("olzhuangrongUse");
						room->sendCompulsoryTriggerLog(p, this);
						room->gainMaxHp(p,1,objectName());
						room->recover(p,RecoverStruct(objectName(),p));
					}
				}
			}else if(change.to==Player::RoundStart&&player->isAlive()&&player->hasSkill(objectName())){
				QList<int>ids;
				for (int i = 0; i < Sanguosha->getCardCount(); i++){
					const Card*c = Sanguosha->getEngineCard(i);
					if(c->getPackage()!="HulaoPass") continue;
					ids << i;
				}
				qShuffle(ids);
				foreach(int id, ids){
					const Card*c = Sanguosha->getCard(id);
					if(c->getTypeId()!=3) continue;
					room->sendCompulsoryTriggerLog(player, this);
					QStringList strs = player->getTag("olzhuangrongList").toStringList();
					strs << c->objectName();
					player->setTag("olzhuangrongList", strs);
					room->moveCardTo(c,nullptr,Player::PlaceTable,true);
					room->moveCardTo(c,player,Player::PlaceEquip,true);
					break;
				}
			}
		} else{
			QString str = data.toString();
			if(str.contains("notifyInvoked:")){
				if(player->getTag("olzhuangrongList").toStringList().contains(str.split("+").last()))
					player->addMark("olzhuangrongNum");
			}
		}
		return false;
	}
};




OLQifuPackage::OLQifuPackage()
	: Package("ol_qifu")
{
	/*General*ol_liubei = new General(this, "ol_liubei$", "shu");
	ol_liubei->addSkill(new OLRende);
	ol_liubei->addSkill("jijiang");*/
	addMetaObject<OLRendeCard>();

	General*ol_guansuo = new General(this, "ol_guansuo*qifu", "shu", 4);
	ol_guansuo->addSkill("xiefang");
	ol_guansuo->addSkill(new OLZhengnan);

	General*ol_baosanniang = new General(this, "ol_baosanniang*qifu", "shu", 4, false);
	ol_baosanniang->addSkill(new OLWuniang);
	ol_baosanniang->addSkill(new OLXushen);
	ol_baosanniang->addRelateSkill("olzhennan");
	skills << new OLZhennan << new OLZhennanWuxiao;
	related_skills.insertMulti("olzhennan", "#olzhennan-wuxiao");
	addMetaObject<OLZhennanCard>();

	General*caoying = new General(this, "caoying*qifu", "wei", 4, false);
	caoying->addSkill(new Lingren);
	caoying->addSkill(new LingrenEffect);
	caoying->addSkill(new Fujian);
	related_skills.insertMulti("lingren", "#lingreneffect");

	General*ol_caochun = new General(this, "ol_caochun*qifu", "wei", 4);
	ol_caochun->addSkill("olshanjia");
	ol_caochun->addSkill("#shanjia-record");
	related_skills.insertMulti("olshanjia", "#shanjia-record");
	related_skills.insertMulti("olshanjia", "#olshanjia-slash-ndl");

	General*ol2_caochun = new General(this, "ol2_caochun*qifu", "wei", 4);
	ol2_caochun->addSkill(new OL2Shanjia);
	addMetaObject<OL2ShanjiaCard>();

	General*xurong = new General(this, "xurong*qifu", "qun", 4);
	xurong->addSkill(new Shajue);
	xurong->addSkill(new Xionghuo);
	xurong->addSkill(new XionghuoPro);
	related_skills.insertMulti("xionghuo", "#xionghuopro");
	addMetaObject<XionghuoCard>();

	General*zhangqiying = new General(this, "zhangqiying*qifu", "qun", 3, false);
	zhangqiying->addSkill(new Falu);
	zhangqiying->addSkill(new Zhenyi);
	zhangqiying->addSkill(new Dianhua);

	General*yuantanyuanshang = new General(this, "yuantanyuanshang*qifu", "qun", 4);
	yuantanyuanshang->addSkill(new Neifa);
	addMetaObject<NeifaCard>();

	General*caoshuang = new General(this, "caoshuang*qifu", "wei", 4);
	caoshuang->addSkill(new Tuogu);
	caoshuang->addSkill(new Shanzhuan);

	General*second_caoshuang = new General(this, "second_caoshuang*qifu", "wei", 4);
	second_caoshuang->addSkill(new SecondTuogu);
	second_caoshuang->addSkill("shanzhuan");

	General*wolongfengchu = new General(this, "wolongfengchu*qifu", "shu", 4);
	wolongfengchu->addSkill(new Youlong);
	wolongfengchu->addSkill(new Luanfeng);
	addMetaObject<YoulongCard>();

	General*panshu = new General(this, "panshu*qifu", "wu", 3, false);
	panshu->addSkill(new Weiyi);
	panshu->addSkill(new Jinzhi("jinzhi"));
	addMetaObject<JinzhiCard>();

	General*second_panshu = new General(this, "second_panshu*qifu", "wu", 3, false);
	second_panshu->addSkill("weiyi");
	second_panshu->addSkill(new Jinzhi("secondjinzhi"));
	addMetaObject<SecondJinzhiCard>();

	General*fengfangnv = new General(this, "fengfangnv*qifu", "qun", 3, false);
	fengfangnv->addSkill(new Zhuangshu);
	fengfangnv->addSkill(new ZhuangshuStart);
	fengfangnv->addSkill(new Chuiti);
	fengfangnv->addRelateSkill("_qiongshu");
	fengfangnv->addRelateSkill("_xishu");
	fengfangnv->addRelateSkill("_jinshu");
	related_skills.insertMulti("zhuangshu", "#zhuangshu");

	General*ol_jiangwan = new General(this, "ol_jiangwan*qifu", "shu", 3);
	ol_jiangwan->addSkill(new Ziruo);
	ol_jiangwan->addSkill(new Xufa);
	addMetaObject<XufaCard>();

	General*ol_lvlingqi = new General(this, "ol_lvlingqi*qifu", "qun", 4,false);
	ol_lvlingqi->addSkill(new Qiwu);
	ol_lvlingqi->addSkill(new OLZhuangrong);
	addMetaObject<QiwuCard>();









}
ADD_PACKAGE(OLQifu)

class Danji : public PhaseChangeSkill
{
public:
	Danji() : PhaseChangeSkill("danji")
	{ // What a silly skill!
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*guanyu, Room*room) const
	{
		if (guanyu->getHandcardNum() > guanyu->getHp()){
			ServerPlayer*the_lord = room->getLord();
			if (the_lord && (the_lord->getGeneralName().contains("caocao") || the_lord->getGeneral2Name().contains("caocao"))){
				LogMessage log;
				log.type = "#DanjiWake";
				log.from = guanyu;
				log.arg = QString::number(guanyu->getHandcardNum());
				log.arg2 = QString::number(guanyu->getHp());
				room->sendLog(log);
			}else if(!guanyu->canWake(objectName()))
				return false;
		}else if(!guanyu->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(guanyu, objectName());

		//room->doLightbox("$DanjiAnimate", 5000);
		room->doSuperLightbox(guanyu, "danji");

		room->setPlayerMark(guanyu, "danji", 1);
		if (room->changeMaxHpForAwakenSkill(guanyu, -1, objectName()))
			room->acquireSkill(guanyu, "mashu");
		return false;
	}
};

class Kunfen : public PhaseChangeSkill
{
public:
	Kunfen() : PhaseChangeSkill("kunfen")
	{

	}

	Frequency getFrequency(const Player*target) const
	{
		if (target != nullptr){
			return target->getMark("fengliang") > 0 ? NotFrequent : Compulsory;
		}

		return Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return PhaseChangeSkill::triggerable(target) && target->getPhase() == Player::Finish;
	}

	bool onPhaseChange(ServerPlayer*target, Room*) const
	{
		if (invoke(target))
			effect(target);

		return false;
	}

private:
	bool invoke(ServerPlayer*target) const
	{
		return getFrequency(target) == Compulsory ? true : target->askForSkillInvoke(this);
	}

	void effect(ServerPlayer*target) const
	{
		Room*room = target->getRoom();

		if (getFrequency(target) == Compulsory)
			room->sendCompulsoryTriggerLog(target, objectName(), true, true, 1);
		else
			room->broadcastSkillInvoke(objectName(), 2);

		room->loseHp(HpLostStruct(target, 1, objectName(), target));
		if (target->isAlive())
			target->drawCards(2, objectName());
	}
};

class Fengliang : public TriggerSkill
{
public:
	Fengliang() : TriggerSkill("fengliang")
	{
		frequency = Wake;
		events << Dying;
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player&&player->getMark("fengliang")<1&&player->hasSkill(objectName());
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (dying.who == player){}
		else if(!player->canWake("fengliang"))
			return false;
		room->sendCompulsoryTriggerLog(player, this);
		room->doSuperLightbox(player, objectName());

		room->addPlayerMark(player, objectName(), 1);
		if (room->changeMaxHpForAwakenSkill(player, -1, objectName())){
			int recover = 2 - player->getHp();
			room->recover(player, RecoverStruct(player, nullptr, recover, objectName()));
			room->handleAcquireDetachSkills(player, "tiaoxin");

			//if (player->hasSkill("kunfen", true)){
				QString translate = Sanguosha->translate(":kunfen2");
				Sanguosha->addTranslationEntry(":kunfen", translate.toStdString().c_str());
				room->doNotify(player, QSanProtocol::S_COMMAND_UPDATE_SKILL, QVariant("kunfen"));
			//}
		}

		return false;
	}
};

JiqiaoCard::JiqiaoCard()
{
	target_fixed = true;
}

void JiqiaoCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	QList<int> card_ids = room->getNCards(subcardsLength()*2, false);
	CardMoveReason reason1(CardMoveReason::S_REASON_TURNOVER, source->objectName(), "jiqiao", "");
	CardsMoveStruct move(card_ids, nullptr, Player::PlaceTable, reason1);
	room->moveCardsAtomic(move, true);
	room->getThread()->delay();
	
	DummyCard get, thro;

	foreach(int id, card_ids){
		const Card*c = Sanguosha->getCard(id);
		if (c->isKindOf("TrickCard"))
			get.addSubcard(c);
		else
			thro.addSubcard(c);
	}

	if (get.subcardsLength() > 0)
		source->obtainCard(&get);

	if (thro.subcardsLength() > 0){
		CardMoveReason reason2(CardMoveReason::S_REASON_NATURAL_ENTER, "", "jiqiao", "");
		room->throwCard(&thro, reason2, nullptr);
	}
	get.deleteLater();
	thro.deleteLater();
}

class JiqiaoVS : public ViewAsSkill
{
public:
	JiqiaoVS() : ViewAsSkill("jiqiao")
	{
		response_pattern = "@@jiqiao";
	}

	bool viewFilter(const QList<const Card*> &, const Card*to_select) const
	{
		return to_select->isKindOf("EquipCard") && !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.length() == 0)
			return nullptr;

		JiqiaoCard*jq = new JiqiaoCard;
		jq->addSubcards(cards);
		return jq;
	}
};

class Jiqiao : public PhaseChangeSkill
{
public:
	Jiqiao() : PhaseChangeSkill("jiqiao")
	{
		view_as_skill = new JiqiaoVS;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Play)
			return false;

		if (!player->canDiscard(player, "he"))
			return false;

		room->askForUseCard(player, "@@jiqiao", "@jiqiao", -1, Card::MethodDiscard);

		return false;
	}
};

class Linglong : public ViewAsEquipSkill
{
public:
	Linglong() : ViewAsEquipSkill("linglong")
	{
	}

	QString viewAsEquip(const Player*target) const
	{
		if (target->hasEquipArea(1) && !target->getArmor())
			return "eight_diagram";
		return "";
	}
};

class LinglongMax : public MaxCardsSkill
{
public:
	LinglongMax() : MaxCardsSkill("#linglong-horse")
	{
	}

	int getExtra(const Player*target) const
	{
		if (target->hasSkill("linglong")&&!target->getDefensiveHorse()&&!target->getOffensiveHorse())
			return 1;
		return 0;
	}
};

class LinglongTrigger : public TriggerSkill
{
public:
	LinglongTrigger() : TriggerSkill("#linglong")
	{
		events << GameStart << EventAcquireSkill << EventLoseSkill << CardsMoveOneTime << InvokeSkill;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventLoseSkill && data.toString() == "linglong"){
			player->removeMark("linglong_qicai");
			room->detachSkillFromPlayer(player, "qicai", false, true);
		} else if (event == InvokeSkill && player->hasSkill("linglong")){
			if (data.toString() != "eight_diagram" || player->getArmor()) return false;
			room->sendCompulsoryTriggerLog(player, "linglong", true, true);
		}else{
			if (player->getTreasure()){
				if(player->getMark("linglong_qicai")>0&&player->hasSkill("qicai", true)){
					player->removeMark("linglong_qicai");
					room->detachSkillFromPlayer(player, "qicai", false, true);
				}
			}else if(player->hasSkill("linglong")){
				if(player->getMark("linglong_qicai")<1||!player->hasSkill("qicai", true)){
					player->addMark("linglong_qicai");
					room->notifySkillInvoked(player, "linglong");
					room->acquireSkill(player, "qicai");
				}
			}
		}
		return false;
	}
};

class Liangzhu : public TriggerSkill
{
public:
	Liangzhu() : TriggerSkill("liangzhu")
	{
		events << HpRecover;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive() && target->getPhase() == Player::Play;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		foreach(ServerPlayer*sun, room->getAllPlayers()){
			if (TriggerSkill::triggerable(sun)&&sun->askForSkillInvoke(this,player)){
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(sun, objectName());
				if (room->askForChoice(sun, objectName(), "draw+letdraw", QVariant::fromValue(player)) == "draw"){
					sun->drawCards(1,objectName());
					sun->setTag("liangzhu_draw" + sun->objectName(), true);
					room->setPlayerMark(sun, "@liangzhu_draw", 1);
				} else {
					player->drawCards(2,objectName());
					player->setTag("liangzhu_draw" + sun->objectName(), true);
					room->setPlayerMark(player, "@liangzhu_draw", 1);
				}
			}
		}
		return false;
	}
};

class Fanxiang : public TriggerSkill
{
public:
	Fanxiang() : TriggerSkill("fanxiang")
	{
		events << EventPhaseStart;
		frequency = Skill::Wake;
		waked_skills = "xiaoji";
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		bool flag = false;
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (p->getTag("liangzhu_draw" + player->objectName()).toBool() && p->isWounded()){
				flag = true;
				break;
			}
		}
		if(!flag&&!player->canWake("fanxiang"))
			return false;
		room->broadcastSkillInvoke(objectName());

		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, "fanxiang");

		//room->doLightbox("$fanxiangAnimate", 5000);
		room->setPlayerMark(player, "fanxiang", 1);
		if (room->changeMaxHpForAwakenSkill(player, 1, objectName())){
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if (p->getTag("liangzhu_draw" + player->objectName()).toBool())
					room->setPlayerMark(p, "@liangzhu_draw", 0);
			}

			room->recover(player, RecoverStruct(objectName(), player));
			room->handleAcquireDetachSkills(player, "-liangzhu|xiaoji");
		}
		return false;
	}
};

class Zhuiji : public DistanceSkill
{
public:
	Zhuiji() : DistanceSkill("zhuiji")
	{
	}

	int getFixed(const Player*from, const Player*to) const
	{
		if (to->getHp()<from->getHp()&&from->hasSkill(objectName()))
			return 1;
		return 0;
	}
};

class CihuaiVS : public ZeroCardViewAsSkill
{
public:
	CihuaiVS() : ZeroCardViewAsSkill("cihuai")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return Slash::IsAvailable(player) && player->getMark("@cihuai") > 0;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		return (pattern.contains("slash") || pattern.contains("Slash")) && player->getMark("@cihuai") > 0;
	}

	const Card*viewAs() const
	{
		Slash*slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("_" + objectName());
		return slash;
	}
};

class Cihuai : public TriggerSkill
{
public:
	Cihuai() : TriggerSkill("cihuai")
	{
		events << EventPhaseStart << CardsMoveOneTime << Death;
		view_as_skill = new CihuaiVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart){
			if (player->getPhase() == Player::Play && !player->isKongcheng()
				&& TriggerSkill::triggerable(player) && player->askForSkillInvoke(this, data)){
				room->showAllCards(player);
				foreach(const Card*card, player->getHandcards()){
					if (card->isKindOf("Slash")){
						room->broadcastSkillInvoke(objectName(), 1);
						return false;
					}
				}
				room->setPlayerMark(player, "cihuai_handcardnum", player->getHandcardNum());
				room->broadcastSkillInvoke(objectName(), 2);
				room->setPlayerMark(player, "@cihuai", 1);
				room->setPlayerMark(player, "ViewAsSkill_cihuaiEffect", 1);
			}
		} else if (triggerEvent == CardsMoveOneTime){
			if (player->getMark("@cihuai") > 0 && player->getHandcardNum() != player->getMark("cihuai_handcardnum")){
				room->setPlayerMark(player, "@cihuai", 0);
				room->setPlayerMark(player, "ViewAsSkill_cihuaiEffect", 0);
			}
		} else if (triggerEvent == Death&&player->getMark("@cihuai") > 0){
			room->setPlayerMark(player, "@cihuai", 0);
			room->setPlayerMark(player, "ViewAsSkill_cihuaiEffect", 0);
		}
		return false;
	}
};


class Nuzhan : public TriggerSkill
{
public:
	Nuzhan() : TriggerSkill("nuzhan")
	{
		events << PreCardUsed << CardUsed << ConfirmDamage;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		if (triggerEvent == PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (TriggerSkill::triggerable(use.from)){
				if (use.card && use.card->isKindOf("Slash") && use.card->isVirtualCard() && use.card->subcardsLength() == 1 && Sanguosha->getCard(use.card->getSubcards().first())->isKindOf("TrickCard")){
					room->broadcastSkillInvoke(objectName(), 1);
					use.m_addHistory = false;
					data = QVariant::fromValue(use);
				}
			}
		} else if (triggerEvent == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (TriggerSkill::triggerable(use.from)){
				if (use.card != nullptr && use.card->isKindOf("Slash") && use.card->isVirtualCard() && use.card->subcardsLength() == 1 && Sanguosha->getCard(use.card->getSubcards().first())->isKindOf("EquipCard"))
					use.card->setFlags("nuzhan_slash");
			}
		} else if (triggerEvent == ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && damage.card->hasFlag("nuzhan_slash")){
				if (damage.from)
					room->sendCompulsoryTriggerLog(damage.from, objectName(), true);

				room->broadcastSkillInvoke(objectName(), 2);

				++damage.damage;
				data = QVariant::fromValue(damage);
			}
		}
		return false;
	}
};

class JspDanqi : public PhaseChangeSkill
{
public:
	JspDanqi() : PhaseChangeSkill("jspdanqi")
	{
		frequency = Wake;
		waked_skills = "mashu,nuzhan";
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*target, Room*room) const
	{
		if (target->getHandcardNum() > target->getHp() && !lordIsLiubei(room)){}
		else if(!target->canWake("jspdanqi"))
			return false;
		room->sendCompulsoryTriggerLog(target, this);
		//room->doLightbox("$JspdanqiAnimate");
		room->doSuperLightbox(target, "jspdanqi");
		room->setPlayerMark(target, objectName(), 1);
		if (room->changeMaxHpForAwakenSkill(target, -1, objectName()))
			room->handleAcquireDetachSkills(target, "mashu|nuzhan");

		return false;
	}

private:
	static bool lordIsLiubei(const Room*room)
	{
		const ServerPlayer*const lord = room->getLord();
		if (lord){
			if (lord->getGeneral() && lord->getGeneralName().contains("liubei"))
				return true;
			if (lord->getGeneral2() && lord->getGeneral2Name().contains("liubei"))
				return true;
		}
		return false;
	}
};


class Chixin : public OneCardViewAsSkill
{
public:
	Chixin() : OneCardViewAsSkill("chixin")
	{
		filter_pattern = ".|diamond";
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern == "jink" || pattern.contains("slash") || pattern.contains("Slash");
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*c = nullptr;
		if (Sanguosha->getCurrentCardUsePattern() == "jink")
			c = new Jink(Card::SuitToBeDecided, -1);
		else
			c = new Slash(Card::SuitToBeDecided, -1);

		c->setSkillName(objectName());
		c->addSubcard(originalCard);
		return c;
	}
};

class Suiren : public PhaseChangeSkill
{
public:
	Suiren() : PhaseChangeSkill("suiren")
	{
		frequency = Limited;
		limit_mark = "@suiren";
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return TriggerSkill::triggerable(target) && target->getPhase() == Player::Start && target->getMark("@suiren") > 0;
	}

	bool onPhaseChange(ServerPlayer*target, Room*room) const
	{
		ServerPlayer*p = room->askForPlayerChosen(target, room->getAlivePlayers(), objectName(), "@suiren-draw", true, true);
		if (p == nullptr)
			return false;

		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(target, "suiren");
		room->setPlayerMark(target, "@suiren", 0);
		
		room->handleAcquireDetachSkills(target, "-yicong");
		int maxhp = target->getMaxHp() + 1;
		room->setPlayerProperty(target, "maxhp", maxhp);
		room->recover(target, RecoverStruct(objectName(), target));

		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), p->objectName());
		p->drawCards(3, objectName());

		return false;
	}
};

GusheCard::GusheCard(QString skill_name) : skill_name(skill_name)
{
}

bool GusheCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.length() < 3 && Self->canPindian(to_select);
}

int GusheCard::pindian(ServerPlayer*from, ServerPlayer*target, const Card*card1, const Card*card2) const
{
	if (!card2||!from->canPindian(target, false)) return -2;

	Room*room = from->getRoom();

	PindianStruct*pindian_struct = new PindianStruct;
	pindian_struct->from = from;
	pindian_struct->to = target;
	pindian_struct->from_card = card1;
	pindian_struct->to_card = card2;
	pindian_struct->from_number = card1->getNumber();
	pindian_struct->to_number = card2->getNumber();
	pindian_struct->reason = skill_name;
	QVariant data = QVariant::fromValue(pindian_struct);

	CardsMoveStruct move1;
	move1.card_ids << pindian_struct->from_card->getEffectiveId();
	move1.from = pindian_struct->from;
	move1.to = nullptr;
	move1.to_place = Player::PlaceTable;
	move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(),
		pindian_struct->to->objectName(), skill_name, "");

	CardsMoveStruct move2;
	move2.card_ids << pindian_struct->to_card->getEffectiveId();
	move2.from = pindian_struct->to;
	move2.to = nullptr;
	move2.to_place = Player::PlaceTable;
	move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(), skill_name, "");

	QList<CardsMoveStruct> moves;
	moves.append(move1);
	moves.append(move2);
	room->moveCardsAtomic(moves, true);

	LogMessage log;
	log.type = "$PindianResult";
	log.from = pindian_struct->from;
	log.card_str = QString::number(pindian_struct->from_card->getEffectiveId());
	room->sendLog(log);

	log.type = "$PindianResult";
	log.from = pindian_struct->to;
	log.card_str = QString::number(pindian_struct->to_card->getEffectiveId());
	room->sendLog(log);

	RoomThread*thread = room->getThread();
	thread->trigger(PindianVerifying, room, from, data);

	pindian_struct = data.value<PindianStruct*>();

	pindian_struct->success = pindian_struct->from_number > pindian_struct->to_number;

	log.type = pindian_struct->success ? "#PindianSuccess" : "#PindianFailure";
	log.from = from;
	log.to << target;
	log.card_str.clear();
	room->sendLog(log);

	JsonArray arg;
	arg << QSanProtocol::S_GAME_EVENT_REVEAL_PINDIAN << pindian_struct->from->objectName() << pindian_struct->from_card->getEffectiveId() << target->objectName()
		<< pindian_struct->to_card->getEffectiveId() << pindian_struct->success << skill_name;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

	data = QVariant::fromValue(pindian_struct);
	thread->trigger(Pindian, room, from, data);

	moves.clear();
	if (room->getCardPlace(pindian_struct->from_card->getEffectiveId()) == Player::PlaceTable){
		CardsMoveStruct move1;
		move1.card_ids << pindian_struct->from_card->getEffectiveId();
		move1.from = pindian_struct->from;
		move1.to = nullptr;
		move1.to_place = Player::DiscardPile;
		move1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(),
			pindian_struct->to->objectName(), skill_name, "");
		moves.append(move1);
	}

	if (room->getCardPlace(pindian_struct->to_card->getEffectiveId()) == Player::PlaceTable){
		CardsMoveStruct move2;
		move2.card_ids << pindian_struct->to_card->getEffectiveId();
		move2.from = pindian_struct->to;
		move2.to = nullptr;
		move2.to_place = Player::DiscardPile;
		move2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName(), skill_name, "");
		moves.append(move2);
	}
	if (!moves.isEmpty())
		room->moveCardsAtomic(moves, true);

	data = QString("pindian:%1:%2:%3:%4:%5").arg(skill_name).arg(from->objectName()).arg(pindian_struct->from_card->getEffectiveId())
		.arg(target->objectName()).arg(pindian_struct->to_card->getEffectiveId());
	thread->trigger(ChoiceMade, room, from, data);

	if (pindian_struct->success) return 1;
	else if (pindian_struct->from_number == pindian_struct->to_number) return 0;
	else if (pindian_struct->from_number < pindian_struct->to_number) return -1;
	return -2;
}

void GusheCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	if (!source->canPindian()) return;
	LogMessage log;
	log.type = "#Pindian";
	log.from = source;
	log.to = targets;
	room->sendLog(log);

	const Card*cardss = nullptr;
	QHash<ServerPlayer*, const Card*> hash;
	foreach(ServerPlayer*target, targets){
		if (!source->canPindian(target, false)) continue;
		PindianStruct*pindian = new PindianStruct;
		pindian->from = source;
		pindian->to = target;
		pindian->from_card = cardss;
		pindian->to_card = nullptr;
		pindian->reason = skill_name;

		RoomThread*thread = room->getThread();
		QVariant data = QVariant::fromValue(pindian);
		thread->trigger(AskforPindianCard, room, source, data);

		pindian = data.value<PindianStruct*>();

		if (!pindian->from_card && !pindian->to_card){
			QList<const Card*> cards = room->askForPindianRace(source, target, skill_name);
			pindian->from_card = cards.first();
			pindian->to_card = cards.last();
		} else if (!pindian->to_card){
			if (pindian->from_card->isVirtualCard())
				pindian->from_card = Sanguosha->getCard(pindian->from_card->getEffectiveId());
			pindian->to_card = room->askForPindian(target, source, skill_name);
		} else if (!pindian->from_card){
			if (pindian->to_card->isVirtualCard())
				pindian->to_card = Sanguosha->getCard(pindian->to_card->getEffectiveId());
			pindian->from_card = room->askForPindian(source, source, skill_name);
		}
		cardss = pindian->from_card;
		hash[target] = pindian->to_card;
	}

	if (!cardss) return;

	foreach(ServerPlayer*target, targets){
		int n = pindian(source, target, cardss, hash[target]);
		if (n == -2) continue;

		QList<ServerPlayer*>losers;
		if (n == 1)
			losers << target;
		else if (n == 0)
			losers << source << target;
		else if (n == -1)
			losers << source;

		foreach(ServerPlayer*p, losers){
			if (p->canDiscard(p, "he")){
				p->setTag(skill_name + "Discard", QVariant::fromValue(source));
				if (room->askForDiscard(p, skill_name, 1, 1, true, true, "gushe-discard:" + source->objectName())) continue;
			}
			source->drawCards(1, skill_name);
		}

		if (losers.contains(source))
			source->gainMark("&raoshe");
	}
}

class GusheVS : public ZeroCardViewAsSkill
{
public:
	GusheVS() : ZeroCardViewAsSkill("gushe")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("GusheCard") < 1 + player->getMark("gushe_extra-Clear") && player->canPindian();
	}

	const Card*viewAs() const
	{
		return new GusheCard;
	}
};

class Gushe : public TriggerSkill
{
public:
	Gushe() : TriggerSkill("gushe")
	{
		events << MarkChanged;
		view_as_skill = new GusheVS;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		MarkStruct mark = data.value<MarkStruct>();
		if (mark.name == "&raoshe" && player->getMark("&raoshe") >= 7)
			room->killPlayer(player);
		return false;
	}
};

class Jici : public TriggerSkill
{
public:
	Jici() : TriggerSkill("jici")
	{
		events << PindianVerifying;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		PindianStruct*pindian = data.value<PindianStruct*>();
		if (pindian->reason != "gushe") return false;

		QList<ServerPlayer*> pindian_players;
		pindian_players << pindian->from << pindian->to;
		room->sortByActionOrder(pindian_players);
		foreach(ServerPlayer*p, pindian_players){
			if (p && p->isAlive() && p->hasSkill(objectName())){
				int n = p->getMark("&raoshe");
				int number = (p == pindian->from) ? pindian->from_number : pindian->to_number;
				if (number < n){
					if (p->askForSkillInvoke(this, QString("jici_invoke:%1").arg(QString::number(n)))){
						room->broadcastSkillInvoke(objectName());
						int num = 0;
						if (p == pindian->from){
							pindian->from_number = qMin(13, pindian->from_number + n);
							num = pindian->from_number;
						} else {
							pindian->to_number = qMin(13, pindian->to_number + n);
							num = pindian->to_number;
						}

						LogMessage log;
						log.type = "#JiciUp";
						log.from = p;
						log.arg = QString::number(num);
						room->sendLog(log);

						data = QVariant::fromValue(pindian);
					}
				} else if (number == n){
					room->broadcastSkillInvoke(objectName());
					if (p->hasSkill("gushe")){
						LogMessage log;
						log.type = "#Jici";
						log.from = p;
						log.arg = objectName();
						log.arg2 = "gushe";
						room->sendLog(log);
					}
					room->notifySkillInvoked(p, objectName());

					room->addPlayerMark(p, "gushe_extra-Clear");
				}
			}
		}
		return false;
	}
};

GuolunCard::GuolunCard()
{
}

bool GuolunCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && !to_select->isKongcheng() && to_select != Self;
}

void GuolunCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isDead() || effect.to->isKongcheng()) return;
	Room*room = effect.from->getRoom();
	int id = room->askForCardChosen(effect.from, effect.to, "h", "guolun");
	room->showCard(effect.to, id);
	if (effect.from->isDead() || effect.from->isNude()) return;
	const Card*card = room->askForCard(effect.from, "..", "guolun-show", id, Card::MethodNone, effect.to);
	if (!card) return;
	int card_id = card->getEffectiveId();
	room->showCard(effect.from, card_id);

	int to_num = Sanguosha->getCard(id)->getNumber();
	int from_num = Sanguosha->getCard(card_id)->getNumber();
	if (to_num == from_num) return;
	QList<CardsMoveStruct> exchangeMove;
	CardsMoveStruct move1(QList<int>(), effect.to, Player::PlaceHand,
		CardMoveReason(CardMoveReason::S_REASON_SWAP, effect.from->objectName(), effect.to->objectName(), "guolun", ""));
	move1.card_ids << card_id;
	CardsMoveStruct move2(QList<int>(), effect.from, Player::PlaceHand,
		CardMoveReason(CardMoveReason::S_REASON_SWAP, effect.to->objectName(), effect.from->objectName(), "guolun", ""));
	move2.card_ids << id;
	exchangeMove.push_back(move1);
	exchangeMove.push_back(move2);
	room->moveCardsAtomic(exchangeMove, true);

	ServerPlayer*drawer = nullptr;
	if (to_num < from_num)
		drawer = effect.to;
	else if (to_num > from_num)
		drawer = effect.from;
	if (drawer == nullptr) return;
	drawer->drawCards(1, "guolun");
}

class Guolun : public ZeroCardViewAsSkill
{
public:
	Guolun() : ZeroCardViewAsSkill("guolun")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("GuolunCard");
	}

	const Card*viewAs() const
	{
		return new GuolunCard;
	}
};

class Songsang : public TriggerSkill
{
public:
	Songsang() : TriggerSkill("songsang")
	{
		events << Death;
		frequency = Limited;
		limit_mark = "@songsangMark";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who == player || player->getMark("@songsangMark")<1) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke("songsang");
		room->doSuperLightbox(player, "songsang");
		room->removePlayerMark(player, "@songsangMark");
		if (player->isWounded())
			room->recover(player, RecoverStruct("songsang", player));
		else
			room->gainMaxHp(player, 1, objectName());
		if (player->hasSkill("zhanji")) return false;
		room->acquireSkill(player, "zhanji");
		return false;
	}
};

class Zhanji : public TriggerSkill
{
public:
	Zhanji() : TriggerSkill("zhanji")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to == player && move.to_place == Player::PlaceHand && move.from_places.contains(Player::DrawPile)){
			if (move.reason.m_reason == CardMoveReason::S_REASON_DRAW && move.reason.m_skillName != objectName()){
				if (player->getPhase() == Player::Play){
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
					player->drawCards(1, objectName());
				}
			}
		}
		return false;
	}
};

MubingCard::MubingCard()
{
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void MubingCard::onUse(Room*, CardUseStruct &) const
{
}

class MubingVS : public ViewAsSkill
{
public:
	MubingVS() : ViewAsSkill("mubing")
	{
		response_pattern = "@@mubing";
		expand_pile = "#mubing";
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (to_select->isEquipped()) return false;
		if (selected.isEmpty()) return true;

		int hand = 0;
		int pile = 0;
		foreach(const Card*card, selected){
			if (Self->getHandcards().contains(card))
				hand += card->getNumber();
			else if (Self->getPile("#mubing").contains(card->getEffectiveId()))
				pile += card->getNumber();
		}
		if (Self->getHandcards().contains(to_select))
			return hand + to_select->getNumber() >= pile;
		else if (Self->getPile("#mubing").contains(to_select->getEffectiveId()))
			return hand >= pile + to_select->getNumber();
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		int hand = 0;
		int hand_num = 0;
		int pile = 0;
		int pile_num = 0;
		foreach(const Card*card, cards){
			if (Self->getHandcards().contains(card)){
				hand += card->getNumber();
				hand_num++;
			} else if (Self->getPile("#mubing").contains(card->getEffectiveId())){
				pile += card->getNumber();
				pile_num++;
			}
		}

		if (hand >= pile && hand_num > 0 && pile_num > 0){
			MubingCard*c = new MubingCard;
			c->addSubcards(cards);
			return c;
		}

		return nullptr;
	}
};

class Mubing : public PhaseChangeSkill
{
public:
	Mubing() : PhaseChangeSkill("mubing")
	{
		view_as_skill = new MubingVS;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Play) return false;
		if (!player->askForSkillInvoke(this)) return false;
		bool up = player->property("mubing_levelup").toBool();
		int show_num = 3;
		if (up)
			show_num++;

		QList<int> list = room->showDrawPile(player, show_num, objectName(), false);
				
		room->notifyMoveToPile(player, list, objectName(), Player::DrawPile, true);
		const Card*c = room->askForUseCard(player, "@@mubing", "@mubing");
		room->notifyMoveToPile(player, list, objectName(), Player::DrawPile, false);
		if (!c) return false;

		DummyCard*dummy = new DummyCard;
		DummyCard*card = new DummyCard;
		DummyCard*all = new DummyCard;

		dummy->deleteLater();
		card->deleteLater();
		all->deleteLater();

		all->addSubcards(list);

		foreach(int id, c->getSubcards()){
			if (!player->handCards().contains(id)){
				list.removeOne(id);
				card->addSubcard(id);
			} else
				dummy->addSubcard(id);
		}

		room->clearAG();

		LogMessage log;
		log.type = "$DiscardCardWithSkill";
		log.from = player;
		log.arg = objectName();
		log.card_str = ListI2S(dummy->getSubcards()).join("+");
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());

		CardMoveReason reason(CardMoveReason::S_REASON_THROW, player->objectName(), "", "mubing", "");
		room->moveCardTo(dummy, player, nullptr, Player::DiscardPile, reason, true);
		if (player->isAlive()){
			CardMoveReason reason(CardMoveReason::S_REASON_UNKNOWN, player->objectName(), objectName(), "");
			room->obtainCard(player, card, reason, true);
			if (!list.isEmpty()){
				DummyCard*left = new DummyCard;
				left->addSubcards(list);
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, "", objectName(), "");
				room->throwCard(left, reason, nullptr);
				delete left;
			}
			if (up && player->isAlive()){
				QList<int> give = card->getSubcards();
				while (room->askForYiji(player, give, objectName(), false, true)){
					if (player->isDead()) break;
				}
			}
		} else {
			CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, "", objectName(), "");
			room->throwCard(all, reason, nullptr);
		}
		return false;
	}
};

ZiquCard::ZiquCard()
{
	mute = true;
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void ZiquCard::onUse(Room*, CardUseStruct &) const
{
}

class ZiquVS : public OneCardViewAsSkill
{
public:
	ZiquVS() : OneCardViewAsSkill("ziqu")
	{
		response_pattern = "@@ziqu!";
	}

	bool viewFilter(const Card*to_select) const
	{
		int num = Self->getHandcards().first()->getNumber();
		QList<const Card*> cards = Self->getHandcards() + Self->getEquips();
		foreach(const Card*c, cards){
			if (c->getNumber() > num)
				num = c->getNumber();
		}
		return to_select->getNumber() >= num;
	}

	const Card*viewAs(const Card*originalCard) const
	{
		ZiquCard*card = new ZiquCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class Ziqu : public TriggerSkill
{
public:
	Ziqu() : TriggerSkill("ziqu")
	{
		events << DamageCaused;
		view_as_skill = new ZiquVS;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to->isDead() || damage.to == player) return false;
		QStringList names = player->property("ziqu_names").toStringList();
		if (names.contains(damage.to->objectName())) return false;
		if (!player->askForSkillInvoke(this, QVariant::fromValue(damage.to))) return false;
		room->broadcastSkillInvoke(objectName());
		names << damage.to->objectName();
		room->setPlayerProperty(player, "ziqu_names", names);
		if (!damage.to->isNude()){
			QList<const Card*> big;
			const Card*give = nullptr;
			int num = damage.to->getCards("he").first()->getNumber();
			foreach(const Card*c, damage.to->getCards("he")){
				if (c->getNumber() > num)
					num = c->getNumber();
			}
			foreach(const Card*c, damage.to->getCards("he")){
				if (c->getNumber() >= num)
					big << c;
			}
			if (big.length() == 1)
				give = big.first();
			else
				give = room->askForUseCard(damage.to, "@@ziqu!", "@ziqu:" + player->objectName());
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, damage.to->objectName(), player->objectName(), objectName(), "");
			if (give == nullptr)
				give = big.at(qrand() % big.length());
			if (give == nullptr) return false;
			room->obtainCard(player, give, reason, true);
		}
		return true;
	}
};

class Diaoling : public PhaseChangeSkill
{
public:
	Diaoling() : PhaseChangeSkill("diaoling")
	{
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::RoundStart
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		int mark = player->getMark("&mubing") + player->getMark("mubing_num");
		if (mark >= 6){
			LogMessage log;
			log.type = "#DiaolingWake";
			log.from = player;
			log.arg = "mubing";
			log.arg2 = QString::number(mark);
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, objectName());
		room->setPlayerProperty(player, "mubing_levelup", true);
		room->setPlayerMark(player, objectName(), 1);
		if (room->changeMaxHpForAwakenSkill(player, 0, objectName())){
			QStringList choices;
			if (player->getLostHp() > 0)
				choices << "recover";
			choices << "draw";
			if (room->askForChoice(player, objectName(), choices.join("+")) == "recover")
				room->recover(player, RecoverStruct("diaoling", player));
			else
				player->drawCards(2, objectName());
			if (player->hasSkill("mubing", true)){
				LogMessage log;
				log.type = "#JiexunChange";
				log.from = player;
				log.arg = "mubing";
				room->sendLog(log);
			}
			room->changeTranslation(player, "mubing", 2);
		}
		return false;
	}
};

class DiaolingRecord : public TriggerSkill
{
public:
	DiaolingRecord() : TriggerSkill("#diaoling-record")
	{
		events << CardsMoveOneTime;
        global = true;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.reason.m_skillName!="mubing"||move.to!=player) return false;
		if (move.to_place!=Player::PlaceHand) return false;
		int n = 0;
		foreach(int id, move.card_ids){
			const Card*c = Sanguosha->getCard(id);
			if (c->isKindOf("Slash"))
				n++;
			else if (c->isKindOf("Weapon"))
				n++;
			else if (c->isKindOf("TrickCard") && c->isDamageCard())
				n++;
		}
		if (n <= 0) return false;
		if (player->getMark("diaoling")<1&&player->hasSkill("diaoling", true))
			room->addPlayerMark(player, "&mubing", n);
		else
			player->addMark("mubing_num", n);
		return false;
	}
};

ManwangCard::ManwangCard()
{
	target_fixed = true;
}

void ManwangCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	int n = subcardsLength();
	if (n >= 1 && source->getMark("manwang_remove_last") < 4)
		room->acquireSkill(source, "panqin");
	if (n >= 2 && source->getMark("manwang_remove_last") < 3)
		source->drawCards(1, "manwang");
	if (n >= 3 && source->getMark("manwang_remove_last") < 2)
		room->recover(source, RecoverStruct("manwang", source));
	if (n >= 4 && source->getMark("manwang_remove_last") < 1){
		source->drawCards(2, "manwang");
		room->detachSkillFromPlayer(source, "panqin");
	}
}

class Manwang : public ViewAsSkill
{
public:
	Manwang() : ViewAsSkill("manwang")
	{
		waked_skills = "panqin,#panqin";
	}

	bool viewFilter(const QList<const Card*> &, const Card*to_select) const
	{
		return !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		ManwangCard*c = new ManwangCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->canDiscard(player, "he");
	}
};

class Panqin : public TriggerSkill
{
public:
	Panqin() : TriggerSkill("panqin")
	{
		waked_skills = "#panqin";
		events << EventPhaseEnd << CardUsed;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseEnd){
			if (player->getPhase() != Player::Play && player->getPhase() != Player::Discard) return false;

			SavageAssault*sa = new SavageAssault(Card::SuitToBeDecided, -1);
			sa->setSkillName("panqin");
			foreach(QVariant card_data, player->getTag("PanqinRecord").toList()){
				int card_id = card_data.toInt();
				if (room->getCardPlace(card_id) == Player::DiscardPile)
					sa->addSubcard(card_id);
			}
			sa->deleteLater();
			if (sa->subcardsLength()<1) return false;

			if (!player->canUse(sa)) return false;

			room->fillAG(sa->getSubcards(), player);
			bool invoke = player->askForSkillInvoke(this, "panqin", false);
			room->clearAG(player);
			if (!invoke) return false;

			room->useCard(CardUseStruct(sa, player), true);
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("SavageAssault") || !use.card->getSkillNames().contains(objectName())) return false;
			int mark = player->getMark("manwang_remove_last");

			if (use.to.length() >= use.card->subcardsLength() && mark < 4){
				room->sendCompulsoryTriggerLog(player, objectName());

				mark++;
				if (mark == 1){
					player->drawCards(2, "manwang");
					room->detachSkillFromPlayer(player, "panqin");
				} else if (mark == 2)
					room->recover(player, RecoverStruct("panqin", player));
				else if (mark == 3)
					player->drawCards(1, "manwang");
				else if (mark >= 4)
					room->acquireSkill(player, "panqin");

				if (mark > 4) return false;
				room->setPlayerMark(player, "manwang_remove_last", mark);
				room->changeTranslation(player, "manwang", 5 - mark);
			}
		}
		return false;
	}
};

class PanqinRecord : public TriggerSkill
{
public:
	PanqinRecord() : TriggerSkill("#panqin")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
        global = true;
	}

	bool trigger(TriggerEvent event, Room*, ServerPlayer*player, QVariant &data) const
	{
		if (event == CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from!=player) return false;
			if (move.from->getPhase() != Player::Play && move.from->getPhase() != Player::Discard) return false;
			if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD){
				QVariantList dis = player->getTag("PanqinRecord").toList();
				int i = 0;
				foreach(int id, move.card_ids){
					if (!dis.contains(QVariant(id))){
						if (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip)
							dis << id;
					}
					i++;
				}
				player->setTag("PanqinRecord", dis);
			}
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.from != Player::Play && change.from != Player::Discard) return false;
			player->removeTag("PanqinRecord");
		}
		return false;
	}
};

JuesiCard::JuesiCard()
{
}

bool JuesiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select != Self && Self->inMyAttackRange(to_select);
}

void JuesiCard::onEffect(CardEffectStruct &effect) const
{
	if (!effect.to->canDiscard(effect.to, "he")) return;

	Room*room = effect.from->getRoom();
	effect.to->setTag("juesiSource", QVariant::fromValue(effect.from));
	const Card*c = room->askForDiscard(effect.to, "juesi", 1, 1, false, true);
	effect.to->removeTag("juesiSource");
	if (!c) return;
	c = Sanguosha->getCard(c->getSubcards().first());
	if (!c->isKindOf("Slash") && effect.from->isAlive() && effect.from->getHp() <= effect.to->getHp()){
		Duel*duel = new Duel(Card::NoSuit, 0);
		duel->setSkillName("_juesi");
		if (!duel->isAvailable(effect.from) || effect.from->isCardLimited(duel, Card::MethodUse) || effect.from->isProhibited(effect.to, duel)) return;
		duel->setFlags("YUANBEN");
		room->useCard(CardUseStruct(duel, effect.from, effect.to), true);
	}
}

class Juesi : public OneCardViewAsSkill
{
public:
	Juesi() : OneCardViewAsSkill("juesi")
	{
		filter_pattern = "Slash";
	}

	const Card*viewAs(const Card*originalcard) const
	{
		JuesiCard*c = new JuesiCard;
		c->addSubcard(originalcard->getId());
		return c;
	}
};

class Zhenlve : public TriggerSkill
{
public:
	Zhenlve() : TriggerSkill("zhenlve")
	{
		events << CardUsed << TrickCardCanceling;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == TrickCardCanceling){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			return effect.card->isNDTrick()&&effect.from&&effect.from->isAlive()&&effect.from->hasSkill(objectName());
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isNDTrick() || !player->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		}
		return false;
	}
};

class ZhenlvePro : public ProhibitSkill
{
public:
	ZhenlvePro() : ProhibitSkill("#zhenlve-pro")
	{
	}

	bool isProhibited(const Player*, const Player*to, const Card*card, const QList<const Player*> &) const
	{
		return card->isKindOf("DelayedTrick")&&to->hasSkill("zhenlve");
	}
};

JianshuCard::JianshuCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool JianshuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	if (to_select == Self) return false;
	if (targets.length() == 1)
		return to_select->inMyAttackRange(targets.first());
	return targets.length()<2;
}

bool JianshuCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return targets.length() == 2;
}

void JianshuCard::onUse(Room*room, CardUseStruct &use) const
{
	room->setTag("jianshuUse",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void JianshuCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	room->doSuperLightbox(source, "jianshu");

	room->removePlayerMark(source, "@jianshuMark");
	CardUseStruct use = room->getTag("jianshuUse").value<CardUseStruct>();

	ServerPlayer*a = use.to.first();
	ServerPlayer*b = use.to.last();
	if (a->isDead()) return;

	CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), a->objectName(), "jianshu", "");
	room->obtainCard(a, this, reason, true);

	if (a->isDead() || b->isDead() || !a->canPindian(b, false)) return;

	room->doAnimate(1, a->objectName(), b->objectName());
	int pindian = a->pindianInt(b, "jianshu");

	QList<ServerPlayer*> losers;

	if (pindian == 1){
		room->askForDiscard(a, "jianshu", 2, 2, false, true);
		losers << b;
	} else if (pindian == -1){
		room->askForDiscard(b, "jianshu", 2, 2, false, true);
		losers << a;
	} else if (pindian == 0){
		losers << a << b;
	}

	room->sortByActionOrder(losers);
	foreach(ServerPlayer*p, losers){
		if (p->isAlive())
			room->loseHp(HpLostStruct(p, 1, "jianshu", source));
	}
}

class Jianshu : public OneCardViewAsSkill
{
public:
	Jianshu() : OneCardViewAsSkill("jianshu")
	{
		filter_pattern = ".|black|.|hand";
		frequency = Limited;
		limit_mark = "@jianshuMark";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("@jianshuMark") > 0;
	}

	const Card*viewAs(const Card*originalCard) const
	{
		JianshuCard*c = new JianshuCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Yongdi : public PhaseChangeSkill
{
public:
	Yongdi() : PhaseChangeSkill("yongdi")
	{
		frequency = Limited;
		limit_mark = "@yongdiMark";
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::RoundStart || player->getMark("@yongdiMark") <= 0) return false;

		QList<ServerPlayer*> males;
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (p->isMale())
				males << p;
		}
		if (males.isEmpty()) return false;

		ServerPlayer*male = room->askForPlayerChosen(player, males, objectName(), "@yongdi-invoke", true, true);
		if (!male) return false;

		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "yongdi");
		room->removePlayerMark(player, "@yongdiMark");

		room->gainMaxHp(male, 1, objectName());
		room->recover(male, RecoverStruct("yongdi", player));

		QStringList skills;

		foreach(const Skill*skill, male->getGeneral()->getVisibleSkillList()){
			if (skill->isLordSkill() && !male->hasLordSkill(skill, true) && !skills.contains(skill->objectName()))
				skills << skill->objectName();
		}

		if (male->getGeneral2()){
			foreach(const Skill*skill, male->getGeneral2()->getVisibleSkillList()){
				if (skill->isLordSkill() && !male->hasLordSkill(skill, true) && !skills.contains(skill->objectName()))
					skills << skill->objectName();
			}
		}

		if (skills.isEmpty()) return false;
		room->handleAcquireDetachSkills(male, skills);
		return false;
	}
};

LizhanCard::LizhanCard()
{
}

bool LizhanCard::targetFilter(const QList<const Player*> &, const Player*to_select, const Player*) const
{
	return to_select->isWounded();
}

void LizhanCard::use(Room*room, ServerPlayer*, QList<ServerPlayer*> &targets) const
{
	room->drawCards(targets, 1, "lizhan");
}

class LizhanVS : public ZeroCardViewAsSkill
{
public:
	LizhanVS() : ZeroCardViewAsSkill("lizhan")
	{
		response_pattern = "@@lizhan";
	}

	const Card*viewAs() const
	{
		return new LizhanCard;
	}
};

class Lizhan : public TriggerSkill
{
public:
	Lizhan() : TriggerSkill("lizhan")
	{
		events << EventPhaseChanging;
		view_as_skill = new LizhanVS;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (p->isWounded()){
				room->askForUseCard(player, "@@lizhan", "@lizhan");
				break;
			}
		}
		return false;
	}
};

WeikuiCard::WeikuiCard()
{
}

bool WeikuiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void WeikuiCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.from->getRoom();
	room->loseHp(HpLostStruct(effect.from, 1, "weikui", effect.from));
	if (effect.from->isDead() || effect.to->isDead() || effect.to->isKongcheng()) return;

	QList<int> dis;
	bool has_jink = false;
	foreach(const Card*c, effect.to->getCards("h")){
		if (c->isKindOf("Jink")) has_jink = true;
		if (effect.from->canDiscard(effect.to, c->getEffectiveId()))
			dis << c->getEffectiveId();
	}
	room->doGongxin(effect.from, effect.to, dis, "weikui");

	if (has_jink){
		Slash*slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("_weikui");
		slash->deleteLater();
		if (effect.from->canSlash(effect.to, slash, false))
			room->useCard(CardUseStruct(slash, effect.from, effect.to), false);
		room->setPlayerMark(effect.from, effect.to->objectName() +"weikuibf-Clear", 1);
	} else {
		if (!effect.from->canDiscard(effect.to, "h")) return;
		int id = room->askForCardChosen(effect.from, effect.to, "h", "weikui", true, Card::MethodDiscard);
		room->throwCard(id, effect.to, effect.from);
	}
}

class Weikui : public ZeroCardViewAsSkill
{
public:
	Weikui() : ZeroCardViewAsSkill("weikui")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("WeikuiCard");
	}

	const Card*viewAs() const
	{
		return new WeikuiCard;
	}
};

class WeikuiBf : public DistanceSkill
{
public:
	WeikuiBf() : DistanceSkill("#weikuibf")
	{
	}

	int getFixed(const Player*from, const Player*to) const
	{
		if (from->getMark(to->objectName()+"weikuibf-Clear")>0&&from->hasSkill(objectName()))
			return 1;
		return 0;
	}
};

LihunCard::LihunCard()
{
	mute = true;
}

bool LihunCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select->isMale() && to_select != Self;
}

void LihunCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.from->getRoom();
	effect.to->setFlags("LihunTarget");
	effect.from->setFlags("LihunSource");// for ai
	effect.from->turnOver();
	room->broadcastSkillInvoke("lihun", 1);
	DummyCard dummy_card(effect.to->handCards());
	dummy_card.deleteLater();

	try {
		if (!effect.to->isKongcheng()){
			CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER, effect.from->objectName(),
				effect.to->objectName(), "lihun", "");
			room->moveCardTo(&dummy_card, effect.to, effect.from, Player::PlaceHand, reason, false);
		}
		effect.from->setFlags("-LihunSource");
	}catch (TriggerEvent triggerEvent){
		if (triggerEvent == TurnBroken || triggerEvent == StageChange){
			effect.from->setFlags("-LihunSource");
			effect.to->setFlags("-LihunTarget");
		}
		throw triggerEvent;
	}
}

class LihunSelect : public OneCardViewAsSkill
{
public:
	LihunSelect() : OneCardViewAsSkill("lihun")
	{
		filter_pattern = ".!";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->canDiscard(player, "he") && !player->hasUsed("LihunCard");
	}

	const Card*viewAs(const Card*originalCard) const
	{
		LihunCard*card = new LihunCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class Lihun : public TriggerSkill
{
public:
	Lihun() : TriggerSkill("lihun")
	{
		events << EventPhaseStart << EventPhaseEnd;
		view_as_skill = new LihunSelect;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->hasUsed("LihunCard");
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*diaochan, QVariant &) const
	{
		if (triggerEvent == EventPhaseEnd && diaochan->getPhase() == Player::Play){
			ServerPlayer*target = nullptr;
			foreach(ServerPlayer*other, room->getOtherPlayers(diaochan)){
				if (other->hasFlag("LihunTarget")){
					other->setFlags("-LihunTarget");
					target = other;
					break;
				}
			}

			if (!target || target->getHp() < 1 || diaochan->isNude())
				return false;

			room->broadcastSkillInvoke(objectName(), 2);
			const Card*to_goback = room->askForExchange(diaochan, objectName(), target->getHp(), target->getHp(), true, "LihunGoBack");

			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, diaochan->objectName(),
				target->objectName(), objectName(), "");
			room->moveCardTo(to_goback, diaochan, target, Player::PlaceHand, reason);
		} else if (triggerEvent == EventPhaseStart && diaochan->getPhase() == Player::NotActive){
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->hasFlag("LihunTarget"))
					p->setFlags("-LihunTarget");
			}
		}

		return false;
	}
};

class Chongzhen : public TriggerSkill
{
public:
	Chongzhen() : TriggerSkill("chongzhen")
	{
		events << CardResponded << CardUsed;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == CardResponded){
			CardResponseStruct resp = data.value<CardResponseStruct>();
			if (resp.m_card->getSkillNames().contains("longdan")
				&& resp.m_who && !resp.m_who->isKongcheng()){
				if (player->askForSkillInvoke(this, resp.m_who)){
					room->doAnimate(1,player->objectName(),resp.m_who->objectName());
					room->broadcastSkillInvoke("chongzhen", 1);
					int card_id = room->askForCardChosen(player, resp.m_who, "h", objectName());
					CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
					room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
				}
			}
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getSkillNames().contains("longdan")){
				foreach(ServerPlayer*p, use.to){
					if (p->isKongcheng()) continue;
					if (player->askForSkillInvoke(this, p)){
						room->doAnimate(1,player->objectName(),p->objectName());
						room->broadcastSkillInvoke("chongzhen", 2);
						int card_id = room->askForCardChosen(player, p, "h", objectName());
						CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
						room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
					}
				}
				if(use.to.isEmpty()&&use.who&&use.who!=player&&!use.who->isKongcheng()){
					if (player->askForSkillInvoke(this, use.who)){
						room->doAnimate(1,player->objectName(),use.who->objectName());
						room->broadcastSkillInvoke("chongzhen", 2);
						int card_id = room->askForCardChosen(player, use.who, "h", objectName());
						CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
						room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
					}
				}
			}
		}
		return false;
	}
};

ZhouxuanzCard::ZhouxuanzCard()
{
	mute = true;
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void ZhouxuanzCard::onUse(Room*, CardUseStruct &) const
{
}

class ZhouxuanzVS :public OneCardViewAsSkill
{
public:
	ZhouxuanzVS() :OneCardViewAsSkill("zhouxuanz")
	{
		expand_pile = "spzhxuan";
		response_pattern = "@@zhouxuanz";
		filter_pattern = ".|.|.|spzhxuan";
	}

	const Card*viewAs(const Card*originalCard) const
	{
		ZhouxuanzCard*card = new ZhouxuanzCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class Zhouxuanz : public TriggerSkill
{
public:
	Zhouxuanz() : TriggerSkill("zhouxuanz")
	{
		events << EventPhaseStart << EventPhaseEnd << CardUsed << CardResponded;
		view_as_skill = new ZhouxuanzVS;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if (player->isKongcheng() || player->getPhase() != Player::Discard) return false;
			int num = 5 - player->getPile("spzhxuan").length();
			if (num <= 0) return false;
			const Card*card = room->askForExchange(player, objectName(), num, 1, false, "@zhouxuanz-put", true);
			if (!card) return false;

			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());

			player->addToPile("spzhxuan", card);
		} else if (event == EventPhaseEnd){
			if (player->getPhase() != Player::Play || player->getPile("spzhxuan").isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player, objectName());
			player->clearOnePrivatePile("spzhxuan");
		} else {
			QList<int> xuan = player->getPile("spzhxuan");
			if (xuan.isEmpty()) return false;

			const Card*card = nullptr;
			if (event == CardUsed)
				card = data.value<CardUseStruct>().card;
			else {
				CardResponseStruct res = data.value<CardResponseStruct>();
				if (!res.m_isUse) return false;
				card = res.m_card;
			}
			if (!card || card->isKindOf("SkillCard")) return false;

			room->sendCompulsoryTriggerLog(player, objectName());

			int num = 1;
			int hand = player->getHandcardNum();
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->getHandcardNum() >= hand){
					num = player->getPile("spzhxuan").length();
					break;
				}
			}
			player->drawCards(num, objectName());
			if (player->isDead() || player->getPile("spzhxuan").isEmpty()) return false;

			int id = -1;
			if (xuan.length() == 1)
				id = xuan.first();
			else {
				const Card*c = room->askForUseCard(player, "@@zhouxuanz", "@zhouxuanz", -1, Card::MethodNone);
				if (c)
					id = c->getSubcards().first();
			}
			if (id < 0) id = xuan.first();
			CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, player->objectName(), objectName(), "");
			room->throwCard(Sanguosha->getCard(id), reason, nullptr);
		}
		return false;
	}
};

class NewZhuiji : public DistanceSkill
{
public:
	NewZhuiji() : DistanceSkill("newzhuiji")
	{
	}

	int getFixed(const Player*from, const Player*to) const
	{
		if (to->getHp()<=from->getHp()&&from->hasSkill(objectName()))
			return 1;
		return 0;
	}
};

class NewShichou : public TargetModSkill
{
public:
	NewShichou(const QString shichou) : TargetModSkill(shichou), shichou(shichou)
	{
		frequency = NotFrequent;
	}

	int getExtraTargetNum(const Player*from, const Card*) const
	{
		if (from->hasSkill(objectName())){
			if (shichou == "newshichou")
				return qMax(1, from->getLostHp());
			else if (shichou == "olnewshichou")
				return from->getLostHp() + 1;
		}
		return 0;
	}
private:
	QString shichou;
};

class OLNewZhuiji : public TriggerSkill
{
public:
	OLNewZhuiji() : TriggerSkill("olnewzhuiji")
	{
		events << TargetSpecified;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		foreach(ServerPlayer*p, use.to){
			if (player->isDead()) return false;
			if (p->isDead() || p->isNude() || player->distanceTo(p) != 1) continue;
			room->sendCompulsoryTriggerLog(player, this);

			QString prompt = "@olnewzhuiji1";

			DummyCard*dummy = new DummyCard;
			foreach(const Card*c, p->getEquips())
				dummy->addSubcard(c);
			if (dummy->subcardsLength() <= 0 || p->isCardLimited(dummy, Card::MethodRecast))
				prompt = "@olnewzhuiji2";
			dummy->deleteLater();

			if (prompt.endsWith("2")){
				if (!p->canDiscard(p, "he")){
					LogMessage log;
					log.type = "#OLNewZhuijiLog";
					log.from = p;
					room->sendLog(log);
					room->showAllCards(p);
				} else
					room->askForDiscard(p, objectName(), 1, 1, false, true, prompt);
				continue;
			}

			if (room->askForDiscard(p, objectName(), 1, 1, true, true, prompt)) continue;
			LogMessage log;
			log.type = "$RecastCard";
			log.from = p;
			log.card_str = ListI2S(dummy->getSubcards()).join("+");
			room->sendLog(log);

			room->moveCardTo(dummy, p, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, p->objectName(), "olnewzhuiji", ""));
			p->drawCards(dummy->subcardsLength(), "recast");
		}
		return false;
	}
};

class OLNewZhuijiBf : public DistanceSkill
{
public:
	OLNewZhuijiBf() : DistanceSkill("#olnewzhuijibf")
	{
	}

	int getFixed(const Player*from, const Player*to) const
	{
		if (to->getHp()<=from->getHp()&&from->hasSkill(objectName()))
			return 1;
		return 0;
	}
};

class Tianhou : public TriggerSkill
{
public:
	Tianhou() : TriggerSkill("tianhou")
	{
		events << EventPhaseStart << Death;
		frequency = Compulsory;
		waked_skills = "lieshu,ningwu,olzhouyu,yanshuang";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if (player->getPhase()==Player::Start){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					QString skill_name = player->getTag("tianhou"+p->objectName()).toString();
					if(!skill_name.isEmpty()){
						room->detachSkillFromPlayer(p,skill_name);
					}
				}
				if(!player->hasSkill(objectName())) return false;
				room->sendCompulsoryTriggerLog(player,this);
				QList<int> ids = room->getNCards(1);
				room->fillAG(ids,player);
				const Card*dc = room->askForCard(player,"..","tianhou0:",ids.first(),Card::MethodNone);
				room->clearAG(player);
				room->returnToTopDrawPile(ids);
				if(dc){
					QList<CardsMoveStruct> exchangeMove;
					CardsMoveStruct move1(ids, player, Player::PlaceHand, CardMoveReason(CardMoveReason::S_REASON_GOTBACK, player->objectName()));
					exchangeMove.push_back(move1);
					CardsMoveStruct move2(dc->getEffectiveId(), nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_RECYCLE, player->objectName()));
					exchangeMove.push_back(move2);
					room->moveCardsAtomic(exchangeMove, false);
				}
				if(player->isAlive()){
					ids = room->showDrawPile(player,1,objectName(),false);
					dc = Sanguosha->getCard(ids.first());
					QString skill_name;
					if(dc->getSuit()==2)
						skill_name = "lieshu";
					if(dc->getSuit()==3)
						skill_name = "ningwu";
					if(dc->getSuit()==0)
						skill_name = "olzhouyu";
					if(dc->getSuit()==1)
						skill_name = "yanshuang";
					if(!skill_name.isEmpty()){
						ServerPlayer*tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"tianhou1:"+skill_name);
						if(tp){
							room->acquireSkill(tp,skill_name);
							player->setTag("tianhou"+tp->objectName(), skill_name);
						}
					}
				}
			}
		}else{
			DeathStruct death = data.value<DeathStruct>();
			if(death.who==player){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					QString skill_name = player->getTag("tianhou"+p->objectName()).toString();
					if(!skill_name.isEmpty()){
						room->detachSkillFromPlayer(p,skill_name);
					}
				}
			}
		}
		return false;
	}
};

class Lieshu : public TriggerSkill
{
public:
	Lieshu() : TriggerSkill("lieshu")
	{
		events << EventPhaseStart;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if (player->getPhase()==Player::Finish){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->getHp()>player->getHp())
						return false;
				}
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						room->loseHp(player,1,true,p,objectName());
					}
				}
			}
		}
		return false;
	}
};

class Ningwu : public TriggerSkill
{
public:
	Ningwu() : TriggerSkill("ningwu")
	{
		events << TargetSpecifying;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash")&&use.to.size()==1){
				ServerPlayer*tp = use.to.first();
				if(!player->isAdjacentTo(tp)){
					foreach(ServerPlayer*p, room->getOtherPlayers(player)){
						if(p->hasSkill(objectName())){
							room->sendCompulsoryTriggerLog(p,this);
							JudgeStruct judge;
							judge.pattern = ".|.|0~"+QString::number(use.card->getNumber());
							judge.who = player;
							judge.reason = objectName();
							judge.good = true;
							room->judge(judge);
							if (judge.isBad()){
								use.nullified_list << "_ALL_TARGETS";
								data.setValue(use);
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class Zhouyu : public TriggerSkill
{
public:
	Zhouyu() : TriggerSkill("olzhouyu")
	{
		events << DamageCaused << Damaged;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature==DamageStruct::Fire){
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						return p->damageRevises(data,-damage.damage);
					}
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature==DamageStruct::Thunder){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						foreach(ServerPlayer*q, room->getAllPlayers()){
							if(player->isAdjacentTo(q))
								room->loseHp(q,1,true,p,objectName());
						}
					}
				}
			}
		}
		return false;
	}
};

class Yanshuang : public TriggerSkill
{
public:
	Yanshuang() : TriggerSkill("yanshuang")
	{
		events << EventPhaseStart;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if (player->getPhase()==Player::Finish){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->getHp()<player->getHp())
						return false;
				}
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						room->loseHp(player,1,true,p,objectName());
					}
				}
			}
		}
		return false;
	}
};

class Chenshuo : public TriggerSkill
{
public:
	Chenshuo() : TriggerSkill("chenshuo")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if (player->getPhase()==Player::Finish){
				DummyCard*dummy = new DummyCard();
				for (int i = 0; i < 3; i++){
					if (player->isKongcheng()||player->isDead())
						break;
					if(i==0)
						room->sendCompulsoryTriggerLog(player,this);
					const Card*dc = room->askForExchange(player,objectName(),1,1,false,"chenshuo0");
					room->showCard(player,dc->getEffectiveId());
					room->getThread()->delay();
					QList<int>ids = room->showDrawPile(player,1,objectName());
					dc = Sanguosha->getCard(dc->getEffectiveId());
					const Card*dc2 = Sanguosha->getCard(ids.first());
					bool has = dc->getType()==dc2->getType()||dc->getSuit()==dc2->getSuit()
						||dc->getNumber()==dc2->getNumber()||dc->sameNameWith(dc2);
					dummy->addSubcard(dc2);
					room->getThread()->delay();
					if(!has) break;
				}
				if(player->isAlive()&&dummy->subcardsLength()>0)
					player->obtainCard(dummy);
				dummy->deleteLater();
			}
		}
		return false;
	}
};

ShengongCard::ShengongCard()
{
	target_fixed = true;
}

static bool shengongTimer(ServerPlayer*a, ServerPlayer*b)
{
	return a->getMark("shengongTimer") < b->getMark("shengongTimer");
}

void ShengongCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	QList<ServerPlayer*> tos = room->getAllPlayers();
	QHash<ServerPlayer*, QString> hash;
	foreach(ServerPlayer*p, tos){
                QElapsedTimer timer;
		timer.start();
		QString cho = room->askForChoice(p,getSkillName(),"shengong1+shengong2+cancel",QVariant::fromValue(source));
		if(timer.elapsed()<3000) hash[p] = cho;
		p->setMark("shengongTimer",timer.elapsed());
	}
	std::stable_sort(tos.begin(), tos.end(), shengongTimer);
	foreach(ServerPlayer*p, tos){
		if(hash[p].isEmpty()||hash[p]=="cancel") continue;
		int id = room->showDrawPile(p,1,getSkillName()).first();
		LogMessage log;
		log.type = "$shengong0";
		log.from = p;
		log.arg = hash[p];
		log.card_str = QString::number(id);
		room->sendLog(log);
		room->addPlayerMark(source,"&"+hash[p],Sanguosha->getCard(id)->getNumber());
		room->throwCard(id,objectName(),nullptr);
		room->getThread()->delay();
	}
	int n = 1;
	if(source->getMark("&shengong1")>=source->getMark("&shengong2")){
		n++;
		if(source->getMark("&shengong2")<1)
			n++;
	}
	QStringList shengongEquips,hasc;
	shengongEquips << "Wushuangji" << "Baihuapao" << "Shimandai" << "Zijinguan"
	<< "GodBlade" << "GodSword" << "GodDiagram" << "GodPao" << "GodHat"
	<< "Wutiesuolian" << "Wuxinghelingshan" << "Huxinjing" << "Heiguangkai" << "Tianjitu" << "Taigongyinfu"
	<< "Bintieshuangji" << "Sanlve" << "Zhaogujing";
	QList<int>ids,ids2;
	const Card*dc = Sanguosha->getEngineCard(getEffectiveId());
	const EquipCard*equip = qobject_cast<const EquipCard*>(dc->getRealCard());
	room->addPlayerMark(source,QString::number(equip->location())+"shengong-PlayClear");
	if(equip->location()>1){
		room->addPlayerMark(source,"2shengong-PlayClear");
		room->addPlayerMark(source,"3shengong-PlayClear");
		room->addPlayerMark(source,"4shengong-PlayClear");
	}
	for (int i = 0; i < Sanguosha->getCardCount(); i++){
		const Card*c = Sanguosha->getEngineCard(i);
		if(shengongEquips.contains(c->getClassName())&&!hasc.contains(c->getClassName())&&room->getCardPlace(i)==Player::PlaceTable){
			const EquipCard*equip2 = qobject_cast<const EquipCard*>(c->getRealCard());
			if(equip->location()==equip2->location()||(equip->location()>1&&equip2->location()==4)){
				hasc << c->getClassName();
				ids << i;
			}
		}
	}
	for (int i = 0; i < n; i++){
		if(ids.isEmpty()) break;
		int id = ids.at(qrand()%ids.length());
		ids.removeOne(id);
		ids2 << id;
	}
	room->setPlayerMark(source,"&shengong1",0);
	room->setPlayerMark(source,"&shengong2",0);
	if(ids2.isEmpty()||source->isDead()) return;
	room->fillAG(ids2,source);
	n = room->askForAG(source, ids2, true, getSkillName());
	room->clearAG(source);
	if(n==-1) n = ids2.first();
	if(n>-1){
		dc = Sanguosha->getEngineCard(n);
		equip = qobject_cast<const EquipCard*>(dc->getRealCard());
		QList<ServerPlayer*> tos2;
		foreach(ServerPlayer*p, tos){
			if(p->hasEquipArea(equip->location()))
				tos2 << p;
		}
		ServerPlayer*to = room->askForPlayerChosen(source,tos2,getSkillName(),"shengongo0:"+dc->objectName());
		if(to){
			dc->use(room,source,QList<ServerPlayer*>()<<to);
			QVariantList sges = room->getTag("shengongEquips").toList();
			sges << n;
			room->setTag("shengongEquips",sges);
		}
	}
}

class ShengongVS : public OneCardViewAsSkill
{
public:
	ShengongVS() : OneCardViewAsSkill("shengong")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		if(to_select->isKindOf("EquipCard")){
			const EquipCard*equip = qobject_cast<const EquipCard*>(to_select->getRealCard());
			return Self->getMark(QString::number(equip->location())+"shengong-PlayClear")<1;
		}
		return false;
	}

	const Card*viewAs(const Card*originalCard) const
	{
		ShengongCard*c = new ShengongCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Shengong : public TriggerSkill
{
public:
	Shengong() : TriggerSkill("shengong")
	{
		events << CardsMoveOneTime << EventPhaseStart;
		view_as_skill = new ShengongVS;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&player==room->getCurrent()){
				QVariantList sges = room->getTag("shengongEquips").toList();
				foreach(int id, move.card_ids){
					if (sges.contains(QVariant(id))){
						sges.removeOne(QVariant(id));
						room->setTag("shengongEquips",sges);
						room->moveCardTo(Sanguosha->getCard(id),nullptr,Player::PlaceTable);
						player->addMark("shengongEquips-Clear");
					}
				}
			}
		}else if(player->getPhase()==Player::Finish){
			int n = player->getMark("shengongEquips-Clear");
			if(n>0){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						p->drawCards(n,objectName());
					}
				}
			}
		}
		return false;
	}
};

class Qisi : public TriggerSkill
{
public:
	Qisi() : TriggerSkill("qisi")
	{
		events << GameStart << DrawNCards;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==GameStart){
			QList<int> ls;
			DummyCard*dummy = new DummyCard();
			foreach(int id, Sanguosha->getRandomCards()){
				if (!room->getCardOwner(id)){
					const Card*dc = Sanguosha->getEngineCard(id);
					if(dc->isKindOf("EquipCard")){
						const EquipCard*equip = qobject_cast<const EquipCard*>(dc->getRealCard());
						if(!ls.contains(equip->location())){
							ls.append(equip->location());
							dummy->addSubcard(dc);
							if(ls.length()>1) break;
						}
					}
				}
			}
			if(dummy->subcardsLength()>0){
				room->sendCompulsoryTriggerLog(player,this);
				player->obtainCard(dummy);
				foreach(int id, dummy->getSubcards()){
					if(player->handCards().contains(id)){
						const Card*dc = Sanguosha->getEngineCard(id);
						room->moveCardTo(dc,nullptr,Player::PlaceTable);
						dc->use(room,player,QList<ServerPlayer*>()<<player);
					}
				}
			}
			dummy->deleteLater();
		}else{
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase"||!player->askForSkillInvoke(this)) return false;
			player->peiyin(this);
			draw.num--;
			data.setValue(draw);
			QString cho = room->askForChoice(player,objectName(),"0+1+2+3+4");
			foreach(int id, room->getDrawPile()+room->getDiscardPile()){
				const Card*dc = Sanguosha->getEngineCard(id);
				if(dc->isKindOf("EquipCard")){
					const EquipCard*equip = qobject_cast<const EquipCard*>(dc->getRealCard());
					if((int)equip->location()==cho.toInt()){
						player->obtainCard(dc);
						break;
					}
				}
			}
		}
		return false;
	}
};

class ZhujiuVs : public ViewAsSkill
{
public:
	ZhujiuVs() : ViewAsSkill("zhujiu")
	{
	}

	bool viewFilter(const QList<const Card*> &, const Card*) const
	{
		return true;//cards.length()<=Self->getMark("zhujiuUseAnaleptic-Clear");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.length()<=Self->getMark("zhujiuUseAnaleptic-Clear")) return nullptr;
		Card*c = Sanguosha->cloneCard("analeptic");
		c->setSkillName(objectName());
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		return pattern.contains("analeptic")&&player->getMark("zhujiuBan-Clear")<1
		&&Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		&&player->getCardCount()>player->getMark("zhujiuUseAnaleptic-Clear");
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>player->getMark("zhujiuUseAnaleptic-Clear")
		&&player->getMark("zhujiuBan-Clear")<1&&Analeptic::IsAvailable(player);
	}
};

class Zhujiu : public TriggerSkill
{
public:
	Zhujiu() : TriggerSkill("zhujiu")
	{
		events << CardUsed;
		view_as_skill = new ZhujiuVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Analeptic")){
				room->addPlayerMark(player,"zhujiuUseAnaleptic-Clear");
				if(use.card->getSkillNames().contains(objectName())){
					foreach(int id, use.card->getSubcards()){
						if(Sanguosha->getCard(id)->getSuit()==1) return false;
					}
					room->addPlayerMark(player,"zhujiuBan-Clear");
				}
			}
		}
		return false;
	}
};

class Jinglei : public TriggerSkill
{
public:
	Jinglei() : TriggerSkill("jinglei")
	{
		events << CardFinished << CardUsed;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->hasFlag("jingleiBf")){
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(p->hasFlag("Global_Dying")) return false;
				}
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->getMark("jingleiUse-Clear")<1&&p->isAlive()&&p->hasTurn()
					&&p->hasSkill(objectName())&&p->askForSkillInvoke(objectName()+"$-1",data)){
						room->damage(DamageStruct(objectName(),nullptr,p,1,DamageStruct::Thunder));
						if(p->isDead()) continue;
						QList<ServerPlayer*>tos;
						foreach(ServerPlayer*q, room->getAlivePlayers()){
							if(q->hasSkill("zhujiu",true)) tos << q;
						}
						ServerPlayer*to = room->askForPlayerChosen(p,tos,objectName(),"jinglei0");
						if(to){
							room->doAnimate(1,p->objectName(),to->objectName());
							int n = to->getMaxHp()-to->getHandcardNum();
							if(n>0){
								if(to->getHandcardNum()>4) continue;
								to->drawCards(qMin(n,5-to->getHandcardNum()),objectName());
							}else if(n<0){
								const Card*dc = room->askForDiscard(to,objectName(),-n,-n);
								if(dc&&to!=p){
									Card*sc = new DummyCard;
									foreach(int id, dc->getSubcards()){
										if(room->getCardOwner(id)) continue;
										sc->addSubcard(id);
									}
									room->giveCard(to,p,sc,objectName());
									sc->deleteLater();
								}
							}
						}
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Analeptic")){
				room->setCardFlag(use.card,"jingleiBf");
			}
		}
		return false;
	}
};

class Xudai : public TriggerSkill
{
public:
	Xudai() : TriggerSkill("xudai")
	{
		events << CardUsed << CardResponded << Damaged;
		frequency = Limited;
		limit_mark = "@xudai";
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(player->getMark("@xudai")<1)
			return false;
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0&&use.whocard){
			}else return false;
		}else if(event==CardResponded){
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (res.m_card->getTypeId()>0&&res.m_toCard){
			}else return false;
		}
		ServerPlayer*to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"xudai0",true,true);
		if(to){
			player->peiyin(this);
			room->doSuperLightbox(player, "xudai");
			room->removePlayerMark(player, "@xudai");
			room->acquireSkill(to,"zhujiu");
		}
		return false;
	}
};

class Liantao : public TriggerSkill
{
public:
	Liantao() : TriggerSkill("liantao")
	{
		events << Dying << EventPhaseStart << DamageDone;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==Dying){
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.damage&&dying.damage->card&&dying.damage->card->getSkillNames().contains(objectName())){
				dying.who->addMark("liantaoDying-PlayClear");
			}
		}else if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getSkillNames().contains(objectName())){
				int n = room->getTag("liantaoDamage").toInt();
				n += damage.damage;
				room->setTag("liantaoDamage",n);
				player->addMark("liantaoDamage-PlayClear");
			}
		}else if(player->getPhase()==Player::Play&&player->getHandcardNum()>0&&player->hasSkill(objectName())){
			ServerPlayer*tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"liantao0",true,true);
			if(tp){
				player->peiyin(this);
				QString chouce = room->askForChoice(tp,objectName(),"red+black",QVariant::fromValue(player));
				room->showAllCards(player);
				room->removeTag("liantaoDamage");
				while(player->getHandcardNum()>0&&tp->isAlive()){
					QStringList ids;
					Card*dc = Sanguosha->cloneCard("duel");
					dc->setSkillName("_liantao");
					foreach(const Card*h, player->getHandcards()){
						if(h->getColorString()==chouce){
							dc->addSubcard(h);
							if(player->canUse(dc,tp))
								ids << h->toString();
							dc->clearSubcards();
						}
					}
					dc->deleteLater();
					if(ids.isEmpty()) break;
					const Card*sc = room->askForCard(player,ids.join(",")+"!","liantao1:"+chouce,QVariant::fromValue(tp),Card::MethodNone);
					if(sc){
						dc->addSubcard(sc);
						if(player->canUse(dc,tp))
							room->useCard(CardUseStruct(dc,player,tp));
						if(player->getMark("liantaoDying-PlayClear")>0||tp->getMark("liantaoDying-PlayClear")>0)
							break;
					}
				}
				int n = room->getTag("liantaoDamage").toInt();
				if(n>0) player->drawCards(n,objectName());
				if(tp->isAlive()&&tp->getMark("liantaoDamage-PlayClear")<1){
					player->drawCards(1,objectName());
					room->addMaxCards(player,1);
					room->setPlayerCardLimitation(player,"use","Slash",true);
				}
			}
		}
		return false;
	}
};











OLSpPackage::OLSpPackage()
	: Package("ol_sp")
{
	General*jsp_sunshangxiang = new General(this, "jsp_sunshangxiang", "shu", 3, false); // JSP 001
	jsp_sunshangxiang->addSkill(new Liangzhu);
	jsp_sunshangxiang->addSkill(new Fanxiang);

	General*jsp_machao = new General(this, "jsp_machao", "qun"); // JSP 002
	jsp_machao->addSkill(new Zhuiji);
	jsp_machao->addSkill(new Cihuai);

	General*jsp_guanyu = new General(this, "jsp_guanyu", "wei"); // JSP 003
	jsp_guanyu->addSkill("wusheng");
	jsp_guanyu->addSkill(new JspDanqi);
	jsp_guanyu->addRelateSkill("nuzhan");
	skills << new Nuzhan;

	General*new_spmachao = new General(this, "new_spmachao", "qun", 4);
	new_spmachao->addSkill(new NewZhuiji);
	new_spmachao->addSkill(new NewShichou("newshichou"));

	General*ol_new_spmachao = new General(this, "ol_new_spmachao", "qun", 4);
	ol_new_spmachao->addSkill(new OLNewZhuiji);
	ol_new_spmachao->addSkill(new OLNewZhuijiBf);
	ol_new_spmachao->addSkill(new NewShichou("olnewshichou"));
	related_skills.insertMulti("olnewzhuiji", "#olnewzhuijibf");

	General*sp_guanyu = new General(this, "sp_guanyu", "wei", 4); // SP 007
	sp_guanyu->addSkill("wusheng");
	sp_guanyu->addSkill(new Danji);

	General*jsp_jiangwei = new General(this, "jsp_jiangwei", "wei");
	jsp_jiangwei->addSkill(new Kunfen);
	jsp_jiangwei->addSkill(new Fengliang);

	General*jsp_zhaoyun = new General(this, "jsp_zhaoyun", "qun", 3);
	jsp_zhaoyun->addSkill(new Chixin);
	jsp_zhaoyun->addSkill(new Suiren);
	jsp_zhaoyun->addSkill("yicong");

	General*bgm_zhaoyun = new General(this, "bgm_zhaoyun", "qun", 3); //*SP 001
	bgm_zhaoyun->addSkill("longdan");
	bgm_zhaoyun->addSkill(new Chongzhen);

	General*jsp_huangyy = new General(this, "jsp_huangyueying", "qun", 3, false);
	jsp_huangyy->addSkill(new Jiqiao);
	jsp_huangyy->addSkill(new Linglong);
	jsp_huangyy->addSkill(new LinglongMax);
	jsp_huangyy->addSkill(new LinglongTrigger);
	related_skills.insertMulti("linglong", "#linglong-horse");
	related_skills.insertMulti("linglong", "#linglong");
	addMetaObject<JiqiaoCard>();

	General*new_sppangde = new General(this, "new_sppangde", "wei", 4);
	new_sppangde->addSkill(new Juesi);
	new_sppangde->addSkill("mashu");
	addMetaObject<JuesiCard>();

	General*ol_caiwenji = new General(this, "ol_caiwenji", "wei", 3, false);
	ol_caiwenji->addSkill(new Chenqing);
	ol_caiwenji->addSkill(new Mozhi);

	General*caiwenji2 = new General(this, "ol_ii_caiwenji", "wei", 3, false);
	caiwenji2->addSkill(new OLChenqing);
	caiwenji2->addSkill("mozhi");

	General*new_sp_jiaxu = new General(this, "new_sp_jiaxu", "wei", 3);
	new_sp_jiaxu->addSkill(new Zhenlve);
	new_sp_jiaxu->addSkill(new ZhenlvePro);
	new_sp_jiaxu->addSkill(new Jianshu);
	new_sp_jiaxu->addSkill(new Yongdi);
	related_skills.insertMulti("zhenlve", "#zhenlve-pro");
	addMetaObject<JianshuCard>();

	General*sp_caoren = new General(this, "sp_caoren", "wei", 4);
	sp_caoren->addSkill(new Lizhan);
	sp_caoren->addSkill(new Weikui);
	sp_caoren->addSkill(new WeikuiBf);
	related_skills.insertMulti("weikui", "#weikuibf");
	addMetaObject<LizhanCard>();
	addMetaObject<WeikuiCard>();

	General*bgm_diaochan = new General(this, "bgm_diaochan", "qun", 3, false); //*SP 002
	bgm_diaochan->addSkill(new Lihun);
	bgm_diaochan->addSkill("biyue");
	addMetaObject<LihunCard>();


	General*wanglang = new General(this, "wanglang", "wei", 3);
	wanglang->addSkill(new Gushe);
	wanglang->addSkill(new Jici);
	addMetaObject<GusheCard>();

	General*sp_pangtong = new General(this, "sp_pangtong", "wu", 3);
	sp_pangtong->addSkill(new Guolun);
	sp_pangtong->addSkill(new Songsang);
	sp_pangtong->addRelateSkill("zhanji");
	addMetaObject<GuolunCard>();
	skills << new Zhanji;

	General*sp_zhangliao = new General(this, "sp_zhangliao", "qun", 4);
	sp_zhangliao->addSkill(new Mubing);
	sp_zhangliao->addSkill(new Ziqu);
	sp_zhangliao->addSkill(new Diaoling);
	sp_zhangliao->addSkill(new DiaolingRecord);
	related_skills.insertMulti("diaoling", "#diaoling-record");
	addMetaObject<MubingCard>();
	addMetaObject<ZiquCard>();

	General*sp_menghuo = new General(this, "sp_menghuo", "qun", 4);
	sp_menghuo->addSkill(new Manwang);
	sp_menghuo->addSkill(new PanqinRecord);
	addMetaObject<ManwangCard>();
	skills << new Panqin;

	General*sp_zhanghe = new General(this, "sp_zhanghe", "qun", 4);
	sp_zhanghe->addSkill(new Zhouxuanz);
	addMetaObject<ZhouxuanzCard>();

	General*ol_puyuan = new General(this, "ol_puyuan", "shu", 4);
	ol_puyuan->addSkill(new Shengong);
	ol_puyuan->addSkill(new Qisi);
	addMetaObject<ShengongCard>();

	General*ol_zhouqun = new General(this, "ol_zhouqun", "shu", 4);
	ol_zhouqun->addSkill(new Tianhou);
	ol_zhouqun->addSkill(new Chenshuo);
	skills << new Lieshu << new Ningwu << new Zhouyu << new Yanshuang;

	General*sp_sunce = new General(this, "sp_sunce", "qun", 4);
	sp_sunce->addSkill(new Liantao);

	General*olsp_liubei = new General(this, "olsp_liubei", "qun", 4);
	olsp_liubei->addSkill(new Zhujiu);
	olsp_liubei->addSkill(new Jinglei);
	olsp_liubei->addSkill(new Xudai);




}
ADD_PACKAGE(OLSp)


class Kangkai : public TriggerSkill
{
public:
	Kangkai() : TriggerSkill("kangkai")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("Slash")){
			foreach(ServerPlayer*to, use.to){
				if (!player->isAlive()) break;
				if (player->distanceTo(to) <= 1 && TriggerSkill::triggerable(player)){
					player->setTag("KangkaiSlash", data);
					bool will_use = room->askForSkillInvoke(player, objectName(), QVariant::fromValue(to));
					player->removeTag("KangkaiSlash");
					if (!will_use) continue;

					room->broadcastSkillInvoke(objectName());

					player->drawCards(1, "kangkai");
					if (!player->isNude() && player != to){
						const Card*card = nullptr;
						if (player->getCardCount() > 1){
							card = room->askForCard(player, "..!", "@kangkai-give:" + to->objectName(), data, Card::MethodNone);
							if (!card)
								card = player->getCards("he").at(qrand() % player->getCardCount());
						} else {
							Q_ASSERT(player->getCardCount() == 1);
							card = player->getCards("he").first();
						}
						CardMoveReason r(CardMoveReason::S_REASON_GIVE, player->objectName(), objectName(), "");
						room->obtainCard(to, card, r);
						if (card->getTypeId() == Card::TypeEquip && room->getCardOwner(card->getEffectiveId()) == to && !to->isLocked(card)){
							to->setTag("KangkaiSlash", data);
							to->setTag("KangkaiGivenCard", QVariant::fromValue(card));
							bool will_use = room->askForSkillInvoke(to, "kangkai_use", "use");
							to->removeTag("KangkaiSlash");
							to->removeTag("KangkaiGivenCard");
							if (will_use)
								room->useCard(CardUseStruct(card, to));
						}
					}
				}
			}
		}
		return false;
	}
};

class Meibu : public TriggerSkill
{
public:
	Meibu() : TriggerSkill("meibu")
	{
		events << EventPhaseStart << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play){
			foreach(ServerPlayer*sunluyu, room->getOtherPlayers(player)){
				if (!player->inMyAttackRange(sunluyu) && TriggerSkill::triggerable(sunluyu) && room->askForSkillInvoke(sunluyu, objectName())){
					room->broadcastSkillInvoke(objectName());
					if (!player->hasSkill("#meibu-filter", true)){
						room->acquireSkill(player, "#meibu-filter", false);
						room->filterCards(player, player->getCards("he"), false);
					}
					QVariantList sunluyus = player->getTag(objectName()).toList();
					sunluyus << QVariant::fromValue(sunluyu);
					player->setTag(objectName(), sunluyus);
					room->insertAttackRangePair(player, sunluyu);
				}
			}
		} else if (triggerEvent == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;

			QVariantList sunluyus = player->getTag(objectName()).toList();
			foreach(QVariant sunluyu, sunluyus){
				ServerPlayer*s = sunluyu.value<ServerPlayer*>();
				room->removeAttackRangePair(player, s);
			}
			room->detachSkillFromPlayer(player, "#meibu-filter");

			player->setTag(objectName(), QVariantList());

			room->filterCards(player, player->getCards("he"), true);
		}
		return false;
	}

	int getEffectIndex(const ServerPlayer*, const Card*card) const
	{
		if (card->isKindOf("Slash"))
			return -2;
		return -1;
	}
};

class Mumu : public TriggerSkill
{
public:
	Mumu() : TriggerSkill("mumu")
	{
		events << EventPhaseStart;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (player->getPhase() == Player::Finish && player->getMark("damage_point_play_phase") == 0){
			QList<ServerPlayer*> weapon_players, armor_players;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->getWeapon() && player->canDiscard(p, p->getWeapon()->getEffectiveId()))
					weapon_players << p;
				if (p != player && p->getArmor())
					armor_players << p;
			}
			ServerPlayer*victim = room->askForPlayerChosen(player, weapon_players+armor_players, objectName(), "@mumu",true,true);
			if (!victim) return false;
			QStringList choices;
			if (armor_players.contains(victim)) choices.append("armor");
			if (weapon_players.contains(victim)) choices.append("weapon");
			QString choice = room->askForChoice(player, objectName(), choices.join("+"));
			if (choice == "weapon"){
				room->broadcastSkillInvoke(objectName(), 1);
				room->throwCard(victim->getWeapon(), victim, player);
				player->drawCards(1, objectName());
			} else {
				room->broadcastSkillInvoke(objectName(), 2);
				int equip = victim->getArmor()->getEffectiveId();
				QList<CardsMoveStruct> exchangeMove;
				CardsMoveStruct move1(equip, player, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_ROB, player->objectName()));
				exchangeMove.push_back(move1);
				if (player->getArmor()){
					CardsMoveStruct move2(player->getArmor()->getEffectiveId(), nullptr, Player::DiscardPile,
						CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, player->objectName()));
					exchangeMove.push_back(move2);
				}
				room->moveCardsAtomic(exchangeMove, true);
			}
		}
		return false;
	}
};

LiluCard::LiluCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void LiluCard::onUse(Room*room, CardUseStruct &card_use) const
{
	int mark = card_use.from->getMark("&lilu");
	int n = subcardsLength();
	room->setPlayerMark(card_use.from, "&lilu", n);
	room->giveCard(card_use.from, card_use.to.first(), this, "lilu");
	if (card_use.from->isAlive() && n > mark && mark > 0){
		room->gainMaxHp(card_use.from, 1, "lilu");
		room->recover(card_use.from, RecoverStruct("lilu", card_use.from));
	}
}

class LiluVS : public ViewAsSkill
{
public:
	LiluVS() : ViewAsSkill("lilu")
	{
		response_pattern = "@@lilu!";
	}

	bool viewFilter(const QList<const Card*> &, const Card*to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		LiluCard*c = new LiluCard;
		c->addSubcards(cards);
		return c;
	}
};

class Lilu : public PhaseChangeSkill
{
public:
	Lilu() : PhaseChangeSkill("lilu")
	{
		view_as_skill = new LiluVS;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Draw) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		int draw = qMin(player->getMaxHp(), 5) - player->getHandcardNum();
		if (draw > 0)
			player->drawCards(draw, objectName());
		if (player->isKongcheng()) return true;
		if (!room->askForUseCard(player, "@@lilu!", "@lilu", -1, Card::MethodNone)){
			room->setPlayerMark(player, "&lilu", 1);
			ServerPlayer*to = room->getOtherPlayers(player).at(qrand() % room->getOtherPlayers(player).length());
			int id = player->getRandomHandCardId();
			room->giveCard(player, to, QList<int>() << id, objectName());
		}
		return true;
	}
};

class Yizhengc : public PhaseChangeSkill
{
public:
	Yizhengc() : PhaseChangeSkill("yizhengc")
	{
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		ServerPlayer*target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@yizhengc-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(target, "&yizhengc+#" + player->objectName());
		return false;
	}
};

class YizhengcEffect : public TriggerSkill
{
public:
	YizhengcEffect() : TriggerSkill("#yizhengc")
	{
		events << EventPhaseStart << PreHpRecover << DamageCaused;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	int Yizhengnum(ServerPlayer*player) const
	{
		Room*room = player->getRoom();
		int num = 0;
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (p->isAlive() && p->hasSkill("yizhengc") && p->getMaxHp() > player->getMaxHp() &&
					player->getMark("&yizhengc+#" + p->objectName()) > 0){
				room->sendCompulsoryTriggerLog(p, "yizhengc", true, true);
				room->loseMaxHp(p, 1, "yizhengc");
				num++;
			}
		}
		return num;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if (player->getPhase() != Player::RoundStart) return false;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->getMark("&yizhengc+#" + player->objectName()) > 0)
					room->setPlayerMark(p, "&yizhengc+#" + player->objectName(), 0);
			}
		} else if (event == PreHpRecover){
			if (player->isDead()) return false;
			RecoverStruct recover = data.value<RecoverStruct>();
			int num = Yizhengnum(player) + qMin(recover.recover, player->getMaxHp() - player->getHp());
			num = qMin(num, player->getMaxHp() - player->getHp());
			if (num <= 0 || num == recover.recover) return false;
			recover.recover = num;
			data = QVariant::fromValue(recover);
		} else {
			if (player->isDead()) return false;
			int num = Yizhengnum(player);
			if (num <= 0) return false;
			DamageStruct damage = data.value<DamageStruct>();
			damage.damage += num;
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class Wanwei : public TriggerSkill
{
public:
	Wanwei() : TriggerSkill("wanwei")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (!room->hasCurrent() || player->getMark("wanwei-Clear") > 0) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from == player && move.from_places.contains(Player::PlaceHand)
			&& ((move.reason.m_reason == CardMoveReason::S_REASON_DISMANTLE
			&& move.reason.m_playerId != move.reason.m_targetId)
			|| (move.to && move.to != move.from && move.to_place == Player::PlaceHand
			&& move.reason.m_reason != CardMoveReason::S_REASON_GIVE
			&& move.reason.m_reason != CardMoveReason::S_REASON_SWAP))){

			if (!player->askForSkillInvoke(this, data)) return false;
			room->broadcastSkillInvoke(this);
			room->addPlayerMark(player, "wanwei-Clear");

			room->fillAG(move.card_ids, player);
			int id = room->askForAG(player, move.card_ids, false, objectName());
			room->clearAG(player);
			const Card*card = Sanguosha->getCard(id);

			QList<int> same_names;
			foreach(int id, room->getDrawPile()){
				const Card*c = Sanguosha->getCard(id);
				if (c->sameNameWith(card))
					same_names << id;
			}

			if (same_names.isEmpty()){
				LogMessage log;
				log.type = "#WanweiDraw";
				log.from = player;
				log.arg = card->objectName();
				room->sendLog(log);
				player->drawCards(1, objectName());
			} else {
				int get = same_names.at(qrand() % same_names.length());
				room->obtainCard(player, get, false);
			}
		}
		return false;
	}
};

class Yuejian : public TriggerSkill
{
public:
	Yuejian() : TriggerSkill("yuejian")
	{
		events << BeforeCardsMove;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (!room->hasCurrent() || player->getMark("yuejian-Clear") >= 2 || player->isKongcheng()) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::DiscardPile && (move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE){

			CardUseStruct use = move.reason.m_useStruct;
			if (!use.card || use.card->isKindOf("SkillCard") || !use.from || use.from == player || !use.to.contains(player)) return false;
			if (!player->askForSkillInvoke(this, QVariant::fromValue(use))) return false;
			room->broadcastSkillInvoke(this);
			room->addPlayerMark(player, "yuejian-Clear");

			room->showAllCards(player);
			if (!use.card->hasSuit() || !room->CardInTable(use.card)) return false;
			foreach(const Card*c, player->getHandcards()){
				if (c->getSuit() == use.card->getSuit())
					return false;
			}

			QList<int> subcards;
			if (use.card->isVirtualCard())
				subcards = use.card->getSubcards();
			else
				subcards << use.card->getId();
			if (!subcards.isEmpty()){
				move.removeCardIds(subcards);
				data = QVariant::fromValue(move);
			}

			room->obtainCard(player, use.card);
		}
		return false;
	}
};

class Zengou : public TriggerSkill
{
public:
	Zengou() : TriggerSkill("zengou")
	{
		events  << CardUsed;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Jink")) return false;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (player->isDead()) break;
				if (p->isDead() || !p->hasSkill(objectName()) || !p->inMyAttackRange(player)) continue;

				if(p->askForSkillInvoke(this,data)){
					p->peiyin(this);
					if(!room->askForCard(p, "^BasicCard", "@zengou-discard", data, objectName()))
						room->loseHp(HpLostStruct(p, 1, objectName(), p));
					use.nullified_list << "_ALL_TARGETS";
					data = QVariant::fromValue(use);
					if (p->isAlive() && room->CardInTable(use.card))
						room->obtainCard(p, use.card);
				}
			}
		} else if (event == CardResponded){
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (!res.m_isUse||!res.m_card->isKindOf("Jink")) return false;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (player->isDead()) break;
				if (p->isDead() || !p->hasSkill(objectName()) || !p->inMyAttackRange(player)) continue;

				if(p->askForSkillInvoke(this,data)){
					p->peiyin(this);
					if(!room->askForCard(p, "^BasicCard", "@zengou-discard", data, objectName()))
						room->loseHp(HpLostStruct(p, 1, objectName(), p));
					res.nullified = true;
					data = QVariant::fromValue(res);
					if (p->isAlive() && room->CardInTable(res.m_card))
						room->obtainCard(p, res.m_card);
				}
			}
		}
		return false;
	}
};

class Zhangji : public PhaseChangeSkill
{
public:
	Zhangji() : PhaseChangeSkill("zhangji")
	{
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive() && target->getPhase() == Player::Finish;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (player->isDead()) return false;
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			if (p->getMark("zhongzuo_damage-Clear") > 0){
				if (p->askForSkillInvoke(this, "draw:" + player->objectName())){
					p->peiyin(this);
					player->drawCards(2, objectName());
				}
			}
			if (p->getMark("zhongzuo_damaged-Clear") > 0 && player->canDiscard(player, "he")){
				if (p->askForSkillInvoke(this, "discard:" + player->objectName())){
					p->peiyin(this);
					room->askForDiscard(player, objectName(), 2, 2, false, true);
				}
			}
		}
		return false;
	}
};

YujueCard::YujueCard(QString zhihu) : zhihu(zhihu)
{
	target_fixed = true;
}

void YujueCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	QStringList choices;
	for (int i = 0; i < 5; i++){
		if (source->hasEquipArea(i))
			choices << QString::number(i);
	}
	if (choices.isEmpty()) return;

	QString choice = room->askForChoice(source, "yujue", choices.join("+"));
	source->throwEquipArea(choice.toInt());

	if (source->isDead()) return;

	QList<ServerPlayer*> targets;
	foreach(ServerPlayer*p, room->getOtherPlayers(source)){
		if (p->isKongcheng()) continue;
		targets << p;
	}
	if (targets.isEmpty()) return;

	QString skill = "yujue";
	if (zhihu == "secondzhihu") skill = "secondyujue";
	ServerPlayer*target = room->askForPlayerChosen(source, targets, skill, "@yujue-invoke");
	room->doAnimate(1, source->objectName(), target->objectName());

	if (target->isKongcheng()) return;
	const Card*c = room->askForExchange(target, "yujue", 1, 1, false, "@yujue-give:" + source->objectName());
	CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), source->objectName(), "yujue", "");
	room->obtainCard(source, c, reason, false);

	if (target->isDead() || source->isDead()) return;
	if (target->hasSkill(zhihu, true)) return;

	QStringList names = source->getTag(zhihu + "_names").toStringList();
	if (!names.contains(target->objectName())){
		names << target->objectName();
		source->setTag(zhihu + "_names", names);
	}
	room->acquireSkill(target, zhihu);
}

class YujueVS : public ZeroCardViewAsSkill
{
public:
	YujueVS() : ZeroCardViewAsSkill("yujue")
	{
	}

	const Card*viewAs() const
	{
		return new YujueCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->hasEquipArea() && !player->hasUsed("YujueCard");
	}
};

class Yujue : public PhaseChangeSkill
{
public:
	Yujue() : PhaseChangeSkill("yujue")
	{
		view_as_skill = new YujueVS;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::RoundStart) return false;
		QStringList names = player->getTag("zhihu_names").toStringList();
		if (names.isEmpty()) return false;
		player->removeTag("zhihu_names");
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (!names.contains(p->objectName())) continue;
			if (!p->hasSkill("zhihu", true)) continue;
			targets << p;
		}
		if (targets.isEmpty()) return false;
		room->sortByActionOrder(targets);
		foreach(ServerPlayer*p, targets){
			if (p->isDead() || !p->hasSkill("zhihu", true)) continue;
			room->detachSkillFromPlayer(p, "zhihu");
		}
		return false;
	}
};

class Tuxing : public TriggerSkill
{
public:
	Tuxing() : TriggerSkill("tuxing")
	{
		events << ThrowEquipArea << DamageCaused;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == ThrowEquipArea){
			if (!player->hasSkill(objectName())) return false;
			QVariantList areas = data.toList();
			for (int i = 0; i < areas.length(); i++){
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				room->gainMaxHp(player, 1, objectName());
				room->recover(player, RecoverStruct("tuxing", player));
			}
			if (player->hasEquipArea()) return false;
			room->loseMaxHp(player, 4, objectName());
			room->addPlayerMark(player, "&tuxing");
		} else {
			int mark = player->getMark("&tuxing");
			if (mark <= 0) return false;
			DamageStruct damage = data.value<DamageStruct>();
			LogMessage log;
			log.type = "#TuxingDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = QString::number(damage.damage);
			log.arg2 = QString::number(damage.damage += mark);
			room->sendLog(log);
			room->broadcastSkillInvoke(this);
			room->notifySkillInvoked(player, objectName());

			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class Luochong : public TriggerSkill
{
public:
	Luochong() : TriggerSkill("luochong")
	{
		events << Damaged << EventPhaseStart << RoundEnd;
	}

	static void changeLuochongTranslation(ServerPlayer*player)
	{
		QStringList str1, str2;
		foreach(QString str, player->property("SkillDescriptionRecord_luochong").toString().split("+")){
			if(str.isEmpty()) continue;
			str1 << str << "|";
		}
		foreach(QString str, player->property("SkillDescriptionChoiceRecord1_luochong").toString().split("+")){
			if(str.isEmpty()) continue;
			str2 << "luochong:"+str << "|";
		}
		Room*room = player->getRoom();
		player->setSkillDescriptionSwap("luochong","%arg11",str1.join("+"));
		player->setSkillDescriptionSwap("luochong","%arg21",str2.join("+"));
		room->changeTranslation(player, "luochong", 1);
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (event == EventPhaseStart && player->getPhase() != Player::Start)
			return false;
		else if(event == RoundEnd){
			room->setPlayerProperty(player, "SkillDescriptionRecord_luochong", "");
			changeLuochongTranslation(player);
			return false;
		}

		QString chosen = player->property("SkillDescriptionRecord_luochong").toString(),
				remove = player->property("SkillDescriptionChoiceRecord1_luochong").toString();
		QStringList choices, chosens = chosen.split("+"), removes = remove.split("+");
		if (!chosens.contains("luochong:recover") && !removes.contains("recover")){
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->isWounded()){
					choices << "recover";
					break;
				}
			}
		}
		if (!chosens.contains("luochong:lose") && !removes.contains("lose"))
			choices << "lose";
		if (!chosens.contains("luochong:discard") && !removes.contains("discard")){
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (player->canDiscard(p, "he")){
					choices << "discard";
					break;
				}
			}
		}
		if (!chosens.contains("luochong:draw") && !removes.contains("draw"))
			choices << "draw";
		if (choices.isEmpty()) return false;
		choices << "cancel";

		QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		if (choice=="cancel") return false;
		chosens << "luochong:" + choice;
		room->setPlayerProperty(player, "SkillDescriptionRecord_luochong", chosens.join("+"));
		room->broadcastSkillInvoke(objectName());

		changeLuochongTranslation(player);
		QList<ServerPlayer*> players;
		if (choice == "recover"){
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->isWounded()) players << p;
			}
		} else if (choice == "discard"){
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (player->canDiscard(p, "he")) players << p;
			}
		} else if (choice == "lose")
			players = room->getOtherPlayers(player);
		else
			players = room->getAlivePlayers();
		if (players.isEmpty()) return false;

		ServerPlayer*t = room->askForPlayerChosen(player, players, objectName(), "@luochong-" + choice, false, true);
		if (choice == "recover")
			room->recover(t, RecoverStruct("luochong", player));
		else if (choice == "lose")
			room->loseHp(HpLostStruct(t, 1, "luochong", player));
		else if (choice == "draw")
			t->drawCards(2, objectName());
		else {
			QList<int> cards;
			for (int i = 0; i < 2; ++i){
				if (t->getCardCount()<=i) break;
				int id = room->askForCardChosen(player, t, "he", objectName(), false, Card::MethodDiscard, cards, i > 0);
				if (id < 0) break;
				cards << id;
			}
			if (!cards.isEmpty()){
				DummyCard dummy(cards);
				room->throwCard(&dummy, t, player);
				dummy.deleteLater();
			}
		}
		return false;
	}
};

class Aicheng : public TriggerSkill
{
public:
	Aicheng() : TriggerSkill("aicheng")
	{
		events << Dying;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DyingStruct dy = data.value<DyingStruct>();
		if (dy.who != player || !player->hasSkill("luochong", true)) return false;

		QString remove = player->property("SkillDescriptionChoiceRecord1_luochong").toString();

		QStringList choices, all, removes = remove.split("+");
		all << "recover" << "lose" << "discard" << "draw";
		foreach(QString str, all){
			if (!removes.contains(str))
				choices << str;
		}
		if (choices.length()<2) return false;

		room->sendCompulsoryTriggerLog(player, this);

		int num = qMin(1, player->getMaxHp()) - player->getHp();
		room->recover(player, RecoverStruct(player, nullptr, num, "aicheng"));

		QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		removes << choice;
		room->setPlayerProperty(player, "SkillDescriptionChoiceRecord1_luochong", removes.join("+"));

		Luochong::changeLuochongTranslation(player);

		LogMessage log;
		log.type = "#FumianFirstChoice";
		log.from = player;
		log.arg = "aicheng:" + choice;
		room->sendLog(log);
		return false;
	}
};

class QiaoliVS : public OneCardViewAsSkill
{
public:
	QiaoliVS() : OneCardViewAsSkill("qiaoli")
	{
		response_or_use = true;
	}

	bool viewFilter(const Card*to_select) const
	{
		int weapon = Self->getMark("qiaoliWeapon-PlayClear"), equip = Self->getMark("qiaoliEquip-PlayClear");
		if (weapon <= 0 && equip <= 0)
			return to_select->isKindOf("EquipCard");
		if (weapon > 0 && equip <= 0)
			return to_select->isKindOf("EquipCard") && !to_select->isKindOf("Weapon");
		if (equip > 0 && weapon <= 0)
			return to_select->isKindOf("Weapon");
		return false;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("qiaoliWeapon-PlayClear") <= 0 || player->getMark("qiaoliEquip-PlayClear") <= 0;
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Duel*duel = new Duel(originalCard->getSuit(), originalCard->getNumber());
		duel->addSubcard(originalCard);
		duel->setSkillName(objectName());
		return duel;
	}
};

class Qiaoli : public TriggerSkill
{
public:
	Qiaoli() : TriggerSkill("qiaoli")
	{
		events << PreCardUsed << Damage << EventPhaseStart;
		view_as_skill = new QiaoliVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == PreCardUsed)
			return 5;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Duel") || !use.card->getSkillNames().contains(objectName())) return false;
			if (Sanguosha->getCard(use.card->getEffectiveId())->isKindOf("Weapon")){
				room->addPlayerMark(player, "qiaoliWeapon-PlayClear");
				room->setCardFlag(use.card, "qiaoliUser_" + player->objectName());
			} else {
				room->addPlayerMark(player, "qiaoliEquip-PlayClear");
				room->addPlayerMark(player, "qiaoliEquip-Clear");
				use.no_respond_list << "_ALL_TARGETS";
				data =  QVariant::fromValue(use);
			}
		} else if (event == Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card||!damage.card->isKindOf("Duel") || !damage.card->getSkillNames().contains(objectName())) return false;
			if (damage.transfer || damage.chain) return false;

			ServerPlayer*user = nullptr;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (damage.card->hasFlag("qiaoliUser_" + p->objectName())){
					user = p;
					break;
				}
			}
			if (!user || user->isDead()) return false;

			int id = damage.card->getSubcards().first();
			const Card*c = Sanguosha->getCard(id);
			if (!c->isKindOf("Weapon")) return false;
			const Weapon*weapon = qobject_cast<const Weapon*>(c->getRealCard());
			if (!weapon) return false;
			int range = weapon->getRange(user);
			if (range <= 0) return false;

			QList<int> draws = room->drawCardsList(user, range, objectName());
			QList<int> hands = user->handCards(), yijis;
			foreach(int id, draws){
				if (hands.contains(id))
					yijis << id;
			}
			if (yijis.isEmpty()) return false;

			QHash<ServerPlayer*, QStringList> hash;

			while (!yijis.isEmpty()){
				if (user->isDead()) break;
				CardsMoveStruct yiji_move = room->askForYijiStruct(user, yijis, objectName(), false, false, true, -1,
											room->getOtherPlayers(user), CardMoveReason(), "", false, false);
				if (!yiji_move.to || yiji_move.card_ids.isEmpty()) break;

				QStringList id_strings = hash[(ServerPlayer*)yiji_move.to];
				foreach(int id, yiji_move.card_ids){
					id_strings << QString::number(id);
					yijis.removeOne(id);
				}
				hash[(ServerPlayer*)yiji_move.to] = id_strings;
			}

			QList<CardsMoveStruct> moves;
			foreach(ServerPlayer*p, room->getOtherPlayers(user)){
				if (p->isDead()) continue;
				QList<int> ids = ListS2I(hash[p]);
				if (ids.isEmpty()) continue;
				hash.remove(p);
				CardsMoveStruct move(ids, user, p, Player::PlaceHand, Player::PlaceHand,
					CardMoveReason(CardMoveReason::S_REASON_GIVE, user->objectName(), p->objectName(), objectName(), ""));
				moves.append(move);
			}
			if (!moves.isEmpty())
				room->moveCardsAtomic(moves, false);
		} else if (event == EventPhaseStart){
			if (player->getPhase() != Player::Finish) return false;
			int mark = player->getMark("qiaoliEquip-Clear");
			for (int i = 0; i < mark; i++){
				if (player->isDead()) break;

				QList<int> equips, card_ids = room->getDrawPile();
				foreach(int id, card_ids){
					if (Sanguosha->getCard(id)->isKindOf("EquipCard"))
						equips << id;
				}
				if (equips.isEmpty()) break;

				room->sendCompulsoryTriggerLog(player, this);

				int id = equips.at(qrand() % equips.length());
				room->obtainCard(player, id);
			}
		}
		return false;
	}
};

class Qingliang : public TriggerSkill
{
public:
	Qingliang() : TriggerSkill("qingliang")
	{
		events << TargetConfirming;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (!room->hasCurrent() || player->getMark("qingliangUsed-Clear") > 0) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isDamageCard()) return false;
		if (!use.to.contains(player) || use.to.length() != 1 || use.from == player||!use.from) return false;
		if (player->isKongcheng() || !player->askForSkillInvoke(this)) return false;
		player->peiyin(this);
		room->addPlayerMark(player, "qingliangUsed-Clear");

		room->showAllCards(player);

		QStringList choices;
		choices << "draw=" + use.from->objectName();

		foreach(int id, player->handCards()){
			if (!player->canDiscard(player, id)) continue;
			QString suit = Sanguosha->getCard(id)->getSuitString();
			if (choices.contains("discard=" + suit)) continue;
			choices << "discard=" + suit;
		}

		QString choice = room->askForChoice(player, objectName(), choices.join("+"));

		if (choice.startsWith("draw")){
			QList<ServerPlayer*> drawers;
			drawers << player << use.from;
			room->sortByActionOrder(drawers);
			room->drawCards(drawers, 1, objectName());
		} else {
			QString suit = choice.split("=").last();
			QList<int> discard;
			foreach(int id, player->handCards()){
				if (!player->canDiscard(player, id)) continue;
				if (Sanguosha->getCard(id)->getSuitString() == suit)
					discard << id;
			}
			if (discard.isEmpty()) return false;

			DummyCard dis(discard);
			room->throwCard(&dis, player);
			dis.deleteLater();

			use.to.clear();
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Wenji : public TriggerSkill
{
public:
	Wenji() : TriggerSkill("wenji")
	{
		events << EventPhaseStart << CardUsed << EventPhaseChanging;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if (player->getPhase() != Player::Play) return false;

			QList<ServerPlayer*> players;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (!p->isNude())
					players << p;
			}
			if (players.isEmpty()) return false;
			ServerPlayer* target = room->askForPlayerChosen(player, players, objectName(), "@wenji-invoke", true, true);
			if (!target || target->isNude()){
				room->setPlayerProperty(player, "wenji_name", "");
				return false;
			}
			room->broadcastSkillInvoke(objectName());
			const Card*c = nullptr;
			const Card*card = room->askForCard(target, "..", "wenji-give:" + player->objectName(), QVariant::fromValue(player), Card::MethodNone);
			if (!card){
				card = target->getCards("he").at(qrand() % target->getCards("he").length());
				c = card;
			} else
				c = Sanguosha->getCard(card->getSubcards().first());

			if (!card || !c) return false;
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "wenji", "");
			room->obtainCard(player, card, reason, true);

			QString name = c->objectName();
			room->setPlayerProperty(player, "wenji_name", name);

			if (c->isKindOf("Slash"))
				name = "slash";
			room->addPlayerMark(player, "&wenji+" + name + "-Clear");
		} else if (event == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			room->setPlayerProperty(player, "wenji_name", "");
		} else {
			if (player->getPhase() == Player::NotActive) return false;
			QString name = player->property("wenji_name").toString();
			if (name == "") return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (!use.card->sameNameWith(name) || use.to.isEmpty()) return false;
			foreach(ServerPlayer*p, room->getOtherPlayers(player))
				use.no_respond_list << p->objectName();
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Tunjiang : public TriggerSkill
{
public:
	Tunjiang() : TriggerSkill("tunjiang")
	{
		events << PreCardUsed << EventPhaseStart << EventPhaseSkipped;
		frequency = Frequent;
        global = true;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == PreCardUsed) return 6;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == PreCardUsed && player->isAlive() && player->getPhase() == Player::Play){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId() != Card::TypeSkill){
				foreach(ServerPlayer*p, use.to){
					if (p != player){
						player->addMark("tunjiang-Clear");
						return false;
					}
				}
			}
		} else if (triggerEvent == EventPhaseStart){
			if (player->getPhase() != Player::Finish||player->getMark("tunjiang-Clear") > 0 || player->getMark("tunjiang_skip_play-Clear") > 0) return false;
			if (!player->hasSkill(objectName())||!player->askForSkillInvoke(this)) return false;
			QStringList kingdoms;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (!kingdoms.contains(p->getKingdom()))
					kingdoms << p->getKingdom();
			}
			room->broadcastSkillInvoke(objectName());
			player->drawCards(kingdoms.length(), objectName());
		} else if (triggerEvent == EventPhaseSkipped){
			if (player->getPhase() != Player::Play) return false;
			player->addMark("tunjiang_skip_play-Clear");
		}
		return false;
	}
};

class Zishu : public TriggerSkill
{
public:
	Zishu() : TriggerSkill("zishu")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
		frequency = Compulsory;
        global = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				QVariantList ids = p->getTag("Zishu_ids").toList();
				if (ids.isEmpty()) continue;
				p->removeTag("Zishu_ids");
				if (!p->hasSkill(objectName())) continue;
				QList<int> list;
				foreach(int id, p->handCards()){
					if (ids.contains(QVariant(id)) && p->canDiscard(p, id))
						list << id;
				}
				if (list.isEmpty()) continue;
				room->sendCompulsoryTriggerLog(p, objectName(), true, true, 2);
				room->throwCard(list,objectName(), p, nullptr);
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player){
				if(player->hasFlag("CurrentPlayer")){
					if(move.reason.m_skillName!=objectName()&&player->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(player, objectName(), true, true, 1);
						player->drawCards(1, objectName());
					}
				}else if(!room->getTag("FirstRound").toBool()){
					QVariantList ids = player->getTag("Zishu_ids").toList();
					foreach(int id, move.card_ids)
						ids << id;
					player->setTag("Zishu_ids", ids);
				}
			}
		}
		return false;
	}
};

class Yingyuan : public TriggerSkill
{
public:
	Yingyuan() : TriggerSkill("yingyuan")
	{
		events << CardUsed << CardResponded;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (!player->hasFlag("CurrentPlayer")) return false;
		const Card*card = nullptr;
		if (event == CardUsed)
			card = data.value<CardUseStruct>().card;
		else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (!res.m_isUse) return false;
			card = res.m_card;
		}

		if (card == nullptr || card->isKindOf("SkillCard")) return false;
		if (player->getMark("yingyuan" + card->getType() + "-Clear") > 0) return false;

		player->setTag("yingyuanCard", QVariant::fromValue(card));
		ServerPlayer*target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@yingyuan:" + card->getType(), true, true);
		player->removeTag("yingyuanCard");
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, "yingyuan" + card->getType() + "-Clear");

		QList<int> list;
		foreach(int id, room->getDrawPile()){
			if (Sanguosha->getCard(id)->getType() == card->getType())
				list << id;
		}
		if (list.isEmpty()) return false;

		int id = list.at(qrand() % list.length());
		room->obtainCard(target, id, true);

		return false;
	}
};

class Junbing : public TriggerSkill
{
public:
	Junbing() : TriggerSkill("junbing")
	{
		events << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive() && player->getPhase() == Player::Finish && player->getHandcardNum() <= 1;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		ServerPlayer*simalang = room->findPlayerBySkillName(objectName());
		if (!simalang || !simalang->isAlive())
			return false;
		if (player->askForSkillInvoke(this, QString("junbing_invoke:%1").arg(simalang->objectName()))){
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(simalang, objectName());
			player->drawCards(1,objectName());
			if (player->objectName() != simalang->objectName()){
				CardMoveReason reason = CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName());
				DummyCard*cards = player->wholeHandCards();
				room->moveCardTo(cards, simalang, Player::PlaceHand, reason);

				int x = qMin(cards->subcardsLength(), simalang->getHandcardNum());

				if (x > 0){
					const Card*return_cards = room->askForExchange(simalang, objectName(), x, x, false, QString("@junbing-return:%1::%2").arg(player->objectName()).arg(cards->subcardsLength()));
					CardMoveReason return_reason = CardMoveReason(CardMoveReason::S_REASON_GIVE, simalang->objectName());
					room->moveCardTo(return_cards, player, Player::PlaceHand, return_reason);
				}
			}
		}
		return false;
	}
};

QujiCard::QujiCard()
{
}

bool QujiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	if (subcardsLength() <= targets.length())
		return false;
	return to_select->isWounded();
}

bool QujiCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	if (targets.length() > 0){
		foreach(const Player*p, targets){
			if (!p->isWounded())
				return false;
		}
		return true;
	}
	return false;
}

void QujiCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets)
		room->cardEffect(this, source, p);

	foreach(int id, getSubcards()){
		if (Sanguosha->getCard(id)->isBlack()){
			room->loseHp(HpLostStruct(source, 1, "quji", source));
			break;
		}
	}
}

void QujiCard::onEffect(CardEffectStruct &effect) const
{
	RecoverStruct recover;
	recover.who = effect.from;
	recover.recover = 1;
	recover.reason = "quji";
	effect.to->getRoom()->recover(effect.to, recover);
}

class Quji : public ViewAsSkill
{
public:
	Quji() : ViewAsSkill("quji")
	{
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*) const
	{
		return selected.length() < Self->getLostHp();
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->isWounded() && !player->hasUsed("QujiCard");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.length() == Self->getLostHp()){
			QujiCard*quji = new QujiCard;
			quji->addSubcards(cards);
			return quji;
		}
		return nullptr;
	}
};

class Hongde : public TriggerSkill
{
public:
	Hongde() : TriggerSkill("hongde")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		int n = 0;
		if (!room->getTag("FirstRound").toBool() && move.to && move.to == player && move.to_place == Player::PlaceHand){
			if (move.card_ids.length() > 1)
				n++;
		}
		if (move.from && move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))){
			int lose = 0;
			for (int i = 0; i < move.card_ids.length(); i++){
				if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip){
					lose++;
					if (lose > 1)
						break;
				}
			}
			if (lose > 1)
				n++;
		}

		if (n <= 0) return false;
		for (int i = 0; i < n; i++){
			if (player->isDead() || !player->hasSkill(objectName())) return false;
			ServerPlayer*target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@hongde-invoke", true, true);
			if (!target) break;
			room->broadcastSkillInvoke(objectName());
			target->drawCards(1, objectName());
		}
		return false;
	}
};

DingpanCard::DingpanCard()
{
}

bool DingpanCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	return targets.isEmpty() && !to_select->getEquips().isEmpty();
}

void DingpanCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->drawCards(1, "dingpan");
	if (effect.to->getEquips().isEmpty() || effect.from->isDead()) return;

	Room*room = effect.from->getRoom();
	QStringList choices;
	if (effect.from->canDiscard(effect.to, "e"))
		choices << "discard";
	choices << "get";

	QString choice = room->askForChoice(effect.to, "dingpan", choices.join("+"), QVariant::fromValue(effect.from));
	if (choice == "discard"){
		if (!effect.from->canDiscard(effect.to, "e")) return;
		int id = room->askForCardChosen(effect.from, effect.to, "e", "dingpan", false, Card::MethodDiscard);
		room->throwCard(id, effect.to, effect.from);
	} else {
		DummyCard*dummy = new DummyCard;
		dummy->addSubcards(effect.to->getEquips());
		room->obtainCard(effect.to, dummy);
		delete dummy;
		room->damage(DamageStruct("dingpan", effect.from, effect.to));
	}
}

class DingpanVS : public ZeroCardViewAsSkill
{
public:
	DingpanVS() : ZeroCardViewAsSkill("dingpan")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		/*int n = 0;
		QList<const Player*> as = player->getAliveSiblings();
		as << player;
		foreach(const Player*p, as){
			if (p->getRole() == "rebel")
				n++;
		}*/
		int n = player->getMark("dingpan-PlayClear");
		return player->usedTimes("DingpanCard") < n;
	}

	const Card*viewAs() const
	{
		return new DingpanCard;
	}
};

class Dingpan : public TriggerSkill
{
public:
	Dingpan() : TriggerSkill("dingpan")
	{
		events << EventPhaseStart << CardFinished << EventAcquireSkill << Death << Revived;
		view_as_skill = new DingpanVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &) const
	{
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if(p->getPhase()==Player::Play&&p->hasSkill(objectName())){
				int n = 0;
				foreach(ServerPlayer*pp, room->getAlivePlayers()){
					if (pp->getRole() == "rebel")
						n++;
				}
				room->setPlayerMark(p, "dingpan-PlayClear", n);
			}
		}
		return false;
	}
};

class Xiashu : public PhaseChangeSkill
{
public:
	Xiashu() : PhaseChangeSkill("xiashu")
	{
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Play || player->isKongcheng()) return false;
		ServerPlayer*target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@xiashu-invoke", true, true);
		if (!target) return false;
		int index = qrand() % 2 + 1;
		if (player->getGeneralName().contains("tenyear_") || player->getGeneral2Name().contains("tenyear_"))
			index += 2;
		room->broadcastSkillInvoke(objectName(), index);

		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "xiashu", "");
		DummyCard*handcards = player->wholeHandCards();
		room->obtainCard(target, handcards, reason, false);

		if (target->isKongcheng()) return false;
		int hand = target->getHandcardNum();
		const Card*show = room->askForExchange(target, objectName(), hand, 1, false, "xiashu-show");
		QList<int> ids = show->getSubcards();
		LogMessage log;
		log.type = "$ShowCard";
		log.from = target;
		log.card_str = ListI2S(ids).join("+");
		room->sendLog(log);
		room->fillAG(ids);
		room->getThread()->delay();

		QStringList choices;
		choices << "getshow";
		if (ids.length() != hand)
			choices << "getnotshow";
		QString choice = room->askForChoice(player, objectName(), choices.join("+"), ListI2V(ids));
		room->clearAG();
		if (choice == "getshow"){
			DummyCard*dummy = new DummyCard(ids);
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
			room->obtainCard(player, dummy, reason);
			delete dummy;
		} else {
			DummyCard*dummy = new DummyCard;
			foreach(int id, target->handCards()){
				if (!ids.contains(id))
					dummy->addSubcard(id);
			}
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
			room->obtainCard(player, dummy, reason, false);
			delete dummy;
		}
		return false;
	}
};

class Kuanshi : public TriggerSkill
{
public:
	Kuanshi(const QString &kuanshi) : TriggerSkill(kuanshi), kuanshi(kuanshi)
	{
		events << EventPhaseStart << EventPhaseChanging;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == EventPhaseChanging)
			return 6;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if (player->getPhase() != Player::Finish) return false;
			ServerPlayer*target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@kuanshi-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			room->setPlayerMark(target, "&" + kuanshi + "+#" + player->objectName(), 1);
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::RoundStart){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if (p->getMark("&" + kuanshi + "+#" + player->objectName()) <= 0) continue;
					room->setPlayerMark(p, "&" + kuanshi + "+#" + player->objectName(), 0);
				}
			} else if (change.to == Player::Draw){
				if (kuanshi != "kuanshi" || player->isSkipped(Player::Draw) || player->getMark("kuanshi_skip") <= 0) return false;
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				room->setPlayerMark(player, "kuanshi_skip", 0);
				player->skip(Player::Draw);
			}
		}
		return false;
	}
private:
	QString kuanshi;
};

class KuanshiMark : public TriggerSkill
{
public:
	KuanshiMark(const QString &kuanshi) : TriggerSkill("#" + kuanshi + "-mark"), kuanshi(kuanshi)
	{
		events << EventLoseSkill << Death;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventLoseSkill){
			if (data.toString() != kuanshi) return false;
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if (p->getMark("&" + kuanshi + "+#" + player->objectName()) <= 0) continue;
				room->setPlayerMark(p, "&" + kuanshi + "+#" + player->objectName(), 0);
			}
		} else if (event == Death){
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != player) return false;
			if (!player->hasSkill(kuanshi, true)) return false;
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if (p->getMark("&" + kuanshi + "+#" + player->objectName()) <= 0) continue;
				room->setPlayerMark(p, "&" + kuanshi + "+#" + player->objectName(), 0);
			}
		}
		return false;
	}
private:
	QString kuanshi;
};

class KuanshiEffect : public TriggerSkill
{
public:
	KuanshiEffect() : TriggerSkill("#kuanshi-effect")
	{
		events << DamageInflicted;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.damage <= 1) return false;
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->isDead()) continue;
			if (damage.to->isDead() || damage.to->getMark("&kuanshi+#" + p->objectName()) <= 0) continue;
			LogMessage log;
			log.type = damage.from != nullptr ? "#KuanshiEffect" : "#KuanshiNoFromEffect";
			log.from = damage.to;
			if (damage.from)
				log.to << damage.from;
			log.arg = "kuanshi";
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			room->broadcastSkillInvoke("kuanshi");
			room->notifySkillInvoked(p, "kuanshi");
			room->setPlayerMark(damage.to, "&kuanshi+#" + p->objectName(), 0);
			room->addPlayerMark(p, "kuanshi_skip");
			return true;
		}
		return false;
	}
};


class Xianfu : public TriggerSkill
{
public:
	Xianfu() : TriggerSkill("xianfu")
	{
		events << Damaged << HpRecover << GameStart;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == GameStart){
			if(player->hasSkill(objectName())){
				ServerPlayer*target = room->askForPlayerChosen(player, room->getOtherPlayers(player), "xianfu", "@xianfu-choose", false);

				LogMessage log;
				log.from = player;
				log.to << target;
				log.arg = "xianfu";
				log.type = "#ChoosePlayerWithSkill";
				room->sendLog(log, player);
		
				log.type = "#InvokeSkill";
				room->sendLog(log, room->getOtherPlayers(player, true));
		
				room->doAnimate(1, player->objectName(), target->objectName(), QList<ServerPlayer*>() << player);
				player->peiyin("xianfu", qrand() % 2 + 1);
				room->notifySkillInvoked(player, "xianfu");
				room->addPlayerMark(target, "&xianfu+#" + player->objectName(), 1, QList<ServerPlayer*>() << player);
				room->setPlayerMark(target, "xianfu_hide_" + player->objectName(), 1);
			}
		} else if (triggerEvent == Damaged){
			int d = data.value<DamageStruct>().damage;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				int mark = player->getMark("&xianfu+#" + p->objectName());
				if (p->isDead() || mark <= 0) continue;
				if (player->getMark("xianfu_hide_" + p->objectName()) > 0){
					room->setPlayerMark(player, "xianfu_hide_" + p->objectName(), 0);
					room->setPlayerMark(player, "&xianfu+#" + p->objectName(), mark);
				}
				for (int i = 0; i < mark; i++){
					if (p->isDead()) break;
					room->sendCompulsoryTriggerLog(p, objectName(), true, true, qrand() % 2 + 3);
					room->damage(DamageStruct(objectName(), nullptr, p, d));
				}
			}
		} else {
			int rec = data.value<RecoverStruct>().recover;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				int mark = player->getMark("&xianfu+#" + p->objectName());
				if (p->isDead() || mark <= 0) continue;
				if (player->getMark("xianfu_hide_" + p->objectName()) > 0 && p->getLostHp() > 0){
					room->setPlayerMark(player, "xianfu_hide_" + p->objectName(), 0);
					room->setPlayerMark(player, "&xianfu+#" + p->objectName(), mark);
				}
				for (int i = 0; i < mark; i++){
					if (p->isDead()) break;
					if (p->getLostHp() > 0)
						room->sendCompulsoryTriggerLog(p, objectName(), true, true, qrand() % 2 + 5);
					room->recover(p, RecoverStruct(p, nullptr, qMin(rec, p->getMaxHp() - p->getHp()), "xianfu"));
				}
			}
		}
		return false;
	}
};

class Chouce : public MasochismSkill
{
public:
	Chouce() : MasochismSkill("chouce")
	{
	}

	void onDamaged(ServerPlayer*player, const DamageStruct &damage) const
	{
		Room*room = player->getRoom();
		for (int i = 0; i < damage.damage; i++){
			if (player->isAlive() && room->askForSkillInvoke(player, objectName())){
				player->peiyin(this);

				JudgeStruct judge;
				judge.pattern = ".";
				judge.play_animation = false;
				judge.reason = objectName();
				judge.who = player;
				room->judge(judge);

				if (judge.card->getColor() == Card::Black){
					QList<ServerPlayer*> targets;
					foreach(ServerPlayer*p, room->getAlivePlayers()){
						if (player->canDiscard(p, "hej"))
							targets << p;
					}
					if (targets.isEmpty()) continue;
					ServerPlayer*target = room->askForPlayerChosen(player, targets, "chouce", "@chouce-discard");
					room->doAnimate(1, player->objectName(), target->objectName());
					int card_id = room->askForCardChosen(player, target, "hej", objectName(), false, Card::MethodDiscard);
					room->throwCard(card_id, room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? nullptr : target, player);
				} else if (judge.card->getColor() == Card::Red){
					ServerPlayer*target = room->askForPlayerChosen(player, room->getAlivePlayers(), "chouce_draw", "@chouce-draw");
					room->doAnimate(1, player->objectName(), target->objectName());
					int n = 1;
					if (target->getMark("&xianfu+#" + player->objectName()) > 0){
						n = 2;
						int mark = target->getMark("xianfu_hide_" + player->objectName());
						if (mark > 0){
							room->setPlayerMark(target, "xianfu_hide_" + player->objectName(), 0);
							room->setPlayerMark(target, "&xianfu+#" + player->objectName(), 0);
							room->setPlayerMark(target, "&xianfu+#" + player->objectName(), mark);
						}
					}
					target->drawCards(n, objectName());
				}
			} else
				break;
		}
	}
};

class Qianya : public TriggerSkill
{
public:
	Qianya() : TriggerSkill("qianya")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.to.contains(player)) return false;
		if (use.card->isKindOf("TrickCard")){
			if (player->isKongcheng()) return false;
			QList<int> handcards = player->handCards();
			room->askForYiji(player, handcards, objectName(), false, false, true, -1, QList<ServerPlayer*>(),
							CardMoveReason(), "qianya-give", true);
		}
		return false;
	}
};

class Shuomeng : public TriggerSkill
{
public:
	Shuomeng() : TriggerSkill("shuomeng")
	{
		events << EventPhaseEnd;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (player->getPhase() != Player::Play) return false;
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (player->canPindian(p))
				targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@shuomeng-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		bool success = player->pindian(target, objectName());
		if (success){
			ExNihilo*ex_nihilo = new ExNihilo(Card::NoSuit, 0);
			ex_nihilo->setSkillName("_shuomeng");
			if (!player->isLocked(ex_nihilo) && !player->isProhibited(player, ex_nihilo))
				room->useCard(CardUseStruct(ex_nihilo, player));
		} else {
			Dismantlement*dismantlement = new Dismantlement(Card::NoSuit, 0);
			dismantlement->setSkillName("_shuomeng");
			if (!target->isLocked(dismantlement) && !target->isProhibited(player, dismantlement) &&
					dismantlement->targetFilter(QList<const Player*>(), player, target))
				room->useCard(CardUseStruct(dismantlement, target, player));
		}
		return false;
	}
};

class Sanwen : public TriggerSkill
{
public:
	Sanwen() : TriggerSkill("sanwen")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (room->getTag("FirstRound").toBool()) return false;
		if (player->getMark("sanwen-Clear") > 0) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!move.to || move.to != player || move.to_place != Player::PlaceHand) return false;
		DummyCard*dummy = new DummyCard;
		QList<const Card*> handcards = player->getCards("h");
		if (handcards.isEmpty()) return false;
		QList<int> shows;
		foreach(int id, move.card_ids){
			const Card*card = Sanguosha->getCard(id);
			if (room->getCardPlace(id) != Player::PlaceHand || room->getCardOwner(id) != player) continue;
			foreach(const Card*c, handcards){
				if (c->sameNameWith(card) && !move.card_ids.contains(c->getEffectiveId())){
					if (!dummy->getSubcards().contains(id))
						dummy->addSubcard(id);
					if (!shows.contains(id))
						shows << id;
					if (!shows.contains(c->getEffectiveId()))
						shows << c->getEffectiveId();
				}
			}
		}
		QList<int> subcards = dummy->getSubcards();
		if (dummy->subcardsLength() > 0 && player->askForSkillInvoke(this, ListI2V(subcards))){
			room->broadcastSkillInvoke(objectName());
			LogMessage log;
			log.type = "$ShowCard";
			log.from = player;
			log.card_str = ListI2S(shows).join("+");
			room->sendLog(log);
			room->fillAG(shows);
			room->getThread()->delay();
			room->clearAG();
			room->addPlayerMark(player, "sanwen-Clear");
			room->throwCard(dummy, player, nullptr);
			player->drawCards(2* subcards.length());
		}
		delete dummy;
		return false;
	}
};

class Qiai : public TriggerSkill
{
public:
	Qiai() : TriggerSkill("qiai")
	{
		events << EnterDying;
		frequency = Limited;
		limit_mark = "@qiaiMark";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (player->getMark("@qiaiMark") <= 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->removePlayerMark(player, "@qiaiMark");
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "qiai");

		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (player->isDead()) return false;
			if (p->isAlive() && !p->isNude()){
				const Card*card = room->askForCard(p, "..", "qiai-give:" + player->objectName(), QVariant::fromValue(player), Card::MethodNone);
				if (!card)
					card = p->getCards("he").at(qrand() % p->getCards("he").length());
				CardMoveReason reason(CardMoveReason::S_REASON_GIVE, p->objectName(), player->objectName(), "qiai", "");
				room->obtainCard(player, card, reason, false);
			}
		}
		return false;
	}
};

DenglouCard::DenglouCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool DenglouCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	int id = getSubcards().first();
	const Card*card = Sanguosha->getEngineCard(id);
	if (card->targetFixed())
		return to_select == Self;
	return card->targetFilter(targets, to_select, Self);
}

void DenglouCard::onUse(Room*room, CardUseStruct &card_use) const
{
	int id = getSubcards().first();
	const Card*card = Sanguosha->getEngineCard(id);
	if (card->targetFixed()) return;
	foreach(ServerPlayer*p, card_use.to)
		room->setPlayerFlag(p, "denglou_target");
}

class DenglouVS : public OneCardViewAsSkill
{
public:
	DenglouVS() : OneCardViewAsSkill("denglou")
	{
		response_pattern = "@@denglou!";
		frequency = Limited;
		limit_mark = "@denglouMark";
	}

	bool viewFilter(const Card*to_select) const
	{
		QStringList cards = Self->property("denglou_ids").toString().split("+");
		return cards.contains(to_select->toString());
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	const Card*viewAs(const Card*originalcard) const
	{
		DenglouCard*card = new DenglouCard;
		card->addSubcard(originalcard);
		return card;
	}
};

class Denglou : public PhaseChangeSkill
{
public:
	Denglou() : PhaseChangeSkill("denglou")
	{
		frequency = Limited;
		limit_mark = "@denglouMark";
		view_as_skill = new DenglouVS;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		if (!player->isKongcheng() || player->getMark("@denglouMark") <= 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->removePlayerMark(player, "@denglouMark");
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "denglou");

		QList<int> views = room->getNCards(4);
		LogMessage log;
		log.type = "$ViewDrawPile";
		log.from = player;
		log.card_str = ListI2S(views).join("+");
		room->sendLog(log, player);

		DummyCard*dummy = new DummyCard;
		foreach(int id, views){
			if (!Sanguosha->getCard(id)->isKindOf("BasicCard")){
				dummy->addSubcard(id);
				views.removeOne(id);
			}
		}
		if (dummy->subcardsLength() > 0)
			room->obtainCard(player, dummy, true);
		delete dummy;
		if (views.isEmpty()) return false;

		QList<ServerPlayer*> _player;
		_player.append(player);

		while (!views.isEmpty()){
			if (player->isDead()) break;
			QList<int> use_ids;

			foreach(int id, views){
				const Card*card = Sanguosha->getEngineCard(id);
				if (player->isLocked(card)) continue;
				if (card->targetFixed()){
					if (card->isAvailable(player)){
						use_ids << id;
					}
				} else {
					foreach(ServerPlayer*p, room->getAlivePlayers()){
						if (!card->targetFilter(QList<const Player*>(), p, player)) continue;
						use_ids << id;
						break;
					}
				}
			}
			if (use_ids.isEmpty()) break;

			CardsMoveStruct move(views, nullptr, player, Player::PlaceTable, Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_PREVIEW, player->objectName(), objectName(), ""));
			QList<CardsMoveStruct> moves;
			moves.append(move);
			room->notifyMoveCards(true, moves, false, _player);
			room->notifyMoveCards(false, moves, false, _player);

			room->setPlayerProperty(player, "denglou_ids", ListI2S(use_ids).join("+"));

			const Card*c = room->askForUseCard(player, "@@denglou!", "@denglou");

			room->setPlayerProperty(player, "denglou_ids", "");

			CardsMoveStruct move2(views, player, nullptr, Player::PlaceHand, Player::PlaceTable,
				CardMoveReason(CardMoveReason::S_REASON_PREVIEW, player->objectName(), objectName(), ""));
			QList<CardsMoveStruct> moves2;
			moves2.append(move2);
			room->notifyMoveCards(true, moves2, false, _player);
			room->notifyMoveCards(false, moves2, false, _player);

			QList<ServerPlayer*> tos;
			const Card*use = nullptr;

			if (!c){
				int id = use_ids.at(qrand() & use_ids.length());
				views.removeOne(id);
				use = Sanguosha->getEngineCard(id);
			} else {
				int id = c->getSubcards().first();
				views.removeOne(id);
				use = Sanguosha->getEngineCard(id);
			}

			if (use->targetFixed())
				room->useCard(CardUseStruct(use, player));
			else {
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if (p->hasFlag("denglou_target")){
						room->setPlayerFlag(p, "-denglou_target");
						tos << p;
					}
				}
				if (!tos.isEmpty()){
					room->sortByActionOrder(tos);
					room->useCard(CardUseStruct(use, player, tos), false);
				} else {
					QList<ServerPlayer*> ava;
					foreach(ServerPlayer*p, room->getAlivePlayers()){
						if (!use->targetFilter(QList<const Player*>(), p, player)) continue;
						ava << p;
					}
					if (ava.isEmpty()) continue;
					room->useCard(CardUseStruct(use, player, ava.at(qrand() % ava.length())));
				}
			}
		}

		if (views.isEmpty()) return false;

		DummyCard*new_dummy = new DummyCard(views);
		CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "denglou", "");
		room->throwCard(new_dummy, reason, nullptr);
		delete new_dummy;
		return false;
	}
};

class Weilu : public TriggerSkill
{
public:
	Weilu() : TriggerSkill("weilu")
	{
		events << Damaged << EventPhaseStart << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.from || damage.from == player) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			QStringList names = player->property("weilu_damage_from").toStringList();
			if (!names.contains(damage.from->objectName())){
				names << damage.from->objectName();
				room->setPlayerProperty(player, "weilu_damage_from", names);
				room->addPlayerMark(damage.from, "&weilu");
			}
		} else if (event == EventPhaseStart){
			if (player->getPhase() != Player::Play) return false;
			QStringList names = player->property("weilu_damage_from").toStringList();
			if (names.isEmpty()) return false;
			bool log = true;
			foreach(QString name, names){
				ServerPlayer*p = room->findPlayerByObjectName(name);
				if (!p || p->isDead() || p->getHp() <= 1) continue;
				if (log){
					log = false;
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				}
				int lose = p->getHp() - 1;
				room->loseHp(HpLostStruct(p, lose, "weilu", player));
				if (p->isAlive())
					room->addPlayerMark(p, "weilu_losehp-Clear", lose);
			}
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			QStringList names = player->property("weilu_damage_from").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "weilu_damage_from", QStringList());
			bool log = true;
			foreach(QString name, names){
				ServerPlayer*p = room->findPlayerByObjectName(name);
				if (!p || p->isDead()) continue;
				if (p->getMark("&weilu") > 0)
					room->removePlayerMark(p, "&weilu");
				int recover = p->getMark("weilu_losehp-Clear");
				room->setPlayerMark(p, "weilu_losehp-Clear", 0);
				recover = qMin(recover, p->getMaxHp() - p->getHp());
				if (recover > 0){
					if (log){
						log = false;
						room->sendCompulsoryTriggerLog(player, objectName(), true, true);
					}
					room->recover(p, RecoverStruct(player, nullptr, recover, "weilu"));
				}
			}
		}
		return false;
	}
};

ZengdaoCard::ZengdaoCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void ZengdaoCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.from->getRoom();
	room->removePlayerMark(effect.from, "@zengdaoMark");
	room->doSuperLightbox(effect.from, "zengdao");
	effect.to->addToPile("zengdao", this);
}

ZengdaoRemoveCard::ZengdaoRemoveCard()
{
	mute = true;
	target_fixed = true;
	will_throw = false;
	m_skillName = "zengdao";
	handling_method = Card::MethodNone;
}

void ZengdaoRemoveCard::onUse(Room*room, CardUseStruct &) const
{
	CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "zengdao", "");
	room->throwCard(this, reason, nullptr);
}

class ZengdaoVS : public ViewAsSkill
{
public:
	ZengdaoVS() : ViewAsSkill("zengdao")
	{
		expand_pile = "zengdao";
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@zengdao!")
			return selected.isEmpty() && Self->getPile("zengdao").contains(to_select->getEffectiveId());
		return to_select->isEquipped();
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@zengdao!"){
			ZengdaoRemoveCard*card = new ZengdaoRemoveCard;
			card->addSubcards(cards);
			return card;
		}
		ZengdaoCard*card = new ZengdaoCard;
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("@zengdaoMark") > 0 && !player->getEquips().isEmpty();
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern == "@@zengdao!";
	}
};

class Zengdao : public TriggerSkill
{
public:
	Zengdao() : TriggerSkill("zengdao")
	{
		events << DamageCaused;
		view_as_skill = new ZengdaoVS;
		frequency = Limited;
		limit_mark = "@zengdaoMark";
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive() && !target->getPile("zengdao").isEmpty();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to->isDead()) return false;
		LogMessage log;
		log.type = "#Zengdao";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);

		CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "zengdao", "");
		if (player->getPile("zengdao").length() == 1){
			room->throwCard(Sanguosha->getCard(player->getPile("zengdao").first()), reason, nullptr);
		} else {
			if (!room->askForUseCard(player, "@@zengdao!", "@zengdao")){
				int id = player->getPile("zengdao").at(qrand() % player->getPile("zengdao").length());
				room->throwCard(Sanguosha->getCard(id), reason, nullptr);
			}
		}
		LogMessage newlog;
		newlog.type = "#ZengdaoDamage";
		newlog.from = player;
		newlog.to << damage.to;
		newlog.arg = QString::number(damage.damage);
		newlog.arg2 = QString::number(++damage.damage);
		room->sendLog(newlog);

		data = QVariant::fromValue(damage);

		return false;
	}
};

class Gangzhi : public TriggerSkill
{
public:
	Gangzhi() : TriggerSkill("gangzhi")
	{
		frequency = Compulsory;
		events << Predamage;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		QList<ServerPlayer*> players;
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to->isDead()) return false;

		if (damage.from && damage.from->isAlive() && damage.from->hasSkill(objectName()) && damage.from != damage.to)
			players << damage.from;
		if (damage.to && damage.to->isAlive() && damage.to->hasSkill(objectName()) && damage.from != damage.to && !players.contains(damage.to))
			players << damage.to;
		if (players.isEmpty()) return false;

		room->sortByActionOrder(players);
		ServerPlayer*player = players.first();
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		//room->loseHp(HpLostStruct(damage.to, damage.damage, objectName(), damage.from, damage.ignore_hujia));
		room->loseHp(HpLostStruct(damage.to, damage.damage, objectName(), damage.from));
		return true;
	}
};

class Beizhan : public TriggerSkill
{
public:
	Beizhan() : TriggerSkill("beizhan")
	{
		events << EventPhaseChanging << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive||!player->hasSkill(objectName())) return false;
			ServerPlayer*target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@beizhan-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());

			QStringList names = room->getTag("beizhan_targets").toStringList();
			if (!names.contains(target->objectName())){
				names << target->objectName();
				room->setTag("beizhan_targets", names);
			}
			room->setPlayerMark(target, "&beizhan", 1);
			int n = qMin(target->getMaxHp(), 5);
			if (target->getHandcardNum() >= n) return false;
			target->drawCards(n - target->getHandcardNum(), objectName());
		} else {
			if (player->getPhase() != Player::RoundStart) return false;
			room->setPlayerMark(player, "&beizhan", 0);
			QStringList names = room->getTag("beizhan_targets").toStringList();
			if (!names.contains(player->objectName())) return false;
			names.removeAll(player->objectName());
			room->setTag("beizhan_targets", names);
			int hand = player->getHandcardNum();
			bool max = true;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p != player && p->getHandcardNum() > hand)
					max = false;
			}
			if (max == false) return false;
			LogMessage log;
			log.type = "#BeizhanEffect";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->addPlayerMark(player, "beizhan_pro-Clear");
		}
		return false;
	}
};

class BeizhanPro : public ProhibitSkill
{
public:
	BeizhanPro() : ProhibitSkill("#beizhan-prohibit")
	{
	}

	bool isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*> &) const
	{
		return from != to && from->getMark("beizhan_pro-Clear") > 0 && !card->isKindOf("SkillCard");
	}
};

FenglveCard::FenglveCard()
{
	target_fixed = true;
	will_throw = false;
	mute = true;
	handling_method = Card::MethodNone;
}

void FenglveCard::onUse(Room*, CardUseStruct &) const
{
}

class FenglveVS : public ViewAsSkill
{
public:
	FenglveVS() : ViewAsSkill("fenglve")
	{
		expand_pile = "#fenglve";
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		QList<const Card*> hand = Self->getHandcards();
		QList<const Card*> equip = Self->getEquips();
		QList<int> judge = Self->getPile("#fenglve");
		int x = 0;
		if (!hand.isEmpty()) x++;
		if (!equip.isEmpty()) x++;
		if (!judge.isEmpty()) x++;
		if (selected.length() < x){
			if (selected.isEmpty())
				return true;
			else if (selected.length() == 1){
				if (hand.contains(selected.first()))
					return !hand.contains(to_select);
				else if (equip.contains(selected.first()))
					return !equip.contains(to_select);
				else
					return !judge.contains(to_select->getEffectiveId());
			} else {
				if (Self->hasCard(selected.first()->getEffectiveId()) && Self->hasCard(selected.last()->getEffectiveId()))
					return judge.contains(to_select->getEffectiveId());
				else if (hand.contains(selected.first()) && judge.contains(selected.last()->getEffectiveId()))
					return equip.contains(to_select);
				else if (hand.contains(selected.last()) && judge.contains(selected.first()->getEffectiveId()))
					return equip.contains(to_select);
				else if (equip.contains(selected.first()) && judge.contains(selected.last()->getEffectiveId()))
					return hand.contains(to_select);
				else if (equip.contains(selected.last()) && judge.contains(selected.first()->getEffectiveId()))
					return hand.contains(to_select);
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern == "@@fenglve!";
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;
		int x = 0;
		if (!Self->isKongcheng()) x++;
		if (!Self->getEquips().isEmpty()) x++;
		if (!Self->getPile("#fenglve").isEmpty()) x++;
		if (cards.length() != x) return nullptr;

		FenglveCard*card = new FenglveCard;
		card->addSubcards(cards);
		return card;
	}
};

class Fenglve : public TriggerSkill
{
public:
	Fenglve() : TriggerSkill("fenglve")
	{
		events << EventPhaseStart << Pindian;
		view_as_skill = new FenglveVS;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if (player->getPhase() != Player::Play) return false;
			QList<ServerPlayer*> targets;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (!player->canPindian(p)) continue;
				targets << p;
			}
			if (targets.isEmpty()) return false;
			ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@fenglve-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			if (player->pindian(target, objectName())){
				if (target->isAllNude()) return false;

				QList<int> judge_ids = target->getJudgingAreaID();
				room->notifyMoveToPile(target, judge_ids, objectName(), Player::PlaceDelayedTrick, true);
				const Card*c = room->askForUseCard(target, "@@fenglve!", "@fenglve:" + player->objectName());
				room->notifyMoveToPile(target, judge_ids, objectName(), Player::PlaceDelayedTrick, false);

				DummyCard*dummy = new DummyCard;
				if (c){
					dummy->addSubcards(c->getSubcards());
					CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "fenglve", "");
					room->obtainCard(player, dummy, reason, false);
				} else {
					if (!target->isKongcheng())
						dummy->addSubcard(target->getRandomHandCardId());
					if (!target->getEquips().isEmpty())
						dummy->addSubcard(target->getEquips().at(qrand() % target->getEquips().length()));
					if (!judge_ids.isEmpty())
						dummy->addSubcard(judge_ids.at(qrand() % judge_ids.length()));
					if (dummy->subcardsLength() > 0){
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "fenglve", "");
						room->obtainCard(player, dummy, reason, false);
					}
				}
				delete dummy;
			} else {
				if (player->isNude()) return false;
				const Card*card = room->askForExchange(player, objectName(), 1, 1, true, "fenglve-give:" + target->objectName());
				CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "fenglve", "");
				room->obtainCard(target, card, reason, false);
			}
		} else {
			PindianStruct*pindian = data.value<PindianStruct*>();
			if (pindian->reason != objectName()) return false;
			/*const Card*card = nullptr;
			ServerPlayer*target = nullptr;
			if (pindian->from == player){
				card = pindian->from_card;
				target = pindian->to;
			} else if (pindian->to == player){
				card = pindian->to_card;
				target = pindian->from;
			}
			if (!card || !target || target->isDead()) return false;*/
			if (pindian->from != player) return false;
			if (pindian->to->isDead() || !room->CardInTable(pindian->from_card)) return false;
			if (!player->askForSkillInvoke(this, QString("fenglve_invoke:%1::%2").arg(pindian->to->objectName()).arg(pindian->from_card->objectName())))
				return false;
			room->broadcastSkillInvoke(objectName());
			room->obtainCard(pindian->to, pindian->from_card, true);
		}
		return false;
	}
};

MoushiCard::MoushiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void MoushiCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->isDead() || effect.to->isDead()) return;
	Room*room = effect.from->getRoom();
	QStringList names = effect.to->property("moushi_from").toStringList();
	if (!names.contains(effect.from->objectName())){
		names << effect.from->objectName();
		room->setPlayerProperty(effect.to, "moushi_from", names);
	}
	room->setPlayerMark(effect.to, "&moushi", 1);
	CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "moushi", "");
	room->obtainCard(effect.to, this, reason, false);
}

class MoushiVS : public OneCardViewAsSkill
{
public:
	MoushiVS() :OneCardViewAsSkill("moushi")
	{
		filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("MoushiCard");
	}

	const Card*viewAs(const Card*originalCard) const
	{
		MoushiCard*c = new MoushiCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Moushi : public TriggerSkill
{
public:
	Moushi() : TriggerSkill("moushi")
	{
		events << EventPhaseChanging << Death << Damage;
		view_as_skill = new MoushiVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		ServerPlayer*current = room->getCurrent();
		if (event == EventPhaseChanging){
			if (!current || current != player) return false;
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.from != Player::Play) return false;
			room->setPlayerProperty(player, "moushi_from", QStringList());
			room->setPlayerMark(player, "&moushi", 0);
		} else if (event == Damage){
			if (player->isDead() || !current || current != player || player->getPhase() != Player::Play) return false;
			QStringList names = player->property("moushi_from").toStringList();
			if (names.isEmpty()) return false;

			DamageStruct damage = data.value<DamageStruct>();
			if (player->getMark("moushi_" + damage.to->objectName() + "-Clear") > 0) return false;
			room->addPlayerMark(player, "moushi_" + damage.to->objectName() + "-Clear");

			QList<ServerPlayer*> xunchens;
			foreach(QString str, names){
				ServerPlayer*xunchen = room->findPlayerByObjectName(str);
				if (xunchen && xunchen->isAlive() && xunchen->hasSkill(objectName()))
					xunchens << xunchen;
			}
			if (xunchens.isEmpty()) return false;

			room->sortByActionOrder(xunchens);
			foreach(ServerPlayer*p, xunchens){
				if (p->isAlive() && p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p, objectName(), true, true);
					p->drawCards(1, objectName());
				}
			}
		} else {
			DeathStruct death = data.value<DeathStruct>();
			room->setPlayerProperty(death.who, "moushi_from", QStringList());
			room->setPlayerMark(death.who, "&moushi", 0);
		}
		return false;
	}
};

class Bizheng : public TriggerSkill
{
public:
	Bizheng() : TriggerSkill("bizheng")
	{
		events << EventPhaseEnd;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (player->getPhase() != Player::Draw) return false;
		ServerPlayer*target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@bizheng-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		target->drawCards(2, objectName());
		QList<ServerPlayer*> players;
		players << player << target;
		room->sortByActionOrder(players);
		foreach(ServerPlayer*p, players){
			if (p->isAlive() && p->getHandcardNum() > p->getMaxHp()){
				room->askForDiscard(p, objectName(), 2, 2, false, true);
			}
		}
		return false;
	}
};

class YidianVS : public ZeroCardViewAsSkill
{
public:
	YidianVS() : ZeroCardViewAsSkill("yidian")
	{
		response_pattern = "@@yidian";
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	const Card*viewAs() const
	{
		return new ExtraCollateralCard;
	}
};

class Yidian : public TriggerSkill
{
public:
	Yidian() : TriggerSkill("yidian")
	{
		events << PreCardUsed;
		view_as_skill = new YidianVS;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isNDTrick() && !use.card->isKindOf("BasicCard")) return false;

		QList<ServerPlayer*> ava;
		room->setCardFlag(use.card, "yidian_distance");
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (use.to.contains(p)) continue;
			if (use.from->canUse(use.card,p))
				ava << p;
		}
		room->setCardFlag(use.card, "-yidian_distance");
		if (ava.isEmpty()) return false;

		QString name = use.card->objectName();
		if (use.card->isKindOf("Slash"))
			name = "slash";
		bool has = false;
		foreach(int id, room->getDiscardPile()){
			const Card*card = Sanguosha->getCard(id);
			QString card_name = card->objectName();
			if (card->isKindOf("Slash"))
				card_name = "slash";
			if (card_name == name){
				has = true;
				break;
			}
		}
		if (has) return false;
		ServerPlayer*target;
		if (use.card->isKindOf("Collateral")){
			QStringList tos;
			tos << use.card->toString();
			foreach(ServerPlayer*t, use.to)
				tos << t->objectName();
			tos << objectName();
			room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
			room->askForUseCard(player, "@@yidian", "@yidian:" + name);
			target = player->getTag("ExtraCollateralTarget").value<ServerPlayer*>();
			player->removeTag("ExtraCollateralTarget");
			if (!target) return false;
		} else {
			player->setTag("YidianData", data);
			target = room->askForPlayerChosen(player, ava, objectName(), "@yidian-invoke:" + name, true);
			if (!target) return false;
			LogMessage log;
			log.type = "#QiaoshuiAdd";
			log.from = player;
			log.to << target;
			log.card_str = use.card->toString();
			log.arg = "yidian";
			room->sendLog(log);
		}
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		use.to.append(target);
		room->sortByActionOrder(use.to);
		data = QVariant::fromValue(use);
		return false;
	}
};

class Guanchao : public TriggerSkill
{
public:
	Guanchao() : TriggerSkill("guanchao")
	{
		events << EventPhaseStart << CardUsed << EventPhaseChanging;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if (player->getPhase() != Player::Play) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			QString choice = room->askForChoice(player, objectName(), "up+down");
			LogMessage log;
			log.type = "#FumianFirstChoice";
			log.from = player;
			log.arg = "guanchao:" + choice;
			room->sendLog(log);
			room->setPlayerFlag(player, "guanchao_" + choice);
			room->setPlayerMark(player, "guanchao-PlayClear", -1);
		} else if (event == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.from != Player::Play) return false;
			if (player->hasFlag("guanchao_up"))
				room->setPlayerFlag(player, "-guanchao_up");
			if (player->hasFlag("guanchao_down"))
				room->setPlayerFlag(player, "-guanchao_down");
		} else {
			const Card*card = nullptr;
			if (event == CardUsed){
				CardUseStruct use = data.value<CardUseStruct>();
				if (use.card->isKindOf("SkillCard")) return false;
				card = use.card;
			}
			if (!card) return false;

			int mark = player->getMark("guanchao-PlayClear");
			int num = card->getNumber();
			if (mark < 0){
				room->setPlayerMark(player, "guanchao-PlayClear", num);
				return false;
			}

			if (player->hasFlag("guanchao_up")){
				if (num > mark){
					room->setPlayerMark(player, "guanchao-PlayClear", num);
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
					player->drawCards(1, objectName());
				} else {
					room->setPlayerFlag(player, "-guanchao_up");
				}
			} else if (player->hasFlag("guanchao_down")){
				if (num < mark){
					room->setPlayerMark(player, "guanchao-PlayClear", num);
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
					player->drawCards(1, objectName());
				} else {
					room->setPlayerFlag(player, "-guanchao_down");
				}
			}
		}
		return false;
	}
};

class Xunxian : public TriggerSkill
{
public:
	Xunxian() : TriggerSkill("xunxian")
	{
		events << CardsMoveOneTime;
	}
    int getPriority(TriggerEvent) const
    {
        return 3;
    }

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (!room->hasCurrent()) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place!=Player::DiscardPile||player->hasFlag("CurrentPlayer")||move.reason.m_playerId!=player->objectName()) return false;
        if ((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE||move.reason.m_reason==CardMoveReason::S_REASON_RESPONSE){
			const Card*card = move.reason.m_extraData.value<const Card*>();
			if (!card||card->isKindOf("SkillCard")||room->getCardOwner(card->getEffectiveId())) return false;
			if (player->getMark("xunxian-Clear") > 0) return false;

			QList<ServerPlayer*> targets;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->getHandcardNum() > player->getHandcardNum())
					targets << p;
			}
			ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@xunxian-give:" + card->objectName(), true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player, "xunxian-Clear");

			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "xunxian", "");
			room->obtainCard(target, card, reason, true);
		}
		return false;
	}
};

class Guanwei : public TriggerSkill
{
public:
	Guanwei() : TriggerSkill("guanwei")
	{
		events << EventPhaseEnd << CardFinished;
        global = true;
	}

	int getPriority(TriggerEvent) const
	{
		return 0;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseEnd){
			if (player->getPhase() != Player::Play) return false;
			int has = 0;
			foreach(QString m, player->getMarkNames()){
				if(m.contains("guanweiUsed")){
					if(has>0) return false;
					has = player->getMark(m);
				}
			}
			if (has<2) return false;
			foreach(ServerPlayer*p, room->findPlayersBySkillName(objectName())){
				if (player->isDead()) break;
				if (p->getMark("guanwei_used-Clear") > 0 || !p->canDiscard(p, "he")) continue;
				const Card*card = room->askForCard(p, "..", "guanwei-invoke:" + player->objectName(), QVariant::fromValue(player), objectName());
				if (!card) continue;
				room->broadcastSkillInvoke(objectName());
				p->addMark("guanwei_used-Clear");
				player->drawCards(2, objectName());
				if (player->isDead()) break;
				player->insertPhase(Player::Play);/*
				room->addPlayerHistory(player, ".");
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					foreach(QString mark, p->getMarkNames()){
						if (mark.endsWith("-PlayClear"))
							room->setPlayerMark(p, mark, 0);
					}
				}
				RoomThread*thread = room->getThread();
				if (!thread->trigger(EventPhaseStart, room, player)){
					thread->trigger(EventPhaseProceeding, room, player);
				}
				thread->trigger(EventPhaseEnd, room, player);*/
			}
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			player->addMark(use.card->getSuitString()+"guanweiUsed-Clear");
		}
		return false;
	}
};

class Gongqing : public TriggerSkill
{
public:
	Gongqing() : TriggerSkill("gongqing")
	{
		events << DamageInflicted;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.from || damage.from->isDead()) return false;
		int n = damage.from->getAttackRange();
		if (n == 3) return false;

		LogMessage log;
		log.from = player;
		log.to << damage.from;
		log.arg = QString::number(damage.damage);
		if (n < 3){
			if (damage.damage <= 1) return false;
			log.type = "#GongqingReduce";
			log.arg2 = QString::number(1);
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			damage.damage = 1;
		} else {
			log.type = "#GongqingAdd";
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
		}
		room->notifySkillInvoked(player, objectName());
		data = QVariant::fromValue(damage);
		return false;
	}
};

QuxiCard::QuxiCard()
{
	mute = true;
}

bool QuxiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	if (to_select == Self || targets.length() >= 2) return false;

	QString pattern = Sanguosha->getCurrentCardUsePattern();
	if (!pattern.startsWith("@@quxi")) return false;
	if (pattern.endsWith("1")){
		if (targets.isEmpty()) return true;
		if (targets.length() == 1)
			return to_select->getHandcardNum() != targets.first()->getHandcardNum();
	} else if (pattern.endsWith("2")){
		if (!targets.isEmpty()){
			if (targets.first()->getMark("&quxifeng") > 0 || targets.first()->getMark("&quxiqian") > 0)
				return true;
			else
				return false;
		}
		QString death_name = Self->property("QuxiDeathPlayer").toString();
		//const Player*death = Self->findChild<const Player*>(death_name);  ?????death
		const Player*death = nullptr;
		foreach(const Player*p, Self->getSiblings()){
			if (p->objectName() == death_name){
				death = p;
				break;
			}
		}
		if (death){
			if (death->getMark("&quxifeng") > 0 || death->getMark("&quxiqian") > 0)
				return true;
			else
				return to_select->getMark("&quxifeng") > 0 || to_select->getMark("&quxiqian") > 0;
		} else if (!death)
			return to_select->getMark("&quxifeng") > 0 || to_select->getMark("&quxiqian") > 0;
	} else if (pattern.endsWith("3")){
		if (targets.isEmpty())
			return to_select->getMark("&quxifeng") > 0 || to_select->getMark("&quxiqian") > 0;
		else
			return true;
	}
	return false;
}

bool QuxiCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	QString pattern = Sanguosha->getCurrentCardUsePattern();
	if (!pattern.startsWith("@@quxi")) return false;
	if (!pattern.endsWith("2"))
		return targets.length() == 2;
	else
		return targets.length() >= 1;
}

void QuxiCard::onUse(Room*room, CardUseStruct &card_use) const
{
	QString pattern = Sanguosha->getCurrentCardUsePattern();
	if (!pattern.startsWith("@@quxi")) return;
	if (pattern.endsWith("1"))
		SkillCard::onUse(room, card_use);
	else {
		CardUseStruct use = card_use;
		QVariant data = QVariant::fromValue(use);
		RoomThread*thread = room->getThread();

		thread->trigger(PreCardUsed, room, card_use.from, data);
		use = data.value<CardUseStruct>();

		room->broadcastSkillInvoke("quxi");

		LogMessage log;
		log.from = card_use.from;
		log.to << card_use.to;
		log.type = "#UseCard";
		log.card_str = toString();
		room->sendLog(log);

		thread->trigger(CardUsed, room, card_use.from, data);
		use = data.value<CardUseStruct>();
		thread->trigger(CardFinished, room, card_use.from, data);
	}
}

void QuxiCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	QString pattern = Sanguosha->getCurrentCardUsePattern();
	if (!pattern.startsWith("@@quxi")) return;

	if (pattern.endsWith("1")){
		ServerPlayer*more,*less;
		if (targets.first()->getHandcardNum() > targets.last()->getHandcardNum()){
			more = targets.first();
			less = targets.last();
		} else {
			more = targets.last();
			less = targets.first();
		}
		if (more->isNude()) return;
		int id = room->askForCardChosen(less, more, "he", "quxi");
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, less->objectName());
		room->obtainCard(less, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
		if (less->isAlive())
			less->gainMark("&quxifeng");
		if (more->isAlive())
			more->gainMark("&quxiqian");
	} else {
		ServerPlayer*first = nullptr,*last = nullptr;
		QStringList choices;
		if (targets.length() == 1 && pattern.endsWith("2")){
			QString death_name = source->property("QuxiDeathPlayer").toString();
			ServerPlayer*death = room->findChild<ServerPlayer*>(death_name);
			if (!death) return;
			first = death;
			last = targets.first();
		} else if (targets.length() >= 2){
			first = targets.first();
			last = targets.last();
		}
		if (!first || !last) return;

		if (first->getMark("&quxifeng") > 0)
			choices << "feng";
		if (first->getMark("&quxiqian") > 0)
			choices << "qian";
		if (choices.isEmpty()) return;

		QString mark, choice = room->askForChoice(source, "quxi", choices.join("+"), QVariant::fromValue(first));
		mark = "&quxi" + choice;
		int num = first->getMark(mark);
		first->loseAllMarks(mark);
		last->gainMark(mark, num);
	}
}

class QuxiVS : public ZeroCardViewAsSkill
{
public:
	QuxiVS() : ZeroCardViewAsSkill("quxi")
	{
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.startsWith("@@quxi");
	}

	const Card*viewAs() const
	{
		return new QuxiCard;
	}
};

class Quxi : public TriggerSkill
{
public:
	Quxi() : TriggerSkill("quxi")
	{
		events << EventPhaseEnd << Death << RoundStart;
		view_as_skill = new QuxiVS;
		frequency = Limited;
		limit_mark = "@quxiMark";
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseEnd){
			if (player->getPhase() != Player::Play || player->getMark("@quxiMark") <= 0 || player->isSkipped(Player::Discard)) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			room->doSuperLightbox(player, "quxi");
			room->removePlayerMark(player, "@quxiMark");

			player->skip(Player::Discard);
			if (player->faceUp())
				player->turnOver();
			if (player->isDead() || room->alivePlayerCount() <= 2) return false;
			if (room->askForUseCard(player, "@@quxi1", "@quxi1", 1, Card::MethodNone)) return false;

			QList<ServerPlayer*> a_list;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				int hand = p->getHandcardNum();
				foreach(ServerPlayer*pp, room->getOtherPlayers(player)){
					if (pp == p || pp->getHandcardNum() == hand) continue;
					a_list << p;
					break;
				}
			}
			if (a_list.isEmpty()) return false;
			ServerPlayer*a = a_list.at(qrand() % a_list.length());
			int hand = a->getHandcardNum();

			QList<ServerPlayer*> b_list;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p == a || p->getHandcardNum() == hand) continue;
				b_list << p;
			}
			if (b_list.isEmpty()) return false;
			ServerPlayer*b = b_list.at(qrand() % b_list.length());

			ServerPlayer*more,*less;
			if (a->getHandcardNum() > b->getHandcardNum()){
				more = a;
				less = b;
			} else {
				more = b;
				less = a;
			}

			QList<ServerPlayer*> tos;
			tos << a << b;
			room->sortByActionOrder(tos);
			LogMessage log;
			log.from = player;
			log.to << tos;
			log.type = "#ChoosePlayerWithSkill";
			log.arg = objectName();
			room->sendLog(log);
			foreach(ServerPlayer*p, tos)
				room->doAnimate(1, player->objectName(), p->objectName());

			if (more->isNude()) return false;
			int id = room->askForCardChosen(less, more, "he", objectName());
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, less->objectName());
			room->obtainCard(less, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
			if (less->isAlive())
				less->gainMark("&quxifeng");
			if (more->isAlive())
				more->gainMark("&quxiqian");
		} else if (event == RoundStart){
			if (room->alivePlayerCount() < 3) return false;
			bool can_transfer = false;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->getMark("&quxifeng") > 0 || p->getMark("&quxiqian") > 0){
					can_transfer = true;
					break;
				}
			}
			if (!can_transfer) return false;
			room->askForUseCard(player, "@@quxi3", "@quxi3", 3, Card::MethodNone);
		} else {
			if (room->alivePlayerCount() < 2) return false;
			DeathStruct death = data.value<DeathStruct>();
			if (death.who == player) return false;
			bool can_transfer = false;
			foreach(ServerPlayer*p, room->getAllPlayers(true)){
				if (p->getMark("&quxifeng") > 0 || p->getMark("&quxiqian") > 0){
					can_transfer = true;
					break;
				}
			}
			if (!can_transfer) return false;
			room->setPlayerProperty(player, "QuxiDeathPlayer", death.who->objectName());
			room->askForUseCard(player, "@@quxi2", "@quxi2", 2, Card::MethodNone);
			room->setPlayerProperty(player, "QuxiDeathPlayer", "");
		}
		return false;
	}
};

class QuxiDraw : public DrawCardsSkill
{
public:
	QuxiDraw() : DrawCardsSkill("#quxi")
	{
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && (target->getMark("&quxifeng") > 0 || target->getMark("&quxiqian") > 0);
	}

	int getDrawNum(ServerPlayer*player, int n) const
	{
		Room*room = player->getRoom();
		int feng = player->getMark("&quxifeng"), qian = player->getMark("&quxiqian");
		if (feng == qian) return n;

		LogMessage log;
		log.type = "#ZhenguEffect";
		log.from = player;
		log.arg = "quxi";
		room->sendLog(log);
		room->broadcastSkillInvoke("quxi");

		return n + feng - qian;
	}
};

class Bixiong : public TriggerSkill
{
public:
	Bixiong() : TriggerSkill("bixiong")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getPhase() != Player::Discard) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player) return false;
		if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD){
			QString mark = "&bixiong+";
			for (int i = 0; i < move.card_ids.length(); i++){
				if (move.from_places.at(i) != Player::PlaceHand) continue;
				const Card*card = Sanguosha->getCard(move.card_ids.at(i));
				QString suit = card->getSuitString() + "_char";
				if (mark.contains(suit)) continue;
				mark = mark + "+" + suit;
			}
			if (mark == "&bixiong+") return false;
			room->sendCompulsoryTriggerLog(player, this);
			foreach(QString m, player->getMarkNames()){
				if (!m.startsWith("&bixiong+")) continue;
				room->setPlayerMark(player, m, 0);
			}
			room->addPlayerMark(player, mark);
		}
		return false;
	}
};

class BixiongClear : public PhaseChangeSkill
{
public:
	BixiongClear() : PhaseChangeSkill("#bixiong-clear")
	{
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::RoundStart) return false;
		foreach(QString m, player->getMarkNames()){
			if (!m.startsWith("&bixiong+")) continue;
			room->setPlayerMark(player, m, 0);
		}
		return false;
	}
};

class BixiongProhibit : public ProhibitSkill
{
public:
	BixiongProhibit() : ProhibitSkill("#bixiong-prohibit")
	{
	}

	bool isProhibited(const Player*, const Player*to, const Card*card, const QList<const Player*> &) const
	{
		foreach(QString m, to->getMarkNames()){
			if (m.startsWith("&bixiong+")&&to->getMark(m)>0)
				return m.contains(card->getSuitString() + "_char");
		}
		return false;
	}
};

class Juanxia : public TriggerSkill
{
public:
	Juanxia(const QString &juanxia) : TriggerSkill(juanxia), juanxia(juanxia)
	{
		events << EventPhaseChanging;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		ServerPlayer*t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@juanxia-invoke", true, true);
		if (!t) return false;
		player->peiyin(objectName());

		int n = 2;
		if (objectName() == "tenyearjuanxia")
			n = 3;

		int num = 0;
		QStringList choices;
		try {
			for (int i = 0; i < n; i++){
				if (player->isDead() || t->isDead()) return false;

				QStringList cards;
				static QList<const SingleTargetTrick*> SingleTargetTricks = Sanguosha->findChildren<const SingleTargetTrick*>();
				foreach(const SingleTargetTrick*st, SingleTargetTricks){
					if (!player->canUse(st, t, true)) continue;
					QString name = st->objectName();
					if (name.startsWith("_")) continue;
					if (!ServerInfo.BanPackages.contains(st->getPackage()) && st->isNDTrick()
						&& !cards.contains(st->objectName()) && !st->isKindOf("Nullification") && !choices.contains(name))
						cards << name;
				}
				if (cards.isEmpty()) break;

				if (i != 0)
					cards << "cancel";

				QString card_name = room->askForChoice(player, objectName(), cards.join("+"), QVariant::fromValue(t));
				if (card_name == "cancel") break;

				choices << card_name;

				Card*card = Sanguosha->cloneCard(card_name);
				if (!card) continue;
				card->setSkillName("_" + juanxia);
				card->deleteLater();
				if (!player->canUse(card, t, true)) continue;
				QList<ServerPlayer*> tos;
				tos << t;
				if (card->isKindOf("Collateral")){
					QList<ServerPlayer*> victims;
					foreach(ServerPlayer*p, room->getOtherPlayers(t)){
						if (t->canSlash(p))
							victims << p;
					}
					if (victims.isEmpty()) continue;

					ServerPlayer*victim = room->askForPlayerChosen(player, victims, "juanxia_collateral", "@zenhui-collateral:" + t->objectName());
					tos << victim;
				}

				num++;
				room->useCard(CardUseStruct(card, player, tos));
			}
		}
		catch (TriggerEvent triggerEvent){
			if (triggerEvent == TurnBroken || triggerEvent == StageChange){
				if (num > 0 && t->isAlive())
					room->addPlayerMark(t, QString("&%1+#%2").arg(objectName()).arg(player->objectName()), num);
			}
			throw triggerEvent;
		}

		if (num > 0 && t->isAlive())
			room->addPlayerMark(t, QString("&%1+#%2").arg(objectName()).arg(player->objectName()), num);
		return false;
	}

private:
	QString juanxia;
};

class JuanxiaSlash : public TriggerSkill
{
public:
	JuanxiaSlash(const QString &juanxia) : TriggerSkill("#" + juanxia + "-slash"), juanxia(juanxia)
	{
		events << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;

		QList<ServerPlayer*> tos;
		QHash<ServerPlayer*, int> hash;

		foreach(QString mark, player->getMarkNames()){
			int mark_num = player->getMark(mark);
			if (!mark.startsWith("&" + juanxia + "+#") || mark_num <= 0) continue;
			QStringList marks = mark.split("#");
			if (marks.length() != 2) continue;
			room->setPlayerMark(player, mark, 0);
			ServerPlayer*to = room->findChild<ServerPlayer*>(marks.last());
			if (!to || to->isDead()) continue;
			hash[to] = mark_num;
			tos << to;
		}
		if (tos.isEmpty()) return false;
		room->sortByActionOrder(tos);

		foreach(ServerPlayer*to, tos){
			if (player->isDead()) return false;
			if (to->isDead() || !player->canSlash(to, false)) continue;

			int mark_num = hash[to];
			if (mark_num <= 0) continue;

			LogMessage log;
			log.type = "#ZhenguEffect";
			log.from = to;
			log.arg = juanxia;
			room->sendLog(log);
			room->notifySkillInvoked(to, juanxia);

			QString prompt = juanxia + "_slash:" + to->objectName() + "::" + QString::number(mark_num);
			if (!player->askForSkillInvoke(juanxia + "_slash", prompt, false)) continue;

			for (int i = 0; i < mark_num; i++){
				if (player->isDead()) return false;
				if (to->isDead() || !player->canSlash(to, false)) break;

				Slash*slash = new Slash(Card::NoSuit, 0);
				slash->setSkillName("_" + juanxia);
				slash->deleteLater();
				room->useCard(CardUseStruct(slash, player, to));
			}
		}
		return false;
	}

private:
	QString juanxia;
};

class Dingcuo : public TriggerSkill
{
public:
	Dingcuo() : TriggerSkill("dingcuo")
	{
		events << Damage << Damaged;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (!room->hasCurrent() || player->getMark("dingcuo-Clear") > 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(this);
		room->addPlayerMark(player, "dingcuo-Clear");

		QList<int> draw_ids = room->drawCardsList(player, 2, objectName());
		const Card*first = Sanguosha->getCard(draw_ids.first());
		const Card*last = Sanguosha->getCard(draw_ids.last());
		if (first->sameColorWith(last)) return false;
		if (player->canDiscard(player, "h"))
			room->askForDiscard(player, objectName(), 1, 1);
		return false;
	}
};

class Chijie : public TriggerSkill
{
public:
	Chijie() : TriggerSkill("chijie")
	{
		events << Damaged << DamageCaused << CardFinished;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard") || use.card->hasFlag("DamageDone")) return false;
			foreach(ServerPlayer*p, use.to){
				if (p->isDead() || !p->hasSkill(objectName()) || use.from == p || p->getMark("chijie-Clear") > 0
				|| !room->CardInPlace(use.card, Player::DiscardPile)) continue;
				room->addPlayerMark(p, "chijie-Clear");
				room->sendCompulsoryTriggerLog(p, this);
				room->obtainCard(p, use.card);
			}
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || damage.card->isKindOf("SkillCard")) return false;
			if (event == Damaged){
				if (!room->hasCurrent() || player->getMark("chijie-Clear") > 0 || !player->hasSkill(objectName())) return false;
				if (room->getCardUser(damage.card) == player) return false;
				if (player->askForSkillInvoke(this, QString("chijie_damage:" + damage.card->objectName()))){
					room->addPlayerMark(player, "chijie-Clear");
					room->setCardFlag(damage.card, "chijie");
					room->setCardFlag(damage.card, "chijie_" + player->objectName());
				}
			} else if (event == DamageCaused){
				if (damage.card->hasFlag("chijie") && !damage.card->hasFlag("chijie_" + damage.to->objectName())){
					LogMessage log;
					log.type = "#ChijiePrevent";
					log.from = damage.to;
					log.arg = objectName();
					log.arg2 = QString::number(damage.damage);
					room->sendLog(log);
					return true;
				}
			}
		}
		return false;
	}
};

YinjuCard::YinjuCard()
{
}

void YinjuCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.from->getRoom();
	room->doSuperLightbox(effect.from, "yinju");
	room->removePlayerMark(effect.from, "@yinjuMark");
	if (!room->hasCurrent()) return;
	room->addPlayerMark(effect.from, "yinju_from-Clear");
	room->addPlayerMark(effect.to, "yinju_to-Clear");
}

class YinjuVS : public ZeroCardViewAsSkill
{
public:
	YinjuVS() : ZeroCardViewAsSkill("yinju")
	{
		frequency = Limited;
		limit_mark = "@yinjuMark";
	}

	const Card*viewAs() const
	{
		return new YinjuCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("@yinjuMark") > 0;
	}
};

class Yinju : public TriggerSkill
{
public:
	Yinju() : TriggerSkill("yinju")
	{
		events << DamageCaused << TargetSpecified;
		view_as_skill = new YinjuVS;
		frequency = Limited;
		limit_mark = "@yinjuMark";
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.to->isDead()) return false;
			if (player->getMark("yinju_from-Clear") <= 0 || damage.to->getMark("yinju_to-Clear") <= 0) return false;
			LogMessage log;
			log.type = damage.to->getLostHp() > 0 ? "#YinjuPrevent1" : "#YinjuPrevent2";
			log.from = player;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());
			int n = qMin(damage.damage, damage.to->getMaxHp() - damage.to->getHp());
			if (n > 0)
				room->recover(damage.to, RecoverStruct(player, nullptr, n, "yinju"));
			return true;
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (player->getMark("yinju_from-Clear") <= 0) return false;
			foreach(ServerPlayer*p, use.to){
				if (p->getMark("yinju_to-Clear") <= 0) continue;
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				player->drawCards(1, objectName());
			}
		}
		return false;
	}
};

class OLFengji : public PhaseChangeSkill
{
public:
	OLFengji() : PhaseChangeSkill("olfengji")
	{
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Draw) return false;
		room->sendCompulsoryTriggerLog(player, this);
		QStringList choices, chosen;
		choices << "draw" << "slash" << "cancel";
		while (choices.length() > 1){
			if (player->isDead()) return false;
			QString choice = room->askForChoice(player, "olfengji", choices.join("+"), QVariant(), chosen.join("+"));
			if (choice == "cancel") break;
			choices.removeOne(choice);
			chosen << choice;
			ServerPlayer*t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@olfengji" + choice);
			room->doAnimate(1, player->objectName(), t->objectName());
			room->addPlayerMark(t, "&olfengji" + choice + "-SelfClear");
		}
		if (!chosen.contains("draw") && player->isAlive())
			room->addPlayerMark(player, "&olfengjidraw-SelfClear");
		if (!chosen.contains("slash") && player->isAlive())
			room->addPlayerMark(player, "&olfengjislash-SelfClear");
		return false;
	}
};

class OLFengjiDraw : public DrawCardsSkill
{
public:
	OLFengjiDraw() : DrawCardsSkill("#olfengji-draw")
	{
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive() && target->getMark("&olfengjidraw-SelfClear") > 0;
	}

	int getDrawNum(ServerPlayer*player, int n) const
	{
		Room*room = player->getRoom();
		LogMessage log;
		log.type = "#ZhenguEffect";
		log.from = player;
		log.arg = "olfengji";
		room->sendLog(log);
		room->broadcastSkillInvoke("olfengji");
		return n += 2* player->getMark("&olfengjidraw-SelfClear");
	}
};

class JinHuaiyuan : public TriggerSkill
{
public:
	JinHuaiyuan() : TriggerSkill("jinhuaiyuan")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		QVariantList Initial = player->property("InitialHandCards").toList();
		if (Initial.isEmpty()) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player || !move.from_places.contains(Player::PlaceHand)) return false;
		QList<int>ids = ListV2I(Initial);
		for (int i = 0; i < move.card_ids.length(); i++){
			if (move.from_places.at(i) != Player::PlaceHand) continue;
			if (!ids.contains(move.card_ids.at(i))) continue;

			ServerPlayer*t = room->askForPlayerChosen(player, room->getAllPlayers(), objectName(), "@jinhuaiyuan-target", false, true);
			player->peiyin(this);

			QStringList choices;
			choices << "maxcards=" + t->objectName() << "attack=" + t->objectName() << "draw=" + t->objectName();
			QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(t));
			if (choice.startsWith("maxcards"))
				room->addPlayerMark(t, "&jinhuaiyuanmaxcards");
			else if (choice.startsWith("attack"))
				room->addPlayerMark(t, "&jinhuaiyuanattack");
			else
				t->drawCards(1, objectName());
			if (player->isDead()) break;
		}
		return false;
	}
};

class JinHuaiyuanDeath : public TriggerSkill
{
public:
	JinHuaiyuanDeath() : TriggerSkill("#jinhuaiyuan")
	{
		events << Death;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->hasSkill("jinhuaiyuan");
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who != player) return false;

		int max = player->getMark("&jinhuaiyuanmaxcards"), attack = player->getMark("&jinhuaiyuanattack");
		if (max <= 0 && attack <= 0) return false;

		QString prompt = QString("@jinhuaiyuan-death:%1::%2").arg(max).arg(attack);
		ServerPlayer*t = room->askForPlayerChosen(player, room->getOtherPlayers(player), "jinhuaiyuan", prompt, true, true);
		if (!t) return false;
		player->peiyin("jinhuaiyuan");

		room->setPlayerMark(player, "&jinhuaiyuanmaxcards", 0);
		room->setPlayerMark(player, "&jinhuaiyuanattack", 0);

		room->addPlayerMark(t, "&jinhuaiyuanmaxcards", max);
		room->addPlayerMark(t, "&jinhuaiyuanattack", max);
		return false;
	}
};

class JinHuaiyuanKeep : public MaxCardsSkill
{
public:
	JinHuaiyuanKeep() : MaxCardsSkill("#jinhuaiyuan-keep")
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player*target) const
	{
		return target->getMark("&jinhuaiyuanmaxcards");
	}
};

class JinHuaiyuanAttack : public AttackRangeSkill
{
public:
	JinHuaiyuanAttack() : AttackRangeSkill("#jinhuaiyuan-attack")
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player*target, bool) const
	{
		return target->getMark("&jinhuaiyuanattack");
	}
};

JinChongxinCard::JinChongxinCard()
{
}

bool JinChongxinCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && !to_select->isKongcheng() && Self != to_select;
}

void JinChongxinCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();

	QList<ServerPlayer*> players;
	players << from << to;
	room->sortByActionOrder(players);

	foreach(ServerPlayer*p, players){
		if (p->isDead() || p->isNude()) continue;

		QList<const Card*> recasts;
		foreach(const Card*c, p->getCards("he")){
			if (p->isCardLimited(c, Card::MethodRecast, true)) continue;
			if (p->isCardLimited(c, Card::MethodRecast, false)) continue;
			recasts << c;
		}
		if (recasts.isEmpty()) continue;

		const Card*c = room->askForCard(p, "..", "@jinchongxin-recast", QVariant::fromValue(from), Card::MethodRecast);
		if (!c)
			c = recasts.at(qrand() % recasts.length());

		LogMessage log;
		log.type = "$RecastCard";
		log.from = p;
		log.card_str = c->toString();
		room->sendLog(log);

		room->moveCardTo(c, p, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, p->objectName(), "jinchongxin", ""));
		p->drawCards(1, "recast");
	}
}

class JinChongxin : public ZeroCardViewAsSkill
{
public:
	JinChongxin() : ZeroCardViewAsSkill("jinchongxin")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		bool recast = false;
		foreach(const Card*c, player->getHandcards() + player->getEquips()){
			if (player->isCardLimited(c, Card::MethodRecast, true)) continue;
			if (player->isCardLimited(c, Card::MethodRecast, false)) continue;
			recast = true;
			break;
		}
		return recast && !player->hasUsed("JinChongxinCard");
	}

	const Card*viewAs() const
	{
		return new JinChongxinCard;
	}
};

class JinDezhang : public PhaseChangeSkill
{
public:
	JinDezhang() : PhaseChangeSkill("jindezhang")
	{
		frequency = Wake;
		waked_skills = "jinweishu";
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if(player->canWake(objectName())||player->property("InitialHandCards").toList().isEmpty()){
			room->sendCompulsoryTriggerLog(player, this);
			room->doSuperLightbox(player, "jindezhang");
			room->addPlayerMark(player, objectName());
			if (room->changeMaxHpForAwakenSkill(player, -1, objectName()))
				room->acquireSkill(player, "jinweishu");
		}
		return false;
	}
};

class JinWeishu : public TriggerSkill
{
public:
	JinWeishu() : TriggerSkill("jinweishu")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.reason.m_reason == CardMoveReason::S_REASON_DRAW){
			if (move.to == player && move.to_place == Player::PlaceHand && move.from_places.contains(Player::DrawPile) &&
					move.reason.m_skillName != objectName() && player->getPhase() != Player::Draw){
				ServerPlayer*t = room->askForPlayerChosen(player, room->getAllPlayers(), objectName(), "@jinweishu-draw", false, true);
				player->peiyin(this);
				t->drawCards(1, objectName());
			}
		} else if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD){
			if (move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)) &&
					player->getPhase() != Player::Discard){
				for (int i = 0; i < move.card_ids.length(); i++){
					if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip){
						QList<ServerPlayer*> targets;
						foreach(ServerPlayer*p, room->getOtherPlayers(player)){
							if (player->canDiscard(p, "he"))
								targets << p;
						}
						if (targets.isEmpty()) break;
						ServerPlayer*t = room->askForPlayerChosen(player, targets, "jinweishu_dis", "@jinweishu-discard", false, true);
						player->peiyin(this);
						int id = room->askForCardChosen(player, t, "he", objectName(), false, Card::MethodDiscard);
						room->throwCard(id, t, player);
					}
				}
			}
		}
		return false;
	}
};

class Xianlve : public PhaseChangeSkill
{
public:
	Xianlve() : PhaseChangeSkill("xianlve")
	{
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isLord() && target->getPhase() == Player::Start;
	}

	static QString getOLdName(ServerPlayer*player)
	{
		QString old;
		foreach(QString mark, player->getMarkNames()){
			if (!mark.startsWith("&xianlve+:+") || player->getMark(mark) <= 0) continue;
			QStringList marks = mark.split("+");
			if (marks.length() != 3) continue;
			old = marks.last();
			break;
		}
		return old;
	}

	static void changeTrick(ServerPlayer*player, QString trick_name)
	{
		QString old = getOLdName(player);
		if (old == trick_name) return;
		Room*room = player->getRoom();
		if (!old.isEmpty())
			room->setPlayerMark(player, "&xianlve+:+" + old, 0);
		if (!trick_name.isEmpty())
			room->setPlayerMark(player, "&xianlve+:+" + trick_name, 1);
	}

	bool onPhaseChange(ServerPlayer*, Room*room) const
	{
		if (!isNormalGameMode(room->getMode()))
			return false;

		QList<int> all_tricks = Sanguosha->getRandomCards(), tricks;
		QStringList names;
		foreach(int id, all_tricks){
			const Card*c = Sanguosha->getEngineCard(id);
			if (!c->isKindOf("TrickCard")) continue;
			QString name = c->objectName();
			if (names.contains(name)) continue;
			names << name;
			tricks << id;
		}
		if (tricks.isEmpty()) return false;

		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			if (!p->askForSkillInvoke(this)) continue;
			room->broadcastSkillInvoke(objectName());
			room->fillAG(tricks, p);
			int id = room->askForAG(p, tricks, false, objectName());
			room->clearAG(p);
			changeTrick(p, Sanguosha->getEngineCard(id)->objectName());
		}
		return false;
	}
};

class XianlveEffect : public TriggerSkill
{
public:
	XianlveEffect() : TriggerSkill("#xianlve-effect")
	{
		events << CardFinished;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (!room->hasCurrent()) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("TrickCard")) return false;

		QString name = use.card->objectName();
		QList<int> all_tricks = Sanguosha->getRandomCards(), tricks;
		QStringList names;
		foreach(int id, all_tricks){
			const Card*c = Sanguosha->getEngineCard(id);
			if (!c->isKindOf("TrickCard")) continue;
			QString name = c->objectName();
			if (names.contains(name)) continue;
			names << name;
			tricks << id;
		}

		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (p->isDead() || !p->hasSkill("xianlve")) continue;
			if (p->getMark("xianlve_used-Clear") > 0) continue;
			QString trick = Xianlve::getOLdName(p);
			if (trick != name) continue;

			room->addPlayerMark(p, "xianlve_used-Clear");
			room->sendCompulsoryTriggerLog(p, "xianlve", true, true);

			QList<int> draw_ids = room->drawCardsList(p, 2, "xianlve"), ids;
			foreach(int id, draw_ids){
				if (p->hasCard(id))
					ids << id;
			}
			if (ids.isEmpty()) continue;

			QHash<ServerPlayer*, QStringList> hash;

			while (p->isAlive()){
				CardsMoveStruct yiji_move = room->askForYijiStruct(p, ids, objectName(), true, false, true, -1,
											room->getOtherPlayers(p), CardMoveReason(), "", false, false);
				if (!yiji_move.to || yiji_move.card_ids.isEmpty()) break;

				QStringList id_strings = hash[(ServerPlayer*)yiji_move.to];
				foreach(int id, yiji_move.card_ids){
					id_strings << QString::number(id);
					ids.removeOne(id);
				}
				hash[(ServerPlayer*)yiji_move.to] = id_strings;
				if (ids.isEmpty()) break;
			}

			QList<CardsMoveStruct> moves;
			foreach(ServerPlayer*pp, room->getOtherPlayers(p)){
				if (pp->isDead()) continue;
				QList<int> ids = ListS2I(hash[pp]);
				if (ids.isEmpty()) continue;
				hash.remove(pp);
				CardsMoveStruct move(ids, p, pp, Player::PlaceHand, Player::PlaceHand,
					CardMoveReason(CardMoveReason::S_REASON_GIVE, p->objectName(), pp->objectName(), "xianlve", ""));
				moves.append(move);
			}
			if (!moves.isEmpty())
				room->moveCardsAtomic(moves, false);

			if (p->isAlive() && !tricks.isEmpty()){
				room->fillAG(tricks, p);
				int id = room->askForAG(p, tricks, true, "xianlve");
				room->clearAG(p);
				QString name;
				if (id > 0)
					name = Sanguosha->getEngineCard(id)->objectName();
				Xianlve::changeTrick(p, name);
			}
		}
		return false;
	}
};

ZaowangCard::ZaowangCard()
{
}

bool ZaowangCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
	return targets.isEmpty();
}

void ZaowangCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*to = effect.to;
	Room*room = to->getRoom();

	room->removePlayerMark(effect.from, "@zaowangMark");
	room->doSuperLightbox(effect.from, "zaowang");

	room->gainMaxHp(to, 1, "zaowang");
	room->recover(to, RecoverStruct("zaowang", effect.from));
	to->drawCards(3, "zaowang");

	if (to->isDead()) return;
	room->setPlayerMark(to, "&zaowang", 1);
}

class ZaowangVS : public ZeroCardViewAsSkill
{
public:
	ZaowangVS() : ZeroCardViewAsSkill("zaowang")
	{
	}

	const Card*viewAs() const
	{
		return new ZaowangCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("@zaowangMark") > 0;
	}
};

class Zaowang : public TriggerSkill
{
public:
	Zaowang() : TriggerSkill("zaowang")
	{
		events << Death << BeforeGameOverJudge;
		view_as_skill = new ZaowangVS;
		frequency = Limited;
		limit_mark = "@zaowangMark";
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*, QVariant &data) const
	{
		if (!isNormalGameMode(room->getMode())) return false;
		DeathStruct death = data.value<DeathStruct>();

		if (event == BeforeGameOverJudge){
			if (!death.who->isLord()) return false;
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if (p->getRole() == "loyalist" && p->getMark("&zaowang") > 0){
					LogMessage log;
					log.type = "#ZhenguEffect";
					log.from = p;
					log.arg = "zaowang";
					room->sendLog(log);
					room->broadcastSkillInvoke("zaowang");

					room->setPlayerMark(p, "&zaowang", 0);
					room->setPlayerProperty(death.who, "role", "loyalist");
					room->setPlayerProperty(p, "role", "lord");
					break;
				}
			}
		} else {
			if (death.who->getRole() != "rebel" || death.who->getMark("&zaowang") <= 0) return false;
			if (!death.damage || !death.damage->from) return false;
			//if (!death.damage->from->getRole().startsWith("l")) return false;
			if (death.damage->from->getRole() == "lord" || death.damage->from->getRole() == "loyalist"){
				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = death.who;
				log.arg = "zaowang";
				room->sendLog(log);
				room->broadcastSkillInvoke("zaowang");
				room->gameOver("lord+loyalist");
			}
		}
		return false;
	}
};


class JinBihun : public TriggerSkill
{
public:
	JinBihun() : TriggerSkill("jinbihun")
	{
		events << TargetSpecifying;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard") || use.to.isEmpty() || player->getHandcardNum() <= player->getMaxCards()) return false;

		ServerPlayer*to = use.to.first();
		int num = use.to.length();
		if (num == 1 && to == player) return false;

		room->sendCompulsoryTriggerLog(player, this);

		bool contain = use.to.contains(player);
		use.to.clear();
		if (contain)
			use.to << player;
		data = QVariant::fromValue(use);

		if (num == 1 && to != player && to->isAlive())
			to->obtainCard(use.card);
		return false;
	}
};

JinJianheCard::JinJianheCard()
{
	will_throw = false;
	handling_method = Card::MethodRecast;
}

bool JinJianheCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	return targets.isEmpty() && to_select->getMark("jinjianheTarget-PlayClear") <= 0;
}

void JinJianheCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	room->addPlayerMark(to, "jinjianheTarget-PlayClear");

	int num = subcardsLength();
	QString type = Sanguosha->getCard(subcards.first())->getType();

	LogMessage log;
	log.type = "$RecastCard";
	log.from = from;
	log.card_str = ListI2S(subcards).join("+");
	room->sendLog(log);

	CardMoveReason reason(CardMoveReason::S_REASON_RECAST, from->objectName());
	reason.m_skillName = "jinjianhe";
	room->moveCardTo(this, from, nullptr, Player::DiscardPile, reason);
	//from->broadcastSkillInvoke("@recast");

	from->drawCards(num, "recast");

	if (to->isDead()) return;

	if (to->getCardCount() < num)
		room->damage(DamageStruct("jinjianhe", from, to, 1, DamageStruct::Thunder));
	else {
		QString pattern = ".";
		if (type == "basic")
			pattern = "BasicCard";
		else if (type == "equip")
			pattern = "EquipCard";
		else if (type == "trick")
			pattern = "TrickCard";
		const Card*ex = room->askForExchange(to, "jinjianhe", num, num, true,
			QString("@jinjianhe-recast:%1:%2:%3").arg(from->objectName()).arg(num).arg(type), true, pattern);
		if (!ex)
			room->damage(DamageStruct("jinjianhe", from, to, 1, DamageStruct::Thunder));
		else {
			QList<int> ex_ids = ex->getSubcards();

			LogMessage log;
			log.type = "$RecastCard";
			log.from = to;
			log.card_str = ListI2S(ex_ids).join("+");
			room->sendLog(log);

			CardMoveReason reason(CardMoveReason::S_REASON_RECAST, to->objectName());
			reason.m_skillName = "jinjianhe";
			room->moveCardTo(ex, to, nullptr, Player::DiscardPile, reason);
			//to->broadcastSkillInvoke("@recast");

			to->drawCards(num, "recast");
		}
	}
}

class JinJianhe : public ViewAsSkill
{
public:
	JinJianhe() : ViewAsSkill("jinjianhe")
	{
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (Self->isCardLimited(to_select, Card::MethodRecast)) return false;
		if (selected.isEmpty()) return true;
		if (to_select->sameNameWith(selected.first())) return true;
		if (to_select->isKindOf("EquipCard") && selected.first()->isKindOf("EquipCard")) return true;
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.length() < 2)
			return nullptr;

		JinJianheCard*c = new JinJianheCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return true;
	}
};

class JinChuanwu : public TriggerSkill
{
public:
	JinChuanwu() : TriggerSkill("jinchuanwu")
	{
		events << Damage << Damaged;
		frequency = Compulsory;
		waked_skills = "#jinchuanwu";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (!room->hasCurrent()) return false;

		int attack = player->getAttackRange();
		QStringList zhu_skills, fu_skills;

		const General*gen = player->getGeneral();
		int i = 0;
		foreach(const Skill*sk, gen->getSkillList()){
			if (sk->isAttachedLordSkill() || !sk->isVisible() || sk->inherits("SPConvertSkill")) continue;
			i++;
			if (player->hasSkill(sk, true))
				zhu_skills << sk->objectName();
			if (i >= attack)
				break;
		}

		const General*gen2 = player->getGeneral2();
		if (gen2){
			i = 0;
			foreach(const Skill*sk, gen2->getSkillList()){
				if (sk->isAttachedLordSkill() || !sk->isVisible() || sk->inherits("SPConvertSkill")) continue;
				i++;
				if (player->hasSkill(sk, true))
					fu_skills << sk->objectName();
				if (i >= attack)
					break;
			}
		}

		int zhu = zhu_skills.length(), fu = fu_skills.length();
		QStringList choices;
		if (zhu > 0)
			choices << QString("zhu=%1").arg(zhu);
		if (fu > 0)
			choices << QString("fu=%1").arg(fu);
		if (choices.isEmpty()) return false;

		room->sendCompulsoryTriggerLog(player, this);

		QString choice = room->askForChoice(player, objectName(), choices.join("+"), data, "", "tip");
		QStringList skills = player->getTag("JinChuanwuSkills").toStringList(), lose_skills, choice_skills = zhu_skills;
		if (choice.startsWith("fu"))
			choice_skills = fu_skills;
		foreach(QString sk, choice_skills){
			if (skills.contains(sk)) continue;
			skills << sk;
			lose_skills << "-" + sk;
		}
		if (!lose_skills.isEmpty()){
			player->setTag("JinChuanwuSkills", skills);
			room->handleAcquireDetachSkills(player, lose_skills);
			player->drawCards(lose_skills.length(), objectName());
		}
		return false;
	}
};

class JinChuanwuSkill : public TriggerSkill
{
public:
	JinChuanwuSkill() : TriggerSkill("#jinchuanwu")
	{
		events << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->isDead()) continue;
			QStringList skills = p->getTag("JinChuanwuSkills").toStringList();
			p->removeTag("JinChuanwuSkills");
			if (skills.isEmpty()) continue;
			room->sendCompulsoryTriggerLog(p, "jinchuanwu", true, true);
			room->handleAcquireDetachSkills(p, skills);
		}
		return false;
	}
};

class Jilei : public TriggerSkill
{
public:
	Jilei() : TriggerSkill("jilei")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*yangxiu, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		ServerPlayer*current = room->getCurrent();
		if (!current || current->getPhase() == Player::NotActive || current->isDead() || !damage.from)
			return false;

		if (room->askForSkillInvoke(yangxiu, objectName(), data)){
			QString choice = room->askForChoice(yangxiu, objectName(), "BasicCard+EquipCard+TrickCard");
			room->broadcastSkillInvoke(objectName());

			LogMessage log;
			log.type = "#Jilei";
			log.from = damage.from;
			log.arg = choice;
			room->sendLog(log);

			QStringList jilei_list = damage.from->getTag(objectName()).toStringList();
			if (jilei_list.contains(choice)) return false;
			jilei_list.append(choice);
			damage.from->setTag(objectName(), QVariant::fromValue(jilei_list));
			QString _type = choice + "|.|.|hand"; // Handcards only
			room->setPlayerCardLimitation(damage.from, "use,response,discard", _type, true);

			QString type_name = choice.replace("Card", "").toLower();
			if (damage.from->getMark("@jilei_" + type_name) == 0)
				room->addPlayerMark(damage.from, "@jilei_" + type_name);
		}

		return false;
	}
};

class JileiClear : public TriggerSkill
{
public:
	JileiClear() : TriggerSkill("#jilei-clear")
	{
		events << EventPhaseChanging << Death;
	}

	int getPriority(TriggerEvent) const
	{
		return 5;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*target, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive)
				return false;
		} else if (triggerEvent == Death){
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != target || target != room->getCurrent())
				return false;
		}
		QList<ServerPlayer*> players = room->getAllPlayers();
		foreach(ServerPlayer*player, players){
			QStringList jilei_list = player->getTag("jilei").toStringList();
			if (!jilei_list.isEmpty()){
				LogMessage log;
				log.type = "#JileiClear";
				log.from = player;
				room->sendLog(log);

				foreach(QString jilei_type, jilei_list){
					room->removePlayerCardLimitation(player, "use,response,discard", jilei_type + "|.|.|hand$1");
					QString type_name = jilei_type.replace("Card", "").toLower();
					room->setPlayerMark(player, "@jilei_" + type_name, 0);
				}
				player->removeTag("jilei");
			}
		}

		return false;
	}
};

class Danlao : public TriggerSkill
{
public:
	Danlao() : TriggerSkill("danlao")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.to.length() <= 1 || !use.to.contains(player)
			|| !use.card->isKindOf("TrickCard")
			|| !room->askForSkillInvoke(player, objectName(), data))
			return false;

		room->broadcastSkillInvoke(objectName());
		player->setFlags("-DanlaoTarget");
		player->setFlags("DanlaoTarget");
		player->drawCards(1, objectName());
		if (player->isAlive() && player->hasFlag("DanlaoTarget")){
			player->setFlags("-DanlaoTarget");
			use.nullified_list << player->objectName();
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

ShefuCard::ShefuCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void ShefuCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	QString mark = "Shefu_" + user_string;
	source->setMark(mark, getEffectiveId() + 1);

	JsonArray arg;
	arg << source->objectName() << mark << (getEffectiveId() + 1);
	room->doNotify(source, QSanProtocol::S_COMMAND_SET_MARK, arg);

	source->addToPile("ambush", this, false);

	LogMessage log;
	log.type = "$ShefuRecord";
	log.from = source;
	log.card_str = QString::number(getEffectiveId());
	log.arg = user_string;
	room->sendLog(log, source);
}

ShefuDialog*ShefuDialog::getInstance(const QString &object)
{
	static ShefuDialog*instance;
	if (instance == nullptr || instance->objectName() != object)
		instance = new ShefuDialog(object);
	return instance;
}

ShefuDialog::ShefuDialog(const QString &object)
	: GuhuoDialog(object, true, true, false, true, true)
{
}

bool ShefuDialog::isButtonEnabled(const QString &button_name) const
{
	return Self->getMark("Shefu_" + button_name)<1;
}

class ShefuViewAsSkill : public OneCardViewAsSkill
{
public:
	ShefuViewAsSkill() : OneCardViewAsSkill("shefu")
	{
		filter_pattern = ".|.|.|hand";
		response_pattern = "@@shefu";
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.contains("@@shefu");
	}

	const Card*viewAs(const Card*originalCard) const
	{
		const Card*c = Self->getTag("shefu").value<const Card*>();
		if (c){
			if (Self->getMark("Shefu_" + c->objectName()) > 0)
				return nullptr;
			ShefuCard*card = new ShefuCard;
			card->setUserString(c->objectName());
			card->addSubcard(originalCard);
			return card;
		}
		return nullptr;
	}
};

class Shefu : public TriggerSkill
{
public:
	Shefu() : TriggerSkill("shefu")
	{
		events << CardUsed << EventPhaseStart;
		view_as_skill = new ShefuViewAsSkill;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId() != Card::TypeBasic && use.card->getTypeId() != Card::TypeTrick)
				return false;
			QString card_name = use.card->objectName();
			if (card_name.contains("slash")) card_name = "slash";
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (ShefuTriggerable(p)){
					room->setTag("ShefuData", data);
					if (p->getMark("Shefu_" + card_name)<1||!p->askForSkillInvoke("shefu_cancel", "data:::" + card_name,false))
						continue;

					room->broadcastSkillInvoke("shefu", 2, p);

					LogMessage log;
					log.type = "#ShefuEffect";
					log.from = p;
					log.to << player;
					log.arg = card_name;
					log.arg2 = "shefu";
					room->sendLog(log);

					CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "shefu", "");
					int id = p->getMark("Shefu_" + card_name) - 1;
					room->setPlayerMark(p, "Shefu_" + card_name, 0);
					room->throwCard(Sanguosha->getCard(id), reason, nullptr);

					use.nullified_list << "_ALL_TARGETS";
					data = QVariant::fromValue(use);
				}
			}
		}else if(player->getPhase() == Player::Finish && !player->isKongcheng() && player->hasSkill(objectName())){
			room->askForUseCard(player, "@@shefu", "@shefu-prompt", -1, Card::MethodNone);
		}
		return false;
	}

	QDialog*getDialog() const
	{
		return ShefuDialog::getInstance(objectName());
	}

	int getEffectIndex(const ServerPlayer*, const Card*) const
	{
		return 1;
	}

private:
	bool ShefuTriggerable(ServerPlayer*chengyu) const
	{
		return !chengyu->hasFlag("CurrentPlayer")
			&& chengyu->hasSkill("shefu") && !chengyu->getPile("ambush").isEmpty();
	}
};

class BenyuViewAsSkill : public ViewAsSkill
{
public:
	BenyuViewAsSkill() : ViewAsSkill("benyu")
	{
		response_pattern = "@@benyu";
	}

	bool viewFilter(const QList<const Card*> &, const Card*to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.length() < Self->getMark("benyu"))
			return nullptr;

		DummyCard*card = new DummyCard;
		card->addSubcards(cards);
		return card;
	}
};

class Benyu : public MasochismSkill
{
public:
	Benyu() : MasochismSkill("benyu")
	{
		view_as_skill = new BenyuViewAsSkill;
	}

	void onDamaged(ServerPlayer*target, const DamageStruct &damage) const
	{
		if (!damage.from || damage.from->isDead())
			return;
		Room*room = target->getRoom();
		int from_handcard_num = damage.from->getHandcardNum(), handcard_num = target->getHandcardNum();
		QVariant data = QVariant::fromValue(damage);
		if (handcard_num == from_handcard_num){
			return;
		} else if (handcard_num < from_handcard_num && handcard_num < 5 && room->askForSkillInvoke(target, objectName(), data)){
			room->broadcastSkillInvoke(objectName(), 1);
			room->drawCards(target, qMin(5, from_handcard_num) - handcard_num, objectName());
		} else if (handcard_num > from_handcard_num){
			room->setPlayerMark(target, objectName(), from_handcard_num + 1);
			//if (room->askForUseCard(target, "@@benyu", QString("@benyu-discard::%1:%2").arg(damage.from->objectName()).arg(from_handcard_num + 1), -1, Card::MethodDiscard)) 
			if (room->askForCard(target, "@@benyu", QString("@benyu-discard::%1:%2").arg(damage.from->objectName()).arg(from_handcard_num + 1), QVariant(), objectName())){
				room->broadcastSkillInvoke(objectName(), 2);
				room->damage(DamageStruct(objectName(), target, damage.from));
			}
		}
		return;
	}
};

class Bingzheng : public TriggerSkill
{
public:
	Bingzheng() : TriggerSkill("bingzheng")
	{
		events << EventPhaseEnd;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (player->getPhase() != Player::Play) return false;
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (p->getHp() != p->getHandcardNum())
				targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@bingzheng-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		QStringList choices;
		if (!target->isKongcheng())
			choices << "discard";
		choices << "draw";
		QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target));
		if (choice == "draw")
			target->drawCards(1, objectName());
		else {
			if (!target->canDiscard(target, "h")) return false;
			room->askForDiscard(target, objectName(), 1, 1);
		}
		if (target->isAlive() && player->isAlive() && target->getHp() == target->getHandcardNum()){
			player->drawCards(1, objectName());
			if (player->isNude() || player == target) return false;
			QList<ServerPlayer*> players;
			players << target;
			QList<int> give = player->handCards() + player->getEquipsId();
			room->askForYiji(player, give, objectName(), false, false, true, -1, players, CardMoveReason(),
							"bingzheng-give:" + target->objectName());
		}
		return false;
	}
};

class SheyanVS : public ZeroCardViewAsSkill
{
public:
	SheyanVS() : ZeroCardViewAsSkill("sheyan")
	{
		response_pattern = "@@sheyan!";
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	const Card*viewAs() const
	{
		return new ExtraCollateralCard;
	}
};

class Sheyan : public TriggerSkill
{
public:
	Sheyan() : TriggerSkill("sheyan")
	{
		events << TargetConfirming;
		view_as_skill = new SheyanVS;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isNDTrick()) return false;

		QList<ServerPlayer*> ava;
		room->setCardFlag(use.card, "tunan_distance");
		foreach(ServerPlayer*p, room->getOtherPlayers(use.from)){
			if (use.to.contains(p)) continue;
			if (use.from->canUse(use.card,p))
				ava << p;
		}
		room->setCardFlag(use.card, "-tunan_distance");
		QStringList choices;
		if(ava.length()>0) choices << "add";
		if(use.to.length()>1) choices << "remove";
		if (choices.isEmpty()) return false;
		choices << "cancel";

		QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		if (choice == "cancel") return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		LogMessage log;
		log.type = "#QiaoshuiAdd";
		log.from = player;
		log.card_str = use.card->toString();
		log.arg = "sheyan";
		if (choice == "add"){
			log.to << room->askForPlayerChosen(player, ava, objectName(), "@sheyan-add:" + use.card->objectName());
			use.to << log.to;
			room->sortByActionOrder(use.to);
		} else {
			log.type = "#QiaoshuiRemove";
			log.to << room->askForPlayerChosen(player, use.to, objectName(), "@sheyan-remove:" + use.card->objectName());
			use.to.removeOne(log.to.first());
		}
		room->sendLog(log);
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), log.to.first()->objectName());
		data = QVariant::fromValue(use);
		return false;
	}
};

BifaCard::BifaCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool BifaCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select->getPile("bifa").isEmpty() && to_select != Self;
}

void BifaCard::use(Room*, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	ServerPlayer*target = targets.first();
	target->setTag("BifaSource" + QString::number(getEffectiveId()), QVariant::fromValue(source));
	target->addToPile("bifa", this, false);
}

class BifaViewAsSkill : public OneCardViewAsSkill
{
public:
	BifaViewAsSkill() : OneCardViewAsSkill("bifa")
	{
		filter_pattern = ".|.|.|hand";
		response_pattern = "@@bifa";
	}

	const Card*viewAs(const Card*originalcard) const
	{
		Card*card = new BifaCard;
		card->addSubcard(originalcard);
		return card;
	}
};

class Bifa : public TriggerSkill
{
public:
	Bifa() : TriggerSkill("bifa")
	{
		events << EventPhaseStart;
		view_as_skill = new BifaViewAsSkill;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (TriggerSkill::triggerable(player) && player->getPhase() == Player::Finish && !player->isKongcheng()){
			room->askForUseCard(player, "@@bifa", "@bifa-remove", -1, Card::MethodNone);
		} else if (player->getPhase() == Player::RoundStart && player->getPile("bifa").length() > 0){
			int card_id = player->getPile("bifa").first();
			ServerPlayer*chenlin = player->getTag("BifaSource" + QString::number(card_id)).value<ServerPlayer*>();
			QList<int> ids;
			ids << card_id;

			LogMessage log;
			log.type = "$BifaView";
			log.from = player;
			log.card_str = QString::number(card_id);
			log.arg = "bifa";
			room->sendLog(log, player);

			room->fillAG(ids, player);
			const Card*cd = Sanguosha->getCard(card_id);
			QString pattern;
			if (cd->isKindOf("BasicCard"))
				pattern = "BasicCard";
			else if (cd->isKindOf("TrickCard"))
				pattern = "TrickCard";
			else if (cd->isKindOf("EquipCard"))
				pattern = "EquipCard";
			QVariant data_for_ai = QVariant::fromValue(pattern);
			pattern.append("|.|.|hand");
			const Card*to_give = nullptr;
			if (!player->isKongcheng() && chenlin && chenlin->isAlive())
				to_give = room->askForCard(player, pattern, "@bifa-give", data_for_ai, Card::MethodNone, chenlin);
			if (chenlin && to_give){
				room->broadcastSkillInvoke(objectName(), 2);
				CardMoveReason reasonG(CardMoveReason::S_REASON_GIVE, player->objectName(), chenlin->objectName(), "bifa", "");
				room->obtainCard(chenlin, to_give, reasonG, false);
				CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, player->objectName(), "bifa", "");
				room->obtainCard(player, cd, reason, false);
			} else {
				room->broadcastSkillInvoke(objectName(), 3);
				CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", objectName(), "");
				room->throwCard(cd, reason, nullptr);
				room->loseHp(HpLostStruct(player, 1, "bifa", chenlin));
			}
			room->clearAG(player);
			player->removeTag("BifaSource" + QString::number(card_id));
		}
		return false;
	}

	int getEffectIndex(const ServerPlayer*, const Card*) const
	{
		return 1;
	}
};

SongciCard::SongciCard()
{
	mute = true;
}

bool SongciCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select->getMark("songci" + Self->objectName()) == 0 && to_select->getHandcardNum() != to_select->getHp();
}

void SongciCard::onEffect(CardEffectStruct &effect) const
{
	int handcard_num = effect.to->getHandcardNum();
	int hp = effect.to->getHp();
	Room*room = effect.from->getRoom();
	room->setPlayerMark(effect.to, "@songci", 1);
	room->addPlayerMark(effect.to, "songci" + effect.from->objectName());
	if (handcard_num > hp){
		room->broadcastSkillInvoke("songci", 2);
		room->askForDiscard(effect.to, "songci", 2, 2, false, true);
	} else if (handcard_num < hp){
		room->broadcastSkillInvoke("songci", 1);
		effect.to->drawCards(2, "songci");
	}
}

class SongciViewAsSkill : public ZeroCardViewAsSkill
{
public:
	SongciViewAsSkill() : ZeroCardViewAsSkill("songci")
	{
	}

	const Card*viewAs() const
	{
		return new SongciCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		if (player->getMark("songci" + player->objectName()) == 0 && player->getHandcardNum() != player->getHp()) return true;
		foreach(const Player*sib, player->getAliveSiblings())
			if (sib->getMark("songci" + player->objectName()) == 0 && sib->getHandcardNum() != sib->getHp())
				return true;
		return false;
	}
};

class Songci : public TriggerSkill
{
public:
	Songci() : TriggerSkill("songci")
	{
		events << Death;
		view_as_skill = new SongciViewAsSkill;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target && target->hasSkill(objectName());
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who != player) return false;
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->getMark("@songci") > 0)
				room->setPlayerMark(p, "@songci", 0);
			if (p->getMark("songci" + player->objectName()) > 0)
				room->setPlayerMark(p, "songci" + player->objectName(), 0);
		}
		return false;
	}
};

class OLBiluan : public PhaseChangeSkill
{
public:
	OLBiluan() : PhaseChangeSkill("olbiluan")
	{
	}

	bool onPhaseChange(ServerPlayer*target, Room*room) const
	{
		if(target->getPhase()==Player::Finish&&target->canDiscard(target,"he")){
			foreach(ServerPlayer*p, room->getOtherPlayers(target)){
				if(p->distanceTo(target)==1){
					if(room->askForCard(target,"..","olbiluan0:",QVariant(),objectName())){
						target->peiyin(this);
						room->addPlayerMark(target, "olbiluanUse");
					}
					break;
				}
			}
		}
		return false;
	}
};

class OLBiluanDist : public DistanceSkill
{
public:
	OLBiluanDist() : DistanceSkill("#olbiluan-dist")
	{
	}

	int getCorrect(const Player*, const Player*to) const
	{
		int n = -to->getMark("ollixiaUse");
		if (to->getMark("olbiluanUse")>0){
			QSet<QString> kingdoms;
			foreach(const Player*p, to->getAliveSiblings())
				kingdoms.insert(p->getKingdom());
			n += kingdoms.count()*to->getMark("olbiluanUse");
		}
		return n;
	}
};

class OLLixia : public PhaseChangeSkill
{
public:
	OLLixia() : PhaseChangeSkill("ollixia")
	{
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool onPhaseChange(ServerPlayer*target, Room*room) const
	{
		if(target->getPhase()==Player::Finish){
			foreach(ServerPlayer*p, room->getOtherPlayers(target)){
				if (p->hasSkill(objectName()) && !target->inMyAttackRange(p)){
					room->sendCompulsoryTriggerLog(p,this);
					QStringList choices,choices2;
					choices << "ollixia1" << "ollixia2";
					if(target->isWounded()) choices << "ollixia3";
					for (int i = 0; i < 2; i++){
						QString choice = room->askForChoice(p,objectName(),choices.join("+"),QVariant::fromValue(target));
						choices.removeOne(choice);
						choices << "cancel";
						choices2 << choice;
					}
					if(choices2.contains("ollixia1"))
						p->drawCards(1, objectName());
					if(choices2.contains("ollixia2"))
						target->drawCards(2, objectName());
					if(choices2.contains("ollixia3"))
						room->recover(target, RecoverStruct(objectName(),p));
					room->addPlayerMark(p, "ollixiaUse");
				}
			}
		}
		return false;
	}
};

OLLianjiCard::OLLianjiCard()
{
}

void OLLianjiCard::onEffect(CardEffectStruct &effect) const
{
	if (!effect.to->hasWeaponArea()) return;
	Room*room = effect.from->getRoom();
	QList<const Card*> weapons;
	foreach(int id, room->getDrawPile()){
		const Card*card = Sanguosha->getCard(id);
		if (!card->isKindOf("Weapon")) continue;
		if (!effect.to->canUse(card)) continue;
		weapons << card;
	}
	if (weapons.isEmpty()) return;

	const Card*weapon = weapons.at(qrand() % weapons.length());
	room->useCard(CardUseStruct(weapon, effect.to));

	if (effect.to->isDead() || effect.from->isDead()) return;
	QList<ServerPlayer*> tos;
	foreach(ServerPlayer*p, room->getOtherPlayers(effect.to)){
		if (effect.to->canSlash(p, nullptr, true))
			tos << p;
	}
	if (tos.isEmpty()) return;

	ServerPlayer*to = room->askForPlayerChosen(effect.from, tos, "ollianji", "@ollianji-target:" + effect.to->objectName());
	LogMessage log;
	log.type = "#CollateralSlash";
	log.from = effect.from;
	log.to << to;
	room->sendLog(log);
	room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, effect.to->objectName(), to->objectName());

	effect.to->setTag("ollianji_weapon", QVariant::fromValue(weapon)); //FOR AI

	if (room->askForUseSlashTo(effect.to, to, "@ollianji-slash:" + to->objectName(), true, false, false, effect.from, this,
							"ollianji_slash_" + effect.from->objectName()))
		effect.to->removeTag("ollianji_weapon");
	else {
		effect.to->removeTag("ollianji_weapon");
		if (effect.from->isDead() || effect.to->isDead() || !effect.to->getWeapon()) return;

		const Card*weapon2 = Sanguosha->getCard(effect.to->getWeapon()->getEffectiveId());

		effect.from->setTag("ollianji_give_weapon", QVariant::fromValue(weapon2)); //FOR AI
		ServerPlayer*give = room->askForPlayerChosen(effect.from, room->getAlivePlayers(), "ollianji_give",
													"@ollianji-give:" + weapon->objectName());
		effect.from->removeTag("ollianji_give_weapon");
		room->giveCard(effect.from, give, weapon2, "ollianji", true);
	}

}

class OLLianji : public OneCardViewAsSkill
{
public:
	OLLianji() : OneCardViewAsSkill("ollianji")
	{
		filter_pattern = ".|.|.|hand!";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("OLLianjiCard");
	}

	const Card*viewAs(const Card*originalcard) const
	{
		OLLianjiCard*card = new OLLianjiCard;
		card->addSubcard(originalcard);
		return card;
	}
};

class OLMoucheng : public PhaseChangeSkill
{
public:
	OLMoucheng() : PhaseChangeSkill("olmoucheng")
	{
		frequency = Wake;
		waked_skills = "tenyearjingong";
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::RoundStart
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getMark("&ollianji") >= 3){
			LogMessage log;
			log.type = "#OLMouchengWake";
			log.from = player;
			log.arg = QString::number(player->getMark("&ollianji"));
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());

		room->doSuperLightbox(player, "olmoucheng");
		room->setPlayerMark(player, "olmoucheng", 1);

		if (room->changeMaxHpForAwakenSkill(player, 0, objectName()))
			room->handleAcquireDetachSkills(player, "-ollianji|tenyearjingong");
		return false;
	}
};

class OLMouchengUse : public TriggerSkill
{
public:
	OLMouchengUse() : TriggerSkill("#olmoucheng-use")
	{
		events << PreCardUsed;
		//frequency = Wake;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (!data.value<CardUseStruct>().card->isKindOf("OLLianjiCard")) return false;
		if (!player->hasSkill("ollianji", true)) return false;
		room->addPlayerMark(player, "&ollianji");
		return false;
	}
};

class OLJuyi : public PhaseChangeSkill
{
public:
	OLJuyi() : PhaseChangeSkill("oljuyi")
	{
		frequency = Wake;
		waked_skills = "benghuai,olweizhong";
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*zhugedan, Room*room) const
	{
		if (zhugedan->getMaxHp() > zhugedan->aliveCount()){
			LogMessage log;
			log.type = "#JuyiWake";
			log.from = zhugedan;
			log.arg = QString::number(zhugedan->getMaxHp());
			log.arg2 = QString::number(zhugedan->aliveCount());
			log.arg3 = objectName();
			room->sendLog(log);
		}else if(!zhugedan->canWake(objectName()))
			return false;
		zhugedan->peiyin(objectName());
		room->notifySkillInvoked(zhugedan, objectName());
		room->doSuperLightbox(zhugedan, "oljuyi");

		room->setPlayerMark(zhugedan, "oljuyi", 1);
		if (room->changeMaxHpForAwakenSkill(zhugedan, 0, objectName())){
			room->drawCards(zhugedan, zhugedan->getMaxHp(), objectName());
			room->handleAcquireDetachSkills(zhugedan, "benghuai|olweizhong");
		}
		return false;
	}
};

class OLWeizhong : public TriggerSkill
{
public:
	OLWeizhong() : TriggerSkill("olweizhong")
	{
		events << MaxHpChanged;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		room->sendCompulsoryTriggerLog(player, this);

		int x = 1, hand = player->getHandcardNum();
		bool min = true;

		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (p->getHandcardNum() < hand){
				min = false;
				break;
			}
		}
		if (min)
			x = 2;

		player->drawCards(x, objectName());
		return false;
	}
};

class Zhidao : public TriggerSkill
{
public:
	Zhidao() : TriggerSkill("zhidao")
	{
		events << Damage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getPhase() != Player::Play || player->getMark("zhidao-PlayClear") > 0) return false;
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to == player) return false;
		room->addPlayerMark(player, "zhidao-PlayClear");

		if (damage.to->isDead() || damage.to->isAllNude()) return false;
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		DummyCard get;
		if (!damage.to->isKongcheng()){
			int id = room->askForCardChosen(player, damage.to, "h", objectName());
			get.addSubcard(id);
		}
		if (!damage.to->getEquips().isEmpty()){
			int id = room->askForCardChosen(player, damage.to, "e", objectName());
			get.addSubcard(id);
		}
		if (!damage.to->getJudgingArea().isEmpty()){
			int id = room->askForCardChosen(player, damage.to, "j", objectName());
			get.addSubcard(id);
		}
		if (get.subcardsLength() > 0){
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
			room->obtainCard(player, &get, reason, false);
			room->addPlayerMark(player, "zhidao-Clear");
		}
		return false;
	}
};

class ZhidaoPro : public ProhibitSkill
{
public:
	ZhidaoPro() : ProhibitSkill("#zhidao-pro")
	{
	}

	bool isProhibited(const Player*from, const Player*to, const Card*card, const QList<const Player*> &) const
	{
		return from->getMark("zhidao-Clear") > 0 && from != to && !card->isKindOf("SkillCard");
	}
};

class SpJili : public TriggerSkill
{
public:
	SpJili() : TriggerSkill("spjili")
	{
		events << TargetConfirming;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard") || !use.card->isRed()) return false;
		if (!use.card->isKindOf("BasicCard") && !use.card->isNDTrick()) return false;

		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->isDead() || use.to.contains(p) || use.from == p || !p->hasSkill(objectName())) continue;
			if (player->distanceTo(p) != 1) return false;
			if (use.from && use.from->isAlive() && use.from->isProhibited(p, use.card)) continue;
			int n = 1;
			if (use.to.contains(use.from))
				n = 2;
			//room->sendCompulsoryTriggerLog(p, objectName(), true, true, n);
			LogMessage log;
			log.type = "#SPJiliAdd";
			log.from = p;
			log.to << use.from;
			log.arg = objectName();
			log.arg2 = use.card->objectName();
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName(), n);
			room->doAnimate(1, use.from->objectName(), p->objectName());
			room->notifySkillInvoked(p, objectName());

			use.to.append(p);
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Wangong : public TriggerSkill
{
public:
	Wangong() : TriggerSkill("wangong")
	{
		events << PreCardUsed << ConfirmDamage << EventAcquireSkill;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card||!damage.card->hasFlag("wangong_ConfirmDamage")) return false;
			LogMessage log;
			log.type = "#WangongDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());
			data = QVariant::fromValue(damage);
		} else if (event == PreCardUsed){
			if (player->getMark("&wangong") + player->getMark("wangong") <= 0) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash")) return false;
			room->setCardFlag(use.card, "wangong_ConfirmDamage");
			room->setPlayerMark(player, "&wangong", 0);
			player->setMark("wangong", 0);
			use.m_addHistory = false;
			data = QVariant::fromValue(use);
		} else {
			if (data.toString() != objectName()) return false;
			if (player->getMark("wangong") <= 0) return false;
			room->setPlayerMark(player, "&wangong", 1);
			player->setMark("wangong", 0);
		}
		return false;
	}
};

class WangongRecord : public TriggerSkill
{
public:
	WangongRecord() : TriggerSkill("#wangong-record")
	{
		events << CardFinished << EventLoseSkill;
        global = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventLoseSkill){
			if (data.toString() != "wangong") return false;
			int n = player->getMark("&wangong");
			if (n <= 0) return false;
			room->setPlayerMark(player, "&wangong", 0);
			player->setMark("wangong", 1);
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (use.card->isKindOf("BasicCard")){
				if (player->hasSkill("wangong", true))
					room->setPlayerMark(player, "&wangong", 1);
				else
					player->setMark("wangong", 1);
			} else {
				room->setPlayerMark(player, "&wangong", 0);
				player->setMark("wangong", 0);
			}
		}
		return false;
	}
};

static QHash<QString,JuguanDialog*>JuguanDialogs;

JuguanDialog*JuguanDialog::getInstance(const QString &object, const QString &card_names)
{
	if (JuguanDialogs[object] == nullptr)
		JuguanDialogs[object] = new JuguanDialog(object, card_names);

	return JuguanDialogs[object];
}

JuguanDialog::JuguanDialog(const QString &object, const QString &card_names)
	: cards(card_names)
{
	setObjectName(object);
	setWindowTitle(Sanguosha->translate(object));
	group = new QButtonGroup(this);

	button_layout = new QVBoxLayout;
	setLayout(button_layout);
	connect(group, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(selectCard(QAbstractButton*)));
}

bool JuguanDialog::isButtonEnabled(const QString &button_name) const
{
	QString mark = objectName() + "_juguan_remove_" + button_name;
	foreach(QString m, Self->getMarkNames()){
		if (m.startsWith(mark) && Self->getMark(m) > 0)
			return false;
	}
	if(Self->isLocked(map[button_name])) return false;
	return cards.startsWith("$")||Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_PLAY||map[button_name]->isAvailable(Self);
}

void JuguanDialog::popup()
{
	Self->removeTag(objectName());

	if (cards.isEmpty()||(!cards.endsWith("!")&&Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_PLAY)){
		emit onButtonClick();
		return;
	}
	foreach(QAbstractButton*button, group->buttons()){
		button_layout->removeWidget(button);
		delete map[button->objectName()];
		map.remove(button->objectName());
		group->removeButton(button);
		delete button;
	}

	QString cards2 = cards;
	cards2.remove("!");
	cards2.remove("$");
	foreach(QString name, cards2.split(",")){
		if(name=="all_slashs"){
			foreach(QString name, Sanguosha->getSlashNames()){
				Card*card = Sanguosha->cloneCard(name);
				if (card) {
					card->setParent(this);
					card->setSkillName(objectName());
					button_layout->addWidget(createButton(card));
				}
			}
			continue;
		}
		Card*card = Sanguosha->cloneCard(name);
		if (card) {
			card->setParent(this);
			card->setSkillName(objectName());
			button_layout->addWidget(createButton(card));
		}
	}

	bool has_enabled_button = false;
	foreach(QAbstractButton*button, group->buttons()){
		bool enabled = isButtonEnabled(button->objectName());
		if (enabled) has_enabled_button = true;
		button->setEnabled(enabled);
	}
	if (!has_enabled_button){
		emit onButtonClick();
		return;
	}
	exec();
}

void JuguanDialog::selectCard(QAbstractButton*button)
{
	Self->setTag(objectName(), QVariant::fromValue(map[button->objectName()]));
	emit onButtonClick();
	accept();
}

QAbstractButton*JuguanDialog::createButton(const Card*card)
{
	QCommandLinkButton*button = new QCommandLinkButton(Sanguosha->translate(card->objectName()));
	button->setObjectName(card->objectName());
	button->setToolTip(card->getDescription());

	map.insert(card->objectName(), card);
	group->addButton(button);
	return button;
}

JuguanCard::JuguanCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool JuguanCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	const Card*_card = Self->getTag("juguan").value<const Card*>();
	if (_card == nullptr)
		return false;

	Card*card = Sanguosha->cloneCard(_card->objectName());
	card->setCanRecast(false);
	card->deleteLater();
	return card && card->targetFilter(targets, to_select, Self);
}

void JuguanCard::onUse(Room*room, CardUseStruct &card_use) const
{
	QString str = getUserString();
	if (str.isEmpty()) return;
	Card*card = Sanguosha->cloneCard(str);
	if (!card) return;
	card->addSubcards(subcards);
	card->setSkillName("juguan");
	room->setCardFlag(card, "juguan:" + card_use.from->objectName());
	card->deleteLater();
	room->useCard(CardUseStruct(card, card_use.from, card_use.to), true);
}

class JuguanVS : public OneCardViewAsSkill
{
public:
	JuguanVS() : OneCardViewAsSkill("juguan")
	{
		response_or_use = true;
	}

	bool viewFilter(const Card*to_select) const
	{
		if (to_select->isEquipped()) return false;
		const Card*c = Self->getTag("juguan").value<const Card*>();
		if (!c || !c->isAvailable(Self)) return false;
		Card*card = Sanguosha->cloneCard(c->objectName());
		if (!card) return false;
		card->addSubcard(to_select);
		card->setSkillName("juguan");
		card->deleteLater();
		return card->isAvailable(Self) && !Self->isLocked(card, true);
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("JuguanCard") && !player->isKongcheng();
	}

	const Card*viewAs(const Card*originalCard) const
	{
		const Card*c = Self->getTag("juguan").value<const Card*>();
		if (c && c->isAvailable(Self)){
			JuguanCard*card = new JuguanCard;
			card->addSubcard(originalCard);
			card->setUserString(c->objectName());
			return card;
		}
		return nullptr;
	}
};

class Juguan : public TriggerSkill
{
public:
	Juguan() : TriggerSkill("juguan")
	{
		events << DamageDone << EventPhaseChanging << DrawNCards;
		view_as_skill = new JuguanVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	QDialog*getDialog() const
	{
		return JuguanDialog::getInstance(objectName(), "slash,duel");
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))){
				QString name;
				foreach(QString flag, damage.card->getFlags()){
					if (!flag.startsWith("juguan:")) continue;
					QStringList flags = flag.split(":");
					if (flags.length() != 2) continue;
					name = flags.last();
					break;
				}
				if (!name.isEmpty())
					room->addPlayerMark(damage.to, "&juguan+#" + name + "-Keep");
			}
			if (damage.from)
				room->setPlayerMark(damage.from, "&juguan+#" + damage.to->objectName() + "-Keep", 0);
		} else if (event == EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::RoundStart) return false;
			int n = 0;
			QString mark = "&juguan+#" + player->objectName() + "-Keep";
			foreach(ServerPlayer*p, room->getAllPlayers(true)){
				if (p->getMark(mark) > 0){
					room->setPlayerMark(p, mark, 0);
					n++;
				}
			}
			if (n > 0 && player->isAlive())
				room->addPlayerMark(player, "juguan_draw-Clear", n);
		} else {
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase") return false;
			int mark = 2* player->getMark("juguan_draw-Clear");
			if (mark < 1) return false;
			room->setPlayerMark(player, "juguan_draw-Clear", 0);

			LogMessage log;
			log.type = "#JuguanExtraDraw";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(mark);
			room->sendLog(log);
			room->broadcastSkillInvoke(this);
			room->notifySkillInvoked(player, objectName());

			draw.num += mark;
			data = QVariant::fromValue(draw);
		}
		return false;
	}
};


class Yuanchou : public TriggerSkill
{
public:
	Yuanchou() : TriggerSkill("yuanchou")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash") || !use.card->isBlack()) return false;

		QList<ServerPlayer*> players;
		if (player->isAlive() && player->hasSkill(objectName()))
			players << player;
		foreach(ServerPlayer*p, use.to){
			if (p->hasSkill(objectName()) && p != use.from)
				players << p;
		}
		room->sortByActionOrder(players);
		foreach(ServerPlayer*p, players)
			room->sendCompulsoryTriggerLog(p, this);

		QList<ServerPlayer*> playerss;
		if (players.contains(player)){
			foreach(ServerPlayer*p, use.to){
				playerss << p;
				p->addQinggangTag(use.card);
			}
		}
		foreach(ServerPlayer*p, players){
			if (p == player || playerss.contains(p)) continue;
			p->addQinggangTag(use.card);
		}
		return false;
	}
};

class JueshengVS : public ZeroCardViewAsSkill
{
public:
	JueshengVS() : ZeroCardViewAsSkill("juesheng")
	{
	}

	const Card*viewAs() const
	{
		Duel*duel = new Duel(Card::NoSuit, 0);
		duel->setSkillName("juesheng");
		return duel;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		Duel*duel = new Duel(Card::NoSuit, 0);
		duel->setSkillName("juesheng");
		duel->deleteLater();
		return duel->isAvailable(player) && player->getMark("@jueshengMark") > 0;
	}
};

class Juesheng : public TriggerSkill
{
public:
	Juesheng() : TriggerSkill("juesheng")
	{
		events << CardUsed << DamageCaused;
		frequency = Limited;
		limit_mark = "@jueshengMark";
		view_as_skill = new JueshengVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Duel") || !use.card->getSkillNames().contains(objectName())) return false;

			room->removePlayerMark(player, "@jueshengMark");
			room->doSuperLightbox(player, objectName());

			room->setTag("Juesheng_" + use.card->toString(), data);
		} else if (event == DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Duel") || !damage.card->getSkillNames().contains(objectName())) return false;

			CardUseStruct use = room->getTag("Juesheng_" + damage.card->toString()).value<CardUseStruct>();
			if (!use.to.contains(damage.to)) return false;
			use.to.removeOne(damage.to);
			room->setTag("Juesheng_" + damage.card->toString(), QVariant::fromValue(use));

			int x = damage.to->getMark("JueshengSlashNum");

			LogMessage log;
			log.type = "#JueshengDamage";
			log.from = damage.from;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(x);
			room->sendLog(log);
			damage.damage = x;
			data = QVariant::fromValue(damage);

			room->acquireOneTurnSkills(damage.to, objectName(), objectName());
			return x <= 0;
		}
		return false;
	}
};

class JuemanVS : public ZeroCardViewAsSkill
{
public:
	JuemanVS() : ZeroCardViewAsSkill("jueman")
	{
		response_pattern = "@@jueman!";
	}

	const Card*viewAs() const
	{
		QString name = Self->property("JuemanCardName").toString();
		Card*c = Sanguosha->cloneCard(name);
		if (!c) return nullptr;
		c->setSkillName("_jueman");
		return c;
	}
};

class Jueman : public TriggerSkill
{
public:
	Jueman() : TriggerSkill("jueman")
	{
		events << PreCardUsed << PreCardResponded << EventPhaseChanging;
		view_as_skill = new JuemanVS;
		frequency = Compulsory;
        global = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			QStringList user = room->getTag("JuemanUser").toStringList();
			room->removeTag("JuemanUser");
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if (p->isDead() || !p->hasSkill(objectName())) continue;
				if (user.contains(p->objectName())){
					room->sendCompulsoryTriggerLog(p, this);
					p->drawCards(1, objectName());
				} else if (user.length()>2){

					Card*c = Sanguosha->cloneCard(user[2]);
					if (!c) continue;
					c->setSkillName("_jueman");
					c->deleteLater();
					if (!c->isAvailable(p)) continue;

					room->sendCompulsoryTriggerLog(p, this);

					room->setPlayerProperty(p, "JuemanCardName", user[2]);
					if (room->askForUseCard(p, "@@jueman!", "@jueman:" + user[2])) continue;
					if (c->targetFixed())
						room->useCard(CardUseStruct(c, p), true);
					else {
						foreach(ServerPlayer*q, room->getAlivePlayers()){
							if (p->canUse(c, q, true)){
								room->useCard(CardUseStruct(c, p, q), true);
								break;
							}
						}
					}
				}
			}
		}else{
			const Card*c = nullptr;
			if (event == PreCardUsed)
				c = data.value<CardUseStruct>().card;
			else {
				CardResponseStruct res = data.value<CardResponseStruct>();
				if (!res.m_isUse) return false;
				c = res.m_card;
			}
			if (!c || !c->isKindOf("BasicCard")) return false;
	
			QStringList user = room->getTag("JuemanUser").toStringList();
	
			if (user.length()<3){
				user << player->objectName();
			}else
				user << c->objectName();
			room->setTag("JuemanUser",user);
		}
		return false;
	}
};

class Ranshang : public TriggerSkill
{
public:
	Ranshang() : TriggerSkill("ranshang")
	{
		events << EventPhaseStart << Damaged;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature != DamageStruct::Fire) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			//player->gainMark("@rsran", damage.damage);
			player->gainMark("&rsran", damage.damage);
		} else {
			if (player->getPhase() != Player::Finish || player->getMark("&rsran") <= 0) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			room->loseHp(HpLostStruct(player, player->getMark("&rsran"), objectName(), player));
		}
		return false;
	}
};

class Hanyong : public TriggerSkill
{
public:
	Hanyong() : TriggerSkill("hanyong")
	{
		events << CardUsed << DamageCaused;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == CardUsed){
			if (!player->hasSkill(objectName())) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("ArcheryAttack") && !use.card->isKindOf("SavageAssault")) return false;
			if (player->getHp() >= room->getTag("TurnLengthCount").toInt()) return false;
			if (!player->askForSkillInvoke(this, data)) return false;
			room->broadcastSkillInvoke(objectName());
			room->setCardFlag(use.card, "hanyong");
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->hasFlag(objectName()) || damage.to->isDead()) return false;
			++damage.damage;
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

OLLuanzhanCard::OLLuanzhanCard()
{
	mute = true;
}

bool OLLuanzhanCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	int n = Self->getMark("olluanzhan_target_num-Clear");
	return targets.length() < n && to_select->hasFlag("olluanzhan_canchoose");
}

void OLLuanzhanCard::onUse(Room*room, CardUseStruct &card_use) const
{
	foreach(ServerPlayer*p, card_use.to)
		room->setPlayerFlag(p, "olluanzhan_extratarget");
}

class OLLuanzhanVS : public ZeroCardViewAsSkill
{
public:
	OLLuanzhanVS() : ZeroCardViewAsSkill("olluanzhan")
	{
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.contains("@@olluanzhan");
	}

	const Card*viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern=="@@olluanzhan1")
			return new ExtraCollateralCard;
		return new OLLuanzhanCard;
	}
};

class OLLuanzhan : public TriggerSkill
{
public:
	OLLuanzhan() : TriggerSkill("olluanzhan")
	{
		events << TargetSpecified << PreCardUsed;
		view_as_skill = new OLLuanzhanVS;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (event == PreCardUsed){
			int n = player->getMark("&luanzhanMark") + player->getMark("luanzhanMark") + player->getMark("olluanzhanMark");
			if (use.to.length()>=n) return false;
			if (!use.card->isKindOf("Slash") && !(use.card->isBlack() && use.card->isNDTrick())) return false;
			if (use.card->isKindOf("Collateral")){
				for (int i = 1; i <= n; i++){
					bool canextra = false;
					foreach(ServerPlayer*p, room->getAlivePlayers()){
						if (use.to.contains(p)) continue;
						if (player->canUse(use.card, p)){
							canextra = true;
							break;
						}
					}
					if (!canextra) break;
					QStringList tos;
					tos << use.card->toString();
					foreach(ServerPlayer*t, use.to)
						tos << t->objectName();
					tos << objectName();
					room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
					room->askForUseCard(player, "@@olluanzhan1", QString("@luanzhan:%1::%2").arg(use.card->objectName()).arg(QString::number(n)));
					ServerPlayer*p = player->getTag("ExtraCollateralTarget").value<ServerPlayer*>();
					player->removeTag("ExtraCollateralTarget");
					if (p) use.to << p;
				}
			}else if(use.card->targetFixed()){
				bool canextra = false;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if (use.to.contains(p) || !player->canUse(use.card,p)) continue;
					room->setPlayerFlag(p, "olluanzhan_canchoose");
					canextra = true;
				}
				if (!canextra) return false;
				room->setPlayerMark(player, "olluanzhan_target_num-Clear", n);
				player->setTag("olluanzhanData", data);
				if (!room->askForUseCard(player, "@@olluanzhan", QString("@luanzhan:%1::%2").arg(use.card->objectName()).arg(n)))
					return false;
				LogMessage log;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					room->setPlayerFlag(p, "-olluanzhan_canchoose");
					if (p->hasFlag("olluanzhan_extratarget")){
						room->setPlayerFlag(p,"-olluanzhan_extratarget");
						log.to << p;
					}
				}
				if (log.to.isEmpty()) return false;
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.card_str = use.card->toString();
				log.arg = "olluanzhan";
				room->sendLog(log);
				use.to << log.to;
			}
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		} else {
			if (!use.card->isKindOf("Slash") && !(use.card->isBlack() && use.card->isNDTrick())) return false;
			int n = player->getMark("&luanzhanMark") + player->getMark("luanzhanMark") + player->getMark("olluanzhanMark");
			if (use.to.length() < n){
				room->setPlayerMark(player, "luanzhanMark", 0);
				room->setPlayerMark(player, "olluanzhanMark", 0);
				room->setPlayerMark(player, "&luanzhanMark", floor(n / 2));
			}
		}
		return false;
	}
};

class OLLuanzhanTargetMod : public TargetModSkill
{
public:
	OLLuanzhanTargetMod() : TargetModSkill("#olluanzhan-target")
	{
		pattern = ".";
	}

	int getExtraTargetNum(const Player*from, const Card*card) const
	{
		if ((card->isKindOf("Slash") || (card->isBlack() && card->isNDTrick()))&&from->hasSkill("olluanzhan"))
			return from->getMark("&luanzhanMark") + from->getMark("luanzhanMark") + from->getMark("olluanzhanMark");
		return 0;
	}
};

class Xingluan : public TriggerSkill
{
public:
	Xingluan(const QString &xingluan) : TriggerSkill(xingluan), xingluan(xingluan)
	{
		events << CardFinished;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getPhase() != Player::Play || player->getMark(xingluan + "-PlayClear") > 0) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard") || use.to.length() != 1) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, xingluan + "-PlayClear");
		QList<int> ids;
		foreach(int id, room->getDrawPile()){
			if (Sanguosha->getCard(id)->getNumber() == 6)
				ids << id;
		}
		if(xingluan=="olxingluan"){
			QList<ServerPlayer*> tos;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if(p->getCardCount()>0)
					tos << p;
				else{
					foreach(const Card*c, player->getCards("j")){
						if(c->getNumber()==6){
							tos << player;
							break;
						}
					}
				}
			}
			foreach(const Card*c, player->getCards("ej")){
				if(c->getNumber()==6){
					tos << player;
					break;
				}
			}
			ServerPlayer*to = room->askForPlayerChosen(player,tos,xingluan,"olxingluan0:",true);
			if(to){
				QString choices = "olxingluan3";
				foreach(const Card*c, to->getCards("ej")){
					if(c->getNumber()==6){
						choices = "olxingluan1+olxingluan3";
						break;
					}
				}
				if(room->askForChoice(player,xingluan,choices)=="olxingluan1"){
					QList<int> disabled_ids;
					foreach(const Card*c, to->getCards("ej")){
						if(c->getNumber()!=6)
							disabled_ids << c->getId();
					}
					int id = room->askForCardChosen(player,to,"ej",xingluan,false,Card::MethodNone,disabled_ids);
					room->obtainCard(player, id, true);
				}else{
					if(!room->askForDiscard(to,xingluan,1,1,true,true,"olxingluan01:"+player->objectName(),".|.|6")){
						const Card*c = room->askForExchange(to,xingluan,1,1,true);
						if (c) room->obtainCard(player, c, false);
					}
				}
			}else{
				QList<int> ag_ids;
				if(!ids.isEmpty()){
					int id = ids.at(qrand() % ids.length());
					ag_ids << id;
					ids.removeOne(id);
					if(!ids.isEmpty())
						ag_ids << ids.at(qrand() % ids.length());
				}
				if(ag_ids.isEmpty()){
					player->drawCards(1,xingluan);
				}else{
					room->fillAG(ag_ids);
					int id = room->askForAG(player, ag_ids, true, xingluan);
					room->clearAG();
					room->obtainCard(player, id, true);
				}
			}
			return false;
		}
		if (ids.isEmpty()){
			if (xingluan == "xingluan"){
				LogMessage log;
				log.type = "#XingluanNoSix";
				log.arg = QString::number(6);
				room->sendLog(log);
			} else if (xingluan == "tenyearxingluan")
				player->drawCards(6, xingluan);
			return false;
		}
		int id = ids.at(qrand() % ids.length());
		room->obtainCard(player, id, true);
		return false;
	}
private:
	QString xingluan;
};

SecondMansiCard::SecondMansiCard()
{
	mute = true;
	target_fixed = true;
}

void SecondMansiCard::onUse(Room*room, CardUseStruct &card_use) const
{
	if (card_use.from->isKongcheng()) return;
	SavageAssault*sa = new SavageAssault(Card::SuitToBeDecided, 0);
	foreach(const Card*c, card_use.from->getHandcards())
		sa->addSubcard(c);
	sa->setSkillName("secondmansi");
	sa->deleteLater();
	if (!sa->isAvailable(card_use.from)) return;
	room->useCard(CardUseStruct(sa, card_use.from), true);
}

class SecondMansiVS : public ZeroCardViewAsSkill
{
public:
	SecondMansiVS() : ZeroCardViewAsSkill("secondmansi")
	{
	}

	const Card*viewAs() const
	{
		if (Self->isKongcheng()) return nullptr;
		SavageAssault*sa = new SavageAssault(Card::SuitToBeDecided, 0);
		foreach(const Card*c, Self->getHandcards())
			sa->addSubcard(c);
		sa->setSkillName("secondmansi");
		sa->deleteLater();
		if (!sa->isAvailable(Self)) return nullptr;
		//return sa;
		return new SecondMansiCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		SavageAssault*sa = new SavageAssault(Card::SuitToBeDecided, 0);
		foreach(const Card*c, player->getHandcards())
			sa->addSubcard(c);
		sa->setSkillName("secondmansi");
		sa->deleteLater();
		if (!sa->isAvailable(player)) return false;
		return !player->isKongcheng() && !player->hasUsed("SecondMansiCard");
	}
};

class SecondMansi : public MasochismSkill
{
public:
	SecondMansi() : MasochismSkill("secondmansi")
	{
		view_as_skill = new SecondMansiVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	void onDamaged(ServerPlayer*player, const DamageStruct &damage) const
	{
		if (!damage.card || !damage.card->isKindOf("SavageAssault")) return;
		Room*room = player->getRoom();
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			room->sendCompulsoryTriggerLog(p, objectName(), true, true);
			p->drawCards(1, objectName());
		}
	}
};

class SecondSouying : public TriggerSkill
{
public:
	SecondSouying() : TriggerSkill("secondsouying")
	{
		events << TargetConfirmed;
	}

	bool triggerable(const ServerPlayer*target, Room*room) const
	{
		return target != nullptr && room->hasCurrent();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard")) return false;
		if (use.from != player) return false;
		if (use.to.length() != 1 || use.from->isDead()) return false;

		ServerPlayer*first = use.to.first();
		if (first == use.from || first->getMark("secondsouying_num_" + use.from->objectName() + first->objectName() + "-Clear") == 1) return false;

		QList<ServerPlayer*> huamans;
		if (use.from->hasSkill(objectName()))
			huamans << use.from;
		if (first->hasSkill(objectName()))
			huamans << first;
		if (huamans.isEmpty()) return false;
		room->sortByActionOrder(huamans);

		foreach(ServerPlayer*p, huamans){
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			if (p->getMark("secondsouying-Clear") > 0 || !p->canDiscard(p, "he")) return false;

			QString prompt = "@secondsouying-dis:" + use.card->objectName();
			if (p == first)
				prompt = "@secondsouying-dis2:" + use.card->objectName();

			if (!room->askForCard(p, "..", prompt, data, objectName())) continue;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(p, "secondsouying-Clear");

			if (p == use.from){
				if (!room->CardInTable(use.card)) return false;
				room->obtainCard(use.from, use.card, true);
			} else {
				use.nullified_list << p->objectName();
				data = QVariant::fromValue(use);
			}
		}
		return false;
	}
};

class SecondZhanyuan : public PhaseChangeSkill
{
public:
	SecondZhanyuan() : PhaseChangeSkill("secondzhanyuan")
	{
		frequency = Wake;
		waked_skills = "secondxili";
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		int mark = player->getMark("&secondzhanyuan_num") + player->getMark("secondzhanyuan_num");
		if (mark > 7){
			LogMessage log;
			log.type = "#ZhanyuanWake";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(mark);
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, objectName());
		room->addPlayerMark(player, objectName());
		if (room->changeMaxHpForAwakenSkill(player, 1, objectName())){
			room->recover(player, RecoverStruct("secondzhanyuan", player));
			QList<ServerPlayer*> males, geters;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->isMale())
					males << p;
			}
			ServerPlayer*male = room->askForPlayerChosen(player, males, objectName(), "@secondzhanyuan-invoke", true);
			if (male){
				room->doAnimate(1, player->objectName(), male->objectName());
				geters << male;
			}
			if (!geters.contains(player))
				geters << player;
			if (geters.isEmpty()) return false;
			room->sortByActionOrder(geters);

			foreach(ServerPlayer*p, geters){
				if (p->hasSkill("secondxili", true)) continue;
				room->handleAcquireDetachSkills(p, "secondxili");
			}
			if (player->hasSkill("secondmansi", true))
				room->handleAcquireDetachSkills(player, "-secondmansi");
		}
		return false;
	}
};

class SecondXili : public TriggerSkill
{
public:
	SecondXili() : TriggerSkill("secondxili")
	{
		events << DamageCaused;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive() && target->hasFlag("CurrentPlayer") && target->hasSkill(objectName(), true);
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to->hasSkill(objectName(), true)) return false;

		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p == damage.from || p->getMark("secondxili-Clear") > 0) continue;
			if (!damage.from || damage.from->isDead() || !damage.from->hasSkill(objectName(), true)) return false;
			if (damage.to->isDead() || damage.to->hasSkill(objectName(), true)) return false;

			if (p->isDead() || !p->hasSkill(objectName()) || p->hasFlag("CurrentPlayer")) continue;
			if (!p->canDiscard(p, "he")) continue;
			if (!room->askForCard(p, "..", "@secondxili-dis:" + damage.to->objectName(), data, objectName())) continue;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(p, "secondxili-Clear");

			LogMessage log;
			log.type = "#SecondxiliDamage";
			log.from = damage.from;
			log.to << damage.to;
			log.arg = QString::number(damage.damage);
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);

			data = QVariant::fromValue(damage);

			QList<ServerPlayer*> drawers;
			drawers << p << damage.from;
			room->sortByActionOrder(drawers);
			room->drawCards(drawers, 2, objectName());
		}
		return false;
	}
};

class JinZhefu : public TriggerSkill
{
public:
	JinZhefu() : TriggerSkill("jinzhefu")
	{
		events << CardFinished << PostCardResponded;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->hasFlag("CurrentPlayer")) return false;

		const Card*card = nullptr;
		if (event == CardFinished)
			card = data.value<CardUseStruct>().card;
		else
			card = data.value<CardResponseStruct>().m_card;
		if (!card || !card->isKindOf("BasicCard")) return false;

		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (p->isKongcheng()) continue;
			targets << p;
		}
		if (targets.isEmpty()) return false;

		QString name = card->objectName();
		ServerPlayer*t = room->askForPlayerChosen(player, targets, objectName(), "@jinzhefu-invoke:" + name, true, true);
		if (!t) return false;
		player->peiyin(this);

		QString pattern = card->getClassName();
		if (card->isKindOf("Slash")){
			pattern = "Slash";
			name = "slash";
		}

		t->setTag("JinZhefuForAI", pattern);
		if (room->askForDiscard(t, objectName(), 1, 1, true, false, "@jinzhefu-discard:" + player->objectName() + "::" + name, pattern))
			return false;
		room->damage(DamageStruct(objectName(), player, t));
		return false;
	}
};

class JinYidu : public TriggerSkill
{
public:
	JinYidu() : TriggerSkill("jinyidu")
	{
		events << CardFinished;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.to.length() != 1) return false;
		if (use.card->isKindOf("Slash") || (use.card->isDamageCard() && use.card->isNDTrick())){
			ServerPlayer*to = use.to.first();
			if (to->isKongcheng() || use.card->hasFlag("DamageDone_" + to->objectName())) return false;
			if (!player->askForSkillInvoke(this, to)) return false;
			player->peiyin(this);

			QList<int> cards;
			for (int i = 0; i < 3; i++){
				if (to->getHandcardNum()<=i) break;
				int id = room->askForCardChosen(player, to, "h", objectName(), false, Card::MethodNone, cards, i > 0);
				if (id < 0) break;
				cards << id;
			}
			room->showCard(to, cards);

			Card::Color color = Sanguosha->getCard(cards.first())->getColor();
			foreach(int id, cards){
				if (Sanguosha->getCard(id)->getColor() != color)
					return false;
			}

			QList<int> to_throw;
			foreach(int id, cards){
				if (!to->canDiscard(to, id)) continue;
				to_throw << id;
			}
			if (to_throw.isEmpty()) return false;

			DummyCard dummy(to_throw);
			room->throwCard(&dummy, to);
		}
		return false;
	}
};

LianzhuCard::LianzhuCard(QString lianzhu) : lianzhu(lianzhu)
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void LianzhuCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.from->getRoom();
	int id = getSubcards().first();
	room->showCard(effect.from, id);
	CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), lianzhu, "");
	room->obtainCard(effect.to, this, reason, true);
	if (effect.to->isDead()) return;
	const Card*card = Sanguosha->getCard(id);
	if (!card->isBlack()){
		if (lianzhu == "tenyearlianzhu" && card->isRed())
			effect.from->drawCards(1, lianzhu);
		return;
	}
	effect.to->setTag("LianzhuFrom", QVariant::fromValue(effect.from));
	const Card*dis = room->askForDiscard(effect.to, lianzhu, 2, 2, true, true, "lianzhu-discard:" + effect.from->objectName());
	effect.to->removeTag("LianzhuFrom");
	if (dis) return;
	effect.from->drawCards(2, lianzhu);
}

TenyearLianzhuCard::TenyearLianzhuCard() : LianzhuCard("tenyearlianzhu")
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

class Lianzhu : public OneCardViewAsSkill
{
public:
	Lianzhu(const QString &lianzhu) : OneCardViewAsSkill(lianzhu), lianzhu(lianzhu)
	{
		filter_pattern = ".";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		if (lianzhu == "lianzhu")
			return !player->hasUsed("LianzhuCard");
		else if (lianzhu == "tenyearlianzhu")
			return !player->hasUsed("TenyearLianzhuCard");
		else
			return false;
	}

	const Card*viewAs(const Card*originalCard) const
	{
		if (lianzhu == "lianzhu"){
			LianzhuCard*card = new LianzhuCard;
			card->addSubcard(originalCard);
			return card;
		} else if (lianzhu == "tenyearlianzhu"){
			TenyearLianzhuCard*card = new TenyearLianzhuCard;
			card->addSubcard(originalCard);
			return card;
		}
		return nullptr;
	}
private:
	QString lianzhu;
};

class Xiahui : public TriggerSkill
{
public:
	Xiahui(const QString &xiahui) : TriggerSkill(xiahui), xiahui(xiahui)
	{
		events << EventPhaseProceeding << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseProceeding){
			if (player->getPhase() != Player::Discard) return false;
			QList<int> blacks;
			foreach(int id, player->handCards()){
				if (Sanguosha->getCard(id)->isBlack())
					blacks << id;
			}
			room->sendCompulsoryTriggerLog(player, this);
			if (blacks.isEmpty()) return false;
			room->ignoreCards(player, blacks);
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if ((move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
				&& move.to_place == Player::PlaceHand && move.from != move.to && move.from == player){
				ServerPlayer*to = (ServerPlayer*)move.to;
				if (!to || to->isDead()) return false;
				QString string = xiahui + "_limited" + player->objectName();
				QVariantList limited = to->getTag(string).toList();
				for (int i = 0; i < move.card_ids.length(); i++){
					if (!Sanguosha->getCard(move.card_ids.at(i))->isBlack()) continue;
					if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip){
						if (!limited.contains(QVariant(move.card_ids.at(i))))
							limited << move.card_ids.at(i);
					}
				}
				if (limited.isEmpty()) return false;
				room->sendCompulsoryTriggerLog(player, this);
				to->setTag(string, limited);

				foreach(int id, ListV2I(limited))
					room->setPlayerCardLimitation(to, "use,response,discard", QString::number(id), false);
			}
		}
		return false;
	}
private:
	QString xiahui;
};

class XiahuiClear : public TriggerSkill
{
public:
	XiahuiClear(const QString &xiahui) : TriggerSkill("#" + xiahui + "-clear"), xiahui(xiahui)
	{
		events << EventLoseSkill << HpChanged << Death << EventPhaseEnd;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventLoseSkill){
			if (player->isDead() || data.toString() != xiahui) return false;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				QString string = xiahui + "_limited" + player->objectName();
				QVariantList limited = p->getTag(string).toList();
				if (limited.isEmpty()) continue;
				QList<int> limit_ids = ListV2I(limited);
				p->removeTag(string);

				foreach(int id, limit_ids){
					room->removePlayerCardLimitation(p, "use,response,discard", QString::number(id) + "$0");
				}
			}
		} else if (event == HpChanged){
			if (player->isDead() || data.isNull() || data.canConvert<RecoverStruct>()) return false;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				QString string = xiahui + "_limited" + p->objectName();
				QVariantList limited = player->getTag(string).toList();
				if (limited.isEmpty()) continue;
				QList<int> limit_ids = ListV2I(limited);
				player->removeTag(string);

				foreach(int id, limit_ids){
					room->removePlayerCardLimitation(player, "use,response,discard", QString::number(id) + "$0");
				}
			}
		} else if (event == EventPhaseEnd){
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if (player->isDead()) return false;
				if (p->isDead() || !p->hasSkill("tenyearxiahui") || player->getMark("tenyearxiahui_lose_" + p->objectName() + "-Clear") <=0)
					continue;
				QString string = "tenyearxiahui_limited" + p->objectName();
				QVariantList limited = player->getTag(string).toList();
				if (limited.isEmpty()) continue;
				QList<int> limited_ids = ListV2I(limited);
				bool contain = false;
				foreach(int id, player->handCards()){
					if (limited_ids.contains(id)){
						contain = true;
						break;
					}
				}
				if (contain) continue;
				room->sendCompulsoryTriggerLog(p, "tenyearxiahui", true, true);
				room->loseHp(HpLostStruct(player, 1, "tenyearxiahui", p));
			}
		} else {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who == player && player->hasSkill("xiahui",true)){
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					QString string = xiahui + "_limited" + player->objectName();
					QVariantList limited = p->getTag(string).toList();
					if (limited.isEmpty()) continue;
					QList<int> limit_ids = ListV2I(limited);
					p->removeTag(string);

					foreach(int id, limit_ids){
						room->removePlayerCardLimitation(p, "use,response,discard", QString::number(id) + "$0");
					}
				}
			} else {
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					QString string = xiahui + "_limited" + p->objectName();
					QVariantList limited = player->getTag(string).toList();
					if (limited.isEmpty()) continue;
					QList<int> limit_ids = ListV2I(limited);
					player->removeTag(string);

					foreach(int id, limit_ids){
						room->removePlayerCardLimitation(player, "use,response,discard", QString::number(id) + "$0");
					}
				}
			}
		}
		return false;
	}
private:
	QString xiahui;
};

class Yishe : public TriggerSkill
{
public:
	Yishe() : TriggerSkill("yishe")
	{
		events << EventPhaseStart << CardsMoveOneTime;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart){
			if (player->getPile("rice").isEmpty() && player->getPhase() == Player::Finish){
				if (player->askForSkillInvoke(this)){
					room->broadcastSkillInvoke(objectName());
					player->drawCards(2, objectName());
					if (!player->isNude()){
						const Card*dummy = room->askForExchange(player, objectName(), 2, 2, true, "@yishe");
						player->addToPile("rice", dummy);
					}
				}
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player && move.from_pile_names.contains("rice") && move.from->getPile("rice").isEmpty() && player->getLostHp() > 0){
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				room->recover(player, RecoverStruct("yishe", player));
			}
		}
		return false;
	}
};

BushiCard::BushiCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void BushiCard::onUse(Room*, CardUseStruct &) const
{
}

class BushiVS : public OneCardViewAsSkill
{
public:
	BushiVS() : OneCardViewAsSkill("bushi")
	{
		response_pattern = "@@bushi";
		expand_pile = "rice,%rice";
	}

	bool viewFilter(const Card*to_select) const{
		foreach(const Player*p, Self->getAliveSiblings(true)){
			if (p->objectName() == Self->property("bushi_from").toString()){
				return p->getPile("rice").contains(to_select->getEffectiveId());
			}
		}
		return false;
	}

	const Card*viewAs(const Card*card) const
	{
		BushiCard*bs = new BushiCard;
		bs->addSubcard(card);
		return bs;
	}
};

class Bushi : public TriggerSkill
{
public:
	Bushi() : TriggerSkill("bushi")
	{
		events << Damage << Damaged;
		view_as_skill = new BushiVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (!player->hasSkill(objectName()) || player->getPile("rice").isEmpty())
			return false;

		DamageStruct damage = data.value<DamageStruct>();
		ServerPlayer*p = damage.to;
		if (event == Damage && player == p) return false;
		if (event == Damaged && damage.from == p) return false;

		if (!damage.from || p->isDead())
			return false;

		for (int i = 0; i < damage.damage; ++i){
			room->setPlayerProperty(p, "bushi_from", player->objectName());
			const Card*c = room->askForUseCard(p, "@@bushi", "@bushi", -1, Card::MethodNone);
			room->setPlayerProperty(p, "bushi_from", "");
			if (!c) break;
			LogMessage log;
			log.type = (p == player) ? "#InvokeSkill" : "#InvokeOthersSkill";
			log.arg = objectName();
			log.from = p;
			log.to << player;
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());

			if (player == p){
				LogMessage log;
				log.type = "$KuangbiGet";
				log.arg = "rice";
				log.from = p;
				log.card_str = Sanguosha->getCard(c->getSubcards().first())->toString();
				room->sendLog(log);
			}
			p->obtainCard(c, true);

			if (p->isDead() || player->getPile("rice").isEmpty())
				break;
		}
		return false;
	}
};

MidaoCard::MidaoCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodResponse;
}

void MidaoCard::onUse(Room*, CardUseStruct &) const
{
}

class MidaoVS : public OneCardViewAsSkill
{
public:
	MidaoVS() : OneCardViewAsSkill("midao")
	{
		response_pattern = "@@midao";
		filter_pattern = ".|.|.|rice";
		expand_pile = "rice";
	}

	const Card*viewAs(const Card*card) const
	{
		MidaoCard*bs = new MidaoCard;
		bs->addSubcard(card);
		return bs;
	}
};

class Midao : public RetrialSkill
{
public:
	Midao() : RetrialSkill("midao", false)
	{
		view_as_skill = new MidaoVS;
	}

	const Card*onRetrial(ServerPlayer*player, JudgeStruct*judge) const
	{
		if (player->getPile("rice").isEmpty())
			return nullptr;

		QStringList prompt_list;
		prompt_list << "@midao-card" << judge->who->objectName() << objectName()
			<< judge->reason << QString::number(judge->card->getEffectiveId());
		QString prompt = prompt_list.join(":");

		Room*room = player->getRoom();
		player->setTag("judgeData", QVariant::fromValue(judge));
		const Card*c = room->askForUseCard(player, "@@midao", prompt, -1, Card::MethodResponse);
		if (c){
			room->broadcastSkillInvoke(objectName());
			return Sanguosha->getCard(c->getEffectiveId());
		}
		return nullptr;
	}
};

class Zongkui : public TriggerSkill
{
public:
	Zongkui() : TriggerSkill("zongkui")
	{
		events << EventPhaseStart << RoundStart;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (event == EventPhaseStart){
			if (player->getPhase() != Player::RoundStart) return false;
			QList<ServerPlayer*> players;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->getMark("&kui") <= 0)
					players << p;
			}
			ServerPlayer*target = room->askForPlayerChosen(player, players, objectName(), "@zongkui-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			target->gainMark("&kui");
		} else {
			int hp = 998;
			QList<ServerPlayer*> players;
			foreach(ServerPlayer*p, room->getOtherPlayers(player))
				hp = qMin(hp,p->getHp());
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->getHp() == hp && p->getMark("&kui") <= 0)
					players << p;
			}
			ServerPlayer*target = room->askForPlayerChosen(player, players, objectName(), "@zongkui-trigger",false,true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			target->gainMark("&kui");
		}
		return false;
	}
};

class Guju : public MasochismSkill
{
public:
	Guju() : MasochismSkill("guju")
	{
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	void onDamaged(ServerPlayer*player, const DamageStruct &) const
	{
		if (player->getMark("&kui") <= 0) return;
		Room*room = player->getRoom();
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			room->sendCompulsoryTriggerLog(p, objectName(), true, true);
			p->drawCards(1, objectName());
		}
	}
};

class Baijia : public PhaseChangeSkill
{
public:
	Baijia() : PhaseChangeSkill("baijia")
	{
		frequency = Wake;
		waked_skills = "spcanshi";
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		int mark = player->getMark("&baijia") + player->getMark("baijia");
		if (mark >= 7){
			LogMessage log;
			log.type = "#BaijiaWake";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(mark);
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, objectName());
		room->addPlayerMark(player, objectName());
		if (room->changeMaxHpForAwakenSkill(player, 1, objectName())){
			room->recover(player, RecoverStruct("baijia", player));
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->isAlive() && p->getMark("&kui") <= 0)
					p->gainMark("&kui");
			}
			room->handleAcquireDetachSkills(player, "-guju|spcanshi");
		}
		mark = player->getMark("&baijia");
		room->addPlayerMark(player, "baijia", mark);
		room->setPlayerMark(player, "&baijia", 0);
		return false;
	}
};

class BaijiaRecord : public TriggerSkill
{
public:
	BaijiaRecord() : TriggerSkill("#baijia")
	{
		events << CardsMoveOneTime;
        global = true;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to == player && move.to_place == Player::PlaceHand && move.reason.m_skillName == "guju"){
			if (player->hasSkill("baijia", true))
				room->addPlayerMark(player, "&baijia", move.card_ids.length());
			else
				player->addMark("baijia", move.card_ids.length());
		}
		return false;
	}
};

SpCanshiCard::SpCanshiCard()
{
}

bool SpCanshiCard::targetFilter(const QList<const Player*> &, const Player*to_select, const Player*Self) const
{
	QStringList names = Self->property("spcanshi_ava").toString().split("+");
	return to_select->getMark("&kui") > 0 && names.contains(to_select->objectName());
}

void SpCanshiCard::onUse(Room*room, CardUseStruct &card_use) const
{
	foreach(ServerPlayer*p, card_use.to){
		room->doAnimate(1, card_use.from->objectName(), p->objectName());
		room->setPlayerFlag(p, "spcanshi_extra");
	}
}

class SpCanshiVS : public ZeroCardViewAsSkill
{
public:
	SpCanshiVS() : ZeroCardViewAsSkill("spcanshi")
	{
		response_pattern = "@@spcanshi";
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	const Card*viewAs() const
	{
		return new SpCanshiCard;
	}
};

class SpCanshi : public TriggerSkill
{
public:
	SpCanshi() : TriggerSkill("spcanshi")
	{
		events << TargetSpecifying;
		view_as_skill = new SpCanshiVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("BasicCard") && !use.card->isNDTrick()) return false;
		if (use.card->isKindOf("Collateral")) return false;
		if (use.to.length() != 1) return false;
		if (use.from->hasSkill(objectName())){
			QStringList ava;
			room->setCardFlag(use.card, "spcanshi_distance");
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if ((use.card->isKindOf("AOE") && p == use.from) || p->getMark("&kui") <= 0) continue;
				if (use.to.contains(p) || room->isProhibited(use.from, p, use.card)) continue;
				if (use.card->targetFixed()){
					if (!use.card->isKindOf("Peach") || p->isWounded())
						ava << p->objectName();
				} else {
					if (use.card->targetFilter(QList<const Player*>(), p, use.from))
						ava << p->objectName();
				}
			}
			room->setCardFlag(use.card, "-spcanshi_distance");
			if (ava.isEmpty()) return false;
			player->setTag("SPCanshiForAI", data);
			room->setPlayerProperty(player, "spcanshi_ava", ava.join("+"));
			if (room->askForUseCard(player, "@@spcanshi", "@spcanshi:" + use.card->objectName())){
				LogMessage log;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if (p->hasFlag("spcanshi_extra")){
						room->setPlayerFlag(p, "-spcanshi_extra");
						use.to.append(p);
						log.to << p;
					}
				}
				if (log.to.isEmpty()) return false;
				room->sortByActionOrder(use.to);
				data = QVariant::fromValue(use);
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.card_str = use.card->toString();
				log.arg = objectName();
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(player, objectName());
				foreach(ServerPlayer*p, log.to){
					if (p->getMark("&kui") > 0)
						p->loseAllMarks("&kui");
				}
			}
		} else {
			if (!use.to.first()->hasSkill(objectName()) || use.from->getMark("&kui") <= 0) return false;
			use.to.first()->setTag("SPCanshi", data);
			bool invoke = use.to.first()->askForSkillInvoke(this, QVariant::fromValue(use.from));
			use.to.first()->removeTag("SPCanshi");
			if (!invoke) return false;
			room->broadcastSkillInvoke(objectName());
			use.to.removeOne(use.to.first());
			data = QVariant::fromValue(use);
			use.from->loseAllMarks("&kui");
		}
		return false;
	}
};

class Yuxu : public TriggerSkill
{
public:
	Yuxu() : TriggerSkill("yuxu")
	{
		events << CardFinished;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getPhase() != Player::Play) return false;
		if (event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (player->getMark("yuxuUse-PlayClear")<1){
				if (!player->askForSkillInvoke(this, data)) return false;
				room->broadcastSkillInvoke(objectName());
				player->drawCards(1, objectName());
				player->addMark("yuxuUse-PlayClear");
			} else {
				player->removeMark("yuxuUse-PlayClear");
				if (player->isNude()) return false;
				room->askForDiscard(player, objectName(), 1, 1, false, true);
			}
		}
		return false;
	}
};

class Shijian : public TriggerSkill
{
public:
	Shijian() : TriggerSkill("shijian")
	{
		events << PreCardUsed << CardFinished;
        global = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getPhase() != Player::Play) return false;
		if (event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (use.card->hasFlag("shijian_second")){
				foreach(ServerPlayer*p, room->findPlayersBySkillName(objectName())){
					if (p->isAlive() && p->hasSkill(objectName()) && p != player && !p->isNude()){
						if (room->askForCard(p, "..", "@shijian-discard:" + player->objectName(), QVariant::fromValue(player), objectName())){
							room->broadcastSkillInvoke(objectName());
							room->acquireOneTurnSkills(player, "shijian", "yuxu");
						}
					}
				}
			}
		} else {
			const Card*card = nullptr;
			if (event == PreCardUsed)
				card = data.value<CardUseStruct>().card;
			
			if (card == nullptr || card->isKindOf("SkillCard")) return false;
			player->addMark("shijian-PlayClear");
			int n = player->getMark("shijian-PlayClear");
			if (n == 2) room->setCardFlag(card, "shijian_second");
		}
		return false;
	}
};

class Huqi : public MasochismSkill
{
public:
	Huqi() : MasochismSkill("huqi")
	{
		frequency = Compulsory;
	}

	void onDamaged(ServerPlayer*target, const DamageStruct &damage) const
	{
		if (target->hasFlag("CurrentPlayer")) return;
		Room*room = target->getRoom();
		room->sendCompulsoryTriggerLog(target, objectName(), true, true);
		JudgeStruct judge;
		judge.who = target;
		judge.reason = objectName();
		judge.pattern = ".|red";
		room->judge(judge);

		if (!judge.isGood()) return;
		if (!damage.from || damage.from->isDead()) return;
		Slash*slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("_huqi");
		slash->deleteLater();
		if (!target->canSlash(damage.from, slash, false)) return;
		room->useCard(CardUseStruct(slash, target, damage.from));
	}
};

class HuqiDistance : public DistanceSkill
{
public:
	HuqiDistance() : DistanceSkill("#huqi-distance")
	{
	}

	int getCorrect(const Player*from, const Player*) const
	{
		if (from->hasSkill("huqi"))
			return -1;
		return 0;
	}
};

ShoufuCard::ShoufuCard()
{
	target_fixed = true;
}

void ShoufuCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	source->drawCards(1, "shoufu");
	if (source->isKongcheng()) return;

	QList<ServerPlayer*> targets;
	foreach(ServerPlayer*p, room->getAlivePlayers()){
		if (!p->getPile("sflu").isEmpty()) continue;
		targets << p;
	}
	if (targets.isEmpty()) return;

	if (!room->askForUseCard(source, "@@shoufu!", "@shoufu", Card::MethodNone)){
		int id = source->getRandomHandCardId();
		targets.at(qrand() % targets.length())->addToPile("sflu", id);
	}
}

ShoufuPutCard::ShoufuPutCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
	m_skillName = "shoufu";
}

bool ShoufuPutCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	return targets.isEmpty() && to_select->getPile("sflu").isEmpty();
}

void ShoufuPutCard::onUse(Room*, CardUseStruct &card_use) const
{
	card_use.to.first()->addToPile("sflu", this);
}

class ShoufuVS : public ViewAsSkill
{
public:
	ShoufuVS() : ViewAsSkill("shoufu")
	{
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY)
			return false;
		else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return selected.isEmpty() && !to_select->isEquipped();
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
			if (!cards.isEmpty()) return nullptr;
			return new ShoufuCard;
		} else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE){
			if (cards.length() != 1) return nullptr;
			ShoufuPutCard*c = new ShoufuPutCard;
			c->addSubcards(cards);
			return c;
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("ShoufuCard");
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern == "@@shoufu!";
	}
};

class Shoufu : public TriggerSkill
{
public:
	Shoufu() : TriggerSkill("shoufu")
	{
		events << CardsMoveOneTime << DamageInflicted;
		view_as_skill = new ShoufuVS;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && !target->getPile("sflu").isEmpty();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == DamageInflicted){
			player->clearOnePrivatePile("sflu");
		} else {
			if (player->getPhase() != Player::Discard) return false;

			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD){
				DummyCard*dummy = new DummyCard;
				foreach(int id, player->getPile("sflu")){
					const Card*card = Sanguosha->getCard(id);
					foreach(int mid, move.card_ids){
						const Card*mc = Sanguosha->getCard(mid);
						if(mc->getType()==card->getType())
							player->addMark("shoufu_"+mc->getType());
					}
					if(player->getMark("shoufu_"+card->getType())>1){
						player->setMark("shoufu_"+card->getType(),0);
						dummy->addSubcard(id);
					}
				}
				if (dummy->subcardsLength() > 0){
					CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "shoufu", "");
					room->throwCard(dummy, reason, nullptr);
				}
				delete dummy;
			}
		}
		return false;
	}
};

class ShoufuLimit : public CardLimitSkill
{
public:
	ShoufuLimit() : CardLimitSkill("#shoufu-limit")
	{
	}

	bool hasShoufuPlayer(const Player*target) const
	{
		foreach(const Player*p, target->getAliveSiblings()){
			if (p->hasSkill("shoufu"))
				return true;
		}
		return false;
	}

	QString limitList(const Player*) const
	{
		return "use,response";
	}

	QString limitPattern(const Player*target) const
	{
		if (target->getPile("sflu").length()>0){
			QStringList patterns;
			foreach(int id, target->getPile("sflu")){
				const Card*card = Sanguosha->getCard(id);
				if (card->isKindOf("BasicCard"))
					patterns << "BasicCard";
				else if (card->isKindOf("TrickCard"))
					patterns << "TrickCard";
				else if (card->isKindOf("EquipCard"))
					patterns << "EquipCard";
			}
			return patterns.join(",");
		}
		return "";
	}
};

GuanxuCard::GuanxuCard()
{
}

bool GuanxuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void GuanxuCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isKongcheng()) return;
	Room*room = effect.from->getRoom();
	QList<int> drawpile = room->getNCards(5, false), hands = effect.to->handCards(), _drawpile;

	foreach(int id, drawpile)
		_drawpile.prepend(id);

	LogMessage log;
	log.type = "$ViewAllCards";
	log.from = effect.from;
	log.to << effect.to;
	log.card_str = ListI2S(hands).join("+");
	room->sendLog(log, effect.from);

	room->notifyMoveToPile(effect.from, hands, "guanxuhand", Player::PlaceHand, true);
	room->notifyMoveToPile(effect.from, _drawpile, "guanxudrawpile", Player::DrawPile, true);

	const Card*card = room->askForUseCard(effect.from, "@@guanxu1", "@guanxu1", 1, Card::MethodNone);

	room->notifyMoveToPile(effect.from, hands, "guanxuhand", Player::PlaceHand, false);
	room->notifyMoveToPile(effect.from, _drawpile, "guanxudrawpile", Player::DrawPile, false);
	room->returnToTopDrawPile(drawpile);

	if (!card || effect.to->isDead()) return;
	int hand_id = -1, drawpile_id = -1;
	foreach(int id, card->getSubcards()){
		if (hands.contains(id))
			hand_id = id;
		else if (drawpile.contains(id))
			drawpile_id = id;
	}
	if (drawpile_id < 0 || hand_id < 0) return;

	int n = 1;
	foreach(int id, drawpile){
		if (id == drawpile_id) break;
		n++;
	}

	room->obtainCard(effect.to, drawpile_id, false);
	room->moveCardsInToDrawpile(effect.to, hand_id, "guanxu", n);

	if (effect.from->isDead() || effect.to->isDead() || effect.to->getHandcardNum() < 3) return;

	QList<int> spade, club, heart, diamond, all;
	foreach(const Card*c, effect.to->getCards("h")){
		if(!effect.from->canDiscard(effect.to,c->getId())) continue;
		if (c->getSuit() == Card::Spade)
			spade << c->getEffectiveId();
		if (c->getSuit() == Card::Club)
			club << c->getEffectiveId();
		if (c->getSuit() == Card::Heart)
			heart << c->getEffectiveId();
		if (c->getSuit() == Card::Diamond)
			diamond << c->getEffectiveId();
	}
	if (spade.length() >= 3)
		all += spade;
	if (club.length() >= 3)
		all += club;
	if (heart.length() >= 3)
		all += heart;
	if (diamond.length() >= 3)
		all += diamond;
	if (all.length() < 3) return;

	if (all.length() == 3){
		DummyCard discard(all);
		room->throwCard(&discard, effect.to, effect.from);
		return;
	}

	room->notifyMoveToPile(effect.from, all, "guanxu", Player::PlaceHand, true);
	const Card*card2 = room->askForUseCard(effect.from, "@@guanxu2", "@guanxu2", 2, Card::MethodDiscard);
	room->notifyMoveToPile(effect.from, all, "guanxu", Player::PlaceHand, false);
	if (card2)
		room->throwCard(card2, effect.to, effect.from);
	else {
		int id = all.at(qrand() % all.length());
		Card::Suit suit = Sanguosha->getCard(id)->getSuit();
		QList<int> _discard;
		foreach(int id, all){
			if (Sanguosha->getCard(id)->getSuit() != suit) continue;
			_discard << id;
			if (_discard.length() >= 3) break;
		}
		DummyCard discard(_discard);
		room->throwCard(&discard, effect.to, effect.from);
	}
}

GuanxuChooseCard::GuanxuChooseCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void GuanxuChooseCard::onUse(Room*, CardUseStruct &) const
{
}

GuanxuDiscardCard::GuanxuDiscardCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void GuanxuDiscardCard::onUse(Room*, CardUseStruct &) const
{
}

class Guanxu : public ViewAsSkill
{
public:
	Guanxu() : ViewAsSkill("guanxu")
	{
		expand_pile = "#guanxu,#guanxuhand,#guanxudrawpile";
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
			return false;
		} else {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if (pattern == "@@guanxu1"){
				if (selected.length() >= 2) return false;
				if (selected.isEmpty())
					return Self->getPile("#guanxuhand").contains(to_select->getEffectiveId()) ||
							Self->getPile("#guanxudrawpile").contains(to_select->getEffectiveId());
				else {
					if (Self->getPile("#guanxuhand").contains(selected.first()->getEffectiveId()))
						return Self->getPile("#guanxudrawpile").contains(to_select->getEffectiveId());
					else
						return Self->getPile("#guanxuhand").contains(to_select->getEffectiveId());
				}
			} else if (pattern == "@@guanxu2")
				return selected.length() < 3 && Self->getPile("#guanxu").contains(to_select->getEffectiveId());
		}
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
			if (!cards.isEmpty()) return nullptr;
			return new GuanxuCard;
		} else {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if (pattern == "@@guanxu1"){
				if (cards.length() != 2) return nullptr;
				GuanxuChooseCard*card = new GuanxuChooseCard;
				card->addSubcards(cards);
				return card;
			} else {
				if (cards.length() != 3) return nullptr;
				GuanxuDiscardCard*card = new GuanxuDiscardCard;
				card->addSubcards(cards);
				return card;
			}
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("GuanxuCard");
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.startsWith("@@guanxu");
	}
};

class Yashi : public MasochismSkill
{
public:
	Yashi() : MasochismSkill("yashi")
	{
	}

	void onDamaged(ServerPlayer*player, const DamageStruct &damage) const
	{
		Room*room = player->getRoom();
		QStringList choices;
		if (damage.from && damage.from->isAlive())
			choices << "wuxiao=" + damage.from->objectName();
		GuanxuCard*card = new GuanxuCard;
		card->setSkillName("guanxu");
		card->deleteLater();
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (p->isKongcheng()|| player->isProhibited(p, card)) continue;
			choices << "guanxu";
			break;
		}

		if (choices.isEmpty() ||!player->askForSkillInvoke(this)) return;
		room->broadcastSkillInvoke(objectName());
		QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(damage));
		if (choice.startsWith("wuxiao")){
			room->addPlayerMark(damage.from, "&yashi");

			foreach(ServerPlayer*p, room->getAllPlayers())
				room->filterCards(p, p->getCards("he"), true);

			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
		} else {
			QList<ServerPlayer*> targets;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->isKongcheng() || player->isProhibited(p, card)) continue;
				targets << p;
			}
			if (targets.isEmpty()) return;
			ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@yashi-guanxu");
			room->useCard(CardUseStruct(card, player, target), true);
		}
	}
};

class YashiClear : public PhaseChangeSkill
{
public:
	YashiClear() : PhaseChangeSkill("#yashi")
	{
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive() && target->getPhase() == Player::RoundStart && target->getMark("&yashi") > 0;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		player->getRoom()->setPlayerMark(player, "&yashi", 0);

		foreach(ServerPlayer*p, room->getAllPlayers())
			room->filterCards(p, p->getCards("he"), false);

		JsonArray args;
		args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
		room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
		return false;
	}
};

class YashiInvalidity : public InvaliditySkill
{
public:
	YashiInvalidity() : InvaliditySkill("#yashi-invalidity")
	{
	}

	bool isSkillValid(const Player*player, const Skill*skill) const
	{
		return player->getMark("&yashi")<1 || skill->getFrequency(player) == Skill::Compulsory;
	}
};

NewZhoufuCard::NewZhoufuCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool NewZhoufuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getPile("incantation").isEmpty();
}

void NewZhoufuCard::use(Room*, ServerPlayer*, QList<ServerPlayer*> &targets) const
{
	targets.first()->addToPile("incantation", this);
}

TenyearZhoufuCard::TenyearZhoufuCard() : NewZhoufuCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

class NewZhoufuVS : public OneCardViewAsSkill
{
public:
	NewZhoufuVS(const QString &zhoufu) : OneCardViewAsSkill(zhoufu), zhoufu(zhoufu)
	{
		filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		if (zhoufu == "newzhoufu")
			return !player->hasUsed("NewZhoufuCard");
		else if (zhoufu == "tenyearzhoufu")
			return !player->hasUsed("TenyearZhoufuCard");
		return false;
	}

	const Card*viewAs(const Card*originalcard) const
	{
		if (zhoufu == "newzhoufu"){
			NewZhoufuCard*card = new NewZhoufuCard;
			card->addSubcard(originalcard);
			return card;
		} else if (zhoufu == "tenyearzhoufu"){
			TenyearZhoufuCard*card = new TenyearZhoufuCard;
			card->addSubcard(originalcard);
			return card;
		}
		return nullptr;
	}
private:
	QString zhoufu;
};

class NewZhoufu : public TriggerSkill
{
public:
	NewZhoufu(const QString &zhoufu) : TriggerSkill(zhoufu), zhoufu(zhoufu)
	{
		events << StartJudge << EventPhaseChanging << CardsMoveOneTime;
		view_as_skill = new NewZhoufuVS(zhoufu);
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == StartJudge){
			if (player->getPile("incantation").isEmpty()) return false;
			int card_id = player->getPile("incantation").first();

			JudgeStruct*judge = data.value<JudgeStruct*>();
			judge->card = Sanguosha->getCard(card_id);

			LogMessage log;
			log.type = "$ZhoufuJudge";
			log.from = player;
			log.arg = objectName();
			log.card_str = QString::number(judge->card->getEffectiveId());
			room->sendLog(log);

			room->moveCardTo(judge->card, nullptr, judge->who, Player::PlaceJudge,
				CardMoveReason(CardMoveReason::S_REASON_JUDGE, judge->who->objectName(), "", "", judge->reason), true);
			judge->updateResult();
			room->setTag("SkipGameRule", (int)triggerEvent);
		}else if (triggerEvent == CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player && move.from_places.contains(Player::PlaceSpecial)
					&& move.from_pile_names.contains("incantation")){
				room->addPlayerMark(player, "newzhoufu_lost-Clear");
			}
		} else {
			if (zhoufu != "newzhoufu") return false;
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				foreach(ServerPlayer*zhangbao, room->getAllPlayers()){
					if (zhangbao->isDead() || !zhangbao->hasSkill(objectName())) continue;
					bool send = true;
					foreach(ServerPlayer*p, room->getAllPlayers()){
						if (p->isDead() || p->getMark("newzhoufu_lost-Clear") <= 0) continue;
						if (send){
							send = false;
							room->sendCompulsoryTriggerLog(zhangbao, objectName(), true, true);
						}
						room->loseHp(HpLostStruct(p, 1, objectName(), zhangbao));
					}
				}
			}
		}
		return false;
	}
private:
	QString zhoufu;
};

class NewYingbing : public TriggerSkill
{
public:
	NewYingbing() : TriggerSkill("newyingbing")
	{
		events << CardUsed << CardResponded;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->getPile("incantation").length() > 0;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		const Card*card = nullptr;
		if (event == CardUsed)
			card = data.value<CardUseStruct>().card;
		else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (!res.m_isUse) return false;
			card = res.m_card;
		}

		if (card == nullptr || card->isKindOf("SkillCard")) return false;

		foreach(ServerPlayer*zhangbao, room->getAllPlayers()){
			if (zhangbao->isDead() || !zhangbao->hasSkill(objectName()) || player->getPile("incantation").isEmpty()) continue;

			foreach(int id, player->getPile("incantation")){
				const Card*c = Sanguosha->getCard(id);
				if (c->getSuit() != card->getSuit()) continue;
				room->sendCompulsoryTriggerLog(zhangbao, objectName(), true, true);
				zhangbao->drawCards(1, objectName());

				int num = zhangbao->getTag("newyingbing" + QString::number(id)).toInt();
				zhangbao->setTag("newyingbing" + QString::number(id), ++num);

				if (zhangbao->getTag("newyingbing" + QString::number(id)).toInt() > 1){
					zhangbao->removeTag("newyingbing" + QString::number(id));
					CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", objectName(), "");
					room->throwCard(Sanguosha->getCard(id), reason, nullptr);
				}
			}
		}
		return false;
	}
};

JianjieCard::JianjieCard()
{
}

bool JianjieCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
		if (targets.isEmpty())
			return to_select->getMark("&dragon_signet") > 0 || to_select->getMark("&phoenix_signet") > 0;
	}
	return targets.length() < 2;
}

bool JianjieCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return targets.length() == 2;
}

void JianjieCard::onUse(Room*room, CardUseStruct &use) const
{
	room->setTag("JianjieUse",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void JianjieCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	CardUseStruct use = room->getTag("JianjieUse").value<CardUseStruct>();
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
		QStringList choices;
		if (use.to.first()->getMark("&dragon_signet") > 0)
			choices << "dragon";
		if (use.to.first()->getMark("&phoenix_signet") > 0)
			choices << "phoenix";
		if (choices.isEmpty()) return;
		QString choice = room->askForChoice(source, "jianjie", choices.join("+"), QVariant::fromValue(use.to.first()));
		QString mark = "&" + choice + "_signet";
		use.to.first()->loseAllMarks(mark);
		use.to.last()->gainMark(mark);
		return;
	}
	use.to.first()->gainMark("&dragon_signet");
	use.to.last()->gainMark("&phoenix_signet");
}

class JianjieVS : public ZeroCardViewAsSkill
{
public:
	JianjieVS() : ZeroCardViewAsSkill("jianjie")
	{
		response_pattern = "@@jianjie!";
	}

	const Card*viewAs() const
	{
		return new JianjieCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("JianjieCard") && player->getMark("jianjie_Round-Keep") > 1;
	}
};

class Jianjie : public TriggerSkill
{
public:
	Jianjie() : TriggerSkill("jianjie")
	{
		events << EventPhaseStart << Death << MarkChanged;
		view_as_skill = new JianjieVS;
        global = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if(player->getPhase() == Player::RoundStart)
				room->addPlayerMark(player, "jianjie_Round-Keep");
			if (player->getPhase() != Player::Start || player->getMark("jianjie_Round-Keep") > 1) return false;
			if (!player->hasSkill(objectName())) return false;
			if (room->askForUseCard(player, "@@jianjie!", "@jianjie")) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			ServerPlayer*first = room->getAlivePlayers().at(qrand() % room->getAlivePlayers().length());
			ServerPlayer*second = room->getOtherPlayers(first).at(qrand() % room->getOtherPlayers(first).length());
			room->doAnimate(1, player->objectName(), first->objectName());
			room->doAnimate(1, player->objectName(), second->objectName());
			first->gainMark("&dragon_signet");
			second->gainMark("&phoenix_signet");
		} else if (event == Death){
			if (!player->hasSkill(objectName())) return false;
			DeathStruct death = data.value<DeathStruct>();
			if (death.who->getMark("&dragon_signet") > 0){
				ServerPlayer*to = room->askForPlayerChosen(player, room->getAlivePlayers(), "jianjie_dragon", "@jianjie-dragon", true, true);
				if (to){
					room->broadcastSkillInvoke(objectName());
					death.who->loseAllMarks("&dragon_signet");
					to->gainMark("&dragon_signet");
				}
			}
			if (death.who->getMark("&phoenix_signet") > 0){
				ServerPlayer*to = room->askForPlayerChosen(player, room->getAlivePlayers(), "jianjie_phoenix", "@jianjie-phoenix", true, true);
				if (to){
					room->broadcastSkillInvoke(objectName());
					death.who->loseAllMarks("&phoenix_signet");
					to->gainMark("&phoenix_signet");
				}
			}
		} else {
			MarkStruct mark = data.value<MarkStruct>();
			if (mark.name == "&dragon_signet"){
				QStringList skills;
				if(player->getMark("&dragon_signet")>0){
					skills << "jianjiehuoji";
					if(player->getMark("&phoenix_signet")>0)
						skills << "jianjieyeyan";
					else
						skills << "-jianjieyeyan";
				}else
					skills << "-jianjiehuoji" << "-jianjieyeyan";
				room->handleAcquireDetachSkills(player, skills, true, false);
			} else if (mark.name == "&phoenix_signet"){
				QStringList skills;
				if(player->getMark("&phoenix_signet")>0){
					skills << "jianjielianhuan";
					if(player->getMark("&dragon_signet")>0)
						skills << "jianjieyeyan";
					else
						skills << "-jianjieyeyan";
				}else
					skills << "-jianjielianhuan" << "-jianjieyeyan";
				room->handleAcquireDetachSkills(player, skills, true, false);
			}
		}
		return false;
	}
};

JianjieHuojiCard::JianjieHuojiCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool JianjieHuojiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	int id = getSubcards().first();
	const Card*card = Sanguosha->getCard(id);
	FireAttack*fire_attack = new FireAttack(card->getSuit(), card->getNumber());
	fire_attack->addSubcard(card);
	fire_attack->setSkillName("jianjiehuoji");
	fire_attack->deleteLater();
	if (Self->isLocked(fire_attack)) return false;
	return fire_attack->targetFilter(targets, to_select, Self);
}

void JianjieHuojiCard::onUse(Room*room, CardUseStruct &card_use) const
{
	int id = getSubcards().first();
	const Card*card = Sanguosha->getCard(id);
	FireAttack*fire_attack = new FireAttack(card->getSuit(), card->getNumber());
	fire_attack->addSubcard(card);
	fire_attack->setSkillName("jianjiehuoji");
	fire_attack->deleteLater();
	room->useCard(CardUseStruct(fire_attack, card_use.from, card_use.to));
}

class JianjieHuoji : public OneCardViewAsSkill
{
public:
	JianjieHuoji() : OneCardViewAsSkill("jianjiehuoji")
	{
		filter_pattern = ".|red";
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		FireAttack*fire_attack = new FireAttack(Card::NoSuit, 0);
		fire_attack->setSkillName("jianjiehuoji");
		fire_attack->deleteLater();
		return player->usedTimes("JianjieHuojiCard") < 3 && !player->isLocked(fire_attack);
	}

	const Card*viewAs(const Card*originalcard) const
	{
		JianjieHuojiCard*card = new JianjieHuojiCard;
		card->addSubcard(originalcard->getId());
		return card;
	}
};

JianjieLianhuanCard::JianjieLianhuanCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
	can_recast = true;
}

bool JianjieLianhuanCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	int id = getSubcards().first();
	const Card*card = Sanguosha->getCard(id);
	IronChain*iron_chain = new IronChain(card->getSuit(), card->getNumber());
	iron_chain->addSubcard(card);
	iron_chain->setSkillName("jianjielianhuan");
	iron_chain->deleteLater();
	if (Self->isLocked(iron_chain)) return false;
	return iron_chain->targetFilter(targets, to_select, Self);
}

bool JianjieLianhuanCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	int id = getSubcards().first();
	const Card*card = Sanguosha->getCard(id);
	IronChain*iron_chain = new IronChain(card->getSuit(), card->getNumber());
	iron_chain->addSubcard(card);
	iron_chain->setSkillName("jianjielianhuan");
	iron_chain->deleteLater();
	return iron_chain->targetsFeasible(targets, Self);
}

void JianjieLianhuanCard::onUse(Room*room, CardUseStruct &card_use) const
{
	int id = getSubcards().first();
	const Card*card = Sanguosha->getCard(id);
	IronChain*iron_chain = new IronChain(card->getSuit(), card->getNumber());
	iron_chain->addSubcard(card);
	iron_chain->setSkillName("jianjielianhuan");
	iron_chain->deleteLater();

	if (card_use.to.isEmpty()){
		CardMoveReason reason(CardMoveReason::S_REASON_RECAST, card_use.from->objectName());
		reason.m_skillName = getSkillName();
		QList<int> ids = getSubcards();
		QList<CardsMoveStruct> moves;
		foreach(int id, ids){
			CardsMoveStruct move(id, nullptr, Player::DiscardPile, reason);
			moves << move;
		}
		room->moveCardsAtomic(moves, true);
		card_use.from->broadcastSkillInvoke("@recast");

		LogMessage log;
		log.type = "#UseCard_Recast";
		log.from = card_use.from;
		log.card_str = iron_chain->toString();
		room->sendLog(log);

		card_use.from->drawCards(1, "recast");
		return;
	}
	room->useCard(CardUseStruct(iron_chain, card_use.from, card_use.to));
}

class JianjieLianhuan : public OneCardViewAsSkill
{
public:
	JianjieLianhuan() : OneCardViewAsSkill("jianjielianhuan")
	{
		filter_pattern = ".|club";
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		IronChain*iron_chain = new IronChain(Card::NoSuit, 0);
		iron_chain->setSkillName("jianjielianhuan");
		iron_chain->deleteLater();
		return player->usedTimes("JianjieLianhuanCard") < 3 && !player->isLocked(iron_chain) &&
				!player->isCardLimited(iron_chain, Card::MethodRecast);
	}

	const Card*viewAs(const Card*originalcard) const
	{
		JianjieLianhuanCard*card = new JianjieLianhuanCard;
		card->addSubcard(originalcard->getId());
		return card;
	}
};

void JianjieYeyanCard::damage(ServerPlayer*shenzhouyu, ServerPlayer*target, int point) const
{
	shenzhouyu->getRoom()->damage(DamageStruct("jianjieyeyan", shenzhouyu, target, point, DamageStruct::Fire));
}

GreatJianjieYeyanCard::GreatJianjieYeyanCard()
{
	mute = true;
	m_skillName = "jianjieyeyan";
}

bool GreatJianjieYeyanCard::targetFilter(const QList<const Player*> &, const Player*, const Player*) const
{
	Q_ASSERT(false);
	return false;
}

bool GreatJianjieYeyanCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	if (subcards.length() != 4) return false;
	QList<Card::Suit> allsuits;
	foreach(int cardId, subcards){
		const Card*card = Sanguosha->getCard(cardId);
		if (allsuits.contains(card->getSuit())) return false;
		allsuits.append(card->getSuit());
	}

	//We can only assign 2 damage to one player
	//If we select only one target only once, we assign 3 damage to the target
	if (QSet<const Player*>(targets.begin(), targets.end()).size() == 1)
		return true;
	else if (QSet<const Player*>(targets.begin(), targets.end()).size() == 2)
		return targets.size() == 3;
	return false;
}

bool GreatJianjieYeyanCard::targetFilter(const QList<const Player*> &targets, const Player*to_select,
	const Player*, int &maxVotes) const
{
	int i = 0;
	foreach(const Player*player, targets)
		if (player == to_select) i++;
	maxVotes = qMax(3 - targets.size(), 0) + i;
	return maxVotes > 0;
}

void GreatJianjieYeyanCard::onUse(Room*room, CardUseStruct &card_use) const
{
	QList<ServerPlayer*> targets;
	foreach(ServerPlayer*sp, card_use.to){
		sp->addMark("jianjieyeyan_damage");
		if(!targets.contains(sp))
			targets << sp;
	}
	card_use.to = targets;
	JianjieYeyanCard::onUse(room, card_use);
}

void GreatJianjieYeyanCard::use(Room*room, ServerPlayer*shenzhouyu, QList<ServerPlayer*> &targets) const
{
	room->broadcastSkillInvoke("jianjieyeyan");
	shenzhouyu->loseAllMarks("&dragon_signet");
	shenzhouyu->loseAllMarks("&phoenix_signet");
	room->doSuperLightbox("simahui", "jianjieyeyan");
	room->loseHp(HpLostStruct(shenzhouyu, 3, "jianjieyeyan", shenzhouyu));

	foreach(ServerPlayer*sp, targets){
		damage(shenzhouyu, sp, sp->getMark("jianjieyeyan_damage"));
		sp->setMark("jianjieyeyan_damage",0);
	}
}

SmallJianjieYeyanCard::SmallJianjieYeyanCard()
{
	mute = true;
	m_skillName = "jianjieyeyan";
}

bool SmallJianjieYeyanCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return !targets.isEmpty();
}

bool SmallJianjieYeyanCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
	return targets.length() < 3;
}

void SmallJianjieYeyanCard::use(Room*room, ServerPlayer*shenzhouyu, QList<ServerPlayer*> &targets) const
{
	room->broadcastSkillInvoke("jianjieyeyan");
	shenzhouyu->loseAllMarks("&dragon_signet");
	shenzhouyu->loseAllMarks("&phoenix_signet");
	room->doSuperLightbox(shenzhouyu, "jianjieyeyan");
	JianjieYeyanCard::use(room, shenzhouyu, targets);
}

void SmallJianjieYeyanCard::onEffect(CardEffectStruct &effect) const
{
	damage(effect.from, effect.to, 1);
}

class JianjieYeyan : public ViewAsSkill
{
public:
	JianjieYeyan() : ViewAsSkill("jianjieyeyan")
	{
		frequency = Limited;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("&dragon_signet") > 0 && player->getMark("&phoenix_signet") > 0;
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (selected.length() >= 4)
			return false;

		if (to_select->isEquipped())
			return false;

		if (Self->isJilei(to_select))
			return false;

		foreach(const Card*item, selected){
			if (to_select->getSuit() == item->getSuit())
				return false;
		}

		return true;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.length() == 0)
			return new SmallJianjieYeyanCard;
		if (cards.length() != 4)
			return nullptr;

		GreatJianjieYeyanCard*card = new GreatJianjieYeyanCard;
		card->addSubcards(cards);

		return card;
	}
};

class Chenghao : public TriggerSkill
{
public:
	Chenghao() : TriggerSkill("chenghao")
	{
		events << DamageInflicted;
		frequency = Frequent;
	}

	int getPriority(TriggerEvent event) const
	{
		return TriggerSkill::getPriority(event) + 1;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (damage.nature == DamageStruct::Normal) return false;
			if (player->isDead() || !player->isChained() || damage.chain) return false;
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			if (!p->askForSkillInvoke(this)) continue;
			p->peiyin(this);

			QList<ServerPlayer*> _player;
			_player.append(p);
			int n = 0;
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if (p->isChained())
					n++;
			}
			if (n <= 0) return false;

			QList<int> ids = room->getNCards(n, false);
			CardsMoveStruct move(ids, nullptr, p, Player::PlaceTable, Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_PREVIEW, p->objectName(), objectName(), ""));
			QList<CardsMoveStruct> moves;
			moves.append(move);
			room->notifyMoveCards(true, moves, false, _player);
			room->notifyMoveCards(false, moves, false, _player);

			QList<int> origin_ids = ids;
			while (room->askForYiji(p, ids, objectName(), true, false, true, -1, room->getAlivePlayers())){
				CardsMoveStruct move(QList<int>(), p, nullptr, Player::PlaceHand, Player::PlaceTable,
					CardMoveReason(CardMoveReason::S_REASON_PREVIEW, p->objectName(), objectName(), ""));
				foreach(int id, origin_ids){
					if (room->getCardPlace(id) != Player::DrawPile){
						move.card_ids << id;
						ids.removeOne(id);
					}
				}
				origin_ids = ids;
				QList<CardsMoveStruct> moves;
				moves.append(move);
				room->notifyMoveCards(true, moves, false, _player);
				room->notifyMoveCards(false, moves, false, _player);
				if (!p->isAlive())
					break;
			}

			if (!ids.isEmpty()){
				if (p->isAlive()){
					CardsMoveStruct move(ids, p, nullptr, Player::PlaceHand, Player::PlaceTable,
										CardMoveReason(CardMoveReason::S_REASON_PREVIEW, p->objectName(), objectName(), ""));
					QList<CardsMoveStruct> moves;
					moves.append(move);
					room->notifyMoveCards(true, moves, false, _player);
					room->notifyMoveCards(false, moves, false, _player);

					DummyCard*dummy = new DummyCard(ids);
					p->obtainCard(dummy, false);
					delete dummy;
				} else {
					DummyCard*dummy = new DummyCard(ids);
					CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, p->objectName(), objectName(), "");
					room->throwCard(dummy, reason, nullptr);
					delete dummy;
				}

			}
		}
		return false;
	}
};

class Yinshi : public TriggerSkill
{
public:
	Yinshi() : TriggerSkill("yinshi")
	{
		events << DamageInflicted;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.nature != DamageStruct::Normal || (damage.card && damage.card->isKindOf("TrickCard"))){
			if (player->getMark("&dragon_signet") > 0 || player->getMark("&phoenix_signet") > 0 || player->getEquip(1)) return false;
			LogMessage log;
			log.type = "#YinshiPrevent";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());
			return true;
		}
		return false;
	}
};

class OLYuhua :public TriggerSkill
{
public:
	OLYuhua() : TriggerSkill("olyuhua")
	{
		events << EventPhaseProceeding;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (player->getPhase() != Player::Discard)
			return false;
		QList<int> not_basics;
		foreach(int id, player->handCards()){
			if (!Sanguosha->getCard(id)->isKindOf("BasicCard"))
				not_basics << id;
		}
		room->sendCompulsoryTriggerLog(player, "olyuhua", true, true);
		if (not_basics.isEmpty()) return false;
		room->ignoreCards(player,not_basics);
		return false;
	}
};

class OLQirang : public TriggerSkill
{
public:
	OLQirang() : TriggerSkill("olqirang")
	{
		events << CardsMoveOneTime;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent , Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!move.to)
			return false;
		if (move.to->objectName() != player->objectName())
			return false;
		if (move.to_place != Player::PlaceEquip)
			return false;
		if (!player->askForSkillInvoke("olqirang", data))
			return false;

		room->broadcastSkillInvoke("olqirang");

		QList<int> trickIDs;
		foreach(int id, room->getDrawPile()){
			if (Sanguosha->getCard(id)->isKindOf("TrickCard"))
				trickIDs.append(id);
		}

		if (trickIDs.isEmpty()){
			LogMessage msg;
			msg.type = "#olqirang-failed";
			room->sendLog(msg);
			return false;
		}

		/*room->fillAG(trickIDs, player);
		int trick_id = room->askForAG(player, trickIDs, true, "olqirang");
		room->clearAG(player);*/
		int trick_id = trickIDs.at(qrand() % trickIDs.length());
		if (trick_id > -1)
			room->obtainCard(player, trick_id, true);

		return false;
	}
};

YinbingCard::YinbingCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void YinbingCard::use(Room*, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	source->addToPile("yinbing", this);
}

class YinbingViewAsSkill : public ViewAsSkill
{
public:
	YinbingViewAsSkill() : ViewAsSkill("yinbing")
	{
		response_pattern = "@@yinbing";
	}

	bool viewFilter(const QList<const Card*> &, const Card*to_select) const
	{
		return to_select->getTypeId() != Card::TypeBasic;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.length() == 0) return nullptr;

		Card*acard = new YinbingCard;
		acard->addSubcards(cards);
		acard->setSkillName(objectName());
		return acard;
	}
};

class Yinbing : public TriggerSkill
{
public:
	Yinbing() : TriggerSkill("yinbing")
	{
		events << EventPhaseStart << Damaged;
		view_as_skill = new YinbingViewAsSkill;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish && !player->isNude()){
			room->askForUseCard(player, "@@yinbing", "@yinbing", -1, Card::MethodNone);
		} else if (triggerEvent == Damaged && !player->getPile("yinbing").isEmpty()){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))){
				room->sendCompulsoryTriggerLog(player, objectName());

				QList<int> ids = player->getPile("yinbing");
				room->fillAG(ids, player);
				int id = room->askForAG(player, ids, false, objectName());
				room->clearAG(player);
				room->throwCard(id, nullptr);
			}
		}

		return false;
	}
};

class Juedi : public PhaseChangeSkill
{
public:
	Juedi() : PhaseChangeSkill("juedi")
	{
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return PhaseChangeSkill::triggerable(target) && target->getPhase() == Player::Start
			&& !target->getPile("yinbing").isEmpty();
	}

	bool onPhaseChange(ServerPlayer*target, Room*room) const
	{
		if (!room->askForSkillInvoke(target, objectName())) return false;
		room->broadcastSkillInvoke(objectName());

		QList<ServerPlayer*> playerlist;
		foreach(ServerPlayer*p, room->getOtherPlayers(target)){
			if (p->getHp() <= target->getHp())
				playerlist << p;
		}
		ServerPlayer*to_give = nullptr;
		if (!playerlist.isEmpty())
			to_give = room->askForPlayerChosen(target, playerlist, objectName(), "@juedi", true);
		if (to_give){
			room->recover(to_give, RecoverStruct("juedi", target));
			DummyCard*dummy = new DummyCard(target->getPile("yinbing"));
			room->obtainCard(to_give, dummy);
			delete dummy;
		} else {
			int len = target->getPile("yinbing").length();
			target->clearOnePrivatePile("yinbing");
			if (target->isAlive())
				room->drawCards(target, len, objectName());
		}
		return false;
	}
};

class SpZhenwei : public TriggerSkill
{
public:
	SpZhenwei() : TriggerSkill("spzhenwei")
	{
		events << TargetConfirming << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if (!p->getPile("zhenweipile").isEmpty()){
						DummyCard*dummy = new DummyCard(p->getPile("zhenweipile"));
						room->obtainCard(p, dummy);
						delete dummy;
					}
				}
			}
			return false;
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.to.length() != 1 || !(use.card->isKindOf("Slash") || (use.card->getTypeId() == Card::TypeTrick && use.card->isBlack())))
				return false;
			ServerPlayer*wp = room->findPlayerBySkillName(objectName());
			if (wp == nullptr || wp->getHp() <= player->getHp())
				return false;
			if (!room->askForCard(wp, "..", QString("@sp_zhenwei:%1").arg(player->objectName()), data, objectName()))
				return false;
			room->broadcastSkillInvoke(objectName());
			if (room->askForChoice(wp, objectName(), "draw+null", data) == "draw"){
				room->drawCards(wp, 1, objectName());
				if (use.card->isKindOf("Slash")){
					if (!use.from->canSlash(wp, use.card, false))
						return false;
				}
				if (use.card->isKindOf("DelayedTrick")){
					if (!use.from||use.from->isProhibited(wp, use.card))
						return false;
					room->moveCardTo(use.card, wp, Player::PlaceDelayedTrick, true);
				} else {
					if (use.from->isProhibited(wp, use.card))
						return false;
					use.to.clear();
					use.to << wp;
					data = QVariant::fromValue(use);
				}
			} else {
				room->setCardFlag(use.card, "zhenweinull");
				if(use.from)
					use.from->addToPile("zhenweipile", use.card);
				use.nullified_list << "_ALL_TARGETS";
				data = QVariant::fromValue(use);
			}
		}
		return false;
	}
};

class Tuifeng : public TriggerSkill
{
public:
	Tuifeng() : TriggerSkill("tuifeng")
	{
		events << Damaged << EventPhaseStart;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			for (int i = 1; i <= damage.damage; i++){
				if (player->isDead() || player->isNude()) break;
				const Card*card = room->askForCard(player, "..", "tuifeng-put", data, Card::MethodNone);
				if (!card) break;
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(player, objectName());
				player->addToPile("tfeng", card);
			}
		} else {
			if (player->getPhase() != Player::Start) return false;
			int n = player->getPile("tfeng").length();
			if (n <= 0) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			player->clearOnePrivatePile("tfeng");
			player->drawCards(2* n, objectName());
			room->addSlashCishu(player, n);
		}
		return false;
	}
};

class Fuqi : public TriggerSkill
{
public:
	Fuqi() : TriggerSkill("fuqi")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard")) return false;

		LogMessage log;
		foreach(ServerPlayer*p, room->getOtherPlayers(use.from)){
			if (p->distanceTo(use.from) != 1) continue;
			log.to << p;
			use.no_respond_list << p->objectName();
		}
		if (log.to.isEmpty()) return false;

		log.type = "#FuqiNoResponse";
		log.from = use.from;
		log.arg = objectName();
		log.card_str = use.card->toString();
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(use.from, objectName());

		data = QVariant::fromValue(use);
		return false;
	}
};

class Jiaozi : public TriggerSkill
{
public:
	Jiaozi() : TriggerSkill("jiaozi")
	{
		events << DamageCaused << DamageInflicted;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (p->getHandcardNum() >= player->getHandcardNum())
				return false;
		}

		LogMessage log;
		log.from = player;
		log.arg = objectName();
		if (event == DamageCaused){
			if (damage.to->isDead()) return false;
			log.type = "#JiaoziDoDamage";
			log.to << damage.to;
		} else {
			log.type = "#JiaoziSufferDamage";
		}
		log.arg2 = QString::number(++damage.damage);
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());

		data = QVariant::fromValue(damage);
		return false;
	}
};

class Qingzhong : public TriggerSkill
{
public:
	Qingzhong() : TriggerSkill("qingzhong")
	{
		events << EventPhaseStart << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (player->getPhase() != Player::Play) return false;
		if (event == EventPhaseStart){
			if (!player->hasSkill(objectName()) || !player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player, "qingzhong-PlayClear");
			player->drawCards(2, objectName());
		} else {
			if (player->getMark("qingzhong-PlayClear") <= 0) return false;
			room->setPlayerMark(player, "qingzhong-PlayClear", 0);
			if (player->isKongcheng()) return false;
			int n = player->getHandcardNum();
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->getHandcardNum() < n)
					n = p->getHandcardNum();
			}
			QList<ServerPlayer*> least_hand;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->getHandcardNum() <= n)
					least_hand << p;
			}
			if (least_hand.contains(player))
				least_hand.removeOne(player);
			if (least_hand.isEmpty()) return false;

			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			ServerPlayer*target = room->askForPlayerChosen(player, least_hand, objectName(), "@qingzhong-invoke");
			room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

			LogMessage log;
			log.type = "#Dimeng";
			log.from = player;
			log.to << target;
			log.arg = QString::number(player->getHandcardNum());
			log.arg2 = QString::number(target->getHandcardNum());
			room->sendLog(log);
			QList<CardsMoveStruct> exchangeMove;
			CardsMoveStruct move1(player->handCards(), target, Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_SWAP, player->objectName(), target->objectName(), "qingzhong", ""));
			CardsMoveStruct move2(target->handCards(), player, Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_SWAP, target->objectName(), player->objectName(), "qingzhong", ""));
			exchangeMove.push_back(move1);
			exchangeMove.push_back(move2);
			room->moveCardsAtomic(exchangeMove, false);
			room->getThread()->delay();
		}
		return false;
	}
};

class WeijingVS : public ZeroCardViewAsSkill
{
public:
	WeijingVS() : ZeroCardViewAsSkill("weijing")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return Slash::IsAvailable(player) && player->getMark("weijing_lun") <= 0;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
			return false;
		return (pattern == "jink" || pattern.contains("slash") || pattern.contains("Slash")) && player->getMark("weijing_lun") <= 0;
	}

	const Card*viewAs() const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
			Slash*slash = new Slash(Card::NoSuit, -1);
			slash->setSkillName(objectName());
			return slash;
		}

		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
			return nullptr;
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "jink"){
			Jink*jink = new Jink(Card::NoSuit, 0);
			jink->setSkillName(objectName());
			return jink;
		} else if (pattern.contains("slash") || pattern.contains("Slash")){
			Slash*slash = new Slash(Card::NoSuit, 0);
			slash->setSkillName(objectName());
			return slash;
		} else
			return nullptr;
	}
};

class Weijing : public TriggerSkill
{
public:
	Weijing() : TriggerSkill("weijing")
	{
		events << PreCardUsed << PreCardResponded;
		view_as_skill = new WeijingVS;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		const Card*card = nullptr;
		if (event == PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard") || !use.card->getSkillNames().contains(objectName())) return false;
			card = use.card;
		} else {
			CardResponseStruct resp = data.value<CardResponseStruct>();
			if (resp.m_card->isKindOf("SkillCard") || !resp.m_card->getSkillNames().contains(objectName())) return false;
			card = resp.m_card;
		}
		if (!card) return false;
		room->addPlayerMark(player, "weijing_lun");
		return false;
	}
};

class Xingzhao : public TriggerSkill
{
public:
	Xingzhao(const QString &xingzhao) : TriggerSkill(xingzhao), xingzhao(xingzhao)
	{
		events << CardUsed <<  EventPhaseChanging << ConfirmDamage;
		frequency = Compulsory;
	}

	int getWounded(Room*room) const
	{
		int n = 0;
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (p->isWounded())
				n++;
		}
		return n;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == CardUsed){
			if (getWounded(room) < 2) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("EquipCard")) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->drawCards(1, objectName());
		} else if (event == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::Discard || getWounded(room) < 3 || player->isSkipped(Player::Discard)) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->skip(Player::Discard);
		} else {
			if (objectName() != "tenyearxingzhao" || getWounded(room) < 4) return false;
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.chain || damage.transfer || !damage.by_user || !damage.card || damage.card->isKindOf("SkillCard")) return false;
			room->sendCompulsoryTriggerLog(player, this);
			damage.damage++;
			data = QVariant::fromValue(damage);
		}
		return false;
	}
private:
	QString xingzhao;
};

class XingzhaoXunxun : public TriggerSkill
{
public:
	XingzhaoXunxun(const QString &xingzhao) : TriggerSkill("#" + xingzhao + "-xunxun"), xingzhao(xingzhao)
	{
		events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill << EventLoseSkill << Revived;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}

	void XunxunChange(Room*room, ServerPlayer*player) const
	{
		int n = 0;
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (p->isWounded())
				n++;
		}
		if(n<1){
			if(player->getTag(xingzhao + "_xunxun").toBool() && player->hasSkill("xunxun", true)){
				player->removeTag(xingzhao + "_xunxun");
				room->detachSkillFromPlayer(player, "xunxun");
			}
		}else{
			if(!player->getTag(xingzhao + "_xunxun").toBool() && !player->hasSkill("xunxun", true)){
				player->setTag(xingzhao + "_xunxun", true);
				room->sendCompulsoryTriggerLog(player, xingzhao, true, true);
				room->acquireSkill(player, "xunxun");
			}
		}
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventLoseSkill){
			if (data.toString() == xingzhao){
				if(player->getTag(xingzhao + "_xunxun").toBool() && player->hasSkill("xunxun", true)){
					player->removeTag(xingzhao + "_xunxun");
					room->detachSkillFromPlayer(player, "xunxun");
				}
			}
		}else{
			if (player->hasSkill(xingzhao))
				XunxunChange(room, player);
		}
		return false;
	}
private:
	QString xingzhao;
};

class Lianpian : public TriggerSkill
{
public:
	Lianpian() : TriggerSkill("lianpian")
	{
		events << TargetSpecifying <<  CardFinished << EventPhaseChanging;
        global = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseChanging){
			room->safeSetPlayerProperty(player,"lianpian_targets", QStringList());
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (event == TargetSpecifying){
				if (player->getMark("lianpian-PlayClear") >= 3) return false;
				if (!room->CardInTable(use.card)||!player->hasSkill(objectName())) return false;
				QList<ServerPlayer*> targets;
				QStringList names = player->property("lianpian_targets").toStringList();
				foreach(ServerPlayer*p, use.to){
					if (p->isAlive() && names.contains(p->objectName()))
						targets << p;
				}
				if (targets.isEmpty()) return false;
				if (!player->askForSkillInvoke(this)) return false;
				room->broadcastSkillInvoke(objectName());
				room->addPlayerMark(player, "lianpian-PlayClear");
				player->drawCards(1, objectName());
				if (targets.contains(player))
					targets.removeOne(player);
				ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@lianpian-give:" + use.card->objectName(), true, true);
				if (!target) return false;
				CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "lianpian", "");
				room->obtainCard(target, use.card, reason, true);
			} else {
				if (!use.from || use.from->isDead()) return false;
				QStringList names;
				foreach(ServerPlayer*p, use.to){
					if (p->isAlive()) names << p->objectName();
				}
				room->safeSetPlayerProperty(use.from,"lianpian_targets", names);
			}
		}
		return false;
	}
};

class Xiying : public TriggerSkill
{
public:
	Xiying(const QString &xiying) : TriggerSkill(xiying), xiying(xiying)
	{
		events << EventPhaseStart << Death << EventPhaseChanging << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == EventPhaseStart)
			return TriggerSkill::getPriority(event);
		return 5;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if (xiying == "secondxiying" && player->isAlive() && player->getPhase() == Player::Finish){
				int mark = player->getMark("secondxiying_damage-Clear");
				for (int i = 0; i < mark; i++){
					QList<int> ids;
					foreach(int id, room->getDrawPile()){
						const Card*card = Sanguosha->getCard(id);
						if (card->isKindOf("Slash") || (card->isDamageCard() && card->isNDTrick()))
							ids << id;
					}
					if (ids.isEmpty()){
						LogMessage log;
						log.type = "#SecondXiyingFail";
						log.arg = objectName();
						log.from = player;
						room->sendLog(log);
						room->broadcastSkillInvoke(this);
						room->notifySkillInvoked(player, objectName());
						break;
					} else {
						room->sendCompulsoryTriggerLog(player, this);
						int id = ids.at(qrand() % ids.length());
						room->obtainCard(player, id, true);
					}
				}
			}

			if (!player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return false;
			if (!player->canDiscard(player, "h")) return false;
			if (!room->askForCard(player, "EquipCard,TrickCard|.|.|hand", xiying + "-invoke", data, objectName())) return false;
			room->addPlayerMark(player, xiying + "_used-PlayClear");
			room->broadcastSkillInvoke(objectName());
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (!room->askForDiscard(p, objectName(), 1, 1, true, true, xiying + "-discard")){
					LogMessage log;
					log.type = "#XiyingEffect";
					log.from = p;
					room->sendLog(log);
					room->setPlayerCardLimitation(p, "use,response", ".", true);
					room->addPlayerMark(p, xiying + "_limit-Clear");
				}
			}
		} else if (event == EventPhaseEnd){
			if (xiying != "secondxiying" || player->isDead() || player->getPhase() != Player::Play) return false;
			if (player->getMark("secondxiying_used-PlayClear") <= 0 || player->getMark("damage_point_play_phase") <= 0) return false;
			room->setPlayerMark(player, "secondxiying_used-PlayClear", 0);
			room->addPlayerMark(player, "secondxiying_damage-Clear");
		} else {
			if (event == Death){
				DeathStruct death = data.value<DeathStruct>();
				if (death.who != player || (room->getCurrent() && player != room->getCurrent())) return false;
			} else {
				PhaseChangeStruct change = data.value<PhaseChangeStruct>();
				if (change.to != Player::NotActive) return false;
			}
			foreach(ServerPlayer*p, room->getAllPlayers(true)){
				int mark = p->getMark(xiying + "_limit-Clear");
				room->setPlayerMark(p, xiying + "_limit-Clear", 0);
				for (int i = 0; i < mark; i++)
					room->removePlayerCardLimitation(p, "use,response", ".$1");
			}
		}
		return false;
	}
private:
	QString xiying;
};

class SpYoudi : public PhaseChangeSkill
{
public:
	SpYoudi() : PhaseChangeSkill("spyoudi")
	{
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Finish || player->isKongcheng()) return false;
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (p->canDiscard(player, "h"))
				targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@spyoudi-invoke", true, true);
		if (!target) return false;

		room->broadcastSkillInvoke(objectName());
		if (!target->canDiscard(player, "h")) return false;
		int card_id = room->askForCardChosen(target, player, "h", objectName(), false, Card::MethodDiscard);
		room->throwCard(card_id, player, target);
		const Card*card = Sanguosha->getCard(card_id);
		if (!card->isKindOf("Slash")){
			if (target->isAlive() && !target->isNude()){
				int card_id = room->askForCardChosen(player, target, "he", objectName());
				CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
				room->obtainCard(player, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);
			}
		}
		if (!card->isBlack())
			player->drawCards(1, objectName());
		return false;
	}
};

DuanfaCard::DuanfaCard()
{
	target_fixed = true;
}

void DuanfaCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	int n = subcardsLength();
	room->addPlayerMark(source, "duanfa_num-PlayClear", n);
	source->drawCards(n, "duanfa");
}

class Duanfa : public ViewAsSkill
{
public:
	Duanfa() : ViewAsSkill("duanfa")
	{
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		int n = Self->getMaxHp() - Self->getMark("duanfa_num-PlayClear");
		return !Self->isJilei(to_select) && to_select->isBlack() && selected.length() < n;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		DuanfaCard*c = new DuanfaCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMaxHp() > player->getMark("duanfa_num-PlayClear");
	}
};

class Qigong : public TriggerSkill
{
public:
	Qigong() : TriggerSkill("qigong")
	{
		events << CardOffset;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardEffectStruct effect = data.value<CardEffectStruct>();
		if (effect.card->isKindOf("Slash")||effect.multiple || !effect.to->isAlive()) return false;
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (!p->canSlash(effect.to, nullptr, false)) continue;
			targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer*target = room->askForPlayerChosen(player, targets, objectName(), "@qigong-invoke:" + effect.to->objectName(), true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(this);

		room->askForUseSlashTo(target, effect.to, "@qigong-slash:" + effect.to->objectName(), false, false, true, nullptr, nullptr, "SlashNoRespond");

		return false;
	}
};

LiehouCard::LiehouCard()
{
}

bool LiehouCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && !to_select->isKongcheng() && Self->inMyAttackRange(to_select);
}

void LiehouCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isDead() || effect.to->isKongcheng()) return;
	Room*room = effect.from->getRoom();
	const Card*card = room->askForExchange(effect.to, "liehou", 1, 1, false, "@liehou-give1:" + effect.from->objectName());
	room->giveCard(effect.to, effect.from, card, "liehou");

	if (effect.from->isDead() || effect.from->isKongcheng()) return;

	QList<ServerPlayer*> targets;
	foreach(ServerPlayer*p, room->getOtherPlayers(effect.to)){
		if (!effect.from->inMyAttackRange(p)) continue;
		targets << p;
	}
	if (targets.isEmpty()) return;

	effect.from->setTag("LiehouTarget", QVariant::fromValue(effect.to));

	QList<int> hands = effect.from->handCards();
	room->askForYiji(effect.from, hands, "liehou", false, false, false, 1, targets, CardMoveReason(), "@liehou-give2");

	effect.from->removeTag("LiehouTarget");
}

class Liehou : public ZeroCardViewAsSkill
{
public:
	Liehou() : ZeroCardViewAsSkill("liehou")
	{
	}

	const Card*viewAs() const
	{
		return new LiehouCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("LiehouCard");
	}
};

class Lanjiang : public PhaseChangeSkill
{
public:
	Lanjiang() : PhaseChangeSkill("lanjiang")
	{
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Finish) return false;

		QList<ServerPlayer*> players;
		int hand = player->getHandcardNum();
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->getHandcardNum() >= hand)
				players << p;
		}
		if (players.isEmpty() || !player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(this);

		foreach(ServerPlayer*p, players){
			if (player->isDead()) return false;
			if (p->isDead()) continue;
			if (!p->askForSkillInvoke("lanjiang_draw", "lanjiang:" + player->objectName(), false)) continue;
			player->drawCards(1, objectName());
		}
		if (player->isDead()) return false;

		QList<ServerPlayer*> targets;
		hand = player->getHandcardNum();
		foreach(ServerPlayer*p, players){
			if (p->isDead()) continue;
			if (p->getHandcardNum() == hand)
				targets << p;
		}
		if (targets.isEmpty()) return false;

		ServerPlayer*t = room->askForPlayerChosen(player, targets, objectName(), "@lanjiang-damage", true);
		if (!t) return false;
		room->doAnimate(1, player->objectName(), t->objectName());
		room->damage(DamageStruct(objectName(), player, t));

		if (player->isDead()) return false;

		targets.clear();
		hand = player->getHandcardNum();
		foreach(ServerPlayer*p, players){
			if (p->isDead()) continue;
			if (p->getHandcardNum() < hand)
				targets << p;
		}
		if (targets.isEmpty()) return false;

		t = room->askForPlayerChosen(player, targets, objectName(), "@lanjiang-draw");
		room->doAnimate(1, player->objectName(), t->objectName());
		t->drawCards(1, objectName());
		return false;
	}
};

class JixianZL : public TriggerSkill
{
public:
	JixianZL() : TriggerSkill("jixianzl")
	{
		events << EventPhaseEnd;
	}

	static int getSkills(ServerPlayer*player)
	{
		int num = 0;
		foreach(const Skill*sk, player->getVisibleSkillList()){
			if (sk->isAttachedLordSkill()) continue;
			num++;
		}
		return num;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (player->getPhase() != Player::Draw) return false;

		Slash*slash = new Slash(Card::NoSuit, 0);
		slash->deleteLater();
		slash->setSkillName("jixianzl");

		QList<ServerPlayer*> players;
		int skill = getSkills(player);
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (!player->canSlash(p, slash, false)) continue;
			if (!p->getEquips().isEmpty())
				players << p;
			else if (p->getLostHp() == 0)
				players << p;
			else if (getSkills(p) > skill)
				players << p;
		}
		if (players.isEmpty()) return false;

		ServerPlayer*t = room->askForPlayerChosen(player, players, objectName(), "@jixianzl-slash", true);
		if (!t) return false;

		room->setCardFlag(slash, "jixianzl_slash_to_" + t->objectName());
		room->setCardFlag(slash, "jixianzl_slash_from_" + player->objectName());

		room->useCard(CardUseStruct(slash, player, t), true);
		return false;
	}
};

class JixianZLEffect : public TriggerSkill
{
public:
	JixianZLEffect() : TriggerSkill("#jixianzl")
	{
		events << CardFinished << DamageDone;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash")) return false;
			if (!damage.card->hasFlag("jixianzl_slash_to_" + player->objectName())) return false;
			room->setCardFlag(damage.card, "jixianzl_slash_damage");
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card || !use.card->isKindOf("Slash")) return false;

			ServerPlayer*from = nullptr,*to = nullptr;
			foreach(QString flag, use.card->getFlags()){
				if (!flag.startsWith("jixianzl_slash_from_")) continue;
				QStringList flags = flag.split("_");
				if (flags.length() != 4) continue;
				from = room->findChild<ServerPlayer*>(flags.last());
				break;
			}
			foreach(QString flag, use.card->getFlags()){
				if (!flag.startsWith("jixianzl_slash_to_")) continue;
				QStringList flags = flag.split("_");
				if (flags.length() != 4) continue;
				to = room->findChild<ServerPlayer*>(flags.last());
				break;
			}

			if (!from || from->isDead()) return false;

			if (to && to->isAlive()){
				int num = 0;
				if (!to->getEquips().isEmpty())
					num++;
				if (to->getLostHp() == 0)
					num++;
				if (JixianZL::getSkills(to) > JixianZL::getSkills(from))
					num++;
				from->drawCards(num, "jixianzl");
			}

			if (use.card->hasFlag("jixianzl_slash_damage")) return false;
			room->loseHp(HpLostStruct(from, 1, "jixianzl", from));
		}
		return false;
	}
};

class Saodi : public TriggerSkill
{
public:
	Saodi() : TriggerSkill("saodi")
	{
		events << TargetSpecifying;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash") && !use.card->isNDTrick()) return false;
		if (use.card->isKindOf("Collateral")) return false;
		ServerPlayer*first = use.to.first();
		if (use.to.length() != 1 || first == player) return false;

		QList<ServerPlayer*> rights, lefts;

		ServerPlayer*next = player->getNextAlive();
		while (next != first){
			rights << next;
			next = next->getNextAlive();
		}

		next = first->getNextAlive();
		while (next != player){
			lefts << next;
			next = next->getNextAlive();
		}

		int right = rights.length(), left = lefts.length();

		if (right <= 0 || left <= 0) return false;
		QString name = use.card->objectName();
		QStringList choices;
		if (right < left)
			choices << "right=" + name;
		else if (left < right)
			choices << "left=" + name;
		else {
			choices << "right=" + name;
			choices << "left=" + name;
		}
		choices << "cancel";

		QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		if (choice == "cancel") return false;

		LogMessage log;
		log.type = "#QiaoshuiAdd";
		log.from = player;
		log.card_str = use.card->toString();
		log.arg = objectName();
		log.to = choice.startsWith("right") ? rights : lefts;
		room->sendLog(log);

		foreach(ServerPlayer*p, log.to)
			room->doAnimate(1, player->objectName(), p->objectName());

		room->broadcastSkillInvoke(this);
		room->notifySkillInvoked(player, objectName());

		use.to << log.to;
		room->sortByActionOrder(use.to);
		data = QVariant::fromValue(use);
		return false;
	}
};

class Zhuitao : public TriggerSkill
{
public:
	Zhuitao() : TriggerSkill("zhuitao")
	{
		events << EventPhaseStart << Damage;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if (player->getPhase() != Player::Start) return false;
			QList<ServerPlayer*> players;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->getMark("&zhuitao+#" + player->objectName()) <= 0)
					players << p;
			}
			if (players.isEmpty()) return false;
			ServerPlayer*t = room->askForPlayerChosen(player, players, objectName(), "@zhuitao-invoke", true, true);
			if (!t) return false;
			room->broadcastSkillInvoke(this);
			room->addPlayerMark(t, "&zhuitao+#" + player->objectName());
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.to->isDead() || damage.to->getMark("&zhuitao+#" + player->objectName()) <= 0) return false;
			LogMessage log;
			log.type = "#ZhuitaoDistance";
			log.arg = objectName();
			log.from = player;
			log.to << damage.to;
			room->sendLog(log);
			room->broadcastSkillInvoke(this);
			room->notifySkillInvoked(player, objectName());
			room->setPlayerMark(damage.to, "&zhuitao+#" + player->objectName(), 0);
		}
		return false;
	}
};

class ZhuitaoDistance : public DistanceSkill
{
public:
	ZhuitaoDistance() : DistanceSkill("#zhuitao")
	{
	}

	int getCorrect(const Player*from, const Player*to) const
	{
		if (from->hasSkill("zhuitao"))
			return -to->getMark("&zhuitao+#"+from->objectName());
		return 0;
	}
};

class Tongxie : public PhaseChangeSkill
{
public:
	Tongxie() : PhaseChangeSkill("tongxie")
	{
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Play) return false;
		QList<ServerPlayer*> targets = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0, 2, "@tongxie-target", true);
		if (targets.isEmpty()) return false;
		player->peiyin(this);

		foreach(ServerPlayer*p, targets)
			room->setPlayerMark(p, "&tongxie+#" + player->objectName(), 1);

		if (targets.length() == 1)
			targets.first()->drawCards(1, objectName());
		else {
			ServerPlayer*first = targets.first(),*last = targets.last();
			if (first->getHandcardNum() < last->getHandcardNum())
				first->drawCards(1, objectName());
			else if (first->getHandcardNum() > last->getHandcardNum())
				last->drawCards(1, objectName());
		}
		return false;
	}
};

class TongxieEffect : public TriggerSkill
{
public:
	TongxieEffect() : TriggerSkill("#tongxie")
	{
		events << CardFinished << DamageInflicted << EventPhaseStart << Death << HpLost;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	QList<ServerPlayer*> getTongxieSources(ServerPlayer*player) const
	{
		QList<ServerPlayer*> sources;
		Room*room = player->getRoom();
		foreach(QString mark, player->getMarkNames()){
			if (!mark.startsWith("&tongxie+#") || player->getMark(mark) <= 0) continue;
			QStringList marks = mark.split("#");
			if (marks.length() != 2) continue;
			ServerPlayer*p = room->findChild<ServerPlayer*>(marks.last());
			if (p) sources << p;
		}
		room->sortByActionOrder(sources);
		return sources;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") || use.to.length() != 1 || use.card->hasFlag("tongxie_slash")) return false;
			ServerPlayer*to = use.to.first();
			foreach(ServerPlayer*p, getTongxieSources(player)){
				foreach(ServerPlayer*q, room->getOtherPlayers(player)){
					if (q->getMark("&tongxie+#"+p->objectName())>0&&q->canSlash(to, false))
						room->askForUseSlashTo(q, to, "@tongxie-slash:" + to->objectName(), false, false, true, p, nullptr, "tongxie_slash");
				}
			}
		} else if (event == HpLost){
			player->addMark("tongxie_losehp-Clear");
		} else if (event == DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			foreach(ServerPlayer*p, getTongxieSources(player)){
				foreach(ServerPlayer*q, room->getOtherPlayers(player)){
					if (q->getMark("&tongxie+#"+p->objectName())<1||q->getMark("tongxie_losehp-Clear")>0) continue;
					QString prompt = QString("tongxieprevent:%1::%2").arg(player->objectName()).arg(damage.damage);
					if (!q->askForSkillInvoke("tongxieprevent", prompt, false)) continue;
					LogMessage log;
					log.from = q;
					log.to << p;
					log.type = "#InvokeOthersSkill";
					log.arg = "tongxie";
					room->sendLog(log);
					p->peiyin("tongxie");
					room->notifySkillInvoked(p, "tongxie");
					room->loseHp(HpLostStruct(q, 1, "tongxie", p));
					return true;
				}
			}
		}else{
			if (event == EventPhaseStart){
				if (player->getPhase() != Player::RoundStart) return false;
			} else if (event == Death){
				if (data.value<DeathStruct>().who != player) return false;
			}
			foreach(ServerPlayer*p, room->getAlivePlayers())
				room->setPlayerMark(p, "&tongxie+#" + player->objectName(), 0);
		}
		return false;
	}
};

class TongxieTargetMod : public TargetModSkill
{
public:
	TongxieTargetMod() : TargetModSkill("#tongxie-target")
	{
	}

    int getResidueNum(const Player*from, const Card*card, const Player*to) const
	{
		if (card->hasFlag("tongxie_slash"))
			return 999;
		if (card->hasTip("dongxin"))
			return 999;
		if (card->getSkillName()=="bianyu")
			return 999;
		if (from->hasSkill("quanyu")&&from->property("quanyuBf").toString().contains("quanyu6"))
			return 999;
		if (from->hasSkill("qiangang")&&to&&to->property("quanyuBf").toString().contains("quanyu6"))
			return 999;
		int n = 0;
		if (from->getPhase() == Player::Play){
			if(to&&to->getMark("chixin-PlayClear")<1&&from->hasSkill("chixin")&&from->inMyAttackRange(to))
				return 999;
			n += from->getMark("&gengzhan_buff");
		}
		if (from->hasSkill("maozhu")){
			foreach(const Skill*s, from->getVisibleSkillList()){
				if (!s->isAttachedLordSkill()) n++;
			}
		}
		return n;
	}

	int getDistanceLimit(const Player*from, const Card*card, const Player*) const
	{
		if (card->hasFlag("tongxie_slash"))
			return 999;
		if (card->getSkillName()=="ol2shanjia")
			return 999;
		if (from->getMark("&wangong")+from->getMark("wangong")>0&&from->hasSkill("wangong"))
			return 999;
		return 0;
	}
};

class KanpoDZVS : public OneCardViewAsSkill
{
public:
	KanpoDZVS() : OneCardViewAsSkill("kanpodz")
	{
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return Slash::IsAvailable(player) && player->getMark("kanpodz_used-Clear") <= 0;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if (!pattern.contains("slash") && !pattern.contains("Slash")) return false;
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
			return false;
		return player->getMark("kanpodz_used-Clear") <= 0;
	}

	bool viewFilter(const Card*card) const
	{
		if (card->isEquipped()) return false;
		Slash*slash = new Slash(Card::SuitToBeDecided, -1);
		slash->addSubcard(card);
		slash->deleteLater();
		slash->setSkillName(objectName());
		return slash->isAvailable(Self);
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
		slash->addSubcard(originalCard);
		slash->setSkillName(objectName());
		return slash;
	}
};

class KanpoDZ : public TriggerSkill
{
public:
	KanpoDZ() : TriggerSkill("kanpodz")
	{
		events << Damage;
		view_as_skill = new KanpoDZVS;
		waked_skills = "#kanpodz";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card || !damage.card->isKindOf("Slash")) return false;
		if (!damage.to || damage.to->isDead() || damage.to->isKongcheng()) return false;
		if (!player->askForSkillInvoke(this, damage.to)) return false;
		player->peiyin(this);

		QList<int> ids;
		Card::Suit suit = damage.card->getSuit();
		foreach(const Card*card, damage.to->getHandcards()){
			if (card->getSuit() == suit)
				ids << card->getEffectiveId();
		}

		int card_id = room->doGongxin(player, damage.to, ids, objectName());
		if (card_id < 0){
			if (ids.isEmpty())
				return false;
			else
			card_id = ids.at(qrand() % ids.length());
		}

		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
		room->obtainCard(player, Sanguosha->getCard(card_id), reason);
		return false;
	}
};

class KanpoDZMark : public TriggerSkill
{
public:
	KanpoDZMark() : TriggerSkill("#kanpodz")
	{
		events << PreChangeSlash;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Slash")) return false;
		if (use.card->getSkillNames().contains("kanpodz"))
			room->addPlayerMark(player, "kanpodz_used-Clear");
		return false;
	}
};

class Gengzhan : public TriggerSkill
{
public:
	Gengzhan() : TriggerSkill("gengzhan")
	{
		events << CardsMoveOneTime;
		waked_skills = "#gengzhan";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from == player || player->getMark("gengzhan-PlayClear") > 0 || !move.from || move.from->getPhase() != Player::Play) return false;
		if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD){
			QList<int> ids;
			foreach(int id, move.card_ids){
				if (!Sanguosha->getCard(id)->isKindOf("Slash")) continue;
				if (room->getCardPlace(id) != Player::DiscardPile) continue;
				ids << id;
			}
			if (ids.isEmpty() || !player->askForSkillInvoke(this)) return false;
			player->peiyin(this);
			player->addMark("gengzhan-PlayClear");
			room->fillAG(ids, player);
			int id = room->askForAG(player, ids, false, objectName(), "@gengzhan-slash");
			room->clearAG(player);
			room->obtainCard(player, id);
		}
		return false;
	}
};

class GengzhanBuff : public TriggerSkill
{
public:
	GengzhanBuff() : TriggerSkill("#gengzhan")
	{
		events << PreCardUsed << EventPhaseStart << EventPhaseEnd;
        global = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash")&&player->hasFlag("CurrentPlayer"))
				player->addMark("gengzhan_record-Clear");
		}else if (event == EventPhaseStart){
			if (!player->isAlive()||player->getPhase()!=Player::Finish||player->getMark("gengzhan_record-Clear")>0) return false;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->isDead()||!p->hasSkill("gengzhan")) continue;
				room->sendCompulsoryTriggerLog(p, "gengzhan", true, true);
				room->addPlayerMark(p, "&gengzhan_buff");
			}
		} else {
			if(player->getPhase()==Player::Play)
				room->setPlayerMark(player, "&gengzhan_buff", 0);
		}
		return false;
	}
};

class Qiongshou : public GameStartSkill
{
public:
	Qiongshou() : GameStartSkill("qiongshou")
	{
		frequency = Compulsory;
		waked_skills = "#qiongshou";
	}

	void onGameStart(ServerPlayer*player) const
	{
		Room*room = player->getRoom();
		room->sendCompulsoryTriggerLog(player, this);
		player->throwEquipArea();
		player->drawCards(4, objectName());
		room->addPlayerMark(player, "qiongshou");
	}
};

class QiongshouKeep : public MaxCardsSkill
{
public:
	QiongshouKeep() : MaxCardsSkill("#qiongshou")
	{
	}

	int getExtra(const Player*target) const
	{
		if (target->hasSkill("qiongshou"))
			return 4* target->getMark("qiongshou");
		return 0;
	}
};

class Fenrui : public PhaseChangeSkill
{
public:
	Fenrui() : PhaseChangeSkill("fenrui")
	{
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		QStringList choices;
		for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++){
			if (!player->hasEquipArea(i))
				choices << QString::number(i);
		}
		if (choices.isEmpty()) return false;
		if (!player->canDiscard(player, "he")) return false;
		if (!room->askForCard(player, "..", "@fenrui", QVariant(), objectName())) return false;
		player->peiyin(this);

		QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		int area = choice.toInt();
		player->obtainEquipArea(area);

		QList<const Card*> equips;
		QList<int> piles = room->getDrawPile();
		foreach(int id, piles){
			const Card*c = Sanguosha->getCard(id);
			if (!c->isKindOf("EquipCard") || !player->canUse(c, player, true)) continue;
			const EquipCard*equip = qobject_cast<const EquipCard*>(c->getRealCard());
			if (!equip) continue;
			if ((int)equip->location() == area)
				equips << c;
		}
		if (equips.isEmpty()) return false;

		const Card*equip = equips.at(qrand() % equips.length());
		room->useCard(CardUseStruct(equip, player));

		if (player->getMark(objectName()) > 0) return false;

		QList<ServerPlayer*> targets;
		int length = player->getEquips().length();
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (length > p->getEquips().length())
				targets << p;
		}
		if (targets.isEmpty()) return false;

		ServerPlayer*t = room->askForPlayerChosen(player, targets, objectName(), "@fenrui-damage", true);
		if (!t) return false;
		room->doAnimate(1, player->objectName(), t->objectName());
		room->addPlayerMark(player, objectName());
		room->damage(DamageStruct(objectName(), player, t, player->getEquips().length() - t->getEquips().length()));
		return false;
	}
};

XiaosiCard::XiaosiCard()
{
}

bool XiaosiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && !to_select->isKongcheng() && Self != to_select;
}

void XiaosiCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	if (to->isKongcheng()) return;

	Room*room = from->getRoom();
	int subcard = subcards.first(), to_id = -1;

	QList<int> basics;
	foreach(int id, to->handCards() + to->getEquipsId()){
		if (Sanguosha->getCard(id)->isKindOf("BasicCard") && to->canDiscard(to, id))
			basics << id;
	}
	if (!basics.isEmpty()){
		to->setTag("XiaosiFrom", QVariant::fromValue(from));
		const Card*dis = room->askForDiscard(to, "xiaosi", 1, 1, false, true, "@xiaosi-discard", "BasicCard");

		if (dis)
			to_id = dis->getEffectiveId();
		else {
			to_id = basics.at(qrand() % basics.length());
			room->throwCard(to_id, to);
		} 
	}

	basics.clear();
	basics << subcard;
	if (to_id > -1)
		basics << to_id;

	while (!basics.isEmpty()){
		if (from->isDead()) break;

		room->setPlayerProperty(from, "XiaosiCards", ListI2S(basics).join("+"));

		foreach(int id, basics)
			room->notifyMoveToPile(from, QList<int>() << id, "xiaosi", room->getCardPlace(id), true);

		const Card*c = room->askForUseCard(from, "@@xiaosi", "@xiaosi");

		foreach(int id, basics)
			room->notifyMoveToPile(from, QList<int>() << id, "xiaosi", room->getCardPlace(id), false);

		if (c) basics.removeOne(c->getEffectiveId());
		else break;
	}

	if (to_id < 0)
		from->drawCards(1, "xiaosi");
}

class Xiaosi : public OneCardViewAsSkill
{
public:
	Xiaosi() : OneCardViewAsSkill("xiaosi")
	{
		expand_pile = "#xiaosi";
		response_pattern = "@@xiaosi";
		waked_skills = "#xiaosi";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("XiaosiCard");
	}

	bool viewFilter(const Card*to_select) const
	{
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
			return !Self->isJilei(to_select) && to_select->isKindOf("BasicCard");
		} else {
			if (Sanguosha->getCurrentCardUsePattern() == "@@xiaosi")
				return to_select->isAvailable(Self) && Self->getPile("#xiaosi").contains(to_select->getEffectiveId());
		}
		return false;
	}

	const Card*viewAs(const Card*originalCard) const
	{
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
			XiaosiCard*card = new XiaosiCard;
			card->addSubcard(originalCard);
			return card;
		} else {
			if (Sanguosha->getCurrentCardUsePattern() == "@@xiaosi")
				return originalCard;
		}
		return nullptr;
	}
};

class XiaosiTargetMod : public TargetModSkill
{
public:
	XiaosiTargetMod() : TargetModSkill("#xiaosi")
	{
		pattern = ".";
	}

	int getResidueNum(const Player*from, const Card*card, const Player*) const
	{
		if (card->isKindOf("BasicCard")&&from->getPile("#xiaosi").contains(card->getEffectiveId()))
			return 999;
		if (card->hasTip("bachao-PlayClear",false))
			return 999;
		return 0;
	}

	int getDistanceLimit(const Player*from, const Card*card, const Player*) const
	{
		if (card->isKindOf("BasicCard")&&from->getPile("#xiaosi").contains(card->getEffectiveId()))
			return 999;
		if (card->hasTip("bachao-PlayClear",false))
			return 999;
		return 0;
	}
};

class Daili : public TriggerSkill
{
public:
	Daili() : TriggerSkill("daili")
	{
		events << EventPhaseChanging << EventAcquireSkill;
		waked_skills = "#daili";
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if (p->isDead() || !p->hasSkill(objectName())) continue;
				QVariantList records = p->getTag("DailiRecord").toList();
				if (records.length() % 2 != 0) continue;
				if (!p->askForSkillInvoke(this)) continue;
				p->peiyin(this);
				p->turnOver();
				QList<int> draws = p->drawCardsList(3, objectName());
				room->showCard(p, draws);
			}
		} else {
			if (data.toString() != objectName()) return false;
			QList<int> records = ListV2I(player->getTag("DailiRecord").toList()), hands = player->handCards();
			foreach(int id, records){
				if (hands.contains(id))
					room->setCardTip(id, "daili");
				else
					records.removeOne(id);
			}
			player->setTag("DailiRecord", ListI2V(records));
		}
		return false;
	}
};

class DailiRecord : public TriggerSkill
{
public:
	DailiRecord() : TriggerSkill("#daili")
	{
		events << ShowCards << CardsMoveOneTime;
        global = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		QVariantList records = player->getTag("DailiRecord").toList();
		if (event == ShowCards){
			QList<int> ids = ListS2I(data.toString().split(":").first().split("+")), hands = player->handCards();
			foreach(int id, ids){
				if (!hands.contains(id) || records.contains(QVariant(id))) continue;
				records << id;
				if (player->hasSkill("daili", true))
					room->setCardTip(id, "daili");
			}
			player->setTag("DailiRecord", records);
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from != player || !move.from_places.contains(Player::PlaceHand)) return false;
			for (int i = 0; i < move.card_ids.length(); i++){
				if (move.from_places.at(i) != Player::PlaceHand) continue;
				if (records.contains(QVariant(move.card_ids.at(i))))
					records.removeOne(QVariant(move.card_ids.at(i)));
			}
			player->setTag("DailiRecord", records);
		}
		return false;
	}
};

class AocaiViewAsSkill : public ZeroCardViewAsSkill
{
public:
	AocaiViewAsSkill() : ZeroCardViewAsSkill("aocai")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasFlag("CurrentPlayer");
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if (player->hasFlag("CurrentPlayer") || player->hasFlag("Global_AocaiFailed")) return false;/*
		if (pattern.contains("slash") || pattern.contains("Slash"))
			return Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
		else if (pattern == "peach")
			return player->getMark("Global_PreventPeach") == 0;
		else if (pattern.contains("analeptic"))
			return true;*/
		if (pattern=="@@aocai") return true;
		foreach(QString cn, pattern.split("+")){
			Card*c = Sanguosha->cloneCard(cn);
			if (c){
				c->deleteLater();
				if (c->getTypeId()==1)
					return true;
			}
		}
		return false;
	}

	const Card*viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern=="@@aocai"){
			return Sanguosha->getCard(Self->getMark("aocaiId"));
		}
		AocaiCard*aocai_card = new AocaiCard;
		/*if (pattern == "peach+analeptic" && Self->getMark("Global_PreventPeach") > 0)
			pattern = "analeptic";*/
		aocai_card->setUserString(pattern);
		return aocai_card;
	}
};

class Aocai : public TriggerSkill
{
public:
	Aocai() : TriggerSkill("aocai")
	{
		//events << CardAsked;
		events << CardUsed;
		view_as_skill = new AocaiViewAsSkill;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == CardUsed){
			foreach(ServerPlayer*p, room->getPlayers()){
				room->setPlayerFlag(p, "-Global_AocaiFailed");
			}
			return false;
		}
		QString pattern = data.toStringList().first();
		if (player!=room->getCurrent()
			&& (pattern.contains("slash") || pattern.contains("Slash") || pattern == "jink")
			&& room->askForSkillInvoke(player, objectName(), data)){
			QList<int> ids = room->getNCards(2);
			QList<int> enabled, disabled;
			foreach(int id, ids){
				if (Sanguosha->getCard(id)->objectName().contains(pattern))
					enabled << id;
				else
					disabled << id;
			}
			int id = Aocai::view(room, player, ids, enabled, disabled);
			if (id > -1){
				const Card*card = Sanguosha->getCard(id);
				room->provide(card);
				return true;
			}
		}
		return false;
	}

	static int view(Room*room, ServerPlayer*player, QList<int> &ids, QList<int> &enabled, QList<int> &disabled)
	{
		int result = -1, index = -1;
		LogMessage log;
		log.type = "$ViewDrawPile";
		log.from = player;
		log.card_str = ListI2S(ids).join("+");
		room->sendLog(log, player);

		room->broadcastSkillInvoke("aocai");
		room->notifySkillInvoked(player, "aocai");
		if (enabled.isEmpty()){
			JsonArray arg;
			arg << "." << false << JsonUtils::toJsonArray(ids);
			room->doNotify(player, QSanProtocol::S_COMMAND_SHOW_ALL_CARDS, arg);
		} else {
			room->fillAG(ids, player, disabled);
			int id = room->askForAG(player, enabled, true, "aocai");
			if (id > -1){
				index = ids.indexOf(id);
				ids.removeOne(id);
				result = id;
			}
			room->clearAG(player);
		}

		room->returnToTopDrawPile(ids);
		if (result == -1)
			room->setPlayerFlag(player, "Global_AocaiFailed");
		else {
			LogMessage log;
			log.type = "#AocaiUse";
			log.from = player;
			log.arg = "aocai";
			log.arg2 = QString::number(index + 1);
			room->sendLog(log);
		}
		return result;
	}
};

AocaiCard::AocaiCard()
{
}

bool AocaiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool AocaiCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;

	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card){
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool AocaiCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*AocaiCard::validateInResponse(ServerPlayer*user) const
{
	Room*room = user->getRoom();
	QList<int> ids = room->getNCards(2);
	QStringList names = user_string.split("+");
	//if (names.contains("slash")) names << "fire_slash" << "thunder_slash";

	QList<int> enabled, disabled;
	foreach(int id, ids){
		const Card*c = Sanguosha->getCard(id);
		if (user->isCardLimited(c,Card::MethodResponse)){
			disabled << id;
			continue;
		}
		foreach(QString cn, names){
			if (c->objectName().endsWith(cn)){
				enabled << id;
				break;
			}
		}
		if (enabled.contains(id)) continue;
		disabled << id;
	}

	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = user;
	log.arg = "aocai";
	room->sendLog(log);

	int id = Aocai::view(room, user, ids, enabled, disabled);
	return Sanguosha->getCard(id);
}

const Card*AocaiCard::validate(CardUseStruct &cardUse) const
{
	cardUse.m_isOwnerUse = false;
	Room*room = cardUse.from->getRoom();
	QList<int> ids = room->getNCards(2);
	QStringList names = user_string.split("+");
	//if (names.contains("slash")) names << "fire_slash" << "thunder_slash";

	QList<int> enabled, disabled;
	foreach(int id, ids){
		const Card*c = Sanguosha->getCard(id);
		if (cardUse.from->isLocked(c)){
			disabled << id;
			continue;
		}
		foreach(QString cn, names){
			if (user_string.isEmpty()){
				if (c->getTypeId()==1&&c->isAvailable(cardUse.from)){
					enabled << id;
					break;
				}
			}else if (c->objectName().endsWith(cn)){
				enabled << id;
				break;
			}
		}
		if (enabled.contains(id)) continue;
		disabled << id;
	}

	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = cardUse.from;
	log.arg = "aocai";
	room->sendLog(log);
	int id = Aocai::view(room, cardUse.from, ids, enabled, disabled);
	if (user_string.isEmpty()&&id>=0){
		room->setPlayerMark(cardUse.from,"aocaiId",id);
		room->askForUseCard(cardUse.from,"@@aocai","aocai0:"+Sanguosha->getCard(id)->objectName());
		return nullptr;
	}
	return Sanguosha->getCard(id);
}

DuwuCard::DuwuCard()
{
	mute = true;
}

bool DuwuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && qMax(0, to_select->getHp()) == subcardsLength() && Self->inMyAttackRange(to_select, subcards);
}

void DuwuCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.to->getRoom();

	if (subcards.length() <= 1)
		room->broadcastSkillInvoke("duwu", 2);
	else
		room->broadcastSkillInvoke("duwu", 1);

	room->damage(DamageStruct("duwu", effect.from, effect.to));
}

class DuwuViewAsSkill : public ViewAsSkill
{
public:
	DuwuViewAsSkill() : ViewAsSkill("duwu")
	{
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->canDiscard(player, "he") && !player->hasFlag("DuwuEnterDying");
	}

	bool viewFilter(const QList<const Card*> &, const Card*to_select) const
	{
		return !Self->isJilei(to_select);
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		DuwuCard*duwu = new DuwuCard;
		if (!cards.isEmpty())
			duwu->addSubcards(cards);
		return duwu;
	}
};

class Duwu : public TriggerSkill
{
public:
	Duwu() : TriggerSkill("duwu")
	{
		events << QuitDying;
		view_as_skill = new DuwuViewAsSkill;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (dying.damage && dying.damage->getReason() == "duwu" && !dying.damage->chain && !dying.damage->transfer){
			ServerPlayer*from = dying.damage->from;
			if (from && from->isAlive()){
				room->setPlayerFlag(from, "DuwuEnterDying");
				room->loseHp(HpLostStruct(from, 1, "duwu", from));
			}
		}
		return false;
	}
};

YuanhuCard::YuanhuCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool YuanhuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	if (!targets.isEmpty())
		return false;

	const Card*card = Sanguosha->getCard(subcards.first());
	const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
	return to_select->getEquip(equip->location()) == nullptr;
}

void YuanhuCard::onUse(Room*room, CardUseStruct &card_use) const
{
	int index = -1;
	if (card_use.to.first() == card_use.from)
		index = 5;
	else if (card_use.to.first()->getGeneralName().contains("caocao"))
		index = 4;
	else {
		const Card*card = Sanguosha->getCard(card_use.card->getSubcards().first());
		if (card->isKindOf("Weapon"))
			index = 1;
		else if (card->isKindOf("Armor"))
			index = 2;
		else if (card->isKindOf("Horse"))
			index = 3;
	}
	room->broadcastSkillInvoke("yuanhu", index);
	SkillCard::onUse(room, card_use);
}

void YuanhuCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*caohong = effect.from;
	Room*room = caohong->getRoom();
	room->moveCardTo(this, caohong, effect.to, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_PUT, caohong->objectName(), "yuanhu", ""));

	const Card*card = Sanguosha->getCard(subcards.first());

	LogMessage log;
	log.type = "$ZhijianEquip";
	log.from = effect.to;
	log.card_str = QString::number(card->getEffectiveId());
	room->sendLog(log);

	if (card->isKindOf("Weapon")){
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (effect.to->distanceTo(p) == 1 && caohong->canDiscard(p, "hej"))
				targets << p;
		}
		if (!targets.isEmpty()){
			ServerPlayer*to_dismantle = room->askForPlayerChosen(caohong, targets, "yuanhu", "@yuanhu-discard:" + effect.to->objectName());
			int card_id = room->askForCardChosen(caohong, to_dismantle, "hej", "yuanhu", false, Card::MethodDiscard);
			room->throwCard(card_id, to_dismantle, caohong);
		}
	} else if (card->isKindOf("Armor")){
		effect.to->drawCards(1, "yuanhu");
	} else if (card->isKindOf("Horse")){
		room->recover(effect.to, RecoverStruct("yuanhu", effect.from));
	}
}

class YuanhuViewAsSkill : public OneCardViewAsSkill
{
public:
	YuanhuViewAsSkill() : OneCardViewAsSkill("yuanhu")
	{
		filter_pattern = "EquipCard";
		response_pattern = "@@yuanhu";
	}

	const Card*viewAs(const Card*originalcard) const
	{
		YuanhuCard*first = new YuanhuCard;
		first->addSubcard(originalcard->getId());
		first->setSkillName(objectName());
		return first;
	}
};

class Yuanhu : public PhaseChangeSkill
{
public:
	Yuanhu() : PhaseChangeSkill("yuanhu")
	{
		view_as_skill = new YuanhuViewAsSkill;
	}

	bool onPhaseChange(ServerPlayer*target, Room*room) const
	{
		if (target->getPhase() == Player::Finish && !target->isNude())
			room->askForUseCard(target, "@@yuanhu", "@yuanhu-equip", -1, Card::MethodNone);
		return false;
	}
};

class Baobian : public TriggerSkill
{
public:
	Baobian() : TriggerSkill("baobian")
	{
		events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill << EventLoseSkill;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == EventLoseSkill){
			if (data.toString() == objectName()){
				QStringList baobian_skills = player->getTag("BaobianSkills").toStringList();
				QStringList detachList;
				foreach(QString skill_name, baobian_skills)
					detachList.append("-" + skill_name);
				room->handleAcquireDetachSkills(player, detachList);
				player->setTag("BaobianSkills", QVariant());
			}
			return false;
		} else if (triggerEvent == EventAcquireSkill){
			if (data.toString() != objectName()) return false;
		}

		if (!player->isAlive() || !player->hasSkill(objectName(), true)) return false;

		acquired_skills.clear();
		detached_skills.clear();
		BaobianChange(room, player, 1, "shensu");
		BaobianChange(room, player, 2, "paoxiao");
		BaobianChange(room, player, 3, "tiaoxin");
		if (!acquired_skills.isEmpty() || !detached_skills.isEmpty())
			room->handleAcquireDetachSkills(player, acquired_skills + detached_skills);
		return false;
	}

private:
	void BaobianChange(Room*room, ServerPlayer*player, int hp, const QString &skill_name) const
	{
		QStringList baobian_skills = player->getTag("BaobianSkills").toStringList();
		if (player->getHp() <= hp){
			if (!baobian_skills.contains(skill_name)){
				room->notifySkillInvoked(player, "baobian");
				if (player->getHp() == hp)
					room->broadcastSkillInvoke("baobian", 4 - hp);
				acquired_skills.append(skill_name);
				baobian_skills << skill_name;
			}
		} else {
			if (baobian_skills.contains(skill_name)){
				detached_skills.append("-" + skill_name);
				baobian_skills.removeOne(skill_name);
			}
		}
		player->setTag("BaobianSkills", QVariant::fromValue(baobian_skills));
	}

	mutable QStringList acquired_skills, detached_skills;
};

class Dianhu : public TriggerSkill
{
public:
	Dianhu() : TriggerSkill("dianhu")
	{
		events << Damage << HpRecover;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event == Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.to->isDead() || !damage.from->hasSkill(objectName()) || damage.damage <= 0) return false;
			ServerPlayer*target = damage.from->getTag("DianhuTarget").value<ServerPlayer*>();
			if (target && target->isAlive() && target == damage.to){
				room->sendCompulsoryTriggerLog(damage.from, objectName(), true, true);
				damage.from->drawCards(1, objectName());
			}
		} else {
			RecoverStruct recover = data.value<RecoverStruct>();
			if (recover.recover <= 0) return false;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (player->isDead()) return false;
				if (!p->hasSkill(objectName()) || p->isDead()) continue;
				ServerPlayer*target = p->getTag("DianhuTarget").value<ServerPlayer*>();
				if (target && target->isAlive() && target == player){
					room->sendCompulsoryTriggerLog(p, objectName(), true, true);
					p->drawCards(1, objectName());
				}
			}
		}
		return false;
	}
};

class DianhuTarget : public GameStartSkill
{
public:
	DianhuTarget() : GameStartSkill("#dianhu-target")
	{
		frequency = Compulsory;
	}

	void onGameStart(ServerPlayer*player) const
	{
		Room*room = player->getRoom();
		ServerPlayer*target = room->askForPlayerChosen(player, room->getOtherPlayers(player), "dianhu", "@dianhu-choose", false, true);
		room->broadcastSkillInvoke("dianhu");
		player->setTag("DianhuTarget", QVariant::fromValue(target));
		room->addPlayerMark(target, "&dianhu");
	}
};

JianjiCard::JianjiCard()
{
}

void JianjiCard::onEffect(CardEffectStruct &effect) const
{
	QList<int> ids = effect.to->drawCardsList(1, "jianji");
	if (ids.isEmpty()) return;
	if (effect.to->isDead()) return;

	Room*room = effect.from->getRoom();
	int id = ids.first();
	if (room->getCardPlace(id) != Player::PlaceHand || room->getCardOwner(id) != effect.to) return;

	const Card*card = Sanguosha->getCard(id);
	if (!effect.to->canUse(card)) return;

	room->addPlayerMark(effect.to, "jianji_id-PlayClear", id + 1);
	room->askForUseCard(effect.to, "@@jianji", "@jianji:" + card->objectName());
	room->setPlayerMark(effect.to, "jianji_id-PlayClear", 0);
}

class Jianji : public ZeroCardViewAsSkill
{
public:
	Jianji() : ZeroCardViewAsSkill("jianji")
	{
		response_pattern = "@@jianji";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("JianjiCard");
	}

	const Card*viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@jianji"){
			int id = Self->getMark("jianji_id-PlayClear") - 1;
			if (id < 0) return nullptr;
			const Card*card = Sanguosha->getEngineCard(id);
			return card;
		}
		return new JianjiCard;
	}
};

class Dujin : public DrawCardsSkill
{
public:
	Dujin() : DrawCardsSkill("dujin")
	{
		frequency = Frequent;
	}

	int getDrawNum(ServerPlayer*player, int n) const
	{
		if (player->askForSkillInvoke(this)){
			player->getRoom()->broadcastSkillInvoke(objectName());
			return n + player->getEquips().length() / 2 + 1;
		} else
			return n;
	}
};

ZiyuanCard::ZiyuanCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void ZiyuanCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.from->getRoom();
	CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "ziyuan", "");
	room->obtainCard(effect.to, this, reason, true);

	if (effect.to->isAlive())
		room->recover(effect.to, RecoverStruct("ziyuan", effect.from));
}

class Ziyuan : public ViewAsSkill
{
public:
	Ziyuan() : ViewAsSkill("ziyuan")
	{
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (to_select->isEquipped()) return false;
		int n = to_select->getNumber();
		int num = 0;
		foreach(const Card*c, selected){
			num = num + c->getNumber();
		}
		return num + n <= 13;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("ZiyuanCard");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;
		int num = 0;
		foreach(const Card*c, cards){
			num = num + c->getNumber();
		}
		if (num != 13) return nullptr;

		ZiyuanCard*card = new ZiyuanCard;
		card->addSubcards(cards);
		return card;
	}
};

class Jugu : public GameStartSkill
{
public:
	Jugu() : GameStartSkill("jugu")
	{
		frequency = Compulsory;
	}

	void onGameStart(ServerPlayer*player) const
	{
		Room*room = player->getRoom();
		int n = player->getMaxHp();
		if (n <= 0) return;
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		player->drawCards(n, objectName());
	}
};

class JuguMax : public MaxCardsSkill
{
public:
	JuguMax() : MaxCardsSkill("#jugu-max")
	{
	}

	int getExtra(const Player*target) const
	{
		if (target->hasSkill("jugu"))
			return target->getMaxHp();
		return 0;
	}
};

class Yuanzi : public PhaseChangeSkill
{
public:
	Yuanzi() : PhaseChangeSkill("yuanzi")
	{
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive() && target->getPhase() == Player::Start;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (player->isDead()) return false;
			if (p->isDead() || !p->hasSkill(objectName()) || p->isKongcheng() || p->getMark("yuanzi_used_lun") > 0) continue;
			if (!p->askForSkillInvoke(this, player)) continue;
			p->peiyin(this);
			DummyCard*hand = p->wholeHandCards();
			room->giveCard(p, player, hand, objectName());
			room->addPlayerMark(p, "yuanzi_used_lun");
			if (room->hasCurrent())
				room->addPlayerMark(p, "yuanzi_" + player->objectName() + "-Clear");
		}
		return false;
	}
};

class YuanziDraw : public TriggerSkill
{
public:
	YuanziDraw() : TriggerSkill("#yuanzi")
	{
		events << Damage;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (!room->hasCurrent()) return false;
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if (player->isDead()) return false;
			if (p->isDead()) continue;
			int mark = p->getMark("yuanzi_" + player->objectName() + "-Clear");
			if (mark <= 0) continue;
			for (int i = 0; i < mark; i++){
				if (p->isDead() || player->isDead()) break;
				if (player->getHandcardNum() < p->getHandcardNum()) break;
				if (!p->askForSkillInvoke("yuanzi", "draw")) break;
				p->peiyin("yuanzi");
				p->drawCards(2, "yuanzi");
			}
		}
		return false;
	}
};

class Liejie : public MasochismSkill
{
public:
	Liejie() : MasochismSkill("liejie")
	{
	}

	void onDamaged(ServerPlayer*player, const DamageStruct &damage) const
	{
		Room*room = player->getRoom();
		const Card*c = room->askForDiscard(player,objectName(),3,1,true,true,"@liejie-discard",".",objectName());
		if (!c) return;
		player->peiyin(this);
		player->drawCards(c->subcardsLength(), objectName());
		ServerPlayer*from = damage.from;
		if (!player->canDiscard(from, "he")) return;

		int red = 0;
		foreach(int id, c->getSubcards()){
			if (Sanguosha->getCard(id)->isRed())
				red++;
		}
		if (red == 0) return;

		QList<int> cards;
		for (int i = 0; i < red; ++i){
			if(from->getCardCount()<=i) break;
			int id = room->askForCardChosen(player, from, "he", objectName(), false, Card::MethodDiscard, cards, true);
			if (id < 0) break;
			cards << id;
		}
		if (cards.isEmpty()) return;
		room->throwCard(cards, objectName(), from, player);
	}
};

class Hongji : public TriggerSkill
{
public:
	Hongji() : TriggerSkill("hongji")
	{
		events << EventPhaseStart << EventPhaseEnd; //<< EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr && target->isAlive();
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == EventPhaseEnd)
			return -2;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (event == EventPhaseStart){
			if (player->getPhase() != Player::Start) return false;
			bool most = true, least = true;
			int hand = player->getHandcardNum();
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->getHandcardNum() > hand)
					most = false;
				else if (p->getHandcardNum() < hand)
					least = false;
				if (!most && !least)
					break;
			}

			foreach(ServerPlayer*p, room->getAllPlayers()){
				if (player->isDead()) break;
				if (p->isDead() || !p->hasSkill(objectName())) continue;
				if (least && p->getMark("hongjiLeast_lun") > 0) continue;
				if (most && p->getMark("hongjiMost_lun") > 0) continue;

				if (least && p->askForSkillInvoke(this, "draw:" + player->objectName())){
					p->peiyin(this);
					room->addPlayerMark(p, "hongjiLeast_lun");
					room->addPlayerMark(player, "&hongji+draw-Clear");
				}

				if (most && p->askForSkillInvoke(this, "play:" + player->objectName())){
					p->peiyin(this);
					room->addPlayerMark(p, "hongjiMost_lun");
					room->addPlayerMark(player, "&hongji+play-Clear");
				}
			}
		} else {
			Player::Phase phase = player->getPhase();
			if (phase == Player::Draw){
				int mark = player->getMark("&hongji+draw-Clear");
				for (int i = 0; i < mark; i++){
					LogMessage log;
					log.type = "#HongjiEffect";
					log.from = player;
					log.arg = objectName();
					log.arg2 = "draw";
					room->sendLog(log);
					room->removePlayerMark(player, "&hongji+draw-Clear");
					player->insertPhase(phase);/*

					RoomThread*thread = room->getThread();
					if (!thread->trigger(EventPhaseStart, room, player))
						thread->trigger(EventPhaseProceeding, room, player);
					thread->trigger(EventPhaseEnd, room, player);*/
				}
			} else if (phase == Player::Play){
				int mark = player->getMark("&hongji+play-Clear");
				for (int i = 0; i < mark; i++){
					LogMessage log;
					log.type = "#HongjiEffect";
					log.from = player;
					log.arg = objectName();
					log.arg2 = "play";
					room->sendLog(log);
					room->removePlayerMark(player, "&hongji+play-Clear");
					player->insertPhase(phase);/*

					RoomThread*thread = room->getThread();
					if (!thread->trigger(EventPhaseStart, room, player))
						thread->trigger(EventPhaseProceeding, room, player);
					thread->trigger(EventPhaseEnd, room, player);*/
				}
			}
		}
		return false;
	}
};

XingguCard::XingguCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool XingguCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	if (!targets.isEmpty() || to_select == Self)
		return false;

	const Card*card = Sanguosha->getCard(subcards.first());
	const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
	return to_select->getEquip(equip->location()) == nullptr && !Self->isProhibited(to_select, card);
}

void XingguCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	room->moveCardTo(this, from, to, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_PUT,
		from->objectName(), "xinggu", ""));

	LogMessage log;
	log.type = "$ZhijianEquip";
	log.from = to;
	log.card_str = QString::number(getEffectiveId());
	room->sendLog(log);

	if (from->isDead()) return;
	QList<int> diamond, drawpile = room->getDrawPile();
	foreach(int id, drawpile){
		if (Sanguosha->getCard(id)->getSuit() == Card::Diamond)
			diamond << id;
	}
	if (diamond.isEmpty()) return;

	int id = diamond.at(qrand() % diamond.length());
	room->obtainCard(from, id);
}

class XingguVS : public OneCardViewAsSkill
{
public:
	XingguVS() : OneCardViewAsSkill("xinggu")
	{
		expand_pile = "xinggu";
		response_pattern = "@@xinggu";
	}

	bool viewFilter(const Card*to_select) const
	{
		return to_select->isKindOf("EquipCard") && Self->getPile(objectName()).contains(to_select->getEffectiveId());
	}

	const Card*viewAs(const Card*originalCard) const
	{
		XingguCard*c = new XingguCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Xinggu : public TriggerSkill
{
public:
	Xinggu() : TriggerSkill("xinggu")
	{
		events << EventPhaseStart << GameStart;
		view_as_skill = new XingguVS;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (event == GameStart){
			QList<int> horses, drawpile = room->getDrawPile(), add;
			foreach(int id, drawpile){
				if (Sanguosha->getCard(id)->isKindOf("Horse"))
					horses << id;
			}
			if (horses.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player, this);

			for (int i = 0; i < 3; i++){
				if (horses.isEmpty()) break;
				int id = horses.at(qrand() % horses.length());
				horses.removeOne(id);
				add << id;
			}

			player->addToPile(objectName(), add);
		} else {
			if (player->getPhase() != Player::Finish) return false;
			if (player->getPile(objectName()).isEmpty()) return false;
			room->askForUseCard(player, "@@xinggu", "@xinggu", -1, Card::MethodNone);
		}
		return false;
	}
};

class OLDuanbing : public TriggerSkill
{
public:
	OLDuanbing() : TriggerSkill("olduanbing")
	{
		events << TargetSpecified << CardUsed;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		if (event == CardUsed){
			QList<ServerPlayer*> targets;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (!player->canSlash(p, use.card, false) || player->distanceTo(p) != 1 || use.to.contains(p)) continue;
				targets << p;
			}
			ServerPlayer*to = room->askForPlayerChosen(player, targets, objectName(), "@olduanbing-target", true);
			if (!to) return false;

			LogMessage log;
			log.type = "#QiaoshuiAdd";
			log.from = player;
			log.to << to;
			log.card_str = use.card->toString();
			log.arg = "olduanbing";
			room->sendLog(log);
			int index = qrand() % 2 + 1;
			if (player->getGeneralName().contains("heqi") || (!player->getGeneralName().contains("dingfeng") && player->getGeneral2Name().contains("heqi")))
				index = 3;
			room->broadcastSkillInvoke(objectName(), index);
			room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), to->objectName());
			room->notifySkillInvoked(player, objectName());

			use.to << to;
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		} else {
			bool trigger = false;
			QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
			for (int i = 0; i < use.to.length(); i++){
				if (jink_list.at(i).toInt() == 1 && player->distanceTo(use.to.at(i)) == 1){
					jink_list.replace(i, QVariant(2));
					if(trigger) continue;
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
					trigger = true;
				}
			}
			player->setTag("Jink_" + use.card->toString(), jink_list);
		}
		return false;
	}
};

OLFenxunCard::OLFenxunCard()
{
}

void OLFenxunCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.from->getRoom();
	room->setPlayerMark(effect.from, effect.to->objectName()+"olfenxunbf-Clear", 1);
	QStringList targets = effect.from->getTag("olfenxun_targets").toStringList();
	if (targets.contains(effect.to->objectName())) return;
	targets << effect.to->objectName();
	effect.from->setTag("olfenxun_targets", targets);
}

class OLFenxunVS : public ZeroCardViewAsSkill
{
public:
	OLFenxunVS() :ZeroCardViewAsSkill("olfenxun")
	{
	}

	const Card*viewAs() const
	{
		return new OLFenxunCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("OLFenxunCard");
	}
};

class OLFenxun : public TriggerSkill
{
public:
	OLFenxun() : TriggerSkill("olfenxun")
	{
		events << EventPhaseChanging << DamageDone;
		view_as_skill = new OLFenxunVS;
        global = true;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from) damage.from->addMark("olduanbing_did_damage" + damage.to->objectName() + "-Clear");
		}else if (data.value<PhaseChangeStruct>().to == Player::NotActive){
			QStringList targets = player->getTag("olfenxun_targets").toStringList();
			player->removeTag("olfenxun_targets");
	
			bool log = true;
			foreach(QString name, targets){
				if (player->isDead()) break;
				if (player->getMark("olduanbing_did_damage" + name + "-Clear") > 0) continue;
				if (log){
					log = false;
					room->sendCompulsoryTriggerLog(player, objectName());
				}
				room->askForDiscard(player, objectName(), 1, 1, false, true);
			}
		}
		return false;
	}
};

class OLFenxunBf : public DistanceSkill
{
public:
	OLFenxunBf() : DistanceSkill("#olfenxunbf")
	{
	}

	int getFixed(const Player*from, const Player*to) const
	{
		if (from->getMark(to->objectName()+"olfenxunbf-Clear")>0&&from->hasSkill("olfenxun"))
			return 1;
		return 0;
	}
};

class NewZhendu : public PhaseChangeSkill
{
public:
	NewZhendu() : PhaseChangeSkill("newzhendu")
	{
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Play)
			return false;

		foreach(ServerPlayer*hetaihou, room->getAllPlayers()){
			if (!TriggerSkill::triggerable(hetaihou))
				continue;

			if (!hetaihou->canDiscard(hetaihou, "h"))
				continue;
			if (room->askForCard(hetaihou, ".", "@newzhendu-discard:" + player->objectName(), QVariant::fromValue(player), objectName())){
				hetaihou->peiyin(this);
				Analeptic*analeptic = new Analeptic(Card::NoSuit, 0);
				analeptic->setSkillName("_newzhendu");
				analeptic->deleteLater();
				room->useCard(CardUseStruct(analeptic, player), true);
				if (player != hetaihou && player->isAlive())
					room->damage(DamageStruct(objectName(), hetaihou, player));
			}
		}
		return false;
	}
};

class NewQiluan : public TriggerSkill
{
public:
	NewQiluan() : TriggerSkill("newqiluan")
	{
		events << Death << EventPhaseChanging;
		frequency = Frequent;
        global = true;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == Death){
			DeathStruct death = data.value<DeathStruct>();
			if (death.damage&&death.damage->from==player)
				player->addMark("newqiluan-Clear", 3);
			else
				player->addMark("newqiluan-Clear");
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if (p->getMark("newqiluan-Clear") > 0 && p->hasSkill(objectName()) && p->askForSkillInvoke(this)){
						room->broadcastSkillInvoke(objectName());
						p->drawCards(p->getMark("newqiluan-Clear"), objectName());
					}
				}
			}
		}
		return false;
	}
};

class SecondOLMoucheng : public PhaseChangeSkill
{
public:
	SecondOLMoucheng() : PhaseChangeSkill("secondolmoucheng")
	{
		frequency = Wake;
		waked_skills = "tenyearjingong";
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::RoundStart
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getMark("&ollianjidamage")<1&&!player->canWake(objectName())) return false;
		room->setPlayerMark(player, "&ollianjidamage", 0);
		room->sendCompulsoryTriggerLog(player, this);

		room->doSuperLightbox(player, "secondolmoucheng");
		room->setPlayerMark(player, "secondolmoucheng", 1);

		if (room->changeMaxHpForAwakenSkill(player, 0, objectName()))
			room->handleAcquireDetachSkills(player, "-ollianji|tenyearjingong");
		return false;
	}
};

class SecondOLMouchengDamage : public TriggerSkill
{
public:
	SecondOLMouchengDamage() : TriggerSkill("#secondolmoucheng-damage")
	{
		events << DamageDone;
		//frequency = Wake;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.card && damage.card->isKindOf("Slash")){
			foreach(QString flag, damage.card->getFlags()){
				if (!flag.startsWith("ollianji_slash_")) continue;
				QString name = flag.split("_").last();
				ServerPlayer*player = room->findChild<ServerPlayer*>(name);
				if (player && player->isAlive() && player->hasSkill("secondolmoucheng", true))
					room->setPlayerMark(player, "&ollianjidamage", 1);
			}
		}
		return false;
	}
};

class SpFenxin : public TriggerSkill
{
public:
	SpFenxin() : TriggerSkill("spfenxin")
	{
		events << Death;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who == player) return false;
		QString role = death.who->getRole();
		if (player->getMark("jieyuan_" + role + "-Keep") > 0) return false;
		room->addPlayerMark(player, "jieyuan_" + role + "-Keep");
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		return false;
	}
};

OLXingwuCard::OLXingwuCard()
{
	mute = true;
	handling_method = Card::MethodNone;
	will_throw = false;
	m_skillName = "olxingwu";
}

void OLXingwuCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*from = effect.from,*to = effect.to;
	Room*room = from->getRoom();
	from->peiyin(m_skillName, 2);

	if (m_skillName == "tenyearxingwu" || from->getPile("xingwu").contains(subcards.first())){
		CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, from->objectName(), m_skillName, "");
		room->throwCard(this, reason, nullptr);
	} else {
		from->turnOver();
		room->throwCard(this, from, nullptr);
	}

	QList<const Card*> equips = to->getEquips();
	if (!equips.isEmpty()){
		DummyCard*dummy = new DummyCard;
		foreach(const Card*equip, equips){
			if (from->canDiscard(to, equip->getEffectiveId()))
				dummy->addSubcard(equip);
		}
		if (dummy->subcardsLength() > 0)
			room->throwCard(dummy, to, from);
		delete dummy;
	}

	if (to->isDead()) return;

	int damage = 2;

	if (m_skillName == "tenyearxingwu"){
		damage = 0;
		QList<Card::Suit> suits;
		foreach(int id, subcards){
			const Card*c = Sanguosha->getCard(id);
			Card::Suit suit = c->getSuit();
			if (suits.contains(suit)) continue;
			suits << suit;
			damage++;
		}
		damage = qMax(damage, 1);
	}

	damage = to->isMale() ? damage : 1;
	room->damage(DamageStruct(m_skillName, from->isAlive() ? from : nullptr, to, damage));
}

TenyearXingwuCard::TenyearXingwuCard() : OLXingwuCard()
{
	mute = true;
	handling_method = Card::MethodNone;
	will_throw = false;
	m_skillName = "tenyearxingwu";
}

class OLXingwuVS : public ViewAsSkill
{
public:
	OLXingwuVS(const QString &xingwu) : ViewAsSkill(xingwu), xingwu(xingwu)
	{
		expand_pile = "xingwu";
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if (selected.length() >= 3) return false;
		if (selected.isEmpty())
			return Self->getPile("xingwu").contains(to_select->getEffectiveId()) ||
				(xingwu == "olxingwu" && Self->getHandcards().contains(to_select) && !Self->isJilei(to_select, true));
		else if (selected.length() == 1){
			if (Self->getPile("xingwu").contains(selected.first()->getEffectiveId()))
				return selected.length() < 3 && Self->getPile("xingwu").contains(to_select->getEffectiveId());
			else
				return xingwu == "olxingwu" && selected.length() < 2 && Self->getHandcards().contains(to_select) && !Self->isJilei(to_select, true);
		} else if (selected.length() == 2){
			if (Self->getPile("xingwu").contains(selected.first()->getEffectiveId()))
				return selected.length() < 3 && Self->getPile("xingwu").contains(to_select->getEffectiveId());
			else
				return false;
		}
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		if (Self->getPile("xingwu").contains(cards.first()->getEffectiveId()) && cards.length() != 3) return nullptr;
		if (Self->getHandcards().contains(cards.first()) && cards.length() != 2) return nullptr;

		if (xingwu == "olxingwu"){
			OLXingwuCard*c = new OLXingwuCard;
			c->addSubcards(cards);
			return c;
		} else if (xingwu == "tenyearxingwu"){
			TenyearXingwuCard*c = new TenyearXingwuCard;
			c->addSubcards(cards);
			return c;
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern == "@@" + xingwu;
	}

private:
	QString xingwu;
};

class OLXingwu : public PhaseChangeSkill
{
public:
	OLXingwu(const QString &xingwu) : PhaseChangeSkill(xingwu), xingwu(xingwu)
	{
		view_as_skill = new OLXingwuVS(xingwu);
	}

	bool onPhaseChange(ServerPlayer*player, Room*room) const
	{
		if (player->getPhase() != Player::Discard || player->isNude()) return false;
		if (xingwu == "tenyearxingwu" && player->isKongcheng()) return false;

		QString pattern = "..";
		if(xingwu == "tenyearxingwu")
			pattern = ".|.|.|hand";

		const Card*card = room->askForCard(player, pattern, "@" + xingwu + "-card", QVariant(), Card::MethodNone);
		if (!card) return false;
		player->peiyin(objectName(), 1);

		LogMessage log;
		log.type = "#InvokeSkill";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);
		room->notifySkillInvoked(player, objectName());

		player->addToPile("xingwu", card);

		if (player->isDead()) return false;
		if (player->getPile("xingwu").length() >= 3 || (player->getHandcardNum() >= 2 && xingwu == "olxingwu"))
			room->askForUseCard(player, "@@" + xingwu, "@olxingwu");
		return false;
	}

private:
	QString xingwu;
};

class NewTianming : public TriggerSkill
{
public:
	NewTianming() : TriggerSkill("newtianming")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.to.contains(player) && use.card->isKindOf("Slash") && room->askForSkillInvoke(player, objectName())){
			room->broadcastSkillInvoke(objectName());
			room->askForDiscard(player, objectName(), 2, 2, false, true);
			player->drawCards(2, objectName());

			int max = player->getHp();
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->getHp() > max)
					max = p->getHp();
			}
			if (player->getHp() == max)
				return false;

			QList<ServerPlayer*> maxs;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if (p->getHp() == max)
					maxs << p;
				if (maxs.size() > 1)
					return false;
			}
			ServerPlayer*mosthp = maxs.first();
			if (room->askForSkillInvoke(mosthp, objectName(),false)){
				room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), mosthp->objectName());
				room->askForDiscard(mosthp, objectName(), 2, 2, false, true);
				mosthp->drawCards(2, objectName());
			}
		}

		return false;
	}
};


class Gongjie : public TriggerSkill
{
public:
	Gongjie() : TriggerSkill("gongjie")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->getPhase()==Player::RoundStart;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*, QVariant &) const
	{
		foreach(ServerPlayer*p, room->getAllPlayers()){
			p->addMark("gongjie_lun");
			if (p->getMark("gongjie_lun")<2&&p->hasSkill(objectName())){
				QList<ServerPlayer*>tos = room->askForPlayersChosen(p,room->getOtherPlayers(p),objectName(),0,p->getCardCount(),"gongjie0:",true,true);
				if(tos.length()>0){
					p->peiyin(this);
					QStringList suits;
					foreach(ServerPlayer*q, tos){
						if (p->getCardCount()>0){
							int id = room->askForCardChosen(q,p,"he",objectName());
							if(id>-1){
								const Card*c = Sanguosha->getCard(id);
								if(!suits.contains(c->getSuitString()))
									suits.append(c->getSuitString());
								q->obtainCard(c,false);
							}
						}
						p->addMark("gongjie"+q->objectName());
					}
					if(p->isAlive())
						p->drawCards(suits.length(),objectName());
				}
			}
		}
		return false;
	}
};

class Xiangxu : public TriggerSkill
{
public:
	Xiangxu() : TriggerSkill("xiangxu")
	{
		events << EventPhaseChanging << CardsMoveOneTime;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					int n = player->getHandcardNum()-p->getHandcardNum();
					if (p->isAlive()&&p->getMark("xiangxu-Clear")>0&&n!=0
						&&p->hasSkill(objectName())&&p->askForSkillInvoke(this)){
						room->broadcastSkillInvoke(objectName());
						p->addMark("xiangxu"+player->objectName());
						if(n>0)
							p->drawCards(qMin(n,5-p->getHandcardNum()),objectName());
						else{
							const Card*dc = room->askForDiscard(p,objectName(),-n,-n);
							if(dc&&dc->subcardsLength()>1)
								room->recover(p,RecoverStruct(objectName(),p));
						}
					}
				}
			}
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceHand)&&move.from==player){
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if (p->getHandcardNum()<player->getHandcardNum())
						return false;
				}
				player->addMark("xiangxu-Clear");
			}else if(move.to_place==Player::PlaceHand&&move.to==player){
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if (p->getHandcardNum()<player->getHandcardNum())
						return false;
				}
				player->addMark("xiangxu-Clear");
			}
		}
		return false;
	}
};

class Xiangzuo : public TriggerSkill
{
public:
	Xiangzuo() : TriggerSkill("xiangzuo")
	{
		events << Dying;
		frequency = Limited;
		limit_mark = "@xiangzuo";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (dying.who==player&&player->getMark("@xiangzuo")>0){
			QList<int> ids = player->handCards()+player->getEquipsId();
			CardsMoveStruct move = room->askForYijiStruct(player,ids,objectName(),false,false,true,-1,room->getOtherPlayers(player),CardMoveReason(),"xiangzuo0",true);
			if(move.to){
				room->removePlayerMark(player,"@xiangzuo");
				//room->broadcastSkillInvoke(objectName());
				room->doSuperLightbox(player,objectName());
				if(player->getMark("gongjie"+move.to->objectName())>0&&player->getMark("xiangxu"+move.to->objectName())>0)
					room->recover(player,RecoverStruct(objectName(),player,move.card_ids.length()));
			}
		}
		return false;
	}
};

class Chishi : public TriggerSkill
{
public:
	Chishi() : TriggerSkill("chishi")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from&&move.from->hasFlag("CurrentPlayer")&&player->getMark("chishiUse-Clear")<1){
				ServerPlayer*from = (ServerPlayer*)move.from;
				bool can = false;
				if (move.from_places.contains(Player::PlaceHand))
					can = from->isKongcheng();
				else if(move.from_places.contains(Player::PlaceEquip))
					can = !from->hasEquip();
				else if(move.from_places.contains(Player::PlaceDelayedTrick))
					can = from->getJudgingArea().isEmpty();
				if(can&&player->askForSkillInvoke(objectName()+"$-1",from)){
					player->addMark("chishiUse-Clear");
					from->drawCards(2,objectName());
					room->addMaxCards(from,2);
				}
			}
		}
		return false;
	}
};

WeimianCard::WeimianCard()
{
	target_fixed = true;
}

void WeimianCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	QStringList choices;
	for (int i = 0; i < 5; i++){
		if (source->hasEquipArea(i))
			choices << QString::number(i);
	}
	if (choices.isEmpty()) return;

	QList<int> eas;
	for (int i = 0; i < 3; i++){
		if (choices.isEmpty()) break;
		QString choice = room->askForChoice(source, getSkillName(), choices.join("+"));
		if (choice=="cancel") break;
		if(!choices.contains("cancel"))
			choices.append("cancel");
		choices.removeOne(choice);
		eas << choice.toInt();
	}
	source->throwEquipArea(eas);

	if (source->isDead()) return;

	ServerPlayer*target = room->askForPlayerChosen(source, room->getAlivePlayers(), getSkillName(), "weimian0:");
	room->doAnimate(1, source->objectName(), target->objectName());
	choices.clear();
	for (int i = 0; i < 5; i++){
		if (!target->hasEquipArea(i))
			choices << QString::number(i);
	}
	if(target->getLostHp()>0) choices << "weimian2";
	choices << "weimian3";
	QStringList choices2;
	QString east = "01234";
	for (int i = 0; i < eas.length(); i++){
		if(choices.isEmpty()||target->isDead()) continue;
		QString choice = room->askForChoice(target, "weimian_target", choices.join("+"));
		choices.removeOne(choice);
		choices2 << choice;
		if(east.contains(choice)){
			foreach(QString str, choices){
				if(east.contains(str))
					choices.removeOne(str);
			}
		}
	}
	foreach(QString str, choices2){
		if(east.contains(str))
			target->obtainEquipArea(str.toInt());
		else if(str=="weimian2")
			room->recover(target,RecoverStruct(getSkillName(),source));
		else{
			target->throwAllHandCards(getSkillName());
			if(target->isDead()) break;
			target->drawCards(4,getSkillName());
		}
		if(target->isDead()) break;
	}
}

class Weimian : public ZeroCardViewAsSkill
{
public:
	Weimian() : ZeroCardViewAsSkill("weimian")
	{
	}

	const Card*viewAs() const
	{
		return new WeimianCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->hasEquipArea() && !player->hasUsed("WeimianCard");
	}
};

class Yongzu : public TriggerSkill
{
public:
	Yongzu() : TriggerSkill("yongzu")
	{
		events << EventPhaseStart;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(player->getPhase()==Player::Start){
			ServerPlayer*to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"yongzu0",true,true);
			if(to){
				player->peiyin(this);
				QString choice2;
				foreach(ServerPlayer*p, QList<ServerPlayer*>() << player << to){
					QStringList choices;
					choices << "yongzu1";
					if(p->getLostHp()>0)
						choices << "yongzu2";
					if(p->isChained()||!p->faceUp())
						choices << "yongzu3";
					if(to->getKingdom()==player->getKingdom()){
						choices << "yongzu4";
						if(p->getKingdom()=="wei")
							choices << "yongzu5=jianxiong";
						if(p->getKingdom()=="qun")
							choices << "yongzu5=tianming";
					}
					if(!choice2.isEmpty()){
						foreach(QString p, choices){
							if(p.contains(choice2))
								choices.removeOne(p);
						}
					}
					QString choice = room->askForChoice(p,objectName(),choices.join("+"));
					choice2 = choice;
					if(choice=="yongzu1")
						p->drawCards(2,objectName());
					else if(choice=="yongzu2")
						room->recover(p,RecoverStruct(objectName(),player));
					else if(choice=="yongzu3"){
						if(p->isChained())
							room->setPlayerChained(p);
						if(!p->faceUp())
							p->turnOver();
					}else if(choice=="yongzu4")
						room->addMaxCards(p,1,false);
					else{
						choices = choice.split("=");
						room->acquireNextTurnSkills(p,objectName(),choices.last());
						choice2 = choices.first();
					}
				}
			}
		}
		return false;
	}
};

class Qingliu : public TriggerSkill
{
public:
	Qingliu() : TriggerSkill("qingliu")
	{
		events << GameStart << QuitDying;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==GameStart){
			room->sendCompulsoryTriggerLog(player,this);
			QString choice = room->askForKingdom(player,objectName(),"qun+wei");
			room->changeKingdom(player,choice);
			player->setTag("chishiKingdom", choice);
		}else if(!player->getTag("chishiDying").toBool()){
			player->setTag("chishiDying", true);
			QString k = player->getTag("chishiKingdom").toString();
			if(k.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player,this);
			if(k=="qun") k = "wei";
			else k = "qun";
			room->changeKingdom(player,k);
		}
		return false;
	}
};

KouchaoCard::KouchaoCard()
{
	target_fixed = true;
}

void KouchaoCard::onUse(Room*room, CardUseStruct &use) const
{
	int n = 0;
	QStringList choices, cns = use.from->property("KouchaoCNS").toString().split("+");
	if(cns.length()<2){
		cns.clear();
		cns << "slash" << "fire_attack" << "dismantlement";
	}
	foreach(QString cn, cns){
		n++;
		if (use.from->getMark(QString("%1%2KouchaoUse_lun").arg(cn).arg(n))>0
		||use.from->getMark(QString("%1KouchaoNum_lun").arg(n))>0) continue;
		Card*dc = Sanguosha->cloneCard(cn);
		if (!dc) continue;
		dc->setSkillName(getSkillName());
		dc->deleteLater();
		if(dc->isAvailable(use.from))
			choices << cn;
	}
	if (choices.isEmpty()) return;
	QString choice = room->askForChoice(use.from, getSkillName(), choices.join("+"));
	room->setPlayerProperty(use.from,"KouchaoUse",choice);
	room->askForUseCard(use.from,"@@kouchao","kouchao0:"+choice);
}

class KouchaoVS : public ViewAsSkill
{
public:
	KouchaoVS() : ViewAsSkill("kouchao")
	{
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.isEmpty()||selected.length()>0){
			return false;
		}else{
			if(pattern=="@@kouchao")
				pattern = Self->property("KouchaoUse").toString();
			Card*dc = Sanguosha->cloneCard(pattern.split("+").first());
			dc->setSkillName(objectName());
			dc->addSubcard(to_select);
			dc->deleteLater();
			return dc->isAvailable(Self);
		}
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		QStringList cns = player->property("KouchaoCNS").toString().split("+");
		if(cns.length()<2){
			cns.clear();
			cns << "slash" << "fire_attack" << "dismantlement";
		}
		int n = 1;
		foreach(QString cn, cns){
			if (player->getMark(QString("%1%2KouchaoUse_lun").arg(cn).arg(n))<1&&player->getMark(QString("%1KouchaoNum_lun").arg(n))<1)
				return player->getCardCount()>0;
			n++;
		}
		return false;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
			return false;
		QStringList cns = player->property("KouchaoCNS").toString().split("+");
		if(cns.length()<2){
			cns.clear();
			cns << "slash" << "fire_attack" << "dismantlement";
		}
		int n = 1;
		foreach(QString cn, cns){
			if (pattern.contains(cn)&&player->getMark(QString("%1%2KouchaoUse_lun").arg(cn).arg(n))<1&&player->getMark(QString("%1KouchaoNum_lun").arg(n))<1)
				return player->getCardCount()>0;
			n++;
		}
		return pattern=="@@kouchao";
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			return new KouchaoCard;
		}else{
			if(pattern=="@@kouchao")
				pattern = Self->property("KouchaoUse").toString();
			Card*dc = Sanguosha->cloneCard(pattern.split("+").first());
			dc->setSkillName(objectName());
			dc->addSubcards(cards);
			return dc;
		}
	}
};

class Kouchao : public TriggerSkill
{
public:
	Kouchao() : TriggerSkill("kouchao")
	{
		events << CardsMoveOneTime << CardFinished << PreCardUsed;
		view_as_skill = new KouchaoVS;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if(move.to_place==Player::DiscardPile&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE){
				QString cn;
				foreach(int id, move.card_ids){
					const Card*c = Sanguosha->getCard(id);
					if (c->isKindOf("BasicCard")||c->isNDTrick())
						cn = c->objectName();
				}
				if(cn.isEmpty()) return false;
				if(cn.contains("slash")) cn = "slash";
				foreach(QString m, player->getMarkNames()){
					if (m.contains("&kouchao+:+"))
						room->setPlayerMark(player,m,0);
				}
				room->setPlayerMark(player,"&kouchao+:+"+cn,1);
			}
		}else if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->getSkillNames().contains(objectName())){
				QStringList cns = player->property("KouchaoCNS").toString().split("+");
				if(cns.length()<2){
					cns.clear();
					cns << "slash" << "fire_attack" << "dismantlement";
				}
				int n = 1;
				foreach(QString cn, cns){
					QString m = QString("%1%2KouchaoUse_lun").arg(cn).arg(n);
					QString m2 = QString("%1KouchaoNum_lun").arg(n);
					if (cn==use.card->objectName()&&player->getMark(m)<1&&player->getMark(m2)<1){
						room->addPlayerMark(player,m2);
						room->addPlayerMark(player,m);
						player->setMark("KouchaoUse",n-1);
						break;
					}
					n++;
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->getSkillNames().contains(objectName())){
				QString cn;
				foreach(QString m, player->getMarkNames()){
					if (m.contains("&kouchao+:+")&&player->getMark(m)>0)
						cn = m.split("+").last();
				}
				if(cn.isEmpty()) return false;
				QStringList cns = player->property("KouchaoCNS").toString().split("+");
				if(cns.length()<2){
					cns.clear();
					cns << "slash" << "fire_attack" << "dismantlement";
				}
				cns[player->getMark("KouchaoUse")] = cn;
				room->setPlayerProperty(player,"KouchaoCNS",cns.join("+"));
				int n = 1;
				foreach(QString cn, cns){
					player->setSkillDescriptionSwap(objectName(),"%arg"+QString::number(n),cn);
					n++;
				}
				room->changeTranslation(player, objectName());
				foreach(QString cn, cns){
					Card*dc = Sanguosha->cloneCard(cn);
					dc->deleteLater();
					if(dc->getTypeId()>1)
						return false;
				}
				cns.clear();
				cns << "snatch" << "snatch" << "snatch";
				room->setPlayerProperty(player,"KouchaoCNS",cns.join("+"));
				n = 1;
				foreach(QString cn, cns){
					player->setSkillDescriptionSwap(objectName(),"%arg"+QString::number(n),cn);
					n++;
				}
				room->changeTranslation(player, objectName());
			}
		}
		return false;
	}
};

HunjiangCard::HunjiangCard()
{
}

bool HunjiangCard::targetFilter(const QList<const Player*> &, const Player*to_select, const Player*Self) const
{
	return Self->inMyAttackRange(to_select);
}

void HunjiangCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	QStringList choices;
	foreach(ServerPlayer*p, targets){
		QString choice = "hunjiang1="+source->objectName()+"+hunjiang2="+source->objectName();
		choice = room->askForChoice(p,getSkillName(),choice,QVariant::fromValue(source));
		choices << choice;
	}
	int n = 0;
	foreach(QString ch, choices){
		LogMessage log;
		log.type = "$hunjiang0";
		log.from = targets[n];
		log.arg = "@"+ch.split("=").first();
		room->sendLog(log);
		n++;
	}
	n = 0;
	foreach(QString ch, choices){
		bool has = true;
		foreach(QString th, choices){
			if(th!=choices.first())
				has = false;
		}
		if(has){
			room->setPlayerMark(targets[n],"&hunjiang-PlayClear",1);
			source->drawCards(1,getSkillName());
		}else{
			if(ch.contains("hunjiang1"))
				room->setPlayerMark(targets[n],"&hunjiang-PlayClear",1);
			else
				source->drawCards(1,getSkillName());
		}
		n++;
	}
}

class Hunjiangvs : public ZeroCardViewAsSkill
{
public:
	Hunjiangvs() : ZeroCardViewAsSkill("hunjiang")
	{
	}

	const Card*viewAs() const
	{
		return new HunjiangCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("HunjiangCard");
	}
};

class Hunjiang : public TriggerSkill
{
public:
	Hunjiang() : TriggerSkill("hunjiang")
	{
		events << CardUsed;
		view_as_skill = new Hunjiangvs;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				QList<ServerPlayer*>aps;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if (!use.to.contains(p)&&p->getMark("&hunjiang-PlayClear")>0)
						aps << p;
				}
				player->setTag("hunjiangUse", data);
				QList<ServerPlayer*>tos = room->askForPlayersChosen(player,aps,objectName(),0,aps.length(),"hunjiang0:",true,false);
				if(tos.length()>0){
					player->peiyin(this);
					use.to << tos;
					room->sortByActionOrder(use.to);
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class Maozhu : public TriggerSkill
{
public:
	Maozhu() : TriggerSkill("maozhu")
	{
		events << DamageCaused;
		frequency = Compulsory;
		waked_skills = "#maozhu_max";
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (player->getMark("maozhuUse-PlayClear")<1&&player->getPhase()==Player::Play){
			DamageStruct damage = data.value<DamageStruct>();
			int n = 0, x = 0;
			foreach(const Skill*s, player->getVisibleSkillList()){
				if (!s->isAttachedLordSkill())
					n++;
			}
			foreach(const Skill*s, damage.to->getVisibleSkillList()){
				if (!s->isAttachedLordSkill())
					x++;
			}
			if(n>x){
				room->sendCompulsoryTriggerLog(player,this);
				player->addMark("maozhuUse-PlayClear");
				return player->damageRevises(data,1);
			}
		}
		return false;
	}
};

class MaozhuMax : public MaxCardsSkill
{
public:
	MaozhuMax() : MaxCardsSkill("#maozhu_max")
	{
	}

	int getExtra(const Player*target) const
	{
		int n = 0;
		if (target->hasSkill("maozhu")){
			foreach(const Skill*s, target->getVisibleSkillList()){
				if (!s->isAttachedLordSkill())
					n++;
			}
		}
		if(target->getMark("&jieyan2")>0&&target->getPhase()==Player::Discard)
			n -= 2;
		return n;
	}
};

JinlanCard::JinlanCard()
{
	target_fixed = true;
}

void JinlanCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	int n = 0;
	foreach(ServerPlayer*p, room->getAlivePlayers()){
		int x = 0;
		foreach(const Skill*s, p->getVisibleSkillList()){
			if (!s->isAttachedLordSkill())
				x++;
		}
		if(x>n) n = x;
	}
	source->drawCards(n-source->getHandcardNum(),getSkillName());
}

class Jinlan : public ZeroCardViewAsSkill
{
public:
	Jinlan() : ZeroCardViewAsSkill("jinlan")
	{
	}

	const Card*viewAs() const
	{
		return new JinlanCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("JinlanCard");
	}
};

class Jianmanvs : public ZeroCardViewAsSkill
{
public:
	Jianmanvs() : ZeroCardViewAsSkill("jianman")
	{
		response_pattern = "@@jianman";
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	const Card*viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@jianman"){
			pattern = Self->property("jianmanUse").toString();
			Card*dc = Sanguosha->cloneCard(pattern);
			dc->setSkillName("_jianman");
			return dc;
		}
		return nullptr;
	}
};

class Jianman : public TriggerSkill
{
public:
	Jianman() : TriggerSkill("jianman")
	{
		events << CardUsed << EventPhaseChanging;
		view_as_skill = new Jianmanvs;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive && player->getMark("jianmanNum-Clear")>1){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if (p->isAlive()&&p->hasSkill(objectName())){
						CardUseStruct use1 = room->getTag("jianmanData1").value<CardUseStruct>();
						CardUseStruct use2 = room->getTag("jianmanData2").value<CardUseStruct>();
						if(use1.from==p||use2.from==p){
							room->sendCompulsoryTriggerLog(p,this);
							if(use1.from==p&&use2.from==p){
								QStringList choices;
								Card*dc = Sanguosha->cloneCard(use1.card->objectName());
								dc->setSkillName("_jianman");
								if(dc->isAvailable(p))
									choices << use1.card->objectName();
								dc->deleteLater();
								dc = Sanguosha->cloneCard(use2.card->objectName());
								dc->setSkillName("_jianman");
								if(dc->isAvailable(p))
									choices << use1.card->objectName();
								dc->deleteLater();
								if(choices.isEmpty()) continue;
								QString cho = room->askForChoice(p,objectName(),choices.join("+"));
								room->setPlayerProperty(p,"jianmanUse",cho);
								if(room->askForUseCard(p,"@@jianman","jianman0:"+cho)) continue;
								dc = Sanguosha->cloneCard(cho);
								dc->setSkillName("_jianman");
								room->useCard(CardUseStruct(dc,p,dc->targetFixed()?QList<ServerPlayer*>():p->getRandomTargets(dc)));
							}else{
								ServerPlayer*tp = use1.from;
								if(tp==p) tp = use2.from;
								if(p->canDiscard(tp,"he")){
									int id = room->askForCardChosen(p,tp,"he",objectName(),false,Card::MethodDiscard);
									if(id>-1) room->throwCard(id,objectName(),tp,p);
								}
							}
						}
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()==1){
				ServerPlayer*cp = room->getCurrent();
				cp->addMark("jianmanNum-Clear");
				int n = cp->getMark("jianmanNum-Clear");
				if(n<3)
					room->setTag("jianmanData"+QString::number(n),data);
			}
		}
		return false;
	}
};

static bool liwenXianLW(ServerPlayer*a, ServerPlayer*b)
{
	return a->getMark("&xianLW") > b->getMark("&xianLW");
}

class Liwen : public TriggerSkill
{
public:
	Liwen() : TriggerSkill("liwen")
	{
		events << CardUsed << EventPhaseChanging << MarkChanged << GameStart;
		global = true;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to==Player::NotActive&&player->getMark("&xianLW")>0&&player->hasSkill(objectName())){
				QList<ServerPlayer*>aps;
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->getMark("&xianLW")<5) aps << p;
				}
				foreach(ServerPlayer*p, room->askForPlayersChosen(player,aps,objectName()+"$-1",0,player->getMark("&xianLW"),"liwen0:",true,true)){
					player->loseMark("&xianLW");
					p->gainMark("&xianLW");
				}
				aps = room->getAlivePlayers();
				std::stable_sort(aps.begin(), aps.end(), liwenXianLW);
				foreach(ServerPlayer*p, aps){
					if(p->getMark("&xianLW")>0){
						QStringList ids;
						room->doAnimate(1,player->objectName(),p->objectName());
						foreach(const Card*c, p->getHandcards()){
							if(c->isKindOf("BasicCard")&&c->isAvailable(p))
								ids << c->toString();
						}
						if(ids.isEmpty()||!room->askForUseCard(p,ids.join(","),"liwen1:"))
							p->loseAllMarks("&xianLW");
					}
				}
			}
		}else if(event==GameStart){
			if (player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&xianLW",3);
			}
		}else if(event==MarkChanged){
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="&xianLW"&&mark.gain<0){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,objectName());
						p->drawCards(-mark.gain,objectName());
					}
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				QString mt;
				foreach(QString m, player->getMarkNames()){
					if(m.contains("liwen+")&&m.contains("_char")){
						room->setPlayerMark(player,m,0);
						mt = m;
					}
				}
				player->setMark("liwen+"+use.card->getType()+"+"+use.card->getSuitString()+"_char",1);
				if(player->hasSkill(objectName(),true)){
					room->setPlayerMark(player,"&liwen+"+use.card->getType()+"+"+use.card->getSuitString()+"_char",1);
					if(mt.contains(use.card->getSuitString())||mt.contains(use.card->getType())){
						if(player->getMark("&xianLW")<5&&player->hasSkill(objectName())){
							room->sendCompulsoryTriggerLog(player,objectName());
							player->gainMark("&xianLW");
						}
					}
				}
			}
		}
		return false;
	}
};

class Zhengyi : public TriggerSkill
{
public:
	Zhengyi() : TriggerSkill("zhengyi")
	{
		events << DamageInflicted << DamageComplete;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature==DamageStruct::Normal&&player->getMark("&xianLW")>0){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(!p->hasSkill(objectName())) continue;
					room->sendCompulsoryTriggerLog(p,this);
					QList<ServerPlayer*>aps;
					int n = 0;
					foreach(ServerPlayer*q, room->getOtherPlayers(player)){
						if(q->getMark("&xianLW")>0&&room->askForChoice(q,objectName(),"yes+no",data)=="yes"){
							n = qMax(n,q->getHp());
							aps << q;
						}
					}
					if(aps.isEmpty()) continue;
					QStringList lh;
					foreach(ServerPlayer*q, aps){
						if(q->getHp()>=n) lh << q->objectName();
					}
					player->setTag("zhengyiLoseHp", lh);
					player->setTag("zhengyiDamage", damage.damage);
					return player->damageRevises(data,-damage.damage);
				}
			}
		}else if(event==DamageComplete){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.prevented){
				int n = player->getTag("zhengyiDamage").toInt();
				if(n<1) return false;
				player->removeTag("zhengyiDamage");
				QStringList lh = player->getTag("zhengyiLoseHp").toStringList();
				ServerPlayer*from = room->findPlayerBySkillName(objectName());
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(lh.contains(p->objectName()))
						room->loseHp(p,n,true,from,objectName());
				}
			}
		}
		return false;
	}
};

HongtuCard::HongtuCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool HongtuCard::targetFilter(const QList<const Player*> &tos, const Player*to_select, const Player*Self) const
{
	return tos.isEmpty()&&Self!=to_select;
}

void HongtuCard::onUse(Room*room, CardUseStruct &use) const
{
	QList<int> ids = getSubcards();
	room->showCard(use.from,ids);
	room->doAnimate(1,use.from->objectName(),use.to.first()->objectName());
	room->notifyMoveToPile(use.to.first(),ids,"hongtu",Player::PlaceHand,true);
	const Card*dc = room->askForUseCard(use.to.first(),"@@hongtu","hongtu1:");
	room->notifyMoveToPile(use.to.first(),ids,"hongtu",Player::PlaceHand,false);
	if(dc){
		ids.removeOne(dc->getEffectiveId());
		for (int i = 0; i < 9; i++){
			int id = ids.at(qrand()%ids.length());
			if(use.from->canDiscard(use.from,id)){
				room->throwCard(id,"hongtu",use.from);
				break;
			}
		}
		if(use.to.first()->isDead())
			return;
		bool max = true, min = true;
		foreach(int id, ids){
			const Card*c = Sanguosha->getCard(id);
			if(c->getNumber()>=dc->getNumber())
				max = false;
			if(c->getNumber()<=dc->getNumber())
				min = false;
		}
		if(max){
			room->acquireOneTurnSkills(use.to.first(),"hongtu","feijun");
		}else if(min){
			room->addPlayerMark(use.to.first(),"hongtuMaxCards-SelfClear",2);
		}else{
			room->acquireOneTurnSkills(use.to.first(),"hongtu","qianxi");
		}
	}else{
		room->damage(DamageStruct("hongtu",use.from,use.to.first(),1,DamageStruct::Fire));
		room->damage(DamageStruct("hongtu",use.from,use.from,1,DamageStruct::Fire));
	}
}

class HongtuVS : public ViewAsSkill
{
public:
	HongtuVS() : ViewAsSkill("hongtu")
	{
		expand_pile = "#hongtu";
	}

	bool viewFilter(const QList<const Card*> &selected, const Card*to_select) const
	{
		if(to_select->isEquipped()) return false;
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern=="@@hongtu"){
			return selected.isEmpty()&&Self->getPile("#hongtu").contains(to_select->getEffectiveId())&&to_select->isAvailable(Self);
		}
		return selected.length()<3;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.contains("@@hongtu");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern=="@@hongtu"){
			if(cards.isEmpty()) return nullptr;
			return cards[0];
		}
		if(cards.length()>=3){
			HongtuCard*card = new HongtuCard;
			card->addSubcards(cards);
			return card;
		}
		return nullptr;
	}
};

class Hongtu : public TriggerSkill
{
public:
	Hongtu() : TriggerSkill("hongtu")
	{
		events << EventPhaseEnd << CardsMoveOneTime;
		view_as_skill = new HongtuVS;
		waked_skills = "feijun,qianxi,#hongtu_max";
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseEnd){
			if (player->getMark("&hongtu-Clear")>1&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				player->drawCards(3,objectName());
				room->askForUseCard(player,"@@hongtu!","hongtu0:");
			}
			room->setPlayerMark(player,"&hongtu-Clear",0);
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player&&player->hasFlag("CurrentPlayer")){
				room->addPlayerMark(player,"&hongtu-Clear",move.card_ids.length());
			}
		}
		return false;
	}
};

class HongtuMax : public MaxCardsSkill
{
public:
	HongtuMax() : MaxCardsSkill("#hongtu_max")
	{
	}

	int getExtra(const Player*target) const
	{
		return target->getMark("hongtuMaxCards-SelfClear");
	}
};

class Xiwu : public TriggerSkill
{
public:
	Xiwu() : TriggerSkill("xiwu")
	{
		events << DamageInflicted;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from&&player->distanceTo(damage.from)<=player->getAttackRange()&&player->getMark("xiwuUse-Clear")<1){
				player->addMark("xiwuUse-Clear");
				if(room->askForDiscard(player,objectName(),1,1,true,true,"xiwu0:",".|red",objectName())){
					player->peiyin(this);
					return player->damageRevises(data,-damage.damage);
				}
			}
		}
		return false;
	}
};

FushiCard::FushiCard()
{
}

bool FushiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard("slash");
	card->setSkillName("fushi");
	card->deleteLater();
	return card->targetFilter(targets, to_select, Self);
}

const Card*FushiCard::validate(CardUseStruct &cardUse) const
{
	Room*room = cardUse.from->getRoom();
	room->throwCard(this,"fushi",nullptr);
	cardUse.from->drawCards(subcardsLength(),"fushi");
	Card*card = Sanguosha->cloneCard("slash");
	card->setSkillName("fushi");
	card->deleteLater();
	if(cardUse.from->isDead())
		return card;
	QStringList choices;
	QList<ServerPlayer*>tos;
	foreach(ServerPlayer*p, room->getAlivePlayers()){
		if(cardUse.to.contains(p)||!cardUse.from->canSlash(p,card)) continue;
		tos << p;
	}
	if(tos.length()>0) choices << "fushi1";
	choices << "fushi2" << "fushi3";
	for (int i = 0; i < subcardsLength(); i++){
		QString choice = room->askForChoice(cardUse.from,"fushi",choices.join("+"),QVariant::fromValue(cardUse));
		if(choice=="fushi1"){
			ServerPlayer*to = room->askForPlayerChosen(cardUse.from,tos,choice);
			if(to){
				room->doAnimate(1,cardUse.from->objectName(),to->objectName());
				cardUse.to << to;
			}
		}else{
			ServerPlayer*to = room->askForPlayerChosen(cardUse.from,cardUse.to,choice);
			if(to){
				room->doAnimate(1,cardUse.from->objectName(),to->objectName());
				to->addMark(choice+"Bf-Clear");
			}
		}
		choices.removeOne(choice);
		if(choices.isEmpty()) break;
	}
	if(subcardsLength()>1&&!choices.contains("fushi2")&&cardUse.to.length()>1){
		bool has = true;
		foreach(ServerPlayer*p1, cardUse.to){
			foreach(ServerPlayer*p2, cardUse.to){
				if(p1!=p2&&!p1->isAdjacentTo(p2))
					has = false;
			}
		}
		cardUse.m_addHistory = !has;
	}
	return card;
}

class FushiVs : public ViewAsSkill
{
public:
	FushiVs() : ViewAsSkill("fushi")
	{
		expand_pile = "fushi";
	}

	bool viewFilter(const QList<const Card*> &, const Card*to_select) const
	{
		return Self->getPile("fushi").contains(to_select->getEffectiveId());
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getPile("fushi").length()>0&&Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.contains("slash")&&Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		FushiCard*card = new FushiCard;
		card->addSubcards(cards);
		return card;
	}
};

class Fushi : public TriggerSkill
{
public:
	Fushi() : TriggerSkill("fushi")
	{
		events << CardFinished << Predamage;
		view_as_skill = new FushiVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->distanceTo(player)<2&&p->hasSkill(objectName())&&room->getCardPlace(use.card->getEffectiveId())==Player::DiscardPile){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("fushi",use.card);
					}
					p->removeMark("fushi2Bf-Clear");
					p->removeMark("fushi3Bf-Clear");
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getSkillNames().contains(objectName())){
				if(damage.to->getMark("fushi2Bf-Clear")>0){
					return player->damageRevises(data,-1);
				}
				if(damage.to->getMark("fushi3Bf-Clear")>0){
					return player->damageRevises(data,1);
				}
			}
		}
		return false;
	}
};

class Dongdao : public TriggerSkill
{
public:
	Dongdao() : TriggerSkill("dongdao")
	{
		events << EventPhaseStart;
		change_skill = true;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::NotActive&&player->getRole()=="rebel"&&room->getMode()=="03_1v2"){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						int n = p->getChangeSkillState(objectName());
						if(n==1){
							ServerPlayer*l = room->getLord();
							if(l&&p->askForSkillInvoke(this,l)){
								room->setChangeSkillState(p, objectName(), 2);
								l->gainAnExtraTurn();
							}
						}else{
							if(player->askForSkillInvoke(this,data,false)){
								room->setChangeSkillState(p, objectName(), 1);
								player->gainAnExtraTurn();
							}
						}
					}
				}
			}
		}
		return false;
	}
};

ZuolianCard::ZuolianCard()
{
}

bool ZuolianCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.length()<Self->getHp()&&!to_select->isKongcheng();
}

void ZuolianCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	QList<int>ids;
	foreach(ServerPlayer*p, targets){
		if(p->isKongcheng()) continue;
		int id = p->handCards().at(qrand()%p->getHandcardNum());
		room->showCard(p,id);
		ids << id;
	}
	if(source->isDead()||ids.isEmpty()||!source->askForSkillInvoke("zuolian","zuolian0:",false)) return;
	qShuffle(ids);
	QList<CardsMoveStruct> exchangeMove;
	foreach(int id, room->getDrawPile()+room->getDiscardPile()){
		if(Sanguosha->getCard(id)->isKindOf("FireSlash")){
			CardsMoveStruct move1(id, room->getCardOwner(ids.first()), Player::PlaceHand, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, source->objectName()));
			exchangeMove << move1;
			CardsMoveStruct move2(ids.first(), nullptr, room->getCardPlace(id), CardMoveReason(CardMoveReason::S_MASK_BASIC_REASON, source->objectName()));
			exchangeMove << move2;
			ids.removeAt(0);
			if(ids.isEmpty()) break;
		}
	}
	if(exchangeMove.isEmpty()){
		foreach(int id, room->getDrawPile()+room->getDiscardPile()){
			if(Sanguosha->getCard(id)->isKindOf("ThunderSlash")){
				CardsMoveStruct move1(id, room->getCardOwner(ids.first()), Player::PlaceHand, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, source->objectName()));
				exchangeMove << move1;
				CardsMoveStruct move2(ids.first(), nullptr, room->getCardPlace(id), CardMoveReason(CardMoveReason::S_MASK_BASIC_REASON, source->objectName()));
				exchangeMove << move2;
				ids.removeAt(0);
				if(ids.isEmpty()) break;
			}
		}
	}
	room->moveCardsAtomic(exchangeMove, true);
}

class Zuolian : public ZeroCardViewAsSkill
{
public:
	Zuolian() : ZeroCardViewAsSkill("zuolian")
	{
	}

	const Card*viewAs() const
	{
		return new ZuolianCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("ZuolianCard");
	}
};

class Jingzhou : public TriggerSkill
{
public:
	Jingzhou() : TriggerSkill("jingzhou")
	{
		events << DamageInflicted;
	}
	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		player->setTag("jingzhouDamage", data);
		QList<ServerPlayer*> tos = room->askForPlayersChosen(player,room->getAlivePlayers(),objectName(),0,player->getHp(),"jingzhou0:"+QString::number(player->getHp()),true,true);
		if(tos.length()>0){
			player->peiyin(this);
			foreach(ServerPlayer*p, tos){
				room->setPlayerChained(p);
			}
		}
		return false;
	}
};

class Pijing : public TriggerSkill
{
public:
	Pijing() : TriggerSkill("pijing")
	{
		events << TargetSpecifying;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")||use.card->isNDTrick()){
				if(use.to.size()==1){
					foreach(ServerPlayer*p, room->getOtherPlayers(player)){
						if(player->getMark("&pijing+#"+p->objectName())>0){
							QString choice = "pijing1="+p->objectName()+"+pijing2+cancel";
							if(use.to.contains(p)) choice = "pijing2+cancel";
							choice = room->askForChoice(p,objectName(),choice,QVariant::fromValue(p));
							if(choice.contains("pijing1")){
								use.to.append(p);
							}else if(choice.contains("pijing2")){
								player->drawCards(1,objectName());
							}
							room->setPlayerMark(player,"&pijing+#"+p->objectName(),0);
						}
					}
				}
				if(player->hasSkill(objectName())&&player->getMark("pijingUse-Clear")<1&&player->hasTurn()){
					QList<ServerPlayer*>tos = room->getCardTargets(player,use.card);
					if(tos.contains(player)) tos.removeOne(player);
					int n = qMax(1,player->getLostHp());
					player->setTag("pijingUse", data);
					tos = room->askForPlayersChosen(player,tos,objectName()+"$-1",0,n,"pijing0:"+QString::number(n),true,true);
					if(tos.length()>0){
						player->addMark("pijingUse-Clear");
						foreach(ServerPlayer*p, tos){
							if(use.to.contains(p)) use.to.removeOne(p);
							else use.to.append(p);
							QList<const Card*> cs = p->getCards("he");
							if(cs.length()>0&&player->isAlive()){
								const Card*c = cs.at(qrand()%cs.length());
								room->giveCard(p,player,c,objectName(),false);
							}
							room->setPlayerMark(p,"&pijing+#"+player->objectName(),1);
						}
					}
				}
				if(use.to.size()>1)
					room->sortByActionOrder(use.to);
				data.setValue(use);
			}
		}
		return false;
	}
};

WeifuCard::WeifuCard()
{
	target_fixed = true;
}

void WeifuCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	JudgeStruct judge;
	judge.who = source;
	judge.reason = "weifu";
	judge.pattern = ".";
	judge.play_animation = false;
	room->judge(judge);
	room->addPlayerMark(source,"&weifu+"+judge.card->getType()+"-Clear");
	const Card*c = Sanguosha->getCard(getEffectiveId());
	if(c->getType()==judge.card->getType())
		source->drawCards(1,"weifu");
}

class WeifuVs : public OneCardViewAsSkill
{
public:
	WeifuVs() : OneCardViewAsSkill("weifu")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		return !Self->isJilei(to_select);
	}

	const Card*viewAs(const Card*originalCard) const
	{
		WeifuCard*c = new WeifuCard;
		c->addSubcard(originalCard);
		return c;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0;
	}
};

class Weifu : public TriggerSkill
{
public:
	Weifu() : TriggerSkill("weifu")
	{
		events << PreCardUsed;
		view_as_skill = new WeifuVs;
		waked_skills = "#weifu";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(player->getMark("&weifu+"+use.card->getType()+"-Clear")>0){
				room->setPlayerMark(player,"&weifu+"+use.card->getType()+"-Clear",0);
			}
		}
		return false;
	}
};

class WeifuMod : public TargetModSkill
{
public:
	WeifuMod() : TargetModSkill("#weifu")
	{
		pattern = ".";
	}

	int getExtraTargetNum(const Player*from, const Card*card) const
	{
		return from->getMark("&weifu+"+card->getType()+"-Clear");
	}

	int getDistanceLimit(const Player*from, const Card*card, const Player*) const
	{
		if (card->hasFlag("tunan_distance"))
			return 999;
		if (card->hasFlag("yidian_distance") && (card->isNDTrick() || card->isKindOf("BasicCard")))
			return 999;
		if (card->getSkillName()=="jiewan")
			return 999;
		if (card->hasFlag("sheyan_distance") && card->isNDTrick())
			return 999;
		if (card->hasFlag("spcanshi_distance") && from->hasSkill("spcanshi"))
			return 999;
		if (from->getMark("&weifu+"+card->getType()+"-Clear")>0)
			return 999;
		return 0;
	}
};

class Kuansai : public TriggerSkill
{
public:
	Kuansai() : TriggerSkill("kuansai")
	{
		events << TargetSpecified;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(use.to.length()>=p->getHp()&&p->hasTurn()
						&&p->getMark("kuansai-Clear")<1&&p->hasSkill(objectName())){
						ServerPlayer*to = room->askForPlayerChosen(p,use.to,objectName()+"$-1","kuansai0:",true,true);
						if(to){
							p->addMark("kuansai-Clear");
							QStringList chosens;
							if(to->getCardCount()>0) chosens << "kuansai1="+p->objectName();
							if(p->getLostHp()>0) chosens << "kuansai2"+p->objectName();
							if(chosens.isEmpty()) continue;
							if(room->askForChoice(to,objectName(),chosens.join("+"),QVariant::fromValue(p)).contains("kuansai2")){
								room->recover(p,RecoverStruct(objectName(),to));
							}else{
								const Card*dc = room->askForExchange(to,objectName(),1,1,true,"kuansai01:"+p->objectName());
								if(dc) room->giveCard(to,p,dc,objectName());
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class Lianju : public TriggerSkill
{
public:
	Lianju() : TriggerSkill("lianju")
	{
		events << CardFinished << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasFlag("CurrentPlayer")){
				QVariantList ids = player->getTag("lianjuUse").toList();
				foreach(int id, use.card->getSubcards()){
					ids << id;
				}
				player->setTag("lianjuUse", ids);
			}
		}else if(player->getPhase()==Player::Finish){
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if(player->getMark("&lianju+#"+p->objectName())>0){
					QList<int> ids, ids2 = ListV2I(player->getTag("lianjuUse").toList());
					foreach(int id, room->getDiscardPile()){
						if(ids2.contains(id)&&p->getMark("&lianju+:+"+Sanguosha->getCard(id)->getColorString())<1)
							ids << id;
					}
					if(ids.length()>0){
						DummyCard*dc = new DummyCard();
						room->fillAG(ids,p);
						for (int i = 0; i < 2; i++){
							int id = room->askForAG(p, ids, true, objectName());
							if(id<0) break;
							room->takeAG(p,id,false,QList<ServerPlayer*>()<<p);
							dc->addSubcard(id);
							ids.removeOne(id);
							if(ids.length()<1) break;
						}
						room->clearAG(p);
						room->obtainCard(p,dc,objectName());
						dc->deleteLater();
					}
					room->setPlayerMark(player,"&lianju+#"+p->objectName(),0);
				}
			}
			if(player->hasSkill(objectName())){
				QList<int> ids, ids2 = ListV2I(player->getTag("lianjuUse").toList());
				foreach(int id, room->getDiscardPile()){
					if(ids2.contains(id)) ids << id;
				}
				if(ids.length()>0){
					DummyCard*dc = new DummyCard();
					room->fillAG(ids,player);
					ServerPlayer*to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"lianju0:",true,true);
					if(to){
						int id = room->askForAG(player, ids, false, objectName());
						if(id>-1){
							ids2 << id;
							ids.removeOne(id);
							dc->addSubcard(id);
							foreach(int idn, ids){
								if(Sanguosha->getCard(id)->getColor()!=Sanguosha->getCard(idn)->getColor())
									ids.removeOne(idn);
							}
							if(ids.length()>0){
								room->fillAG(ids,player);
								id = room->askForAG(player, ids, true, objectName());
								if(id>-1) dc->addSubcard(id);
								room->clearAG(player);
							}
						}
					}
					room->clearAG(player);
					if(dc->subcardsLength()>0&&to->isAlive()){
						room->obtainCard(to,dc,objectName());
						room->setPlayerMark(to,"&lianju+#"+player->objectName(),1);
						if(player->isAlive()){
							foreach(QString m, player->getMarkNames()){
								if(m.contains("&lianju+:+"))
									room->setPlayerMark(player,m,0);
							}
							room->setPlayerMark(player,"&lianju+:+"+dc->getColorString(),1);
						}
					}
					dc->deleteLater();
				}
			}
			player->removeTag("lianjuUse");
		}
		return false;
	}
};

class Shilv : public TriggerSkill
{
public:
	Shilv() : TriggerSkill("shilv")
	{
		events << Damaged << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event==Damaged){
			room->sendCompulsoryTriggerLog(player,this);
			player->drawCards(1,objectName());
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&(move.reason.m_skillName=="lianju"||move.reason.m_skillName=="shilv")){
				QVariantList ids = room->getTag("shi_lvs").toList();
				foreach(int id, move.to->handCards()){
					if(move.card_ids.contains(id)){
						room->setCardTip(id,"shi_lv");
						ids << id;
					}
				}
				room->setTag("shi_lvs",ids);
			}else if(move.to_place==Player::DiscardPile&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD&&move.from){
				QVariantList ids = room->getTag("shi_lvs").toList();
				DummyCard*dc = new DummyCard();
				foreach(int id, move.card_ids){
					if(ids.contains(QVariant(id))){
						ids.removeOne(QVariant(id));
						if(!room->getCardOwner(id))
							dc->addSubcard(id);
					}
				}
				room->setTag("shi_lvs",ids);
				if(dc->subcardsLength()&&move.from->isAlive()){
					room->sendCompulsoryTriggerLog(player,this);
					room->obtainCard((ServerPlayer*)move.from,dc);
				}
				dc->deleteLater();
			}else if(move.from_places.contains(Player::PlaceHand)){
				QVariantList ids = room->getTag("shi_lvs").toList();
				foreach(int id, move.card_ids){
					if(ids.contains(QVariant(id)))
						ids.removeOne(QVariant(id));
				}
				room->setTag("shi_lvs",ids);
			}
		}
		return false;
	}
};

class Changxin : public TriggerSkill
{
public:
	Changxin() : TriggerSkill("changxin")
	{
		events << DamageInflicted << EventPhaseStart;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event==DamageInflicted){
			if(player->getMark("changxin-Clear")>0) return false;
			player->addMark("changxin-Clear");
			room->sendCompulsoryTriggerLog(player,this);
			QList<int>ids = room->showDrawPile(player,3,objectName(),false,false);
			DummyCard*dc = new DummyCard();
			room->fillAG(ids,player);
			int n = 0;
			player->setTag("changxinDamage", data);
			while(player->isAlive()){
				int id = room->askForAG(player, ids, true, objectName());
				if(id<0) break;
				room->takeAG(player,id,false,QList<ServerPlayer*>()<<player);
				dc->addSubcard(id);
				ids.removeOne(id);
				if(Sanguosha->getCard(id)->getSuit()==2) n--;
				if(ids.length()<1) break;
			}
			room->clearAG(player);
			room->throwCard(dc,objectName(),player);
			dc->deleteLater();
			return player->damageRevises(data,n);
		}else if(player->getPhase()==Player::Draw){
			room->sendCompulsoryTriggerLog(player,this);
			QList<int>ids = room->showDrawPile(player,3,objectName(),false,false);
			int n = 0;
			foreach(int id, ids){
				if(Sanguosha->getCard(id)->getSuit()==2) n++;
			}
			room->getThread()->delay();
			player->drawCards(n,objectName());
		}
		return false;
	}
};

class Runwei : public TriggerSkill
{
public:
	Runwei() : TriggerSkill("runwei")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(player->getPhase()==Player::Discard&&player->isWounded()){
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if(p->hasSkill(objectName())&&p->askForSkillInvoke(this,player)){
					p->peiyin(this);
					if(player->canDiscard(player,"he")&&room->askForChoice(p,objectName(),"runwei1+runwei2",QVariant::fromValue(player))=="runwei2"){
						room->askForDiscard(player,objectName(),1,1,false,true);
						room->addMaxCards(player,1);
					}else{
						player->drawCards(1,objectName());
						room->addMaxCards(player,-1);
					}
				}
			}
		}
		return false;
	}
};

class Qingyuan : public TriggerSkill
{
public:
	Qingyuan() : TriggerSkill("qingyuan")
	{
		events << DamageInflicted << GameStart << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event==CardsMoveOneTime){
			if(player->getMark("qingyuanUse-Clear")>0||!player->hasTurn()) return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to->getMark("&qingyuan")>0&&move.to!=player){
				QList<const Card*>cs;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(p->getMark("&qingyuan")>0)
						cs << p->getHandcards();
				}
				if(cs.length()>0){
					player->addMark("qingyuanUse-Clear");
					room->sendCompulsoryTriggerLog(player,this);
					room->obtainCard(player,cs.at(qrand()%cs.length()),objectName(),false);
				}
			}
			return false;
		}else if(event==DamageInflicted){
			if(player->getMark("qingyuanDamage")>0) return false;
			player->addMark("qingyuanDamage");
		}
		QList<ServerPlayer*>cs;
		foreach(ServerPlayer*p, room->getOtherPlayers(player)){
			if(p->getMark("&qingyuan")<1)
				cs << p;
		}
		ServerPlayer*to = room->askForPlayerChosen(player,cs,objectName(),"qingyuan0:",false,true);
		if(to) to->gainMark("&qingyuan");
		return false;
	}
};

class ZhongshenVs : public OneCardViewAsSkill
{
public:
	ZhongshenVs() : OneCardViewAsSkill("zhongshen")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		if(Self->getMark(to_select->toString()+"zhongshen_lun")<1||!to_select->isRed())
			return false;
		Card*dc = Sanguosha->cloneCard("jink");
		dc->setSkillName(objectName());
		dc->addSubcard(to_select);
		dc->deleteLater();
		return !Self->isLocked(dc);
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*dc = Sanguosha->cloneCard("jink");
		dc->setSkillName(objectName());
		dc->addSubcard(originalCard);
		return dc;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.contains("jink")&&Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}
};

class Zhongshen : public TriggerSkill
{
public:
	Zhongshen() : TriggerSkill("zhongshen")
	{
		events << CardsMoveOneTime;
		view_as_skill = new ZhongshenVs;
		global = true;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player&&move.reason.m_skillName!="InitialHandCards"){
				foreach(int id, player->handCards()){
					if(move.card_ids.contains(id)){
						room->addPlayerMark(player,QString::number(id)+"zhongshen_lun");
						if(player->hasSkill(objectName(),true))
							room->setCardTip(id,"zhongshen_lun");
					}
				}
			}
		}
		return false;
	}
};

class QingyaVs : public OneCardViewAsSkill
{
public:
	QingyaVs() : OneCardViewAsSkill("qingya")
	{
		expand_pile = "#qingya";
	}

	bool viewFilter(const Card*to_select) const
	{
		return Self->getPile("#qingya").contains(to_select->getEffectiveId())&&to_select->isAvailable(Self);
	}

	const Card*viewAs(const Card*originalCard) const
	{
		return originalCard;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.contains("qingya");
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}
};

class Qingya : public TriggerSkill
{
public:
	Qingya() : TriggerSkill("qingya")
	{
		events << TargetSpecified << EventPhaseEnd;
		view_as_skill = new QingyaVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.to.size()==1&&player->hasSkill(objectName())){
				ServerPlayer*ap = player->getNextAlive();
				int n = 0;
				while(ap!=use.to.first()){
					ap = ap->getNextAlive();
					n++;
				}
				int x = 0;
				ap = use.to.first()->getNextAlive();
				while(ap!=player){
					ap = ap->getNextAlive();
					x++;
				}
				if(n<1||x<1) return false;
				if(player->askForSkillInvoke(this)){
					player->peiyin(this);
					QString ps = QString::number(player->getPhase()+1);
					player->addMark(ps+"qingyaUse-Clear");
					QVariantList ids = player->getTag(ps+"qingyaUse").toList();
					if(n<x||(n==x&&room->askForChoice(player,objectName(),"qingya1+qingya2")=="qingya1")){
						QList<ServerPlayer*>tos;
						ap = player->getNextAlive();
						tos << ap;
						while(ap!=player){
							ap = ap->getNextAlive();
							tos << ap;
							if(ap==use.to.first()) break;
						}
						foreach(ServerPlayer*p, tos){
							if(player->canDiscard(p,"h")){
								int id = room->askForCardChosen(player,p,"h",objectName(),false,Card::MethodDiscard);
								room->throwCard(id,objectName(),p,player);
								ids << id;
							}
						}
					}else{
						QList<ServerPlayer*>tos;
						ap = use.to.first()->getNextAlive();
						tos << use.to.first();
						while(ap!=use.to.first()){
							tos << ap;
							ap = ap->getNextAlive();
							if(ap==player) break;
						}
						foreach(ServerPlayer*p, tos){
							if(player->canDiscard(p,"h")){
								int id = room->askForCardChosen(player,p,"h",objectName(),false,Card::MethodDiscard);
								room->throwCard(id,objectName(),p,player);
								ids << id;
							}
						}
					}
					player->setTag(ps+"qingyaUse", ids);
				}
			}
		}else{
			QString ps = QString::number(player->getPhase());
			foreach(ServerPlayer*p, room->getAllPlayers()){
				QList<int> ids,ids2 = ListV2I(player->getTag(ps+"qingyaUse").toList());
				if(ids2.isEmpty()) continue;
				player->removeTag(ps+"qingyaUse");
				foreach(int id, room->getDiscardPile()){
					if(ids2.contains(id)) ids << id;
				}
				if(ids.isEmpty()) continue;
				room->notifyMoveToPile(p, ids, objectName(), Player::DiscardPile, true);
				room->askForUseCard(p,"@@qingya","qingya0:");
			}
		}
		return false;
	}
};

class Tielun : public TriggerSkill
{
public:
	Tielun() : TriggerSkill("tielun")
	{
		events << CardUsed;
		frequency = Compulsory;
		waked_skills = "#tielun_dist";
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				room->addPlayerMark(player,"&tielun_lun");
			}
		}
		return false;
	}
};

class TielunDist : public DistanceSkill
{
public:
	TielunDist() : DistanceSkill("#tielun_dist")
	{
	}

	int getCorrect(const Player*from, const Player*) const
	{
		if(from->hasSkill("tielun",true))
			return -from->getMark("&tielun_lun");
		return 0;
	}
};

olYichengCard::olYichengCard()
{
	target_fixed = true;
}

void olYichengCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	QList<int>ids = room->showDrawPile(source,source->getMaxHp(),"olyicheng");
	room->notifyMoveToPile(source,ids,"olyicheng",Player::PlaceTable,true);
	const Card*dc = room->askForUseCard(source,"@@olyicheng","olyicheng0:");
	room->notifyMoveToPile(source,ids,"olyicheng",Player::PlaceTable,false);
	if(dc&&source->isAlive()){
		QList<CardsMoveStruct> moves;
		CardMoveReason reason(CardMoveReason::S_REASON_OVERRIDE, source->objectName());
		reason.m_skillName = "olyicheng";
		QList<int>ids2 = ids;
		foreach(int id, dc->getSubcards()){
			if(ids.contains(id)){
				ids2.removeOne(id);
				CardsMoveStruct move(id, source, Player::PlaceHand, reason);
				moves << move;
			}else{
				ids2 << id;
				CardsMoveStruct move(id, nullptr, Player::PlaceTable, reason);
				moves << move;
			}
		}
		room->moveCardsAtomic(moves, true);
		int n = 0;
		foreach(int id, ids){
			n += Sanguosha->getCard(id)->getNumber();
		}
		foreach(int id, ids2){
			n -= Sanguosha->getCard(id)->getNumber();
		}
		if(n>0&&source->isAlive()&&source->askForSkillInvoke("olyicheng","olyicheng1",false)){
			moves.clear();
			CardsMoveStruct move1(source->handCards(), nullptr, Player::DrawPile, reason);
			moves << move1;
			CardsMoveStruct move2(ids2, source, Player::PlaceHand, reason);
			moves << move2;
			room->moveCardsAtomic(moves, false, true);
		}else{
			room->returnToTopDrawPile(ids2);
		}
	}
	foreach(int id, ids){
		if(room->getCardPlace(id)!=Player::PlaceTable)
			ids.removeOne(id);
	}
	room->throwCard(ids,"",nullptr);
}

olYicheng2Card::olYicheng2Card()
{
	target_fixed = true;
}

void olYicheng2Card::onUse(Room*, CardUseStruct &) const
{
}

class olYicheng : public ViewAsSkill
{
public:
	olYicheng() : ViewAsSkill("olyicheng")
	{
		expand_pile = "#olyicheng";
	}

	bool viewFilter(const QList<const Card*> &selects, const Card*to_select) const
	{
		if(to_select->isEquipped()) return false;
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.isEmpty()) return false;
		return selects.length()<Self->getPile("#olyicheng").length()*2;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("olYichengCard")<1;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.contains("olyicheng");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			return new olYichengCard;
		}else{
			int n = 0, x = 0;
			foreach(const Card*c, cards){
				if(Self->getPile("#olyicheng").contains(c->getEffectiveId()))
					n++;
				else
					x++;
			}
			if(cards.isEmpty()||n!=x) return nullptr;
			olYicheng2Card*card = new olYicheng2Card;
			card->addSubcards(cards);
			return card;
		}
	}
};

ChanshuangCard::ChanshuangCard()
{
}

bool ChanshuangCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty()&&Self!=to_select;
}

void ChanshuangCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	targets.prepend(source);
	QHash<ServerPlayer*,QString> hash;
	foreach(ServerPlayer*p, targets){
		QStringList choices;
		foreach(const Card*c, p->getHandcards()+p->getEquips()){
			if(!p->isCardLimited(c,Card::MethodRecast)){
				choices << "chanshuang1";
				break;
			}
		}
		foreach(ServerPlayer*p2, room->getAlivePlayers()){
			if(p->canSlash(p2)){
				choices << "chanshuang2";
				break;
			}
		}
		if(p->canDiscard(p,"he"))
			choices << "chanshuang3";
		if(choices.isEmpty()) continue;
		hash[p] = room->askForChoice(p,"chanshuang",choices.join("+"));
	}
	foreach(ServerPlayer*p, targets){
		if(hash[p]=="chanshuang1"){
			const Card*dc = room->askForCard(p,"..!","chanshuang1",QVariant(),Card::MethodRecast);
			if(dc){
				p->broadcastSkillInvoke("@recast");
				CardMoveReason reason(CardMoveReason::S_REASON_RECAST, p->objectName(), "chanshuang", "");
				room->moveCardTo(dc,nullptr,Player::DiscardPile,reason);
				p->drawCards(1,"recast");
			}
		}
		if(hash[p]=="chanshuang2"){
			room->askForUseCard(p,"Slash!","chanshuang2",-1,Card::MethodUse,false);
		}
		if(hash[p]=="chanshuang3"){
			room->askForDiscard(p,"chanshuang",2,2,false,true);
		}
	}
}

class ChanshuangVs : public ZeroCardViewAsSkill
{
public:
	ChanshuangVs() : ZeroCardViewAsSkill("chanshuang")
	{
	}

	const Card*viewAs() const
	{
		return new ChanshuangCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("ChanshuangCard");
	}
};

class Chanshuang : public TriggerSkill
{
public:
	Chanshuang() : TriggerSkill("chanshuang")
	{
		events << CardUsed << CardsMoveOneTime << EventPhaseStart;
		view_as_skill = new ChanshuangVs;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash"))
				room->setPlayerMark(player,"chanshuangSlash-Clear",1);
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player){
				if(move.reason.m_reason==CardMoveReason::S_REASON_RECAST)
					room->setPlayerMark(player,"chanshuangRECAST-Clear",1);
				else if((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD&&move.card_ids.length()==2)
					room->setPlayerMark(player,"chanshuangDISCARD-Clear",1);
			}
		}else if(player->getPhase()==Player::Finish){
			int n = player->getMark("chanshuangSlash-Clear")+player->getMark("chanshuangRECAST-Clear")+player->getMark("chanshuangDISCARD-Clear");
			if(n<1) return false;
			room->sendCompulsoryTriggerLog(player,this);
			QStringList choices;
			choices << "chanshuang1" << "chanshuang2" << "chanshuang3";
			for (int i = 0; i < n; i++){
				if(choices.first()=="chanshuang1"){
					const Card*dc = room->askForCard(player,"..!","chanshuang1",QVariant(),Card::MethodRecast);
					if(dc){
						player->broadcastSkillInvoke("@recast");
						CardMoveReason reason(CardMoveReason::S_REASON_RECAST, player->objectName(), objectName(), "");
						room->moveCardTo(dc,nullptr,Player::DiscardPile,reason);
						player->drawCards(1,"recast");
					}
				}
				if(choices.first()=="chanshuang2"){
					room->askForUseCard(player,"Slash!","chanshuang2",-1,Card::MethodUse,false);
				}
				if(choices.first()=="chanshuang3"){
					room->askForDiscard(player,objectName(),2,2,false,true);
				}
				choices.removeAt(0);
			}
		}
		return false;
	}
};

class Zhanjin : public ViewAsEquipSkill
{
public:
	Zhanjin() : ViewAsEquipSkill("zhanjin")
	{
	}

	QString viewAsEquip(const Player*target) const
	{
		if (target->hasEquipArea(0) && !target->getWeapon())
			return "axe";
		return "";
	}
};

XuanzhuCard::XuanzhuCard()
{
	handling_method = MethodNone;
}

bool XuanzhuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool XuanzhuCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool XuanzhuCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*XuanzhuCard::validateInResponse(ServerPlayer*user) const
{
	user->addToPile("xuanzhu",this);
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	card->setSkillName("xuanzhu");
	card->deleteLater();
	return card;
}

const Card*XuanzhuCard::validate(CardUseStruct &use) const
{
	use.from->addToPile("xuanzhu",this);
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	card->setSkillName("xuanzhu");
	card->deleteLater();
	return card;
}

Xuanzhu2Card::Xuanzhu2Card()
{
	target_fixed = true;
}

void Xuanzhu2Card::onUse(Room*room, CardUseStruct &use) const
{
	QList<int> ids;
	if(use.from->getChangeSkillState("xuanzhu")==1)
		ids = room->getAvailableCardList(use.from,"basic","xuanzhu");
	else{
		foreach(int id, room->getAvailableCardList(use.from,"trick","xuanzhu")){
			if(Sanguosha->getCard(id)->isKindOf("SingleTargetTrick"))
				ids << id;
		}
	}
	if(ids.isEmpty()) return;
	room->fillAG(ids,use.from);
	int id = room->askForAG(use.from,ids,true,"xuanzhu");
	room->clearAG(use.from);
	if(id>-1){
		room->setPlayerMark(use.from,"xuanzhuId",id);
		room->askForUseCard(use.from,"@@xuanzhu","xuanzhu0:"+Sanguosha->getEngineCard(id)->objectName());
	}
}

class XuanzhuVs : public ViewAsSkill
{
public:
	XuanzhuVs() : ViewAsSkill("xuanzhu")
	{
	}

	bool viewFilter(const QList<const Card*> &selects, const Card*) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.isEmpty()) return false;
		return selects.length()<1;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("xuanzhuUse-Clear")<1&&player->getCardCount()>0;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(player->getMark("xuanzhuUse-Clear")>0||player->getCardCount()<1||!player->hasTurn()
		||Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE) return false;
		int n = player->getChangeSkillState("xuanzhu");
		foreach(QString pc, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc){
				dc->setSkillName("xuanzhu");
				dc->deleteLater();
				if(n==1&&dc->isKindOf("BasicCard"))
					return true;
				else if(n==2&&dc->isKindOf("TrickCard")&&dc->isSingleTargetCard())
					return true;
			}
		}
		return pattern.contains("xuanzhu");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			return new Xuanzhu2Card;
		}else{
			if(cards.isEmpty()) return nullptr;
			if(pattern.contains("xuanzhu"))
				pattern = Sanguosha->getEngineCard(Self->getMark("xuanzhuId"))->objectName();
			XuanzhuCard*card = new XuanzhuCard;
			card->setUserString(pattern);
			card->addSubcards(cards);
			return card;
		}
	}
};

class Xuanzhu : public TriggerSkill
{
public:
	Xuanzhu() : TriggerSkill("xuanzhu")
	{
		events << CardFinished << PreCardUsed;
		view_as_skill = new XuanzhuVs;
		change_skill = true;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->getSkillNames().contains(objectName())){
				QList<int> ids = player->getPile("xuanzhu");
				if(Sanguosha->getCard(ids.last())->getTypeId()!=3){
					room->askForDiscard(player,objectName(),1,1,false,true);
				}else{
					room->throwCard(ids,objectName(),nullptr);
					player->drawCards(ids.length(),objectName());
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->getSkillNames().contains(objectName())){
				room->setPlayerMark(player,"xuanzhuUse-Clear",1);
				int n = player->getChangeSkillState(objectName());
				if(n==1) n++;
				else n--;
				room->setChangeSkillState(player, objectName(), n);
			}
		}
		return false;
	}
};

class Jiane : public TriggerSkill
{
public:
	Jiane() : TriggerSkill("jiane")
	{
		frequency = Compulsory;
		events << CardOnEffect << CardOffset << PostCardEffected << CardEffected << TrickCardCanceling;
		waked_skills = "#zhidao_pro";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardOnEffect){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->getTypeId()>0&&effect.from&&effect.from!=player&&effect.from->hasSkill(objectName())){
				effect.card->setFlags("JianeOnEffect");
			}
		}else if(event==CardOffset){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->getTypeId()>0&&player->hasSkill(objectName())){
				room->addPlayerMark(player,"Jiane-Clear");
			}
		}else if(event==PostCardEffected){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->hasFlag("JianeOnEffect")){
				room->addPlayerMark(player,"JianeNotOffset-Clear");
			}
		}else if(event==CardEffected){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->getTypeId()>0&&player->getMark("JianeNotOffset-Clear")>0){
				effect.no_offset = true;
				data.setValue(effect);
			}
		}else if(event==TrickCardCanceling){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->getTypeId()>0&&player->getMark("JianeNotOffset-Clear")>0){
				effect.no_offset = true;
				data.setValue(effect);
			}
		}
		return false;
	}
};

class JianePro : public ProhibitSkill
{
public:
	JianePro() : ProhibitSkill("#zhidao_pro")
	{
	}

	bool isProhibited(const Player*, const Player*to, const Card*card, const QList<const Player*> &) const
	{
		return !card->isKindOf("SkillCard")&&to->getMark("Jiane-Clear")>0;
	}
};

QushiCard::QushiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool QushiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty()&&Self!=to_select;
}

void QushiCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	source->drawCards(1,"qushi");
	foreach(ServerPlayer*p, targets){
		if(p->isAlive()&&!source->isKongcheng()){
			const Card*dc = room->askForExchange(source,"qushi",1,1);
			p->addToPile("qu_shi",dc,false);
		}
	}
}

class QushiVs : public ZeroCardViewAsSkill
{
public:
	QushiVs() : ZeroCardViewAsSkill("qushi")
	{
	}

	const Card*viewAs() const
	{
		return new QushiCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("QushiCard");
	}
};

class Qushi : public TriggerSkill
{
public:
	Qushi() : TriggerSkill("qushi")
	{
		events << CardFinished << EventPhaseStart;
		view_as_skill = new QushiVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->getPile("qu_shi").length()>0;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach(int id, player->getPile("qu_shi")){
					const Card*dc = Sanguosha->getCard(id);
					if(dc->getType()!=use.card->getType()) continue;
					foreach(ServerPlayer*p, use.to){
						player->addMark(dc->getType()+p->objectName()+"qushi-Clear");
					}
				}
			}
		}else if(player->getPhase()==Player::Finish){
			foreach(int id, player->getPile("qu_shi")){
				int n = 0;
				foreach(ServerPlayer*p, room->getPlayers()){
					if(player->getMark(Sanguosha->getCard(id)->getType()+p->objectName()+"qushi-Clear")>0)
						n++;
				}
				room->throwCard(id,objectName(),nullptr);
				n = qMin(5,n);
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(p->hasSkill(objectName()))
						p->drawCards(n,objectName());
				}
			}
		}
		return false;
	}
};

WeijieCard::WeijieCard()
{
}

bool WeijieCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool WeijieCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool WeijieCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*WeijieCard::validateInResponse(ServerPlayer*user) const
{
	Room*room = user->getRoom();
	QStringList choices;
	foreach(int id, Sanguosha->getRandomCards()){
		const Card*c = Sanguosha->getCard(id);
		if(c->isKindOf("BasicCard")&&!choices.contains(c->objectName()))
			choices << c->objectName();
	}
	QString cho = room->askForChoice(user,"weijie",choices.join("+"));
	room->addPlayerMark(user,"weijieUse-Clear");
	QList<ServerPlayer*>tos;
	foreach(ServerPlayer*p, room->getAlivePlayers()){
		if(user->distanceTo(p)==1&&user->canDiscard(p,"h"))
			tos << p;
	}
	ServerPlayer*to = room->askForPlayerChosen(user,tos,"weijie","weijie0:",false,true);
	LogMessage log;
	log.type = "$weijie0";
	log.from = user;
	log.arg = cho;
	room->sendLog(log);
	user->peiyin("weijie");
	if(to){
		int id = room->askForCardChosen(user,to,"h","weijie",false,Card::MethodDiscard);
		if(id>-1){
			room->throwCard(id,"weijie",to,user);
			const Card*c = Sanguosha->getCard(id);
			if(c->objectName()==cho||(c->objectName().contains("slash")&&cho=="slash")){
				cho = room->askForChoice(user,"weijie",user_string);
				Card*card = Sanguosha->cloneCard(cho);
				card->setSkillName("_weijie");
				card->deleteLater();
				return card;
			}
		}
	}
	return nullptr;
}

const Card*WeijieCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	QStringList choices;
	foreach(int id, Sanguosha->getRandomCards()){
		const Card*c = Sanguosha->getCard(id);
		if(c->isKindOf("BasicCard")&&!choices.contains(c->objectName()))
			choices << c->objectName();
	}
	QString cho = room->askForChoice(use.from,"weijie",choices.join("+"));
	room->addPlayerMark(use.from,"weijieUse-Clear");
	QList<ServerPlayer*>tos;
	foreach(ServerPlayer*p, room->getAlivePlayers()){
		if(use.from->distanceTo(p)==1&&use.from->canDiscard(p,"h"))
			tos << p;
	}
	ServerPlayer*to = room->askForPlayerChosen(use.from,tos,"weijie","weijie0:",false,true);
	LogMessage log;
	log.type = "$weijie0";
	log.from = use.from;
	log.arg = cho;
	room->sendLog(log);
	use.from->peiyin("weijie");
	if(to){
		int id = room->askForCardChosen(use.from,to,"h","weijie",false,Card::MethodDiscard);
		if(id>-1){
			room->throwCard(id,"weijie",to,use.from);
			const Card*c = Sanguosha->getCard(id);
			if(c->objectName()==cho||(c->objectName().contains("slash")&&cho=="slash")){
				cho = room->askForChoice(use.from,"weijie",user_string);
				Card*card = Sanguosha->cloneCard(cho);
				card->setSkillName("_weijie");
				card->deleteLater();
				return card;
			}
		}
	}
	return nullptr;
}

class Weijie : public ZeroCardViewAsSkill
{
public:
	Weijie() : ZeroCardViewAsSkill("weijie")
	{
        response_or_use = true;
	}

	const Card*viewAs() const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		WeijieCard*card = new WeijieCard;
		card->setUserString(pattern);
		return card;
	}
	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(player->getMark("weijieUse-Clear")>0) return false;
		bool can = false;
		foreach(const Player*p, player->getAliveSiblings()){
			if(p->hasFlag("CurrentPlayer"))
				can = true;
		}
		if(!can) return false;
		foreach(QString pc, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc){
				dc->deleteLater();
				if(dc->isKindOf("BasicCard"))
					return true;
			}
		}
		return pattern.contains("weijie");
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		if(player->getMark("weijieUse-Clear")>0) return false;
		foreach(const Player*p, player->getAliveSiblings()){
			if(p->hasFlag("CurrentPlayer"))
				return true;
		}
		return false;
	}
};

class MiuyanVs : public OneCardViewAsSkill
{
public:
	MiuyanVs() : OneCardViewAsSkill("miuyan")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		Card*dc = Sanguosha->cloneCard("fire_attack");
		dc->setSkillName("miuyan");
		dc->addSubcard(to_select);
		dc->deleteLater();
		return to_select->isBlack()
			&&dc->isAvailable(Self);
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*dc = Sanguosha->cloneCard("fire_attack");
		dc->setSkillName("miuyan");
		dc->addSubcard(originalCard);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0
			&&player->getMark("banmiuyan_lun")<1;
	}
};

class Miuyan : public TriggerSkill
{
public:
	Miuyan() : TriggerSkill("miuyan")
	{
		events << CardFinished << ShowCards;
		view_as_skill = new MiuyanVs;
		change_skill = true;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())&&player->hasSkill(objectName(),true)){
				int n = player->getChangeSkillState("miuyan");
				if(n==1){
					if(use.card->hasFlag("DamageDone")){
						room->setChangeSkillState(player, "miuyan", 2);
						DummyCard*dc = new DummyCard();
						foreach(ServerPlayer*p, room->getOtherPlayers(player)){
							dc->clearSubcards();
							foreach(const Card*c, p->getCards("he")){
								if(p->getMark(c->toString()+"miuyanShow-"+QString::number(player->getPhase())+"Clear")>0
								||p->getMark(c->toString()+player->objectName()+"miuyanShow-"+QString::number(player->getPhase())+"Clear")>0)
								dc->addSubcard(c);
							}
							player->obtainCard(dc);
						}
						dc->deleteLater();
					}
				}else{
					room->setChangeSkillState(player, "miuyan", 1);
					room->addPlayerMark(player, "banmiuyan_lun");
				}
			}
		}else{
			QStringList show = data.toString().split(":");
			foreach(QString id, show.first().split("+")){
				if(show.length()>1)
					player->addMark(id+show.last()+"miuyanShow-"+QString::number(player->getPhase())+"Clear");
				else
					player->addMark(id+"miuyanShow-"+QString::number(player->getPhase())+"Clear");
			}
		}
		return false;
	}
};

class Shilu : public TriggerSkill
{
public:
	Shilu() : TriggerSkill("shilu")
	{
		events << Damaged;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		room->sendCompulsoryTriggerLog(player,this);
		player->drawCards(player->getHp(),objectName());
		QList<ServerPlayer*>tos;
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if(player->inMyAttackRange(p)&&!p->isKongcheng())
				tos << p;
		}
		ServerPlayer*to = room->askForPlayerChosen(player,tos,objectName());
		if(to){
			room->doAnimate(1,player->objectName(),to->objectName());
			int id = room->askForCardChosen(player,to,"h",objectName());
			room->showCard(to,id);
			WrappedCard*w_card = Sanguosha->getWrappedCard(id);
			Card*clone = Sanguosha->cloneCard("slash",w_card->getSuit(),w_card->getNumber());
			clone->setSkillName("shilu");
			w_card->takeOver(clone);
			room->broadcastUpdateCard(room->getPlayers(),id,w_card);
		}
		return false;
	}
};

ChenglieCard::ChenglieCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool ChenglieCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	int n = 0;
	foreach(const Player*p, Self->getAliveSiblings()){
		if(p->hasFlag("chenglieTo")) n++;
	}
	return targets.length()<n&&to_select->hasFlag("chenglieTo");
}

bool ChenglieCard::targetFixed() const
{
	QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
	return pattern.contains("1");
}

bool ChenglieCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	int n = 0;
	foreach(const Player*p, Self->getAliveSiblings()){
		if(p->hasFlag("chenglieTo")) n++;
	}
	return targets.length()==n;
}

void ChenglieCard::onUse(Room*, CardUseStruct &use) const
{
	int n = 0;
	foreach(ServerPlayer*tp, use.to){
		tp->addToPile("chenglie",subcards[n],false);
		n++;
	}
}

class ChenglieVs : public ViewAsSkill
{
public:
	ChenglieVs() : ViewAsSkill("chenglie")
	{
		expand_pile = "#chenglie";
	}

	bool viewFilter(const QList<const Card*> &selects, const Card*card) const
	{
		if(card->isEquipped()) return false;
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.contains("1")){
			if(selects.length()>1) return false;
			return selects.isEmpty()
			||Self->getPile("#chenglie").contains(card->getEffectiveId())!=Self->getPile("#chenglie").contains(selects[0]->getEffectiveId());
		}
		int n = 0;
		foreach(const Player*p, Self->getAliveSiblings()){
			if(p->hasFlag("chenglieTo")) n++;
		}
		return selects.length()<n&&Self->getPileName(card->getEffectiveId())=="#chenglie";
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.contains("chenglie");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.contains("1")){
			if(cards.length()<2) return nullptr;
		}else{
			int n = 0;
			foreach(const Player*p, Self->getAliveSiblings()){
				if(p->hasFlag("chenglieTo")) n++;
			}
			if(cards.length()<n) return nullptr;
		}
		ChenglieCard*card = new ChenglieCard;
		card->addSubcards(cards);
		return card;
	}
};

class Chenglie : public TriggerSkill
{
public:
	Chenglie() : TriggerSkill("chenglie")
	{
		events << CardFinished << CardUsed << CardOnEffect;
		view_as_skill = new ChenglieVs;
		waked_skills = "#chenglie_mod";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("chenglieUse")){
				room->setCardFlag(use.card,"-chenglieUse");
				foreach(ServerPlayer*tp, use.to){
					room->throwCard(tp->getPile("chenglie"),"chenglie",nullptr);
					tp->setFlags("-chenglieRespond");
				}
			}
		}else if(event==CardOnEffect){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->hasFlag("chenglieUse")&&!effect.to->hasFlag("chenglieRespond")){
				QList<int> ids = effect.to->getPile("chenglie");
				if(ids.length()>0&&Sanguosha->getCard(ids.first())->isRed()){
					room->recover(effect.to,RecoverStruct(objectName()));
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.whocard&&use.whocard->hasFlag("chenglieUse")&&use.who&&use.who->isAlive()){
				player->setFlags("chenglieRespond");
				QList<int> ids = player->getPile("chenglie");
				if(ids.length()>0&&Sanguosha->getCard(ids.first())->isRed()){
					const Card*dc = room->askForExchange(player,objectName(),1,1,true,"chenglie0:"+use.who->objectName());
					if(dc){
						room->giveCard(player,use.who,dc,objectName());
					}
				}
			}
			if(!use.card->isKindOf("Slash")||use.to.length()<2||!player->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(player,this);
			QList<int> ids = room->showDrawPile(player,use.to.length(),objectName(),false);
			room->notifyMoveToPile(player,ids,"chenglie",Player::DrawPile);
			const Card*dc = room->askForUseCard(player,"@@chenglie1","chenglie1:");
			if(dc){
				QList<CardsMoveStruct> moves;
				CardMoveReason reason(CardMoveReason::S_REASON_OVERRIDE, player->objectName(),objectName(),"");
				foreach(int id, dc->getSubcards()){
					if(ids.contains(id)){
						CardsMoveStruct move2(id, player, Player::PlaceHand, reason);
						moves << move2;
						ids.removeOne(id);
					}else{
						CardsMoveStruct move1(id, nullptr, Player::DrawPile, reason);
						moves << move1;
						ids << id;
					}
				}
				room->moveCardsAtomic(moves, false);
			}
			foreach(ServerPlayer*tp, use.to){
				room->setPlayerFlag(tp,"chenglieTo");
			}
			room->notifyMoveToPile(player,ids,"chenglie",Player::DrawPile);
			room->askForUseCard(player,"@@chenglie2","chenglie2:");
			foreach(ServerPlayer*tp, use.to){
				room->setPlayerFlag(tp,"-chenglieTo");
			}
			room->setCardFlag(use.card,"chenglieUse");
		}
		return false;
	}
};

class ChenglieMod : public TargetModSkill
{
public:
	ChenglieMod() : TargetModSkill("#chenglie_mod")
	{
	}

	int getExtraTargetNum(const Player*from, const Card*) const
	{
		if (from->hasSkill("chenglie"))
			return 2;
		return 0;
	}
};

class Jieyan : public TriggerSkill
{
public:
	Jieyan() : TriggerSkill("jieyan")
	{
		events << CardsMoveOneTime << EventPhaseStart << EventPhaseEnd << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from==player&&player->getPhase()==Player::Discard&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
				QVariantList ids = player->getTag("jieyanIds").toList();
				foreach(int id, move.card_ids){
					ids << id;
				}
				player->setTag("jieyanIds", ids);
			}
		}else if(triggerEvent==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to==Player::Discard&&player->getMark("&jieyan1")>0){
				room->setPlayerMark(player,"&jieyan1",0);
				player->skip(Player::Discard);
			}
		}else if(triggerEvent == EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->hasSkill(objectName())){
				ServerPlayer*to = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"jieyan0:",true,true);
				if(to){
					room->damage(DamageStruct(objectName(),player,to));
					if(to->isDead()) return false;
					if(to->getLostHp()>0&&room->askForChoice(to,objectName(),"jieyan1+jieyan2")=="jieyan2"){
						room->recover(to,RecoverStruct(objectName(),player));
						room->setPlayerMark(to,"&jieyan2",1);
						room->setPlayerMark(to,"jieyan2"+player->objectName(),1);
					}else{
						room->setPlayerMark(to,"&jieyan1",1);
					}
				}
			}
		}else if(triggerEvent == EventPhaseEnd){
			QList<int>ids,ids2 = ListV2I(player->getTag("jieyanIds").toList());
			player->removeTag("jieyanIds");
			if(player->getPhase()==Player::Discard&&player->getMark("&jieyan2")>0){
				room->setPlayerMark(player,"&jieyan2",0);
				foreach(int id, room->getDiscardPile()){
					if(ids2.contains(id))
						ids << id;
				}
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(player->getMark("&jieyan2"+p->objectName())>0){
						room->setPlayerMark(player,"jieyan2"+p->objectName(),0);
						room->fillAG(ids,p);
						ServerPlayer*to = room->askForPlayerChosen(p,room->getOtherPlayers(player),objectName(),"jieyan3:",true);
						room->clearAG(p);
						if(to){
							room->doAnimate(1,p->objectName(),to->objectName());
							room->giveCard(p,to,ids,objectName());
						}
						break;
					}
				}
			}
		}
		return false;
	}
};

class Jinghua : public TriggerSkill
{
public:
	Jinghua() : TriggerSkill("jinghua")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from==player){
				if(move.is_last_handcard&&player->getMark("jinghuaUse")<1&&player->askForSkillInvoke(this,objectName())){
					player->peiyin(this);
					player->addMark("jinghuaUse");
					room->changeTranslation(player,objectName());
				}
				if(move.to&&move.to!=player&&(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))){
					room->sendCompulsoryTriggerLog(player,this);
					ServerPlayer*to = (ServerPlayer*)move.to;
					if(player->getMark("jinghuaUse")<1){
						room->recover(to,RecoverStruct(objectName(),player));
					}else{
						room->loseHp(to,1,true,player,objectName());
					}
				}
			}
		}
		return false;
	}
};

class Shuiyue : public TriggerSkill
{
public:
	Shuiyue() : TriggerSkill("shuiyue")
	{
		events << Damaged << EnterDying;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (triggerEvent == Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from&&damage.from!=player&&TriggerSkill::triggerable(damage.from)){
				room->sendCompulsoryTriggerLog(damage.from,this);
				if(damage.from->getMark("shuiyueUse")<1){
					player->drawCards(1,objectName());
				}else{
					room->askForDiscard(player,objectName(),1,1,false);
				}
			}
		}else{
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.damage&&dying.damage->from&&dying.damage->from!=player&&dying.damage->from->getMark("shuiyueUse")<1
				&&TriggerSkill::triggerable(dying.damage->from)&&dying.damage->from->askForSkillInvoke(this,objectName())){
				dying.damage->from->peiyin(this);
				dying.damage->from->addMark("shuiyueUse");
				room->changeTranslation(dying.damage->from,objectName());
			}
		}
		return false;
	}
};

PingduanCard::PingduanCard()
{
}

bool PingduanCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
	return targets.isEmpty();
}

void PingduanCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		const Card*dc = room->askForUseCard(p,"BasicCard","pingduan1:");
		if(dc) p->drawCards(1,getSkillName());
		dc = room->askForCard(p,"TrickCard","pingduan2:",QVariant(),Card::MethodRecast);
		if(dc){
			p->broadcastSkillInvoke("@recast");
			CardMoveReason reason(CardMoveReason::S_REASON_RECAST, p->objectName(), getSkillName(), "");
			room->moveCardTo(dc,nullptr,Player::DiscardPile,reason);
			p->drawCards(1,"recast");
			p->drawCards(1,getSkillName());
		}
		if(source->isAlive()&&p->hasEquip()&&p->askForSkillInvoke(getSkillName(),"pingduan3:"+source->objectName(),false)){
			int id = room->askForCardChosen(source,p,"e",getSkillName());
			if(id>-1){
				room->obtainCard(source,id);
				p->drawCards(1,getSkillName());
			}
		}
	}
}

class Pingduan : public ZeroCardViewAsSkill
{
public:
	Pingduan() : ZeroCardViewAsSkill("pingduan")
	{
	}

	const Card*viewAs() const
	{
		return new PingduanCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("PingduanCard");
	}
};

class LeiluanVs : public ViewAsSkill
{
public:
	LeiluanVs() : ViewAsSkill("leiluan")
	{
	}

	bool viewFilter(const QList<const Card*> &selects, const Card*card) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.contains("leiluan")||qMax(1,Self->getMark("&leiluan"))<=selects.length()) return false;
		if(pattern.isEmpty()){
			const Card*dc = Self->getTag("leiluan").value<const Card*>();
			if(!dc) return false;
			pattern = dc->objectName();
		}
		foreach(QString pc, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc){
				dc->addSubcards(selects);
				dc->addSubcard(card);
				dc->deleteLater();
				if(dc->isKindOf("BasicCard")&&Self->getMark("leiluan_guhuo_remove_"+pc+"_lun")<1&&!Self->isLocked(dc))
					return true;
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(pattern.contains("leiluan")) return true;
		if(Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE
		||player->getMark("leiluanUse-Clear")>0||!player->hasTurn()) return false;
		foreach(QString pc, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc){
				dc->deleteLater();
				if(dc->isKindOf("BasicCard")&&player->getMark("leiluan_guhuo_remove_"+pc+"_lun")<1)
					return true;
			}
		}
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.contains("leiluan")){
			pattern = Self->property("leiluanUse").toString();
			Card*dc = Sanguosha->cloneCard(pattern);
			if(dc){
				dc->setSkillName(objectName());
				return dc;
			}
		}
		if(cards.length()<qMax(1,Self->getMark("&leiluan"))) return nullptr;
		if(pattern.isEmpty()){
			const Card*dc = Self->getTag("leiluan").value<const Card*>();
			if(!dc) return nullptr;
			pattern = dc->objectName();
		}
		foreach(QString pc, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc){
				dc->setSkillName(objectName());
				dc->addSubcards(cards);
				if(dc->isKindOf("BasicCard")&&Self->getMark("leiluan_guhuo_remove_"+pc+"_lun")<1&&!Self->isLocked(dc))
					return dc;
				dc->deleteLater();
			}
		}
		return nullptr;
	}
};

class Leiluan : public TriggerSkill
{
public:
	Leiluan() : TriggerSkill("leiluan")
	{
		events << RoundEnd << PreCardUsed << CardsMoveOneTime;
		view_as_skill = new LeiluanVs;
	}
	QDialog*getDialog() const
	{
		return GuhuoDialog::getInstance(objectName(), true, false);
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==RoundEnd){
			if(player->getMark("leiluanBasic_lun")>=player->getMark("&leiluan")&&player->hasSkill(objectName())){
				QStringList ps;
				foreach(QString pc, room->getTag("leiluanNDTrick").toStringList()){
					Card*dc = Sanguosha->cloneCard(pc);
					if(dc){
						dc->setSkillName(objectName());
						dc->deleteLater();
						if(!ps.contains(pc)&&dc->isAvailable(player))
							ps << pc;
					}
				}
				if(player->getCardCount()>=player->getMark("&leiluan")&&ps.length()>0){
					QString ch = room->askForChoice(player,objectName(),ps.join("+"));
					room->setPlayerProperty(player,"leiluanUse",ch);
					room->askForUseCard(player,"@@leiluan","leiluan0:"+ch);
				}
			}
			if(player->getMark("leiluanUse_lun")<1&&player->hasSkill(objectName(),true)){
				room->setPlayerMark(player,"&leiluan",1);
			}
			if(player==room->getAllPlayers().last())
				room->removeTag("leiluanNDTrick");
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				QStringList ps = room->getTag("leiluanNDTrick").toStringList();
				foreach(int id, move.card_ids){
					if(Sanguosha->getCard(id)->isNDTrick())
						ps << Sanguosha->getCard(id)->objectName();
				}
				room->setTag("leiluanNDTrick",ps);
			}
		}else if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("BasicCard")){
				room->addPlayerMark(player,"leiluanBasic_lun");
				room->addPlayerMark(player,"leiluan_guhuo_remove_"+use.card->objectName()+"_lun");
			}
			if(use.card->getSkillNames().contains(objectName())){
				room->addPlayerMark(player,"leiluanUse-Clear");
				if(use.card->isKindOf("TrickCard")){
					int n = qMax(1,player->getMark("&leiluan"));
					player->drawCards(n,objectName());
				}
				room->addPlayerMark(player,"leiluanUse_lun");
				if(player->getMark("leiluanUse_lun")==1){
					room->addPlayerMark(player,"&leiluan");
				}
			}
		}
		return false;
	}
};

class Fuchao : public TriggerSkill
{
public:
	Fuchao() : TriggerSkill("fuchao")
	{
		events << CardFinished << CardUsed << CardResponded;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardEffected){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->hasFlag("fuchaoUse1"))
				effect.no_offset = true;
			if(effect.card->hasFlag("fuchaoUse2")){
				if(effect.card->hasFlag("fuchaoUse"+player->objectName()))
					effect.extra_effect++;
				else
					effect.nullified = true;
			}
			data.setValue(effect);
		}else if(event==CardResponded){
			CardResponseStruct resp = data.value<CardResponseStruct>();
			if(resp.m_toCard&&resp.m_who&&resp.m_who!=player&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				QString choice = "fuchao2";
				if(player->canDiscard(player,"he")||player->canDiscard(resp.m_who,"he"))
					choice = "fuchao1+fuchao2";
				room->setCardFlag(resp.m_toCard,"fuchaoUse"+player->objectName());
				if(room->askForChoice(player,objectName(),choice,data)=="fuchao2"){
					room->setCardFlag(resp.m_toCard,"fuchaoUse2");
				}else{
					room->setCardFlag(resp.m_toCard,"fuchaoUse1");
					if(player->canDiscard(player,"he"))
						room->askForDiscard(player,objectName(),1,1,false,true);
					if(player->canDiscard(resp.m_who,"he")){
						int id = room->askForCardChosen(player,resp.m_who,"he",objectName(),false,Card::MethodDiscard);
						if(id>-1) room->throwCard(id,objectName(),resp.m_who,player);
					}
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.whocard&&use.who&&use.who!=player&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				QString choice = "fuchao2";
				if(player->canDiscard(player,"he")||player->canDiscard(use.who,"he"))
					choice = "fuchao1+fuchao2";
				room->setCardFlag(use.whocard,"fuchaoUse"+player->objectName());
				if(room->askForChoice(player,objectName(),choice,data)=="fuchao2"){
					room->setCardFlag(use.whocard,"fuchaoUse2");
				}else{
					room->setCardFlag(use.whocard,"fuchaoUse1");
					if(player->canDiscard(player,"he"))
						room->askForDiscard(player,objectName(),1,1,false,true);
					if(player->canDiscard(use.who,"he")){
						int id = room->askForCardChosen(player,use.who,"he",objectName(),false,Card::MethodDiscard);
						if(id>-1) room->throwCard(id,objectName(),use.who,player);
					}
				}
			}
		}
		return false;
	}
};

class Jinming : public TriggerSkill
{
public:
	Jinming() : TriggerSkill("jinming")
	{
		events << EventPhaseChanging << CardUsed << CardsMoveOneTime << Damage << HpRecover;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::Start){
				QStringList choices;
				for (int i = 1; i < 5; i++){
					QString ch = QString("jinming%1").arg(i);
					if(player->getMark(ch)<1) choices << ch;
				}
				if(choices.length()>0&&player->askForSkillInvoke(this)){
					player->peiyin(this);
					QString ch = room->askForChoice(player,objectName(),choices.join("+"));
					player->setMark("jinmingNum",ch.split("g").last().toInt());
					room->setPlayerMark(player,"&"+ch+"-Clear",1);
				}
			}else if(change.to==Player::NotActive){
				player->removeTag("jinming3");
				if (player->getMark("&jinming1-Clear")>0){
					room->sendCompulsoryTriggerLog(player,this);
					player->drawCards(player->getMark("jinmingNum"),objectName());
					if(player->getMark("jinming1-Clear")<1){
						//room->loseHp(player, 1, true, player, objectName());
						player->addMark("jinming1");
						QStringList args = player->getTag("jinmingNum").toStringList();
						args << "jinming1" << "|";
						player->setTag("jinmingNum", args);
						player->setSkillDescriptionSwap(objectName(),"%arg11",args.join("+"));
						room->changeTranslation(player, objectName());
					}
				}
				if (player->getMark("&jinming2-Clear")>0){
					room->sendCompulsoryTriggerLog(player,this);
					player->drawCards(player->getMark("jinmingNum"),objectName());
					if(player->getMark("jinming2-Clear")<2){
						//room->loseHp(player, 1, true, player, objectName());
						player->addMark("jinming2");
						QStringList args = player->getTag("jinmingNum").toStringList();
						args << "jinming2" << "|";
						player->setTag("jinmingNum", args);
						player->setSkillDescriptionSwap(objectName(),"%arg11",args.join("+"));
						room->changeTranslation(player, objectName());
					}
				}
				if (player->getMark("&jinming3-Clear")>0){
					room->sendCompulsoryTriggerLog(player,this);
					player->drawCards(player->getMark("jinmingNum"),objectName());
					if(player->getMark("jinming3-Clear")<3){
						//room->loseHp(player, 1, true, player, objectName());
						player->addMark("jinming3");
						QStringList args = player->getTag("jinmingNum").toStringList();
						args << "jinming3" << "|";
						player->setTag("jinmingNum", args);
						player->setSkillDescriptionSwap(objectName(),"%arg11",args.join("+"));
						room->changeTranslation(player, objectName());
					}
				}
				if (player->getMark("&jinming4-Clear")>0){
					room->sendCompulsoryTriggerLog(player,this);
					player->drawCards(player->getMark("jinmingNum"),objectName());
					if(player->getMark("jinming4-Clear")<4){
						//room->loseHp(player, 1, true, player, objectName());
						player->addMark("jinming4");
						QStringList args = player->getTag("jinmingNum").toStringList();
						args << "jinming4" << "|";
						player->setTag("jinmingNum", args);
						player->setSkillDescriptionSwap(objectName(),"%arg11",args.join("+"));
						room->changeTranslation(player, objectName());
					}
				}
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&&move.to_place==Player::DiscardPile&&player->getMark("&jinming4-Clear")>0
			&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
				for (int i = 0; i < move.card_ids.length(); i++){
					if(move.from_places[i]==Player::PlaceHand||move.from_places[i]==Player::PlaceEquip)
						player->addMark("jinming4-Clear");
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark("&jinming3-Clear")>0){
				QStringList choices = player->getTag("jinming3").toStringList();
				if(!choices.contains(use.card->getType()))
					choices << use.card->getType();
				player->setMark("jinming3-Clear",choices.size());
				player->setTag("jinming3", choices);
			}
		}else if (event == Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if (player->getMark("&jinming2-Clear")>0){
				player->addMark("jinming2-Clear",damage.damage);
			}
		} else {
			RecoverStruct recover = data.value<RecoverStruct>();
			if (player->getMark("&jinming1-Clear")>0){
				player->addMark("jinming1-Clear",recover.recover);
			}
		}
		return false;
	}
};

class Xiaoshi : public TriggerSkill
{
public:
	Xiaoshi() : TriggerSkill("xiaoshi")
	{
		events << TargetSpecifying << CardFinished;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("BasicCard")||use.card->isNDTrick()){
				if(player->getMark("xiaoshiUse-PlayClear")<1&&player->getPhase()==Player::Play){
					room->setCardFlag(use.card,"tunan_distance");
					player->setTag("xiaoshiUse", data);
					QList<ServerPlayer*>aps = room->getCardTargets(player,use.card,use.to);
					ServerPlayer*to = room->askForPlayerChosen(player,aps,objectName(),"xiaoshi0:",true,true);
					room->setCardFlag(use.card,"-tunan_distance");
					if(to){
						use.to.append(to);
						player->peiyin(this);
						room->sortByActionOrder(use.to);
						data.setValue(use);
						room->setCardFlag(use.card,"xiaoshiUse");
						player->addMark("xiaoshiUse-PlayClear");
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("xiaoshiUse")&&!use.card->hasFlag("DamageDone")){
				int n = player->getMark("jinmingNum");
				player->setTag("xiaoshiUse", data);
				ServerPlayer*to = room->askForPlayerChosen(player,use.to,"xiaoshi1","xiaoshi1:"+QString::number(n),true);
				if(to){
					room->doAnimate(1,player->objectName(),to->objectName());
					to->drawCards(n,objectName());
				}else
					room->loseHp(player,1,true,player,objectName());
			}
		}
		return false;
	}
};

class Yanliang : public TriggerSkill
{
public:
	Yanliang() : TriggerSkill("yanliang$")
	{
		events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent triggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (triggerEvent == EventAcquireSkill&&player->hasLordSkill(this,true)){
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->getPhase()==Player::Play&&!p->hasSkill("yanliangvs",true)){
					room->attachSkillToPlayer(p, "yanliangvs");
				}
			}
		}else if(player->getPhase()!=Player::Play)
			return false;
		if (triggerEvent == EventPhaseStart){
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if (p->hasLordSkill(this,true)){
					room->attachSkillToPlayer(player, "yanliangvs");
					break;
				}
			}
		}else{
			if (player->hasSkill("yanliangvs",true))
				room->detachSkillFromPlayer(player, "yanliangvs", true);
		}
		return false;
	}
};

YanliangCard::YanliangCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	mute = true;
}

bool YanliangCard::targetFilter(const QList<const Player*> &targets, const Player*from, const Player*to) const
{
	return targets.isEmpty()&&from!=to&&to->hasLordSkill("yanliang");
}

void YanliangCard::onUse(Room*room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach(ServerPlayer*p, use.to){
		if (p->isWeidi()) {
			room->broadcastSkillInvoke("weidi",-1,p);
			room->notifySkillInvoked(p, "weidi");
		}else
			room->broadcastSkillInvoke("yanliang",-1,p);
		use.from->skillInvoked("yanliang",0,p);
		room->giveCard(use.from,p,this,getSkillName(),true);
		Card*dc = Sanguosha->cloneCard("analeptic");
		dc->setSkillName("_yanliang");
		if(use.from->isAlive()&&use.from->canUse(dc))
			room->useCard(CardUseStruct(dc,use.from),true);
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class YanliangVs : public OneCardViewAsSkill
{
public:
	YanliangVs() : OneCardViewAsSkill("yanliangvs&")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		return to_select->isKindOf("EquipCard");
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*dc = new YanliangCard;
		dc->addSubcard(originalCard);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getKingdom()=="qun"
		&&player->usedTimes("YanliangCard")<1&&Analeptic::IsAvailable(player);
	}
};

RenxianCard::RenxianCard()
{
	target_fixed = true;
}

void RenxianCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	if(subcards.isEmpty()){
		source->addMark("renxian2-PlayClear");
		while(source->isAlive()){
			source->drawCards(2,getSkillName());
			foreach(const Card*c, source->getHandcards()){
				if(c->isDamageCard())
					return;
			}
			room->getThread()->delay();
		}
	}else{
		source->addMark("renxian1-PlayClear");
		while(source->canDiscard(source,"he")){
			bool can = false;
			foreach(const Card*c, source->getHandcards()){
				if(c->isDamageCard())
					can = true;
			}
			if(!can) break;
			room->askForDiscard(source,getSkillName(),2,2,false,true);
			room->getThread()->delay();
		}
	}
}

class RenxianVs : public ViewAsSkill
{
public:
	RenxianVs() : ViewAsSkill("renxian")
	{
	}

	bool viewFilter(const QList<const Card*> &selects, const Card*card) const
	{
		return selects.length()<2&&!Self->isJilei(card);
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("RenxianCard");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		Card*dc = new RenxianCard;
		dc->addSubcards(cards);
		return dc;
	}
};

class Renxian : public TriggerSkill
{
public:
	Renxian() : TriggerSkill("renxian")
	{
		events << EventPhaseEnd;
		view_as_skill = new RenxianVs;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==EventPhaseEnd){
			if(player->getMark("renxian1-PlayClear")>0){
				room->sendCompulsoryTriggerLog(player,this);
				while(player->isAlive()){
					player->drawCards(2,objectName());
					foreach(const Card*c, player->getHandcards()){
						if(c->isKindOf("Slash")||(c->isKindOf("TrickCard")&&c->isDamageCard()))
							return false;
					}
					room->getThread()->delay();
				}
			}
			if(player->getMark("renxian2-PlayClear")>0){
				room->sendCompulsoryTriggerLog(player,this);
				while(player->canDiscard(player,"he")){
					room->askForDiscard(player,objectName(),2,2,false,true);
					bool can = false;
					foreach(const Card*c, player->getHandcards()){
						if(c->isKindOf("Slash")||(c->isKindOf("TrickCard")&&c->isDamageCard()))
							can = true;
					}
					if(!can) break;
					room->getThread()->delay();
				}
			}
		}
		return false;
	}
};

class HuiyunVs : public OneCardViewAsSkill
{
public:
	HuiyunVs() : OneCardViewAsSkill("huiyun")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		Card*dc = Sanguosha->cloneCard("fire_attack");
		dc->addSubcard(to_select);
		dc->setSkillName(objectName());
		dc->deleteLater();
		return dc->isAvailable(Self);
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*dc = Sanguosha->cloneCard("fire_attack");
		dc->addSubcard(originalCard);
		dc->setSkillName(objectName());
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0;
	}
};

class Huiyun : public TriggerSkill
{
public:
	Huiyun() : TriggerSkill("huiyun")
	{
		events << CardFinished << CardOnEffect << CardEffected << ShowCards;
		view_as_skill = new HuiyunVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == CardEffected) return -2;
		return TriggerSkill::getPriority(triggerEvent);
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				foreach(ServerPlayer*tp, use.to){
					if(tp->isAlive()){
						int n = tp->getMark("huiyunId");
						if(n<0) continue;
						QStringList choices;
						if(player->getMark("huiyun1_lun")<1&&tp->hasCard(n))
							choices << "huiyun1="+tp->objectName();
						if(player->getMark("huiyun2_lun")<1&&tp->hasCard(n))
							choices << "huiyun2="+tp->objectName();
						if(player->getMark("huiyun3_lun")<1)
							choices << "huiyun3="+tp->objectName();
						if(choices.isEmpty()) continue;
						QString choice = room->askForChoice(player,objectName(),choices.join("+"),QVariant::fromValue(tp));
						player->addMark(choice.split("=").first()+"_lun");
						if(choice.contains("huiyun1")&&Sanguosha->getCard(n)->isAvailable(tp)&&room->askForUseCard(tp,QString::number(n),"huiyun10:")){
							DummyCard*dc = new DummyCard(tp->handCards());
							if(dc->subcardsLength()>0){
								tp->broadcastSkillInvoke("@recast");
								LogMessage log;
								log.type = "$RecastCard";
								log.from = tp;
								log.card_str = ListI2S(dc->getSubcards()).join("+");
								room->sendLog(log);
								CardMoveReason reason(CardMoveReason::S_REASON_RECAST, tp->objectName(), objectName(), "");
								room->moveCardTo(dc,nullptr,Player::DiscardPile,reason);
								tp->drawCards(dc->subcardsLength(),"recast");
							}
							dc->deleteLater();
						}
						if(choice.contains("huiyun2")){
							choices.clear();
							foreach(const Card*h, tp->getHandcards()){
								if(h->isAvailable(tp))
									choices << h->toString();
							}
							if(choices.length()>0&&room->askForUseCard(tp,choices.join(","),"huiyun20:")){
								tp->broadcastSkillInvoke("@recast");
								LogMessage log;
								log.type = "$RecastCard";
								log.from = tp;
								log.card_str = QString::number(n);
								room->sendLog(log);
								CardMoveReason reason(CardMoveReason::S_REASON_RECAST, tp->objectName(), objectName(), "");
								room->moveCardTo(Sanguosha->getCard(n),nullptr,Player::DiscardPile,reason);
								tp->drawCards(1,"recast");
							}
						}
						if(choice.contains("huiyun3")&&tp->askForSkillInvoke("huiyun30","huiyun30:",false)){
							tp->drawCards(1,objectName());
						}
					}
				}
			}
		}else if(event==CardEffected){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->getSkillNames().contains(objectName())){
				
			}
			effect.to->setFlags("-huiyunBf");
		}else if(event==CardOnEffect){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->getSkillNames().contains(objectName())){
				effect.to->setFlags("huiyunBf");
				effect.to->setMark("huiyunId",-1);
			}
		}else if(event==ShowCards&&player->hasFlag("huiyunBf")){
			QStringList show = data.toString().split(":");
			foreach(QString id, show.first().split("+")){
				player->setMark("huiyunId",id.toInt());
			}
		}
		return false;
	}
};

BojueCard::BojueCard()
{
}

bool BojueCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void BojueCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*target, targets){
		QList<ServerPlayer*> tos;
		tos << source << target;
		int n = source->getHandcardNum()+target->getHandcardNum(),x = 0;
		foreach(ServerPlayer*p, tos){
			QString choice = "bojue1";
			if(p->canDiscard(p,"he")) choice = "bojue1+bojue2";
			choice = room->askForChoice(p,"bojue",choice);
			p->setTag("bojue_choice", choice);
		}
		foreach(ServerPlayer*p, tos){
			if(p->getTag("bojue_choice").toString()=="bojue1")
				p->drawCards(1,"bojue");
			else
				room->askForDiscard(p,"bojue",1,1,false,true);
			x += p->getHandcardNum();
		}
		n = n-x;
		if(n<0) n = -n;
		if(n==0){
			if(source->canDiscard(target,"he"))
				room->throwCard(room->askForCardChosen(source,target,"he","bojue",false,Card::MethodDiscard),
					"bojue",target,source);
			if(target->canDiscard(source,"he"))
				room->throwCard(room->askForCardChosen(target,source,"he","bojue",false,Card::MethodDiscard),
					"bojue",source,target);
		}else if(n==2){
			Card*dc = Sanguosha->cloneCard("slash");
			dc->setSkillName("_bojue");
			if(source->canSlash(target,dc,false))
				room->useCard(CardUseStruct(dc,source,target));
			if(target->canSlash(source,dc,false))
				room->useCard(CardUseStruct(dc,target,source));
			dc->deleteLater();
		}
	}
}

class Bojue : public ZeroCardViewAsSkill
{
public:
	Bojue() : ZeroCardViewAsSkill("bojue")
	{
	}

	const Card*viewAs() const
	{
		return new BojueCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("BojueCard")<2;
	}
};

class OLYangwei : public TriggerSkill
{
public:
	OLYangwei() : TriggerSkill("olyangwei")
	{
		events << ConfirmDamage << DamageForseen << CardsMoveOneTime;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			int n = player->getMark("&olyangwei1");
			if(n>0){
				room->setPlayerMark(player,"&olyangwei1",0);
				room->sendCompulsoryTriggerLog(player,this);
				player->damageRevises(data,n);
			}
		}else if(event==DamageForseen){
			DamageStruct damage = data.value<DamageStruct>();
			int n = player->getMark("&olyangwei2");
			if(n>0){
				room->setPlayerMark(player,"&olyangwei2",0);
				room->sendCompulsoryTriggerLog(player,this);
				player->damageRevises(data,n);
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD
				&&player==move.from&&player->getPhase()!=Player::Discard&&player->hasSkill(objectName())){
				int n = player->getMark("olyangwei2-Clear");
				player->addMark("olyangwei2-Clear",move.card_ids.length());
				if(n<2&&n+move.card_ids.length()>1)
					room->addPlayerMark(player,"&olyangwei2");
			}else if(move.reason.m_reason==CardMoveReason::S_REASON_DRAW&&move.reason.m_skillName!="InitialHandCards"
				&&player==move.to&&player->getPhase()!=Player::Draw&&player->hasSkill(objectName())){
				int n = player->getMark("olyangwei1-Clear");
				player->addMark("olyangwei1-Clear",move.card_ids.length());
				if(n<2&&n+move.card_ids.length()>1)
					room->addPlayerMark(player,"&olyangwei1");
			}
		}
		return false;
	}
};

class Jiaowei : public TriggerSkill
{
public:
	Jiaowei() : TriggerSkill("jiaowei")
	{
		events << CardUsed;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->isBlack()&&player->getHandcardNum()>player->getHp()){
				LogMessage log;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(p->getHp()<=player->getHp()){
						use.no_respond_list << p->objectName();
						log.to << p;
					}
				}
				if(log.to.length()>0){
					room->sendCompulsoryTriggerLog(player,this);
					log.type = "#jiaoweiLog";
					log.from = use.from;
					log.card_str = use.card->toString();
					room->sendLog(log);
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class JiaoweiLimit : public CardLimitSkill
{
public:
	JiaoweiLimit() : CardLimitSkill("#jiaowei_limit")
	{
	}

	QString limitList(const Player*) const
	{
		return "ignore";
	}

	QString limitPattern(const Player*target,const Card*card) const
	{
		if (card->hasTip("neixun-SelfClear",false))
			return card->toString();/*
		if (card->hasTip("fengwei_lun",false))
			return card->toString();*/
		if (target->hasSkill("jiaowei"))
			return ".|black|.|hand";
		return "";
	}
};

class Bianyu : public TriggerSkill
{
public:
	Bianyu() : TriggerSkill("bianyu")
	{
		events << Damage << Damaged << CardUsed;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&damage.to->isAlive()&&damage.to->getHandcardNum()>0&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				QList<int> ids;
				for (int i = 0; i < qMin(damage.to->getHandcardNum(),damage.to->getLostHp()); i++){
					int id = room->askForCardChosen(player,damage.to,"h",objectName(),false,Card::MethodNone,ids);
					if(id<0) break;
					ids << id;
					WrappedCard*card = Sanguosha->getWrappedCard(id);
					Card*dc = Sanguosha->cloneCard("Slash",card->getSuit(),card->getNumber());
					dc->setSkillName("bianyu");
					card->takeOver(dc);
					room->notifyUpdateCard(damage.to, id, card);
				}
			}
		}else if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&player->getHandcardNum()>0&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				int n = qMin(player->getHandcardNum(),player->getLostHp());
				const Card*dc = room->askForExchange(player,objectName(),n,n,false,"bianyu0:"+QString::number(n));
				if(dc){
					foreach(int id, dc->getSubcards()){
						WrappedCard*card = Sanguosha->getWrappedCard(id);
						Card*dc = Sanguosha->cloneCard("Slash",card->getSuit(),card->getNumber());
						dc->setSkillName("bianyu");
						card->takeOver(dc);
						room->notifyUpdateCard(player, id, card);
					}
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>1){
				foreach(const Card*h, player->getHandcards()){
					if(h->getSkillName()==objectName())
						room->filterCards(player,QList<const Card*>()<<h,true);
				}
			}
		}
		return false;
	}
};

class OLFengyao : public TriggerSkill
{
public:
	OLFengyao() : TriggerSkill("olfengyao")
	{
		frequency = Compulsory;
		events << DamageCaused << CardsMoveOneTime;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(player!=damage.to){
				QList<ServerPlayer*>tos;
				foreach(const Card*c, player->getCards("ej")){
					if(c->getSuit()==0&&player->canDiscard(player,c->getId())){
						tos << player;
						break;
					}
				}
				foreach(const Card*c, damage.to->getCards("ej")){
					if(c->getSuit()==0&&player->canDiscard(damage.to,c->getId())){
						tos << damage.to;
						break;
					}
				}
				ServerPlayer*to = room->askForPlayerChosen(player,tos,objectName(),"olfengyao0:");
				if(to){
					room->sendCompulsoryTriggerLog(player,this);
					QList<int>ids;
					foreach(const Card*c, to->getCards("ej")){
						if(c->getSuit()!=0)
							ids << c->getId();
					}
					int id = room->askForCardChosen(player,to,"ej",objectName(),false,Card::MethodDiscard,ids);
					room->throwCard(id,objectName(),to,player);
					player->damageRevises(data,1);
				}
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceEquip)){
				int n = 0;
				foreach(int id, move.card_ids){
					if(move.from_places[n]==Player::PlaceEquip&&Sanguosha->getCard(id)->getSuit()==0){
						room->sendCompulsoryTriggerLog(player,this);
						room->recover(player,RecoverStruct(objectName(),player));
						break;
					}
					n++;
				}
			}
		}
		return false;
	}
};

class Guanbian : public TriggerSkill
{
public:
	Guanbian() : TriggerSkill("guanbian")
	{
		events << GameStart << RoundEnd << ChoiceMade;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==GameStart){
			room->sendCompulsoryTriggerLog(player,this);
		}else if(event==RoundEnd){
			if(data.toInt()==1){
				room->sendCompulsoryTriggerLog(player,objectName());
				room->detachSkillFromPlayer(player,objectName());
			}
		}else if(event==ChoiceMade){
			QString str = data.toString();
			if(str.contains("notifyInvoked:")){
				QStringList strs = str.split(":");
				if(strs[1]=="xiongni"||strs[1]=="fengshang"){
					room->sendCompulsoryTriggerLog(player,objectName());
					room->detachSkillFromPlayer(player,objectName());
				}
			}
		}
		return false;
	}
};

class GuanbianMax : public MaxCardsSkill
{
public:
	GuanbianMax() : MaxCardsSkill("#guanbian_max")
	{
	}

	int getExtra(const Player*target) const
	{
		if(target->hasSkill("guanbian"))
			return target->getSiblings(true).length();
		if(target->hasEquip()&&target->hasSkill("gongqiao")){
			foreach (int id, target->getEquipsId()){
				if(Sanguosha->getEngineCard(id)->isKindOf("EquipCard"))
					return 3;
			}
		}
		return 0;
	}
};

class GuanbianDist : public DistanceSkill
{
public:
	GuanbianDist() : DistanceSkill("#guanbian_dist")
	{
	}

	int getCorrect(const Player*from, const Player*to) const
	{
		int n = 0;
		if(from->hasSkill("guanbian"))
			n += from->getSiblings(true).length();
		if(to->hasSkill("guanbian"))
			n += to->getSiblings(true).length();
		return n;
	}
};

class Xiongni : public TriggerSkill
{
public:
	Xiongni() : TriggerSkill("xiongni")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart&&player->getPhase()==Player::Play&&player->canDiscard(player,"he")){
			const Card*c = room->askForCard(player,"..","xiongni0:",data,objectName());
			if(c){
				player->peiyin(this);
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->canDiscard(p,"he")&&room->askForCard(p,".|"+c->getSuitString(),"xiongni1:"+c->getSuitString(),QVariant::fromValue(player)))
						continue;
					room->damage(DamageStruct(objectName(),player,p));
				}
			}
		}
		return false;
	}
};

FengshangCard::FengshangCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool FengshangCard::targetFixed() const
{
	QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
	return !pattern.contains("fengshang");
}

bool FengshangCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
	return targets.length()<2;
}

bool FengshangCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return targets.length()==2;
}

void FengshangCard::onUse(Room*room, CardUseStruct &card_use) const
{
	room->setTag("fengshangUse",QVariant::fromValue(card_use));
	if(subcards.isEmpty()){
		QList<int>ids;
		foreach(int id, room->getDiscardPile()){
			const Card*c = Sanguosha->getCard(id);
			if(card_use.from->getMark(c->toString()+"fengshangId-Clear")>0
				&&card_use.from->getMark(c->getSuitString()+"fengshang_lun")<1)
				ids << id;
		}
		if(ids.length()>1){
			room->notifyMoveToPile(card_use.from,ids,"fengshang");
			room->askForUseCard(card_use.from,"@@fengshang","fengshang0:");
			room->notifyMoveToPile(card_use.from,ids,"fengshang",Player::DiscardPile,false);
		}else
			room->addPlayerHistory(card_use.from,"FengshangCard",-1);
	}else
		SkillCard::onUse(room, card_use);
}

void FengshangCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	int n = 0;
	CardUseStruct use = room->getTag("fengshangUse").value<CardUseStruct>();
	foreach(ServerPlayer*p, use.to){
		const Card*c = Sanguosha->getCard(subcards[n]);
		source->addMark(c->getSuitString()+"fengshang_lun");
		room->giveCard(source,p,c,getSkillName());
		n++;
	}
	if(!use.to.contains(source)){
		Card*dc = Sanguosha->cloneCard("analeptic");
		dc->setSkillName("_fengshang");
		n = source->usedTimes("Analeptic");
		room->addPlayerHistory(source,"Analeptic",0);
		bool has = dc->isAvailable(source);
		room->addPlayerHistory(source,"Analeptic",n);
		if(has) room->useCard(CardUseStruct(dc,source));
		dc->deleteLater();
	}
}

class FengshangVs : public ViewAsSkill
{
public:
	FengshangVs() : ViewAsSkill("fengshang")
	{
		expand_pile = "#fengshang";
		response_pattern = "@@fengshang";
	}

	bool viewFilter(const QList<const Card*> &selects, const Card*card) const
	{
		if(selects.length()>0&&selects[0]->getSuit()!=card->getSuit()) return false;
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		return selects.length()<2&&pattern.contains("fengshang")
		&&Self->getPileName(card->getId())=="#fengshang";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("FengshangCard");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.contains("fengshang")&&cards.length()<2) return nullptr;
		Card*dc = new FengshangCard;
		dc->addSubcards(cards);
		return dc;
	}
};

class Fengshang : public TriggerSkill
{
public:
	Fengshang() : TriggerSkill("fengshang")
	{
		events << Dying << CardsMoveOneTime;
		view_as_skill = new FengshangVs;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==Dying){
			QList<int>ids;
			foreach(int id, room->getDiscardPile()){
				const Card*c = Sanguosha->getCard(id);
				if(player->getMark(c->toString()+"fengshangId-Clear")>0
					&&player->getMark(c->getSuitString()+"fengshang_lun")<1)
					ids << id;
			}
			if(ids.length()>1){
				room->notifyMoveToPile(player,ids,"fengshang");
				room->askForUseCard(player,"@@fengshang","fengshang0:");
				room->notifyMoveToPile(player,ids,"fengshang",Player::DiscardPile,false);
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				foreach(int id, move.card_ids){
					player->addMark(QString::number(id)+"fengshangId-Clear");
				}
			}
		}
		return false;
	}
};

class Zhibing : public TriggerSkill
{
public:
	Zhibing() : TriggerSkill("zhibing$")
	{
		events << EventPhaseStart << CardUsed;
		frequency = Compulsory;
		global = true;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart&&player->getPhase()==Player::Start){
			if(player->hasLordSkill(this)){
				bool has = true;
				int n = player->getMark("zhibingNum");
				if(n>=3&&player->getMark("zhibing3")<1){
					player->addMark("zhibing3");
					if(has){
						has = false;
						room->sendCompulsoryTriggerLog(player,this);
					}
					room->gainMaxHp(player,1,objectName());
					room->recover(player,RecoverStruct(objectName(),player));
				}
				if(n>=6&&player->getMark("zhibing6")<1){
					player->addMark("zhibing6");
					if(has){
						has = false;
						room->sendCompulsoryTriggerLog(player,this);
					}
					room->acquireSkill(player,"fencheng");
				}
				if(n>=9&&player->getMark("zhibing9")<1){
					player->addMark("zhibing9");
					if(has){
						has = false;
						room->sendCompulsoryTriggerLog(player,this);
					}
					room->acquireSkill(player,"benghuai");
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->isBlack()&&player->getKingdom()=="qun"){
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->getMark("&zhibing")<9&&p->hasLordSkill(this,true))
						room->addPlayerMark(p,"&zhibing");
					p->addMark("zhibingNum");
				}
			}
		}
		return false;
	}
};

class Kuangxiang : public TriggerSkill
{
public:
	Kuangxiang() : TriggerSkill("kuangxiang")
	{
		events << EventPhaseStart << DrawNCards << DamageCaused;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart&&player->getPhase()==Player::Start){
			if(player->hasSkill(objectName())){
				ServerPlayer*to = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"kuangxiang:0",true,true);
				if(to){
					player->peiyin(this);
					int n = 4-to->getHandcardNum();
					if(n>0){
						foreach(int id, to->drawCardsList(n,objectName())){
							if(!to->hasCard(id)) continue;
							const Card*c = Sanguosha->getCard(id);
							room->setCardTip(id,"kuangxiang");
							if(c->isBlack()){
								room->setCardFlag(id,"kuangxiangBlack");
							}else if(c->isRed()){
								room->setCardFlag(id,"kuangxiangRed");
								room->addPlayerMark(to,"&kuangxiang+red",1,QList<ServerPlayer*>()<<to);
							}
						}
					}else if(n<0)
						room->askForDiscard(to,objectName(),-n,-n);
				}
			}
		}else if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("kuangxiangBlack")){
				room->sendCompulsoryTriggerLog(player,objectName());
				player->damageRevises(data,1);
			}
		}else if(event==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase") return false;
			int n = player->getMark("&kuangxiang+red");
			if(n>0){
				draw.num -= n;
				room->sendCompulsoryTriggerLog(player,objectName());
				data.setValue(draw);
				room->setPlayerMark(player,"&kuangxiang+red",0);
			}
		}
		return false;
	}
};

class Shisuan : public TriggerSkill
{
public:
	Shisuan() : TriggerSkill("shisuan")
	{
		frequency = Compulsory;
		events << Damaged;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.from->isAlive()&&player->canDiscard(player,"he")){
				room->sendCompulsoryTriggerLog(player,this);
				room->askForDiscard(player,objectName(),1,1,false,true);
				QString choice = "shisuan1+shisuan3";
				if(damage.from->hasEquip())
					choice = "shisuan1+shisuan2="+player->objectName()+"+shisuan3";
				choice = room->askForChoice(damage.from,objectName(),choice,data);
				if(choice.contains("shisuan1")){
					room->loseHp(damage.from,1,true,player,objectName());
				}
				if(choice.contains("shisuan2")){
					const Card*c = room->askForCard(damage.from,".|.|.|equipped!","shisuan0:"+player->objectName(),data,Card::MethodNone);
					if(c) room->giveCard(damage.from,player,c,objectName());
				}
				if(choice.contains("shisuan3")){
					damage.from->turnOver();
				}
			}
		}
		return false;
	}
};

class ZonglveVs : public OneCardViewAsSkill
{
public:
	ZonglveVs() : OneCardViewAsSkill("zonglve")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		Card*dc = Sanguosha->cloneCard("slash");
		dc->addSubcard(to_select);
		dc->setSkillName(objectName());
		dc->deleteLater();
		return dc->isAvailable(Self);
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*dc = Sanguosha->cloneCard("slash");
		dc->addSubcard(originalCard);
		dc->setSkillName(objectName());
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0&&player->getMark("zonglveUse-PlayClear")<1
		&&Slash::IsAvailable(player);
	}
};

class Zonglve : public TriggerSkill
{
public:
	Zonglve() : TriggerSkill("zonglve")
	{
		events << PreCardUsed << Damage;
		view_as_skill = new ZonglveVs;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				room->addPlayerMark(player,"zonglveUse-PlayClear");
			}
		}else if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&damage.to->getCardCount(true,true)>0){
				foreach(int id, damage.card->getSubcards()){
					if(id>-1&&Sanguosha->getEngineCard(id)->isKindOf("Slash"))
						return false;
				}
				if(!player->askForSkillInvoke(this,damage.to))
					return false;
				player->peiyin(this);
				QList<int>ids;
				DummyCard*dc = new DummyCard;
				for (int i = 0; i < 3; i++){
					int id = room->askForCardChosen(player,damage.to,"hej",objectName(),false,Card::MethodNone,ids);
					if(id<0) break;
					dc->addSubcard(id);
					foreach(const Card*c, damage.to->getCards("hej")){
						if(!ids.contains(c->getEffectiveId())&&room->getCardPlace(id)==room->getCardPlace(c->getEffectiveId()))
							ids << c->getEffectiveId();
					}
					if(ids.length()>=damage.to->getCardCount(true,true))
						break;
				}
				dc->deleteLater();
				player->obtainCard(dc);
			}
		}
		return false;
	}
};

ShuziCard::ShuziCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void ShuziCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		QStringList names;
		foreach(int id, subcards){
			const Card*c = Sanguosha->getCard(id);
			if(c->isKindOf("Slash")) names << "slash";
			else names << c->objectName();
		}
		room->giveCard(source,p,this,"shuzi");
		if(p->isDead()||source->isDead()) continue;
		const Card*dc = room->askForExchange(p,"shuzi",1,1,false,"shuzi0:"+source->objectName());
		if(dc){
			room->giveCard(p,source,dc,"shuzi");
			dc = Sanguosha->getCard(dc->getEffectiveId());
			if(names.contains(dc->objectName())&&p->isAlive(),source->isAlive()){
				QList<ServerPlayer*> tos;
				tos << p;
				foreach(ServerPlayer*q, room->getOtherPlayers(p)){
					foreach(const Card*c, q->getCards("ej")){
						if(source->isProhibited(p,c)) continue;
						if(c->isKindOf("EquipCard")){
							int n = qobject_cast<const EquipCard*>(c->getRealCard())->location();
							if(p->getEquip(n)) continue;
						}
						tos << q;
						break;
					}
				}
				ServerPlayer*to = room->askForPlayerChosen(source,tos,"shuzi","shuzi1:"+p->objectName(),true);
				if(to){
					if(to==p)
						room->damage(DamageStruct("shuzi",source,p));
					else{
						QList<int>ids;
						foreach(const Card*c, to->getCards("ej")){
							if(source->isProhibited(p,c)) ids << c->getEffectiveId();
							if(c->isKindOf("EquipCard")){
								int n = qobject_cast<const EquipCard*>(c->getRealCard())->location();
								if(p->getEquip(n)) ids << c->getEffectiveId();
							}
						}
						int id = room->askForCardChosen(source,to,"he","shuzi",false,Card::MethodNone,ids);
						dc = Sanguosha->getCard(id);
						if(dc) room->moveCardTo(dc,p,room->getCardPlace(id));
					}
				}
			}
		}
	}
}

class Shuzi : public ViewAsSkill
{
public:
	Shuzi() : ViewAsSkill("shuzi")
	{
	}

	bool viewFilter(const QList<const Card*> &selects, const Card*card) const
	{
		return selects.length()<2&&!card->isEquipped();
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->hasUsed("ShuziCard");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.length()<2) return nullptr;
		Card*dc = new ShuziCard;
		dc->addSubcards(cards);
		return dc;
	}
};

class Kuangshou : public TriggerSkill
{
public:
	Kuangshou() : TriggerSkill("kuangshou")
	{
		events << DamageDone << Damaged;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==DamageDone){
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				p->addMark("kuangshou-Clear");
			}
		}else if(event==Damaged&&player->hasSkill(objectName())){
			room->sendCompulsoryTriggerLog(player,this);
			player->drawCards(3,objectName());
			int n = player->getMark("kuangshou-Clear");
			room->askForDiscard(player,objectName(),n,n,false,true);
		}
		return false;
	}
};

JiguCard::JiguCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void JiguCard::onUse(Room*, CardUseStruct &use) const
{
	QList<int>ids;
	Card*dc = new DummyCard;
	foreach(int id, subcards){
		if(use.from->handCards().contains(id))
			ids << id;
		else
			dc->addSubcard(id);
	}
	use.from->addToPile("ji_gu",ids);
	use.from->obtainCard(dc);
	dc->deleteLater();
}

class JiguVs : public ViewAsSkill
{
public:
	JiguVs() : ViewAsSkill("jigu")
	{
		expand_pile = "ji_gu";
		response_pattern = "@@jigu";
	}

	bool viewFilter(const QList<const Card*> &, const Card*card) const
	{
		return !card->isEquipped();
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.length()<2) return nullptr;
		int n = 0;
		foreach(const Card*h, cards){
			if(Self->getPileName(h->getId())=="ji_gu")
				n++;
			else
				n--;
		}
		if(n!=0) return nullptr;
		Card*dc = new JiguCard;
		dc->addSubcards(cards);
		return dc;
	}
};

class Jigu : public TriggerSkill
{
public:
	Jigu() : TriggerSkill("jigu")
	{
		events << EventPhaseChanging << CardsMoveOneTime;
		view_as_skill = new JiguVs;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().from != Player::NotActive) return false;
			foreach(ServerPlayer*p, room->getAllPlayers()){
				QList<int>ids = p->getPile("ji_gu");
				if(ids.length()>0&&player->getMaxHp()==p->getMaxHp()&&p->hasSkill(objectName())){
					room->askForUseCard(p,"@@jigu","jigu0");
				}
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if(move.to_place==Player::DiscardPile&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE
				&&move.from&&move.from->getPhase()!=Player::Play&&player->hasSkill(objectName())
				&&player->getPile("ji_gu").length()<player->getMaxHp()){
				QList<int>ids;
				foreach(int id, move.card_ids){
					if(!room->getCardOwner(id)&&Sanguosha->getCard(id)->getSuit()!=2)
						ids << id;
				}
				if(ids.length()>0){
					room->sendCompulsoryTriggerLog(player,this);
					player->addToPile("ji_gu",ids);
				}
			}
		}
		return false;
	}
};

JiewanCard::JiewanCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool JiewanCard::targetFilter(const QList<const Player*> &targets, const Player*to, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard("snatch");
	card->setSkillName("jiewan");
	foreach(int id, subcards){
		if(!Self->getPile("ji_gu").contains(id))
			card->addSubcard(id);
	}
	card->deleteLater();
	return card->targetFilter(targets,to,Self);
}

const Card*JiewanCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	Card*card = Sanguosha->cloneCard("snatch");
	Card*dc = new DummyCard;
	foreach(int id, subcards){
		if(use.from->getPile("ji_gu").contains(id))
			dc->addSubcard(id);
		else
			card->addSubcard(id);
	}
	room->throwCard(dc,"jiewan",nullptr);
	if(dc->subcardsLength()<1)
		room->loseMaxHp(use.from,1,"jiewan");
	dc->deleteLater();
	card->setSkillName("jiewan");
	card->deleteLater();
	return card;
}

class JiewanVs : public ViewAsSkill
{
public:
	JiewanVs() : ViewAsSkill("jiewan")
	{
		expand_pile = "ji_gu";
		response_pattern = "@@jiewan";
        response_or_use = true;
	}

	bool viewFilter(const QList<const Card*> &cards, const Card*card) const
	{
		int j = 0;
		foreach(const Card*h, cards){
			if(Self->getPile("ji_gu").contains(h->getId())){
				j++;
				if(j>1&&Self->getPile("ji_gu").contains(card->getId()))
					return false;
			}else{
				if(!Self->getPile("ji_gu").contains(card->getId()))
					return false;
			}
		}
		return !card->isEquipped();
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		int j = 0;
		Card*card = Sanguosha->cloneCard("snatch");
		card->setSkillName("jiewan");
		card->deleteLater();
		foreach(const Card*h, cards){
			if(Self->getPile("ji_gu").contains(h->getId()))
				j++;
			else
				card->addSubcard(h);
		}
		if(card->subcardsLength()<1||(j>0&&j!=2)||Self->isLocked(card)) return nullptr;
		Card*dc = new JiewanCard;
		dc->addSubcards(cards);
		return dc;
	}
};

class Jiewan : public TriggerSkill
{
public:
	Jiewan() : TriggerSkill("jiewan")
	{
		events << EventPhaseStart;
		view_as_skill = new JiewanVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if (player->getPhase() == Player::Start){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasSkill(objectName())&&p->getHandcardNum()>0)
						room->askForUseCard(p,"@@jiewan","jiewan0");
				}
			}else if(player->getPhase() == Player::Finish){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->getPile("ji_gu").length()==p->getHandcardNum()&&p->hasSkill(objectName())){
						int x = 0;
						foreach(ServerPlayer*q, room->getAlivePlayers()){
							if(q->getMaxHp()>x) x = q->getMaxHp();
						}
						if(p->getMaxHp()>=x) continue;
						room->sendCompulsoryTriggerLog(p,objectName());
						room->gainMaxHp(p,1,objectName());
					}
				}
			}
		}
		return false;
	}
};

XianyingCard::XianyingCard()
{
	target_fixed = true;
}

void XianyingCard::onUse(Room*room, CardUseStruct &use) const
{
	room->throwCard(this,"xianying",use.from);
	QStringList strs;
	foreach(QString m, use.from->getMarkNames()){
		if(m.contains("&xianying+:+")&&use.from->getMark(m)>0){
			room->setPlayerMark(use.from,m,0);
			m.remove("&xianying+:+");
			m.remove("_lun");
			strs = m.split("+");
		}
	}
	strs << QString::number(subcardsLength());
	room->setPlayerMark(use.from,"&xianying+:+"+strs.join("+")+"_lun",1);
	strs.clear();
	foreach(int id, subcards){
		const Card*c = Sanguosha->getEngineCard(id);
		if(c->isKindOf("Slash")) strs << "slash";
		else strs << c->objectName();
	}
	foreach(QString m, strs){
		if(m!=strs.last())
			return;
	}
	room->setPlayerProperty(use.from,"xianyingName",strs.first());
}

class XianyingVs : public ViewAsSkill
{
public:
	XianyingVs() : ViewAsSkill("xianying")
	{
	}
	bool viewFilter(const QList<const Card*> &, const Card*card) const
	{
		if(Sanguosha->getCurrentCardUsePattern()=="@@xianying1")
			return false;
		return !Self->isJilei(card);
	}
	bool isEnabledAtResponse(const Player*, const QString &pattern) const
	{
		return pattern.contains("@@xianying");
	}
	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}
	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(Sanguosha->getCurrentCardUsePattern()=="@@xianying1"){
			Card*dc = Sanguosha->cloneCard(Self->property("xianyingName").toString());
			dc->setSkillName(objectName());
			return dc;
		}
		foreach(QString m, Self->getMarkNames()){
			if(m.contains("&xianying+:+")&&Self->getMark(m)>0){
				m.remove("&xianying+:+");
				m.remove("_lun");
				if(ListS2I(m.split("+")).contains(cards.length()))
					return nullptr;
			}
		}
		Card*dc = new XianyingCard;
		dc->addSubcards(cards);
		return dc;
	}
};

class Xianying : public TriggerSkill
{
public:
	Xianying() : TriggerSkill("xianying")
	{
		events << EventPhaseChanging << EventPhaseStart << Damaged;
		view_as_skill = new XianyingVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach(ServerPlayer*p, room->getAllPlayers()){
				QString pn = p->property("xianyingName").toString();
				if(!pn.isEmpty()){
					Card*dc = Sanguosha->cloneCard(pn);
					if(!dc) continue;
					dc->setSkillName(objectName());
					if(dc->isAvailable(p))
						room->askForUseCard(p,"@@xianying1","xianying1:"+pn);
					dc->deleteLater();
					room->safeSetPlayerProperty(p,"xianyingName",QString(""));
				}
			}
		}else{
			if(event==EventPhaseStart){
				if (player->getPhase() != Player::Start) return false;
			}
			if(player->hasSkill(objectName())&&player->askForSkillInvoke(this,data)){
				player->drawCards(2,objectName());
				QString pattern = "@@xianying0";
				foreach(QString m, player->getMarkNames()){
					if(m.contains("&xianying+:+")&&player->getMark(m)>0&&m.contains("0"))
						pattern = "@@xianying0!";
				}
				if(!room->askForUseCard(player,pattern,"xianying0")){
					QStringList strs;
					foreach(QString m, player->getMarkNames()){
						if(m.contains("&xianying+:+")&&player->getMark(m)>0){
							room->setPlayerMark(player,m,0);
							m.remove("&xianying+:+");
							m.remove("_lun");
							strs = m.split("+");
						}
					}
					strs << "0";
					room->setPlayerMark(player,"&xianying+:+"+strs.join("+")+"_lun",1);
				}
			}
		}
		return false;
	}
};

OLLiyongCard::OLLiyongCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool OLLiyongCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard("duel");
	card->setSkillName("olliyong");
	card->deleteLater();
	if(Self->getChangeSkillState("olliyong")==1){
		card->addSubcards(subcards);
		return card->targetFilter(targets,to_select,Self);
	}
	return targets.length()<1&&!to_select->isProhibited(Self,card);
}

const Card*OLLiyongCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	if(use.from->getChangeSkillState("olliyong")==1){
		Card*card = Sanguosha->cloneCard("duel");
		card->addSubcards(subcards);
		card->setSkillName("olliyong");
		card->deleteLater();
		room->setChangeSkillState(use.from,"olliyong",2);
		return card;
	}
	room->setChangeSkillState(use.from,"olliyong",1);
	return this;
}

void OLLiyongCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	room->throwCard(this,"olliyong",source);
	foreach(ServerPlayer*p, targets){
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName("_olliyong");
		dc->setFlags("YUANBEN");
		room->useCard(CardUseStruct(dc,p,source));
		dc->deleteLater();
	}
}

class OLLiyongVs : public OneCardViewAsSkill
{
public:
	OLLiyongVs() : OneCardViewAsSkill("olliyong")
	{
	}

	bool isResponseOrUse() const
	{
		return Self->getChangeSkillState("olliyong")==1;
	}

	bool viewFilter(const Card*to_select) const
	{
		if(Self->getChangeSkillState(objectName())==1){
			foreach(QString m, Self->getMarkNames()){
				if(m.contains("&olliyong+")&&m.contains("-Clear")&&Self->getMark(m)>0&&m.contains(to_select->getSuitString()))
					return false;
			}
			Card*dc = Sanguosha->cloneCard("duel");
			dc->setSkillName(objectName());
			dc->addSubcard(to_select);
			dc->deleteLater();
			return dc->isAvailable(Self);
		}else{
			foreach(QString m, Self->getMarkNames()){
				if(m.contains("&olliyong+")&&m.contains("-Clear")&&Self->getMark(m)>0&&m.contains(to_select->getSuitString())&&!Self->isJilei(to_select))
					return true;
			}
			return false;
		}
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*dc = new OLLiyongCard;
		dc->addSubcard(originalCard);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0;
	}
};

class OLLiyong : public TriggerSkill
{
public:
	OLLiyong() : TriggerSkill("olliyong")
	{
		events << PreCardUsed;
		view_as_skill = new OLLiyongVs;
		change_skill = true;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->hasSkill(objectName(),true);
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark(use.card->getSuitString()+"olliyong-Clear")<1){
				player->addMark(use.card->getSuitString()+"olliyong-Clear");
				QStringList strs;
				foreach(QString m, player->getMarkNames()){
					if(m.contains("&olliyong+")&&m.contains("-Clear")&&player->getMark(m)>0){
						room->setPlayerMark(player,m,0);
						m.remove("&olliyong+");
						m.remove("-Clear");
						strs = m.split("+");
					}
				}
				strs << use.card->getSuitString()+"_char";
				room->setPlayerMark(player,"&olliyong+"+strs.join("+")+"-Clear",1);
			}
		}
		return false;
	}
};

class Guliang : public TriggerSkill
{
public:
	Guliang() : TriggerSkill("guliang")
	{
		events << CardUsed;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach(ServerPlayer*p, use.to){
					if(player!=p&&p->hasSkill(objectName())){
						if(p->getMark("guliangUse-Clear")>0){
							room->sendCompulsoryTriggerLog(p,objectName());
							use.no_respond_list << p->objectName();
							data.setValue(use);
						}else if(p->askForSkillInvoke(objectName()+"$-1",data)){
							p->addMark("guliangUse-Clear");
							use.nullified_list << p->objectName();
							data.setValue(use);
						}
					}
				}
			}
		}
		return false;
	}
};

class Xutu : public TriggerSkill
{
public:
	Xutu() : TriggerSkill("xutu")
	{
		events << GameStart << EventPhaseStart << CardsMoveOneTime;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==GameStart){
			if(player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				player->addToPile("xt_zhi",room->getNCards(3));
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				foreach(int id, move.card_ids){
					player->addMark(QString::number(id)+"xutuId-Clear");
				}
			}
		}else if(player->getPhase()==Player::Finish){
			foreach(ServerPlayer*p, room->getAllPlayers()){
				QList<int>ids = p->getPile("xt_zhi");
				if(ids.length()>0&&p->hasSkill(objectName())){
					QList<int>ids2;
					foreach(int id, room->getDiscardPile()){
						if(p->getMark(QString::number(id)+"xutuId-Clear")>0)
							ids2 << id;
					}
					if(ids2.isEmpty()) continue;
					room->sendCompulsoryTriggerLog(p,this);
					room->fillAG(ids2,p);
					int id1 = room->askForAG(p,ids2,ids2.length()<2,"xutu1","xutu1");
					if(id1<0) id1 = ids2.first();
					room->clearAG(p);
					room->fillAG(ids,p);
					int id2 = room->askForAG(p,ids,ids.length()<2,"xutu2","xutu2");
					if(id2<0) id2 = ids.first();
					room->clearAG(p);
					room->throwCard(id2,objectName(),nullptr);
					if(p->isDead()) continue;
					p->addToPile("xt_zhi",id1);
					if(p->isDead()) continue;
					ids = p->getPile("xt_zhi");
					id1 = 0;
					id2 = 0;
					const Card*fc = Sanguosha->getCard(ids.first());
					foreach(int id, ids){
						const Card*c = Sanguosha->getCard(id);
						if(fc->getNumber()==c->getNumber())
							id1++;
						if(fc->getSuit()==c->getSuit())
							id2++;
					}
					if(id1>=3||id2>=3){
						ServerPlayer*tp = room->askForPlayerChosen(p,room->getAlivePlayers(),objectName(),"xutu3");
						if(tp){
							room->doAnimate(1,p->objectName(),tp->objectName());
							Card*dc = new DummyCard(ids);
							tp->obtainCard(dc);
							dc->deleteLater();
							if(p->isDead()) continue;
							p->addToPile("xt_zhi",room->getNCards(3));
						}
					}
				}
			}
		}
		return false;
	}
};

JingxianCard::JingxianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool JingxianCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	return targets.length()<1&&to_select->getMark("jingxianUse-PlayClear")<1;
}

void JingxianCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		room->giveCard(source,p,this,"jingxian");
		QStringList choices;
		choices << "jingxian1" << "jingxian2";
		for (int i = 0; i < subcardsLength(); i++){
			if(source->isDead()) choices.removeOne("jingxian2");
			if(choices.isEmpty()||p->isDead()) break;
			QString chouce = room->askForChoice(p,"jingxian",choices.join("+"),QVariant::fromValue(source));
			choices.removeOne(chouce);
			if(chouce=="jingxian1"){
				source->drawCards(1,"jingxian");
				p->drawCards(1,"jingxian");
			}else{
				QList<int>ids = room->getDrawPile();
				qShuffle(ids);
				foreach(int id, ids){
					if(Sanguosha->getCard(id)->isKindOf("Slash")){
						room->obtainCard(source,id);
						break;
					}
				}
			}
		}
	}
}

class Jingxian : public ViewAsSkill
{
public:
	Jingxian() : ViewAsSkill("jingxian")
	{
	}

	bool viewFilter(const QList<const Card*> &selects, const Card*card) const
	{
		return selects.length()<2&&card->getTypeId()>1;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.length()<1) return nullptr;
		Card*dc = new JingxianCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		foreach(const Player*p, player->getAliveSiblings()){
			if(p->getMark("jingxianUse-PlayClear")<1)
				return player->getCardCount()>1;
		}
		return false;
	}
};

XiayongCard::XiayongCard()
{
}

bool XiayongCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.length()<1&&to_select!=Self;
}

void XiayongCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		room->addPlayerMark(p,"&xiayong-SelfClear");
		room->addPlayerMark(source,"drank");
	}
}

class Xiayongvs : public ViewAsSkill
{
public:
	Xiayongvs() : ViewAsSkill("xiayong")
	{
	}

	bool viewFilter(const QList<const Card*> &, const Card*) const
	{
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		Card*dc = new XiayongCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("XiayongCard")<1;
	}
};

class Xiayong : public TriggerSkill
{
public:
	Xiayong() : TriggerSkill("xiayong")
	{
		events << CardFinished << MarkChange;
		view_as_skill = new Xiayongvs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&!use.to.contains(player)&&player->getMark("&xiayong-SelfClear")>0){
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->hasSkill(objectName(),true)){
						room->askForUseSlashTo(p,player,"xiayong0:"+player->objectName(),false,false,false,nullptr,nullptr,"SlashIgnoreArmor");
					}
				}
			}
		}else if(event==MarkChange){
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="drank"&&mark.gain<0&&player->hasSkill(objectName(),true)){
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->getMark("&xiayong-SelfClear")>0){
						mark.gain++;
						data.setValue(mark);
						return mark.gain==0;
					}
				}
			}
		}
		return false;
	}
};

class Nilan : public TriggerSkill
{
public:
	Nilan() : TriggerSkill("nilan")
	{
		events << Damage << Damaged;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.reason!=objectName()&&player->askForSkillInvoke(this,data)){
				QString choice = "nilan2";
				QList<const Card*>hs = player->getHandcards();
				if(hs.length()>0) choice = "nilan1+nilan2";
				choice = room->askForChoice(player,objectName(),choice);
				QStringList thss = player->getTag("nilan_choices").toStringList();
				if(choice=="nilan2"){
					thss << "nilan2";
					player->setTag("nilan_choices", thss);
					player->drawCards(2,objectName());
				}else{
					thss << "nilan1";
					player->setTag("nilan_choices", thss);
					player->throwAllHandCards(objectName());
					foreach(const Card*h, hs){
						if(h->isKindOf("Slash")){
							ServerPlayer*p = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"nilan0",true);
							if(p){
								room->damage(DamageStruct(objectName(),player,p));
								break;
							}
						}
					}
				}
			}
		}else if(event==Damaged){
			//DamageStruct damage = data.value<DamageStruct>();
			QStringList thss = player->getTag("nilan_choices").toStringList();
			player->removeTag("nilan_choices");
			foreach(QString h, thss){
				if(!player->askForSkillInvoke(this,"nilan3:"+h,false)) continue;
				if(h=="nilan2")
					player->drawCards(2,objectName());
				else{
					QList<const Card*>hs = player->getHandcards();
					player->throwAllHandCards(objectName());
					foreach(const Card*h, hs){
						if(h->isKindOf("Slash")){
							ServerPlayer*p = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"nilan0",true);
							if(p){
								room->damage(DamageStruct(objectName(),player,p));
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class Jueyavs : public ViewAsSkill
{
public:
	Jueyavs() : ViewAsSkill("jueya")
	{
	}

	bool viewFilter(const QList<const Card*> &, const Card*) const
	{
		return false;
	}

	const Card*viewAs(const QList<const Card*> &) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card*c = Self->getTag("jueya").value<const Card*>();
			if(c) pattern = c->objectName();
		}
		foreach(QString pn, pattern.split("+")){
			if(Self->getMark("jueya_guhuo_remove_"+pn)>0) continue;
			Card*dc = Sanguosha->cloneCard(pn);
			if(dc){
				dc->setSkillName("jueya");
				if(dc->isKindOf("BasicCard")&&!Self->isLocked(dc))
					return dc;
				dc->deleteLater();
			}
		}
		return nullptr;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE||!player->isKongcheng()) return false;
		foreach(QString pn, pattern.split("+")){
			if(player->getMark("jueya_guhuo_remove_"+pn)>0) continue;
			Card*dc = Sanguosha->cloneCard(pn);
			if(dc){
				dc->deleteLater();
				dc->setSkillName("jueya");
				if(dc->isKindOf("BasicCard")&&!player->isLocked(dc))
					return true;
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		if(!player->isKongcheng()) return false;
		foreach(int id, Sanguosha->getRandomCards()){
			const Card*c = Sanguosha->getCard(id);
			if(c->isKindOf("BasicCard")&&player->getMark("jueya_guhuo_remove_"+c->objectName())<1){
				Card*dc = Sanguosha->cloneCard(c->objectName());
				dc->setSkillName("jueya");
				dc->deleteLater();
				if(dc->isAvailable(player))
					return true;
			}
		}
		return false;
	}
};

class Jueya : public TriggerSkill
{
public:
	Jueya() : TriggerSkill("jueya")
	{
		events << PreCardUsed;
		view_as_skill = new Jueyavs;
	}
	QDialog*getDialog() const
	{
		return GuhuoDialog::getInstance(objectName(), true, false);
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->getSkillNames().contains(objectName())){
				room->addPlayerMark(player,"jueya_guhuo_remove_"+use.card->objectName());
			}
		}
		return false;
	}
};

class Bingcai : public TriggerSkill
{
public:
	Bingcai() : TriggerSkill("bingcai")
	{
		events << CardUsed;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
    bool trigger(TriggerEvent event, Room*room, ServerPlayer*, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()==1){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					p->addMark("bingcaiUse-Clear");
					if(p->getMark("bingcaiUse-Clear")==1&&p->hasSkill(objectName())){
						const Card*c = room->askForCard(p,"TrickCard","bingcai0:"+use.card->objectName(),data,Card::MethodRecast);
						if(c){
							p->skillInvoked(this);
							p->broadcastSkillInvoke("@recast");
							room->moveCardTo(c,nullptr,Player::DiscardPile,CardMoveReason(CardMoveReason::S_REASON_RECAST, p->objectName(), objectName(), ""));
							p->drawCards(1,"recast");
							if((c->isDamageCard()&&use.card->isDamageCard())||(!c->isDamageCard()&&!use.card->isDamageCard())){
								use.extra_use++;
								data.setValue(use);
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class Lixianvs : public ViewAsSkill
{
public:
	Lixianvs() : ViewAsSkill("lixian")
	{
	}

	bool viewFilter(const QList<const Card*> &cards, const Card*card) const
	{
		return cards.isEmpty()&&card->hasTip("lixian");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()) pattern = "slash";
		foreach(QString pn, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(pn);
			if(dc){
				dc->setSkillName("jueya");
				dc->addSubcards(cards);
				if(!Self->isLocked(dc))
					return dc;
				dc->deleteLater();
			}
		}
		return nullptr;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE) return false;
		return !player->isKongcheng()&&(pattern.contains("slash")||pattern.contains("jink"));
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return !player->isKongcheng()&&Slash::IsAvailable(player);
	}
};

class Lixian : public TriggerSkill
{
public:
	Lixian() : TriggerSkill("lixian")
	{
		events << TargetSpecified << EventPhaseStart;
		view_as_skill = new Lixianvs;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()==2){
				foreach(ServerPlayer*p, use.to){
					p->addMark(use.card->toString()+"lixianTrick-Clear");
				}
			}
		}else{
			if(player->getPhase()==Player::Finish){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						Card*dc = new DummyCard;
						foreach(int id, room->getDiscardPile()){
							if(p->getMark(QString::number(id)+"lixianTrick-Clear")>0)
								dc->addSubcard(id);
						}
						if(dc->subcardsLength()>0){
							room->sendCompulsoryTriggerLog(p,this);
							p->obtainCard(dc);
							foreach(int id, dc->getSubcards()){
								if(p->handCards().contains(id))
									room->setCardTip(id,"lixian");
							}
						}
						dc->deleteLater();
					}
				}
			}
		}
		return false;
	}
};

class LixianLimit : public CardLimitSkill
{
public:
	LixianLimit() : CardLimitSkill("#lixian_limit")
	{
	}

	QString limitList(const Player*) const
	{
		return "use";
	}

	QString limitPattern(const Player*target, const Card*card) const
	{
		if (card->hasTip("lixian")&&target->hasSkill("lixian"))
			return card->toString();
		if (target->getMark("jiaoyuPhase-Clear")>0&&target->getMark("&jiaoyu+"+card->getColorString()+"_lun")<1)
			return card->toString();
		if (card->isKindOf("DelayedTrick")&&target->getMark("duoqiBf")>0)
			return card->toString();
		if (card->isKindOf("EquipCard")&&target->hasSkill("fuzhai"))
			return card->toString();
		return "";
	}
};

class OLZhaohuo : public TriggerSkill
{
public:
	OLZhaohuo() : TriggerSkill("olzhaohuo")
	{
		events << Damaged;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==Damaged){
			player->addMark("olzhaohuoDamaged-Clear");
			if(player->getMark("olzhaohuoDamaged-Clear")!=1) return false;
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if(p->hasFlag("CurrentPlayer")&&player->getHandcardNum()>0&&p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p,this);
					const Card*dc = room->askForExchange(player,objectName(),1,1,false,"olzhaohuo0:"+p->objectName());
					if(dc){
						QString subcard = dc->subcardString();
						subcard.remove("$");
						room->setPlayerCardLimitation(p,"use,response,discard",subcard,true);
						p->obtainCard(dc,false);
					}
				}
			}
		}
		return false;
	}
};

WenrenCard::WenrenCard()
{
}

bool WenrenCard::targetFilter(const QList<const Player*> &, const Player*, const Player*) const
{
	return true;
}

void WenrenCard::use(Room*, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		int n = 0;
		if(p->isKongcheng())
			n++;
		if(p->getHandcardNum()<=source->getHandcardNum())
			n++;
		p->drawCards(n,"wenren");
	}
}

class Wenren : public ViewAsSkill
{
public:
	Wenren() : ViewAsSkill("wenren")
	{
	}

	bool viewFilter(const QList<const Card*> &, const Card*) const
	{
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		Card*dc = new WenrenCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("WenrenCard")<1;
	}
};

class Zongluan : public TriggerSkill
{
public:
	Zongluan() : TriggerSkill("zongluan")
	{
		events << EventPhaseStart << DamageDone;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!=Player::Start||!player->hasSkill(objectName())) return false;
			ServerPlayer*tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"zongluan0",true,true);
			if(tp){
				player->peiyin(this);
				Card*dc = Sanguosha->cloneCard("slash");
				dc->setSkillName("_zongluan");
				QList<ServerPlayer*>tps;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(tp->inMyAttackRange(p)&&tp->canSlash(p,dc))
						tps << p;
				}
				room->removeTag("zongluanDamage");
				tps = room->askForPlayersChosen(tp,tps,objectName(),1,9,"zongluan1");
				foreach(ServerPlayer*p, tps)
					room->doAnimate(1,tp->objectName(),p->objectName());
				foreach(ServerPlayer*p, tps){
					dc->setFlags("zongluanUse");
					if(p->isAlive())
						room->useCard(CardUseStruct(dc,tp,p));
				}
				dc->deleteLater();
				int n = room->getTag("zongluanDamage").toInt();
				if(n>0&&player->isAlive())
					room->askForDiscard(player,objectName(),n,n,false,true);
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && damage.card->hasFlag("zongluanUse")){
				int n = room->getTag("zongluanDamage").toInt();
				n++;
				room->setTag("zongluanDamage",n);
			}
		}
		return false;
	}
};

class OLXuhe : public TriggerSkill
{
public:
	OLXuhe() :TriggerSkill("olxuhe")
	{
		events << EventPhaseStart << EventPhaseEnd;
	}

	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if (player->getPhase() != Player::Play) return false;
		if (event == EventPhaseStart) {
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke("xuhe");
			room->loseMaxHp(player, 1, "olxuhe");
			if (player->isDead()) return false;

			QStringList choices;
			QList<ServerPlayer*> players;
			foreach (ServerPlayer*p, room->getAllPlayers()) {
				if (player->distanceTo(p) <= 1)
					players << p;
			}

			foreach (ServerPlayer*p, players) {
				if (player->canDiscard(p, "he")) {
					choices << "discard";
					break;
				}
			}
			choices << "draw";
			if (room->askForChoice(player, objectName(), choices.join("+")) == "discard") {
				foreach (ServerPlayer*p, players) {
					if (player->canDiscard(p, "he")) {
						int card_id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
						room->throwCard(card_id, objectName(), p, player);
					}
				}
			} else
				room->drawCards(players, 1, objectName());
		} else {
			foreach (ServerPlayer*p, room->getOtherPlayers(player)) {
				if (p->getMaxHp() < player->getMaxHp())
					return false;
			}
			room->sendCompulsoryTriggerLog(player, objectName());
			room->gainMaxHp(player, 1, objectName());

			QStringList choices;
			if (player->isWounded())
				choices << "recover";
			choices << "selfdraw";
			QString choice = room->askForChoice(player, objectName(), choices.join("+"));
			if (choice == "recover")
				room->recover(player, RecoverStruct("xuhe", player));
			else
				player->drawCards(2, objectName());
		}
		return false;
	}
};

class Jiaoyu : public TriggerSkill
{
public:
	Jiaoyu() : TriggerSkill("jiaoyu")
	{
		events << RoundStart << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==RoundStart){
			if(!player->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(player,this);
			QList<const Card*>cs;
			for (int i = 0; i < qMax(1,player->getEquips().length()); i++){
				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.pattern = ".";
				judge.play_animation = false;
				room->judge(judge);
				if(player->isDead()) return false;
				if(!room->getCardOwner(judge.card->getEffectiveId())){
					cs << judge.card;
					if(judge.card->isRed())
						player->addMark("jiaoyuRed");
				}
			}
			QString color = room->askForChoice(player,objectName(),"red+black");
			Card*dc = new DummyCard;
			foreach (const Card*c, cs) {
				if(color==c->getColorString())
					dc->addSubcard(c->getEffectiveId());
			}
			room->setPlayerMark(player,"&jiaoyu+"+color+"_lun",1);
			player->addMark("jiaoyuUse-SelfClear");
			player->obtainCard(dc);
			dc->deleteLater();
			player->setMark("jiaoyuRed",0);
		}else{
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.from!=Player::Finish||change.to!=Player::NotActive||player->getMark("jiaoyuUse-SelfClear")<1) return false;
			//room->sendCompulsoryTriggerLog(player,objectName());
			room->setPlayerMark(player,"jiaoyuPhase-Clear",1);
			LogMessage log;
			log.type = "#HongjiEffect";
			log.from = player;
			log.arg = objectName();
			log.arg2 = "play";
			room->sendLog(log);
			change.to = Player::Play;
			data.setValue(change);
			player->insertPhase(Player::NotActive);/*
			room->broadcastProperty(player, "phase");
			if (!room->getThread()->trigger(EventPhaseStart, room, player))
				room->getThread()->trigger(EventPhaseProceeding, room, player);
			room->getThread()->trigger(EventPhaseEnd, room, player);
			player->setPhase(Player::NotActive);
			room->broadcastProperty(player, "phase");
			room->setPlayerMark(player,"jiaoyuPhase",0);*/
		}
		return false;
	}
};

class Neixun : public TriggerSkill
{
public:
	Neixun() : TriggerSkill("neixun")
	{
		events << CardFinished;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->getTypeId()!=3&&player->hasFlag("CurrentPlayer")){
				player->addMark("neixunUse-Clear");
				if(player->getMark("neixunUse-Clear")==1){
					foreach (ServerPlayer*p, room->getOtherPlayers(player)) {
						if(p->hasSkill(objectName())){
							foreach (QString m, p->getMarkNames()) {
								if(m.contains("&jiaoyu+")&&p->getMark(m)>0){
									if(m.contains(use.card->getColorString())){
										if(p->getCardCount()>0){
											room->sendCompulsoryTriggerLog(p,this);
											const Card*dc = room->askForExchange(p,objectName(),1,1,true,"neixun0:"+player->objectName());
											if(dc){
												room->giveCard(p,player,dc,objectName());
												int id = p->drawCardsList(1,objectName()).first();
												if(p->handCards().contains(id))
													room->setCardTip(id,"neixun-SelfClear");
											}
										}
									}else{
										if(player->getCardCount()>0){
											room->sendCompulsoryTriggerLog(p,this);
											int id = room->askForCardChosen(p,player,"he",objectName());
											if(id>=0){
												room->obtainCard(p,id,false);
												player->drawCards(1,objectName());
												if(p->handCards().contains(id))
													room->setCardTip(id,"neixun-SelfClear");
											}
										}
									}
									break;
								}
							}
						}
					}
				}
			}
		}
		return false;
	}
};

SiqiCard::SiqiCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool SiqiCard::targetFilter(const QList<const Player*> &tos, const Player*to, const Player*Self) const
{
	return tos.isEmpty()&&!Self->isProhibited(to,Sanguosha->getCard(getEffectiveId()));
}

bool SiqiCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	return targets.length()>0||!Self->isProhibited(Self,Sanguosha->getCard(getEffectiveId()));
}

const Card*SiqiCard::validate(CardUseStruct &use) const
{
	if(use.to.isEmpty()) use.to << use.from;
	return Sanguosha->getCard(getEffectiveId());
}

class SiqiVs : public OneCardViewAsSkill
{
public:
	SiqiVs() : OneCardViewAsSkill("siqi")
	{
		response_pattern = "@@siqi";
		expand_pile = "#siqi";
        response_or_use = true;
	}

	bool viewFilter(const Card*to_select) const
	{
		if(Self->getPileName(to_select->getEffectiveId())=="#siqi"&&!Self->isLocked(to_select)){
			return to_select->isKindOf("Peach")||to_select->isKindOf("Analeptic")
			||to_select->isKindOf("ExNihilo") ||to_select->isKindOf("EquipCard");
		}
		return false;
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*dc = new SiqiCard;
		dc->addSubcard(originalCard);
		return dc;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}
};

class Siqi : public TriggerSkill
{
public:
	Siqi() : TriggerSkill("siqi")
	{
		events << CardsMoveOneTime << Damaged;
		view_as_skill = new SiqiVs;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&move.from==player){
				QList<int>ids;
				foreach(int id, move.card_ids){
					if(player->getMark(QString::number(id)+"siqiId-Clear")>0
					||move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip)){
						player->setMark(QString::number(id)+"siqiId-Clear",0);
						if(!room->getCardOwner(id)&&Sanguosha->getCard(id)->isRed())
							ids << id;
					}
				}
				if(ids.length()>0){
					room->sendCompulsoryTriggerLog(player,this);
					room->moveCardsToEndOfDrawpile(player,ids,objectName());
				}
			}else if(move.to_place==Player::PlaceTable&&move.from==player
				&&(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))){
				foreach(int id, move.card_ids){
					player->addMark(QString::number(id)+"siqiId-Clear");
				}
			}
		}else{
			if (!player->askForSkillInvoke(this,data)) return false;
			player->peiyin(this);
			QList<int>ids2,ids3,ids = room->getNCards(9,true,false);
			foreach(int id, ids){
				if(ids2.isEmpty()){
					if(Sanguosha->getCard(id)->isRed())
						ids3 << id;
					else
						ids2 << id;
				}else
					ids2 << id;
			}
			room->fillAG(ids,player,ids2);
			int tid = room->askForAG(player,ids3,ids3.length()<2,objectName(),"siqi0");
			room->clearAG(player);
			room->returnToEndDrawPile(ids);
			if(tid<0){
				if(ids3.isEmpty())
					return false;
				tid = ids3.first();
			}
			ids3.clear();
			foreach(int id, ids){
				ids3 << id;
				if(tid==id) break;
			}
			ids3 = room->showDrawPile(player,ids3.length(),objectName(),true,false);
			while(player->isAlive()&&ids3.length()>0){
				room->notifyMoveToPile(player,ids3,"siqi");
				const Card*c = room->askForUseCard(player,"@@siqi","siqi1");
				if(c) ids3.removeOne(c->getEffectiveId());
				else break;
			}
			Card*dc = new DummyCard(ids3);
			room->throwCard(dc,objectName(),nullptr);
			dc->deleteLater();
		}
		return false;
	}
};

QiaozhiCard::QiaozhiCard()
{
	target_fixed = true;
}

void QiaozhiCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	QList<int> ids = room->showDrawPile(source,2,"qiaozhi");
	room->fillAG(ids,source);
	int id = room->askForAG(source,ids,false,"qiaozhi");
	ids.removeOne(id);
	room->clearAG(source);
	room->obtainCard(source,id);
	if(source->handCards().contains(id))
		room->setCardTip(id,"qiaozhi");
	room->throwCard(ids,"qiaozhi",nullptr);
}

class Qiaozhi : public OneCardViewAsSkill
{
public:
	Qiaozhi() : OneCardViewAsSkill("qiaozhi")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		return !Self->isJilei(to_select);
	}

	const Card*viewAs(const Card*originalCard) const
	{
		Card*dc = new QiaozhiCard;
		dc->addSubcard(originalCard);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		foreach(const Card*h, player->getHandcards()){
			if(h->hasTip("qiaozhi")) return false;
		}
		return player->canDiscard(player,"he");
	}
};

class Choulie : public TriggerSkill
{
public:
	Choulie() : TriggerSkill("choulie")
	{
		events << TargetConfirmed << EventPhaseStart;
		frequency = Limited;
		limit_mark = "@choulie";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.card->getSkillNames().contains(objectName()) &&use.to.contains(player)
				&&player->getMark("&choulie+#"+use.from->objectName()+"-Clear")>0&&player->canDiscard(player,"he")){
				if(room->askForCard(player,"BasicCard,Weapon","choulie2:",data)){
					use.nullified_list << player->objectName();
					data.setValue(use);
				}
			}
		}else{
			if(player->getPhase()==Player::RoundStart){
				if(player->getMark("@choulie")>0&&player->hasSkill(objectName())){
					ServerPlayer*p = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"choulie0",true,true);
					if(p){
						player->peiyin(this);
						room->removePlayerMark(player,"@choulie");
						room->doSuperLightbox(player,objectName());
						room->setPlayerMark(p,"&choulie+#"+player->objectName()+"-Clear",1);
						player->addMark("choulieUse-Clear");
					}
				}
			}else if(player->getPhase()!=Player::NotActive&&player->getMark("choulieUse-Clear")>0){
				foreach (ServerPlayer*p, room->getAllPlayers()) {
					if(p->getMark("&choulie+#"+player->objectName()+"-Clear")>0&&player->canDiscard(player,"he")){
						if(room->askForCard(player,"..","choulie1:"+p->objectName(),QVariant::fromValue(p))){
							Card*dc = Sanguosha->cloneCard("slash");
							dc->setSkillName("_choulie");
							if(player->canSlash(p,dc,false)){
								dc->setFlags("SlashIgnoreArmor");
								p->addQinggangTag(dc);
								room->useCard(CardUseStruct(dc,player,p));
							}
							dc->deleteLater();
						}
					}
				}
			}
		}
		return false;
	}
};

class Zhuijiao : public TriggerSkill
{
public:
	Zhuijiao() : TriggerSkill("zhuijiao")
	{
		events << CardUsed << CardFinished << DamageCaused;
		frequency = Compulsory;
		global = true;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&player->hasSkill(objectName())){
				if(player->getMark("zhuijiaoDamage")==1){
					room->sendCompulsoryTriggerLog(player,this);
					player->drawCards(1,objectName());
					use.card->setFlags("zhuijiaoUse");
				}
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				if(use.card->hasFlag("DamageDone")){
					player->setMark("zhuijiaoDamage",2);
				}else{
					player->setMark("zhuijiaoDamage",1);
					if(use.card->hasFlag("zhuijiaoUse"))
						room->askForDiscard(player,objectName(),1,1,false,true);
				}
			}
		}else if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("zhuijiaoUse")){
				player->damageRevises(data,1);
			}
		}
		return false;
	}
};

class Fengwei : public TriggerSkill
{
public:
	Fengwei() : TriggerSkill("fengwei")
	{
		events << RoundStart << DamageInflicted << EventPhaseChanging;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==RoundStart){
			if(player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				QString choice = room->askForChoice(player,objectName(),"1+2+3+4");
				foreach (int id, player->drawCardsList(choice.toInt(),objectName())){
					if(player->handCards().contains(id))
						room->setCardTip(id,"fengwei_lun");
				}
				//room->setPlayerMark(player,"&fengweiDamage_lun",choice.toInt());
			}
		}else if(event==DamageInflicted){/*
			if(player->getMark("&fengweiDamage_lun")>0){
				room->sendCompulsoryTriggerLog(player,objectName());
				room->removePlayerMark(player,"&fengweiDamage_lun");
				player->damageRevises(data,1);
			}*/
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getTypeId()>0){
				foreach (const Card*h, player->getHandcards()){
					if(h->hasTip("fengwei")){
						room->sendCompulsoryTriggerLog(player,objectName());
						player->damageRevises(data,1);
						break;
					}
				}
			}
		}else if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::Draw){
				foreach (const Card*h, player->getHandcards()){
					if(h->hasTip("fengwei")){
						room->sendCompulsoryTriggerLog(player,objectName());
						player->skip(change.to);
						break;
					}
				}
			}
		}
		return false;
	}
};

ZonghuCard::ZonghuCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool ZonghuCard::targetFixed() const
{
	return user_string.contains("jink");
}

bool ZonghuCard::targetFilter(const QList<const Player*> &tos, const Player*to, const Player*Self) const
{
	Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("_zonghu");
	dc->deleteLater();
	return dc->targetFilter(tos,to,Self);
}

const Card*ZonghuCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	ServerPlayer*tp = room->askForPlayerChosen(use.from,room->getOtherPlayers(use.from),"zonghu","zonghu0",true,true);
	if(!tp) return nullptr;
	room->giveCard(use.from,tp,this,"zonghu");
	room->addPlayerMark(use.from,"&zonghu_lun");
	room->addPlayerMark(use.from,"zonghuUse-Clear");
	Card*dc = Sanguosha->cloneCard(user_string.contains("jink")?"jink":"slash");
	dc->setSkillName("_zonghu");
	dc->deleteLater();
	return dc;
}

class Zonghu : public ViewAsSkill
{
public:
	Zonghu() : ViewAsSkill("zonghu")
	{
	}

	bool viewFilter(const QList<const Card*> &selects, const Card*) const
	{
		return selects.length()<=Self->getMark("&zonghu_lun");
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.length()<=Self->getMark("&zonghu_lun")) return nullptr;
		ZonghuCard*dc = new ZonghuCard;
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.isEmpty()) pattern = "slash";
		dc->setUserString(pattern);
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(Sanguosha->currentRoomState()->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE) return false;
		return player->getMark("zonghuUse-Clear")<1&&player->getMark("&zonghu_lun")<player->getCardCount()
		&&player->hasTurn()&&(pattern.contains("slash")||pattern.contains("jink"));
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("zonghuUse-Clear")<1&&player->getMark("&zonghu_lun")<player->getCardCount()&&Slash::IsAvailable(player);
	}
};

DeruCard::DeruCard()
{
	will_throw = false;
}

bool DeruCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void DeruCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	QStringList choices;
	foreach(int id, Sanguosha->getRandomCards()){
		const Card*c = Sanguosha->getCard(id);
		if(c->isKindOf("BasicCard")&&!choices.contains(c->objectName())){
			if(c->isKindOf("Slash")){
				if(!choices.contains("slash"))
					choices << "slash";
			}else
				choices << c->objectName();
		}
	}
	foreach(ServerPlayer*p, targets){
		QStringList has,_choices = choices;
		_choices << "cancel";
		for (int i = 0; i < qMax(1,p->getHandcardNum()); i++){
			QString cn = room->askForChoice(source,"deru",_choices.join("+"),QVariant::fromValue(p));
			if(cn=="cancel") break;
			_choices.removeOne(cn);
			has << cn;
		}
		bool yes = false,no = false;
		QList<const Card*>hs = p->getHandcards();
		foreach(const Card*c, hs){
			if(c->isKindOf("BasicCard")){
				if(c->isKindOf("Slash")){
					if(has.contains("slash"))
						yes = true;
					else
						no = true;
				}else if(has.contains(c->objectName()))
					yes = true;
				else
					no = true;
			}
		}
		if(yes||!no)
			room->recover(source,RecoverStruct("deru",source));
		if(no){
			qShuffle(hs);
			foreach(const Card*c, hs){
				if(c->isKindOf("BasicCard")){
					source->obtainCard(c);
					break;
				}
			}
		}else if(source->getHandcardNum()<source->getHp())
			source->drawCards(source->getHp()-source->getHandcardNum(),"deru");
	}
}

class Deru : public ViewAsSkill
{
public:
	Deru() : ViewAsSkill("deru")
	{
	}

	bool viewFilter(const QList<const Card*> &, const Card*) const
	{
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		Card*dc = new DeruCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("DeruCard")<1;
	}
};

class Linjie : public TriggerSkill
{
public:
	Linjie() : TriggerSkill("linjie")
	{
		events << Damaged;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.from->canDiscard(damage.from,"h")){
				room->sendCompulsoryTriggerLog(player,this);
				const Card*dc = room->askForDiscard(damage.from,objectName(),1,1);
				if(dc&&Sanguosha->getCard(dc->getEffectiveId())->isKindOf("Slash")){
					player->drawCards(1,objectName());
				}
			}
		}
		return false;
	}
};

JiaweiCard::JiaweiCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool JiaweiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*dc = Sanguosha->cloneCard("duel");
	dc->setSkillName("jiawei");
	dc->addSubcards(subcards);
	dc->deleteLater();
	return targets.isEmpty()&&to_select->getMark("sgsjiaweiJink-Clear")>0
		&&dc->targetFilter(targets,to_select,Self);
}

const Card*JiaweiCard::validate(CardUseStruct &) const
{
	Card*dc = Sanguosha->cloneCard("duel");
	dc->setSkillName("jiawei");
	dc->addSubcards(subcards);
	dc->deleteLater();
	return dc;
}

class JiaweiVs : public ViewAsSkill
{
public:
	JiaweiVs() : ViewAsSkill("jiawei")
	{
		response_pattern = "@@jiawei";
        response_or_use = true;
	}

	bool viewFilter(const QList<const Card*> &, const Card*c) const
	{
		return !c->isEquipped();
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName("jiawei");
		dc->addSubcards(cards);
		dc->deleteLater();
		if(Self->isLocked(dc)) return nullptr;
		dc = new JiaweiCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}
};

class Jiawei : public TriggerSkill
{
public:
	Jiawei() : TriggerSkill("jiawei")
	{
		events << EventPhaseChanging << CardOffset << CardFinished;
		view_as_skill = new JiaweiVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive||player->getMark("sgsjiaweiDuel-Clear")<1) return false;
			foreach (ServerPlayer*p, room->getAllPlayers()){
				if(p->hasSkill(objectName())&&p->getHandcardNum()>0)
					room->askForUseCard(p,"@@jiawei","jiawei0");
			}
		}else if(event==CardOffset){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->isKindOf("Slash")){
				room->addPlayerMark(effect.to,"sgsjiaweiJink-Clear");
				foreach (ServerPlayer*p, room->getAlivePlayers())
					p->addMark("sgsjiaweiDuel-Clear");
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Duel")&&use.card->getSkillNames().contains(objectName())
				&&use.card->hasFlag("DamageDone")&&player->getMark("jiaweiUse_lun")<1){
				QList<ServerPlayer*>tos;
				tos << player << room->getCurrent();
				ServerPlayer*to = room->askForPlayerChosen(player,tos,objectName(),"jiawei1",true);
				if(to){
					player->addMark("jiaweiUse_lun");
					room->doAnimate(1,player->objectName(),to->objectName());
					if(to->getHandcardNum()<5&&to->getHandcardNum()<to->getMaxCards())
						to->drawCards(qMin(5-to->getHandcardNum(),to->getMaxCards()-to->getHandcardNum()),objectName());
				}
			}
		}
		return false;
	}
};

LunzhanCard::LunzhanCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool LunzhanCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*dc = Sanguosha->cloneCard("duel");
	dc->setSkillName("lunzhan");
	dc->addSubcards(subcards);
	dc->deleteLater();
	return to_select->getMark("lunzhanBan-Clear")<1
	&&dc->targetFilter(targets,to_select,Self);
}

const Card*LunzhanCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	Card*dc = Sanguosha->cloneCard("duel");
	dc->setSkillName("lunzhan");
	dc->addSubcards(subcards);
	dc->deleteLater();
	foreach (QString m, use.from->getMarkNames()){
		if(m.contains("&lunzhan+:+")&&use.from->getMark(m)>0){
			room->setPlayerMark(use.from,m,0);
			m.remove("-Clear");
			QStringList ms = m.split("+");
			ms << QString::number(subcardsLength());
			room->setPlayerMark(use.from,ms.join("+")+"-Clear",1);
			return dc;
		}
	}
	room->setPlayerMark(use.from,"&lunzhan+:+"+QString::number(subcardsLength())+"-Clear",1);
	return dc;
}

class LunzhanVs : public ViewAsSkill
{
public:
	LunzhanVs() : ViewAsSkill("lunzhan")
	{
        response_or_use = true;
	}

	bool viewFilter(const QList<const Card*> &cards, const Card*) const
	{
		return cards.length()<5;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		foreach (QString m, Self->getMarkNames()){
			if(m.contains("&lunzhan+:+")&&Self->getMark(m)>0&&m.contains(QString::number(cards.length()))){
				return nullptr;
			}
		}
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName("lunzhan");
		dc->addSubcards(cards);
		dc->deleteLater();
		if(Self->isLocked(dc)) return nullptr;
		dc = new LunzhanCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0;
	}
};

class Lunzhan : public TriggerSkill
{
public:
	Lunzhan() : TriggerSkill("lunzhan")
	{
		events << CardFinished;
		view_as_skill = new LunzhanVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer*p, use.to){
					player->addMark(p->objectName()+"lunzhanTo-Clear");
				}
			}
			if(use.card->isKindOf("Duel")&&use.card->getSkillNames().contains(objectName())){
				if(use.card->hasFlag("DamageDone_"+use.to.last()->objectName())&&use.to.length()==1){
					int n = player->getMark(use.to.last()->objectName()+"lunzhanTo-Clear");
					if(n>0&&player->askForSkillInvoke(this,n)){
						player->drawCards(n,objectName());
						room->addPlayerMark(use.to.last(),"lunzhanBan-Clear");
					}
				}
			}
		}
		return false;
	}
};

class OLJuejue : public TriggerSkill
{
public:
	OLJuejue() : TriggerSkill("oljuejue")
	{
		events << TargetSpecifying << PreCardUsed;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer*p, use.to)
					player->addMark(p->objectName()+"oljuejueTo-Clear");
				if(use.card->hasFlag("oljuejueBf")&&use.to.length()==1&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					int n = player->getMark(use.to.last()->objectName()+"oljuejueTo-Clear");
					room->askForDiscard(use.to.last(),objectName(),n,n,false,true);
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()>0&&use.to.last()!=player&&player->getHandcardNum()>0
			&&use.card->subcardsLength()>=player->getHandcardNum()&&player->getMark("oljuejueUse-Clear")<1){
				QList<int>ids = player->handCards();
				foreach (int id, use.card->getSubcards())
					ids.removeOne(id);
				if(ids.isEmpty()){
					player->addMark("oljuejueUse-Clear");
					room->setCardFlag(use.card,"oljuejueBf");
				}
			}
		}
		return false;
	}
};

LucunCard::LucunCard()
{
}

bool LucunCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool LucunCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool LucunCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*LucunCard::validateInResponse(ServerPlayer*user) const
{
	Room*room = user->getRoom();
	QStringList choices;
	foreach(QString pc, user_string.split("+")){
		if(user->getMark("lucun_guhuo_remove_"+pc+"_lun")>0) continue;
		Card*dc = Sanguosha->cloneCard(pc);
		if(dc){
			dc->deleteLater();
			if(dc->isKindOf("BasicCard")||dc->isNDTrick())
				choices << pc;
		}
	}
	QString cho = room->askForChoice(user,"lucun",choices.join("+"));
	room->addPlayerMark(user,"lucun_guhuo_remove_"+cho+"_lun");
	room->addPlayerMark(user,"lucunUse-Clear");
	Card*card = Sanguosha->cloneCard(cho);
	card->setSkillName("lucun");
	card->deleteLater();
	return card;
}

const Card*LucunCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	QStringList choices;
	foreach(QString pc, user_string.split("+")){
		if(use.from->getMark("lucun_guhuo_remove_"+pc+"_lun")>0) continue;
		Card*dc = Sanguosha->cloneCard(pc);
		if(dc){
			dc->deleteLater();
			if(dc->isKindOf("BasicCard")||dc->isNDTrick())
				choices << pc;
		}
	}
	QString cho = room->askForChoice(use.from,"lucun",choices.join("+"));
	room->addPlayerMark(use.from,"lucun_guhuo_remove_"+cho+"_lun");
	room->addPlayerMark(use.from,"lucunUse-Clear");
	Card*card = Sanguosha->cloneCard(cho);
	card->setSkillName("lucun");
	card->deleteLater();
	return card;
}

class LucunVs : public ZeroCardViewAsSkill
{
public:
	LucunVs() : ZeroCardViewAsSkill("lucun")
	{
        response_or_use = true;
	}

	const Card*viewAs() const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card*c = Self->getTag("lucun").value<const Card*>();
			if(c) pattern = c->objectName();
		}
		if(pattern.isEmpty()) return nullptr;
		LucunCard*card = new LucunCard;
		card->setUserString(pattern);
		return card;
	}
	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(player->getMark("lucunUse-Clear")>0||Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
		foreach(QString pc, pattern.split("+")){
			if(player->getMark("lucun_guhuo_remove_"+pc+"_lun")>0) continue;
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc){
				dc->deleteLater();
				if(dc->isKindOf("BasicCard")||dc->isNDTrick())
					return player->hasTurn();
			}
		}
		return pattern.contains("lucun");
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("lucunUse-Clear")<1;
	}
};

class Lucun : public TriggerSkill
{
public:
	Lucun() : TriggerSkill("lucun")
	{
		events << EventPhaseChanging << CardFinished;
		view_as_skill = new LucunVs;
	}
	QDialog*getDialog() const
	{
		return GuhuoDialog::getInstance(objectName(), true, true);
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasFlag("lucunUse")){
						p->setFlags("-lucunUse");
						QList<int>ids = p->getPile("lu_cun");
						if(ids.isEmpty()) continue;
						room->fillAG(ids,p);
						int id = room->askForAG(player,ids,false,objectName());
						room->clearAG(p);
						room->throwCard(id,objectName(),nullptr);
						QString cn;
						foreach(QString m, p->getMarkNames()){
							if(m.contains("lucun_guhuo_remove_")&&p->getMark(m)>0)
								cn = m;
						}
						if(Sanguosha->getCard(id)->sameNameWith(cn))
							id = 2;
						else
							id = 1;
						p->drawCards(id,objectName());
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())&&player->hasSkill(objectName())){
				ServerPlayer*cp = room->getCurrent();
				if(cp->isAlive()){
					const Card*dc = room->askForExchange(cp,objectName(),1,1,false,"lucun0:"+player->objectName());
					if(dc) player->addToPile("lu_cun",dc);
					player->setFlags("lucunUse");
				}
			}
		}
		return false;
	}
};

class Tuisheng : public TriggerSkill
{
public:
	Tuisheng() : TriggerSkill("tuisheng")
	{
		events << EventPhaseStart << Dying;
		frequency = Limited;
		limit_mark = "@tuisheng";
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!=Player::Start) return false;
		}else{
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.who!=player) return false;
		}
		if(player->getMark("@tuisheng")>0&&player->askForSkillInvoke(this,data)){
			player->peiyin(this);
			room->removePlayerMark(player,"@tuisheng");
			room->doSuperLightbox(player,objectName());
			foreach(QString m, player->getMarkNames()){
				if(m.contains("lucun_guhuo_remove_"))
					room->setPlayerMark(player,m,0);
			}
			if(room->askForChoice(player,objectName(),"tuisheng1+tuisheng2")=="tuisheng1"){
				player->addToPile("lu_cun",player->handCards());
			}else{
				ServerPlayer*cp = room->getCurrent();
				if(cp->isAlive())
					cp->obtainCard(dummyCard(player->getPile("lu_cun")));
				room->recover(player,RecoverStruct(objectName(),player));
			}
			room->recover(player,RecoverStruct(objectName(),player));
		}
		return false;
	}
};

class Pengbi : public TriggerSkill
{
public:
	Pengbi() : TriggerSkill("pengbi")
	{
		events << EventPhaseStart;
		waked_skills = "yintian,biri";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()!=Player::RoundStart) return false;
			player->addMark("pengbiRound");
		}
		if(player->getMark("pengbiRound")==1&&player->hasSkill(objectName())){
			room->sendCompulsoryTriggerLog(player,this);
			QString choice = room->askForChoice(player,objectName(),"yintian+biri");
			room->acquireSkill(player,choice);
			if(choice=="yintian") choice = "biri";
			else choice = "yintian";
			ServerPlayer*tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"pengbi0:"+choice,true);
			if(tp){
				room->doAnimate(1,player->objectName(),tp->objectName());
				room->acquireSkill(tp,choice);
			}
		}
		return false;
	}
};

DiciCard::DiciCard()
{
	mute = true;
}

bool DiciCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void DiciCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	source->peiyin("dici",1);
	foreach(ServerPlayer*p, targets){
		if(source->isDead()||p->isDead()) continue;
		const Card*dc = room->askForExchange(p,"dici",1,1,false,"dici0:"+source->objectName());
		if(dc){
			source->obtainCard(dc,false);
			room->recover(p,RecoverStruct("dici",source));
			room->setPlayerChained(p,false);
			foreach(ServerPlayer*q, room->getOtherPlayers(p)){
				if(p->isAdjacentTo(q)){
					room->setPlayerChained(q,true);
				}
			}
			room->setPlayerMark(p,"&dici+#"+source->objectName(),1);
		}
	}
}

class DiciVs : public ZeroCardViewAsSkill
{
public:
	DiciVs() : ZeroCardViewAsSkill("dici")
	{
	}

	const Card*viewAs() const
	{
		return new DiciCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("DiciCard")<1;
	}
};

class Dici : public TriggerSkill
{
public:
	Dici() : TriggerSkill("dici")
	{
		events << ChainStateChanged;
		view_as_skill = new DiciVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==ChainStateChanged){
			if (player->isChained()){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(player->getMark("&dici+#"+p->objectName())>0){
						p->peiyin(objectName(),2);
						room->setPlayerMark(player,"&dici+#"+p->objectName(),0);
						const Card*dc = room->askForExchange(player,"dici",1,1,false,"dici1:"+p->objectName(),true);
						if(dc)
							p->obtainCard(dc,false);
						else
							room->damage(DamageStruct(objectName(),p,player,1,DamageStruct::Thunder));
					}
				}
			}
		}
		return false;
	}
};

class Yintian : public TriggerSkill
{
public:
	Yintian() : TriggerSkill("yintian")
	{
		events << HpRecover << ConfirmDamage;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==ConfirmDamage){
			if (player->getMark("&yintian+#recover")>0&&player->hasSkill(objectName())){
				room->setPlayerMark(player,"&yintian+#recover",0);
				room->sendCompulsoryTriggerLog(player,this);
				player->damageRevises(data,1);
			}
		}else{
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if(p->getMark("yintian_recover-Clear")>0) continue;
				p->addMark("yintian_recover-Clear");
				if(p->hasSkill(objectName(),true))
					room->setPlayerMark(p,"&yintian+#recover",1);
				else
					p->addMark("yintian+#recover");
			}
		}
		return false;
	}
};

class Biri : public TriggerSkill
{
public:
	Biri() : TriggerSkill("biri")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place==Player::PlaceHand&&move.reason.m_reason!=CardMoveReason::S_REASON_DRAW){
				player->addMark("biriNum-Clear");
				if(player->getMark("biriNum-Clear")==1&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					player->drawCards(1,objectName());
				}
			}
		}
		return false;
	}
};

class Fulve : public TriggerSkill
{
public:
	Fulve() : TriggerSkill("fulve")
	{
		events << TargetSpecified << CardFinished << ConfirmDamage;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.to.length()==1&&player->hasSkill(objectName())
			&&(use.card->isKindOf("Slash")||(use.card->isNDTrick()&&use.card->isDamageCard()))){
				QStringList choices;
				if(player->getMark("fulve1-Clear")<1)
					choices << "fulve1";
				if(player->getMark("fulve2-Clear")<1)
					choices << "fulve2";
				if(choices.isEmpty()||!player->askForSkillInvoke(this,data)) return false;
				player->peiyin(this);
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
				room->setCardFlag(use.card,choice);
				player->addMark(choice+"-Clear");
				player->setTag("fulveChoice", choice);
			}
			if(use.card->hasFlag("fulve2")){
				foreach(ServerPlayer*p, use.to){
					if(p->getCardCount()>0){
						int id = room->askForCardChosen(player,p,"he",objectName());
						if(id>-1) room->obtainCard(player,id,false);
					}
				}
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			QString choice = player->getTag("fulveChoice").toString();
			if(!choice.isEmpty()&&use.card->hasFlag(choice)){
				if(choice=="fulve1") choice = "fulve2";
				else choice = "fulve1";
				foreach(ServerPlayer*p, use.to){
					if(p->isAlive())
						room->askForUseSlashTo(p,player,"fulve0:"+player->objectName(),false,false,false,nullptr,nullptr,choice);
				}
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("fulve1"))
				player->damageRevises(data,1);
		}
		return false;
	}
};

SibingCard::SibingCard()
{
}

bool SibingCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("sibing");
	dc->deleteLater();
	return dc->targetFilter(targets,to_select,Self);
}

void SibingCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("_sibing");
	dc->deleteLater();
	room->useCard(CardUseStruct(dc,source,targets));
}

class SibingVs : public ViewAsSkill
{
public:
	SibingVs() : ViewAsSkill("sibing")
	{
		response_pattern = "@@sibing";
	}

	bool viewFilter(const QList<const Card*> &cards, const Card*card) const
	{
		return cards.isEmpty()&&card->isBlack()&&!Self->isJilei(card);
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		Card*dc = new SibingCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return false;
	}
};

class Sibing : public TriggerSkill
{
public:
	Sibing() : TriggerSkill("sibing")
	{
		events << TargetSpecified << CardFinished;
		view_as_skill = new SibingVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()&&use.to.length()==1&&player->hasSkill(objectName())&&player->canDiscard(player,"he")){
				QString prompt = "sibing0:"+use.to.last()->objectName();
				room->setTag("sibingData",data);
				const Card*sc = room->askForDiscard(player,objectName(),998,1,true,true,prompt,".|red",objectName());
				if(sc){
					player->peiyin(this);
					prompt = "sibing1:"+use.card->objectName();
					if(room->askForDiscard(use.to.last(),objectName(),sc->subcardsLength(),sc->subcardsLength(),true,true,prompt,".|red")){
					}else{
						use.no_respond_list << use.to.last()->objectName();
						data.setValue(use);
					}
				}
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()){
				foreach(ServerPlayer*p, use.to){
					if(use.card->hasFlag("DamageDone_"+p->objectName())) continue;
					if(p->isAlive()&&p->hasSkill(objectName())&&p->canDiscard(p,"he")){
						room->askForUseCard(p,"@@sibing","sibing2");
					}
				}
			}
		}
		return false;
	}
};

class Liance : public TriggerSkill
{
public:
	Liance() : TriggerSkill("liance")
	{
		events << CardsMoveOneTime << DamageCaused;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from_places.contains(Player::PlaceHand)&&move.from==player&&player->hasSkill(objectName())){
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(p->getHandcardNum()<player->getHandcardNum())
						return false;
				}
				if(player->getMark("lianceUse-Clear")<1&&player->hasTurn()
				&&player->askForSkillInvoke(objectName()+"$-1",data)){
					player->addMark("lianceUse-Clear");
					room->addPlayerMark(player,"&liance+#sgs-Clear");
					int n = player->getMaxHp()-player->getHandcardNum();
					if(n>0) player->drawCards(n,objectName());
				}
			}
		}else{
			foreach(ServerPlayer*p, room->getAllPlayers()){
				if(p->getMark("&liance+#sgs-Clear")>0){
					room->setPlayerMark(p,"&liance+#sgs-Clear",0);
					room->sendCompulsoryTriggerLog(p,objectName());
					player->damageRevises(data,1);
				}
			}
		}
		return false;
	}
};

class Pingzhong : public TriggerSkill
{
public:
	Pingzhong() : TriggerSkill("pingzhong")
	{
		events << PreCardUsed << Damaged;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				int n = 1;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(p->isKongcheng()) n++;
				}
				player->addMark("pingzhongUse-Clear");
				if(n>=player->getMark("pingzhongUse-Clear")&&use.to.length()>0&&player->hasSkill(objectName())){
					QList<ServerPlayer*>tps = room->getCardTargets(player,use.card,use.to);
					tps = room->askForPlayersChosen(player,tps,objectName(),0,n,"pingzhong0:"+use.card->objectName()+":"+QString::number(n));
					if(tps.length()>0){
						player->peiyin(this);
						use.to << tps;
						room->sortByActionOrder(use.to);
						data.setValue(use);
					}
					
				}
			}
		}else if(event==Damaged&&player->hasSkill(objectName())){
			int n = 1;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if(p->isKongcheng()) n++;
			}
			room->sendCompulsoryTriggerLog(player,this);
			player->drawCards(n,objectName());
		}
		return false;
	}
};

SuyiCard::SuyiCard()
{
}

bool SuyiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void SuyiCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		QList<int>ids = room->getDrawPile();
		qShuffle(ids);
		foreach(int id, ids){
			const Card*c = Sanguosha->getCard(id);
			if(c->isKindOf("EquipCard")&&c->isAvailable(p)){
				room->useCard(CardUseStruct(c,p,p));
				Card*dc = Sanguosha->cloneCard("slash");
				dc->setSkillName("_suyi");
				if(source->canSlash(p,dc,false))
					room->useCard(CardUseStruct(dc,source,p));
				dc->deleteLater();
				break;
			}
		}
	}
}

class Suyi : public ViewAsSkill
{
public:
	Suyi() : ViewAsSkill("suyi")
	{
		response_pattern = "@@suyi";
	}
	bool viewFilter(const QList<const Card*> &, const Card*) const
	{
		return false;
	}

	const Card*viewAs(const QList<const Card*> &) const
	{
		return new SuyiCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("SuyiCard")<1;
	}
};

XieweiCard::XieweiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool XieweiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool XieweiCard::targetFixed() const
{
	return user_string.contains("jink");
}

bool XieweiCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*XieweiCard::validateInResponse(ServerPlayer*user) const
{
	Room*room = user->getRoom();
	user->addToPile("xw_er",subcards);
	if(user->isDead()) return nullptr;
	QString cho = room->askForChoice(user,"xiewei",user_string);
	Card*card = Sanguosha->cloneCard(cho);
	card->setSkillName("xiewei");
	card->deleteLater();
	return card;
}

const Card*XieweiCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	use.from->addToPile("xw_er",subcards);
	if(use.from->isDead()) return nullptr;
	QString cho = room->askForChoice(use.from,"xiewei",user_string);
	Card*card = Sanguosha->cloneCard(cho);
	card->setSkillName("xiewei");
	card->deleteLater();
	return card;
}

class Xiewei : public ViewAsSkill
{
public:
	Xiewei() : ViewAsSkill("xiewei")
	{
	}
	bool viewFilter(const QList<const Card*> &cards, const Card*card) const
	{
		return cards.length()<2&&!card->isEquipped();
	}
	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.length()<2) return nullptr;
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()) pattern = "slash";
		XieweiCard*card = new XieweiCard;
		card->setUserString(pattern);
		card->addSubcards(cards);
		return card;
	}
	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		return Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		&&(pattern.contains("slash")||pattern.contains("jink"))&&player->getHandcardNum()>1;
	}
	bool isEnabledAtPlay(const Player*player) const
	{
		Card*card = Sanguosha->cloneCard("slash");
		card->setSkillName("xiewei");
		card->deleteLater();
		return card->isAvailable(player)&&player->getHandcardNum()>1;
	}
};

YouqueCard::YouqueCard()
{
	will_throw = false;
}

bool YouqueCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty()&&to_select!=Self&&Self->canPindian(to_select);
}

void YouqueCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		if(source->canPindian(p)){
			PindianStruct*pd = source->PinDian(p,"youque",this);
			Card*dc = Sanguosha->cloneCard("slash");
			dc->setSkillName("_youque");
			if(pd->success){
				foreach(const Card*h, p->getHandcards()){
					if(h->getNumber()>pd->to_card->getNumber()&&p->canDiscard(p,h->getId()))
						dc->addSubcard(h);
				}
				room->throwCard(dc,"youque",p);
				dc->clearSubcards();
				if(p->canSlash(source,dc,false))
					room->useCard(CardUseStruct(dc,p,source));
			}else if(pd->from_number<pd->to_number){
				foreach(const Card*h, source->getHandcards()){
					if(h->getNumber()>pd->from_card->getNumber()&&source->canDiscard(source,h->getId()))
						dc->addSubcard(h);
				}
				room->throwCard(dc,"youque",source);
				dc->clearSubcards();
				if(source->canSlash(p,dc,false))
					room->useCard(CardUseStruct(dc,source,p));
			}
			dc->deleteLater();
		}
	}
}

class YouqueVs : public ViewAsSkill
{
public:
	YouqueVs() : ViewAsSkill("youque")
	{
		expand_pile = "xw_er";
	}

	bool viewFilter(const QList<const Card*> &cards, const Card*card) const
	{
		int n = 13;
		foreach(int id, Self->getPile("xw_er")){
			const Card*c = Sanguosha->getCard(id);
			if(c->getNumber()<n) n = c->getNumber();
		}
		return cards.isEmpty()&&card->getNumber()<=n
		&&Self->getPileName(card->getEffectiveId())=="xw_er";
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		Card*dc = new YouqueCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getPile("xw_er").length()>0&&player->getMark("youqueBan-Clear")<1;
	}
};

class Youque : public TriggerSkill
{
public:
	Youque() : TriggerSkill("youque")
	{
		events << Damage;
		view_as_skill = new YouqueVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&damage.card->getSkillNames().contains("youque")){
				if(player->hasSkill(objectName(),true)){
					player->drawCards(2,objectName());
					bool has = false;
					foreach(int id, player->getPile("xw_er")){
						if(Sanguosha->getCard(id)->getSuit()==0) has = true;
					}
					if(!has)
						room->addPlayerMark(player,"youqueBan-Clear");
				}
				if(damage.to->hasSkill(objectName(),true)){
					damage.to->drawCards(2,objectName());
					bool has = false;
					foreach(int id, damage.to->getPile("xw_er")){
						if(Sanguosha->getCard(id)->getSuit()==0) has = true;
					}
					if(!has)
						room->addPlayerMark(damage.to,"youqueBan-Clear");
				}
			}
		}
		return false;
	}
};

class Kuangxin : public TriggerSkill
{
public:
	Kuangxin() : TriggerSkill("kuangxin")
	{
		events << EventPhaseStart;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if (player->getPhase()==Player::Play&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				QStringList choices;
				for (int i = 0; i < player->getHp(); i++) {
					choices << QString("kuangxin0=%1").arg(i);
				}
				QString choice = room->askForChoice(player,objectName(),choices.join("+"));
				int n = choice.split("=").last().toInt();
				room->loseHp(player,n,true,player,objectName());
				if(player->isDead()) return false;
				n = player->getLostHp()+1;
				const Card*dc = room->askForExchange(player,objectName(),n,n,false,"kuangxin1:"+QString::number(n));
				if(dc){
					room->showCard(player,dc->getSubcards());
					room->setPlayerMark(player,"kuangxinShow",dc->subcardsLength());
				}
			}else if (player->getPhase()==Player::Start){
				if(player->hasSkill(objectName(),true))
					room->setPlayerMark(player,"&kuangxin",player->getHp());
				player->setMark("&kuangxin",player->getHp());
			}else if (player->getPhase()==Player::Finish){
				int n = player->getMark("&kuangxin");
				if(n>0&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					room->setPlayerProperty(player,"hp",n);
				}
			}
		}
		return false;
	}
};

class Leishi : public TriggerSkill
{
public:
	Leishi() : TriggerSkill("leishi")
	{
		events << CardFinished << StartJudge << ShowCards;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->hasFlag("leishiBf")
			&&player->getMark("leishiUse-Clear")<player->getMark("kuangxinShow")
			&&player->hasSkill(objectName())&&player->askForSkillInvoke(this,data)){
				player->addMark("leishiUse-Clear");
				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.pattern = ".|"+use.card->getSuitString();
				room->judge(judge);
				if(player->isDead()) return false;
				if(!room->getCardOwner(judge.card->getEffectiveId()))
					player->obtainCard(judge.card);
				if(judge.isGood()){
					ServerPlayer*tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"leishi0");
					if(tp) room->damage(DamageStruct(objectName(),player,tp,1,DamageStruct::Thunder));
				}else{
					const Card*c = room->askForCardShow(player,player,objectName());
					if(c){
						room->setCardTip(c->getId(),"leishi-Clear");
						room->showCard(player,c->getId());
					}
				}
			}
		}else if(event==StartJudge){
			JudgeStruct*judge = data.value<JudgeStruct*>();
			foreach(const Card*c, player->getHandcards()){
				if(c->hasTip("leishi-Clear",false)){
					judge->card = c;
					room->moveCardTo(c, nullptr, judge->who, Player::PlaceJudge,
						CardMoveReason(CardMoveReason::S_REASON_JUDGE, judge->who->objectName(), "", judge->reason), true);
					judge->updateResult();
					room->setTag("SkipGameRule", (int)event);
					break;
				}
			}
		}else if(event==ShowCards){
			QList<int> ids = ListS2I(data.toString().split(":").first().split("+"));
			foreach(int id, ids){
				room->setCardFlag(id,"leishiBf");
			}
		}
		return false;
	}
};

class Duoqi : public TriggerSkill
{
public:
	Duoqi() : TriggerSkill("duoqi")
	{
		events << TurnStart << Damage << AfterDrawNCards;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TurnStart){
			if (player->getMark("TurnLengthCount")<1){
				if(room->getTag("Global_ExtraTurn"+player->objectName()).toBool()) return false;
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						room->setPlayerMark(p,"duoqiBf",1);
						p->changePhase(p->getPhase(),Player::Play);
						p->setPhase(Player::NotActive);
						room->broadcastProperty(p, "phase");
						room->setPlayerMark(p,"duoqiBf",0);
					}
				}
			}
		}else if(event==AfterDrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards") return false;
			player->setTag("duoqiIds", ListI2V(draw.card_ids));
			foreach(int id, draw.card_ids){
				room->setCardTip(id,"duo_qi");
			}
		}else if(player->hasSkill(objectName())){
			DamageStruct damage = data.value<DamageStruct>();
			player->addMark(damage.to->objectName()+"duoqiDamage-Clear");
			if(player->getMark(damage.to->objectName()+"duoqiDamage-Clear")==1){
				QList<int> hands = ListV2I(damage.to->getTag("duoqiIds").toList());
				foreach(int id, hands){
					if(room->getCardOwner(id)==player)
						hands.removeOne(id);
				}
				if(hands.length()>0){
					qShuffle(hands);
					room->obtainCard(player,hands.first(),false);
					if(player->handCards().contains(hands.first()))
						room->setCardTip(hands.first(),"duo_qi");
				}
			}
		}
		return false;
	}
};

KuanmoCard::KuanmoCard()
{
	will_throw = false;
}

bool KuanmoCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void KuanmoCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	room->setPlayerMark(source,"&olmorumo",1);
	room->addPlayerMark(source,"kuanmo_rumo");
	foreach(ServerPlayer*p, targets){
		room->setPlayerMark(source,"&kuan_mo+#"+p->objectName(),1);
		room->setPlayerMark(p,"&kuan_mo+#"+source->objectName(),1);
	}
}

class KuanmoVs : public ZeroCardViewAsSkill
{
public:
	KuanmoVs() : ZeroCardViewAsSkill("kuanmo")
	{
		response_pattern = "@@kuanmo!";
	}

	const Card*viewAs() const
	{
		return new KuanmoCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("kuanmo_rumo")<1;
	}
};

class Kuanmo : public TriggerSkill
{
public:
	Kuanmo() : TriggerSkill("kuanmo")
	{
		events << ConfirmDamage << Death;
		view_as_skill = new KuanmoVs;
		waked_skills = "#olmorumo";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(player->getMark("&kuan_mo+#"+damage.to->objectName())>0){
				player->addMark("kuan_mo-Clear");
				if(player->getMark("kuan_mo-Clear")==1)
					player->damageRevises(data,1);
			}
		}else{
			DeathStruct death = data.value<DeathStruct>();
			if(player->getMark("&kuan_mo+#"+death.who->objectName())>0){
				room->setPlayerMark(player,"&kuan_mo+#"+death.who->objectName(),0);
				QList<int> hands = ListV2I(death.who->getTag("duoqiIds").toList());
				foreach(int id, hands){
					if(room->getCardOwner(id)==player)
						hands.removeOne(id);
				}
				if(!hands.isEmpty()&&death.damage&&death.damage->from==player){
					player->obtainCard(dummyCard(hands),false);
					foreach(int id, player->handCards()){
						if(hands.contains(id))
							room->setCardTip(id,"duo_qi");
					}
				}
				if(player->hasSkill(objectName()))
					room->askForUseCard(player,"@@kuanmo!","kuanmo0");
			}
		}
		return false;
	}
};

GangqianCard::GangqianCard()
{
}

bool GangqianCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*dc = Sanguosha->cloneCard("fire_slash");
	dc->setSkillName("gangqian");
	dc->addSubcard(this);
	dc->deleteLater();
	return Self->isAdjacentTo(to_select)&&dc->targetFilter(targets,to_select,Self);
}

void GangqianCard::onUse(Room*room, CardUseStruct &card_use) const
{
	Card*dc = Sanguosha->cloneCard("fire_slash");
	dc->setSkillName("gangqian");
	dc->addSubcard(this);
	card_use.card = dc;
	dc->onUse(room, card_use);
	dc->deleteLater();
}

class GangqianVs : public OneCardViewAsSkill
{
public:
	GangqianVs() : OneCardViewAsSkill("gangqian")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		if(Self->getMark("gangqianQi")>0)
			return to_select->isKindOf("EquipCard");
		return to_select->isKindOf("TrickCard");
	}

	const Card*viewAs(const Card*original) const
	{
		if(Self->getMark("gangqianQi")>0){
			Card*dc = new GangqianCard;
			dc->addSubcard(original);
			return dc;
		}
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName("gangqian");
		dc->addSubcard(original);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0;
	}
};

class Gangqian : public TriggerSkill
{
public:
	Gangqian() : TriggerSkill("gangqian")
	{
		events << PreCardUsed;
		view_as_skill = new GangqianVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				if(use.card->hasTip("duo_qi"))
					room->setPlayerMark(player,"gangqianQi",1);
				else
					room->setPlayerMark(player,"gangqianQi",0);
			}
		}
		return false;
	}
};

HuanhuoCard::HuanhuoCard()
{
}

bool HuanhuoCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.length()<subcardsLength()&&to_select!=Self;
}

bool HuanhuoCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return targets.length()==subcardsLength();
}

void HuanhuoCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		room->setPlayerMark(p,"&huanhuo+#"+source->objectName()+"-SelfPlayClear",1);
	}
}

class HuanhuoVs : public ViewAsSkill
{
public:
	HuanhuoVs() : ViewAsSkill("huanhuo")
	{
		response_pattern = "@@huanhuo";
	}

	bool viewFilter(const QList<const Card*> &cards, const Card*card) const
	{
		return cards.length()<2&&!Self->isJilei(card);
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		Card*dc = new HuanhuoCard;
		dc->addSubcards(cards);
		return dc;
	}
};

class Huanhuo : public TriggerSkill
{
public:
	Huanhuo() : TriggerSkill("huanhuo")
	{
		events << RoundStart << EventPhaseProceeding;
		view_as_skill = new HuanhuoVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==RoundStart){
			if(player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				player->drawCards(2,objectName());
				room->askForUseCard(player,"@@huanhuo","huanhuo0");
			}
		}else if(event==EventPhaseProceeding){
			if(player->getPhase()!=Player::Play) return false;
			foreach(ServerPlayer*p, room->getOtherPlayers(player)){
				if(player->getMark("&huanhuo+#"+p->objectName()+"-SelfPlayClear")>0){
					int n = 0;
					QList<const Card*>hs = player->getHandcards();
					while(player->isAlive()&&n<2&&!hs.isEmpty()){
						qShuffle(hs);
						bool has = false;
						foreach(const Card*h, hs){
							if(h->isAvailable(player)){
								if(room->askForUseCard(player,h->toString(),"huanhuo1:"+h->objectName())){
									hs = player->getHandcards();
									qShuffle(hs);
									foreach(const Card*h, hs){
										if(player->canDiscard(player,h->getId())){
											room->throwCard(h,objectName(),player);
											break;
										}
									}
									n++;
									has = true;
								}
								break;
							}
						}
						if(!has){
							player->setFlags("Global_PlayPhaseTerminated");
							break;
						}
						hs = player->getHandcards();
					}
				}
			}
		}
		return false;
	}
};

OLQingshiCard::OLQingshiCard()
{
}

bool OLQingshiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	return targets.isEmpty()&&to_select->hasFlag("olqingshiTo");
}

void OLQingshiCard::use(Room*room, ServerPlayer*, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		room->setPlayerFlag(p,"olqingshiToBf");
	}
}

class OLQingshiVs : public OneCardViewAsSkill
{
public:
	OLQingshiVs() : OneCardViewAsSkill("olqingshi")
	{
		response_pattern = "@@olqingshi";
	}

	bool viewFilter(const Card*to_select) const
	{
		return Self->canDiscard(Self,to_select->getId());
	}

	const Card*viewAs(const Card*original) const
	{
		Card*dc = new OLQingshiCard;
		dc->addSubcard(original);
		return dc;
	}
};

class OLQingshi : public TriggerSkill
{
public:
	OLQingshi() : TriggerSkill("olqingshi")
	{
		events << EventPhaseStart << TargetSpecifying << Damage << CardsMoveOneTime;
		view_as_skill = new OLQingshiVs;
		waked_skills = "#olmorumo";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if (player->getPhase()==Player::Start&&player->hasSkill(objectName())){
				if(player->getMark("&olmorumo")<1&&player->askForSkillInvoke(this)){
					player->peiyin(this);
					player->addMark("olqingshi_rumo");
					room->setPlayerMark(player,"&olmorumo",1);
					foreach(ServerPlayer*p, room->getAllPlayers()){
						foreach(int id, Sanguosha->getRandomCards()){
							if(room->getCardOwner(id)) continue;
							const Card*c = Sanguosha->getCard(id);
							if(c->isDamageCard()&&c->isSingleTargetCard()){
								p->setMark("olqingshiId",id+1);
								p->obtainCard(c);
								if(p->handCards().contains(id)){
									room->setCardTip(id,"olqingshi");
									room->setCardFlag(id,"olqingshiBf");
								}
								break;
							}
						}
					}
				}
				if(player->getMark("olqingshi_rumo")>0){
					foreach(ServerPlayer*p, room->getAlivePlayers()){
						if(p->handCards().contains(p->getMark("olqingshiId")-1))
							return false;
					}
					room->sendCompulsoryTriggerLog(player,this);
					foreach(ServerPlayer*p, room->getAllPlayers()){
						int id = p->getMark("olqingshiId")-1;
						room->obtainCard(p,id);
						if(p->handCards().contains(id)){
							room->setCardTip(id,"olqingshi");
							room->setCardFlag(id,"olqingshiBf");
						}
					}
				}
			}
		}else if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card){
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(p->getMark("olqingshiId")-1==damage.card->getEffectiveId()){
						foreach(ServerPlayer*q, room->getAllPlayers()){
							if(q->hasSkill(objectName())){
								room->sendCompulsoryTriggerLog(q,objectName());
								q->drawCards(1,objectName());
							}
						}
						break;
					}
				}
			}
		}else if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()==1&&use.card->getEffectiveId()==player->getMark("olqingshiId")-1){
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->hasSkill(objectName())){
						room->setCardFlag(use.card,"tunan_distance");
						QList<ServerPlayer*>tps = room->getCardTargets(player,use.card,use.to);
						room->setCardFlag(use.card,"-tunan_distance");
						if(tps.isEmpty()||p->getCardCount()<1) continue;
						foreach(ServerPlayer*q, tps)
							room->setPlayerFlag(q,"olqingshiTo");
						p->setTag("olqingshiUse", data);
						room->askForUseCard(p,"@@olqingshi","olqingshi0:"+use.card->objectName());
						foreach(ServerPlayer*q, tps){
							room->setPlayerFlag(q,"-olqingshiTo");
							if(q->hasFlag("olqingshiToBf")){
								room->setPlayerFlag(q,"-olqingshiToBf");
								use.to.clear();
								use.to << q;
								data.setValue(use);
							}
						}
					}
				}
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if(move.to_place==Player::DiscardPile&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE
				&&move.from&&player->hasSkill(objectName())){
				foreach(int id, move.card_ids){
					if(Sanguosha->getCard(id)->hasFlag("olqingshiBf")&&!room->getCardOwner(id)){
						room->sendCompulsoryTriggerLog(player,objectName());
						room->obtainCard(player,id);
					}
				}
			}
		}
		return false;
	}
};

MiluoCard::MiluoCard()
{
	will_throw = false;
}

bool MiluoCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.length()<subcardsLength()&&to_select!=Self;
}

bool MiluoCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return targets.length()==subcardsLength();
}

void MiluoCard::onUse(Room*room, CardUseStruct &card_use) const
{
	room->setTag("miluoUse",QVariant::fromValue(card_use));
	SkillCard::onUse(room, card_use);
}

void MiluoCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	room->showCard(source,subcards);
	int i = 0;
	CardUseStruct use = room->getTag("miluoUse").value<CardUseStruct>();
	foreach(ServerPlayer*p, use.to){
		p->addMark(source->objectName()+"miluoBf_lun");
		room->giveCard(source,p,Sanguosha->getCard(subcards[i]),"miluo",true);
		if(p->handCards().contains(subcards[i]))
			room->setCardTip(subcards[i],"miluo_lun");
		i++;
	}
}

class MiluoVs : public ViewAsSkill
{
public:
	MiluoVs() : ViewAsSkill("miluo")
	{
		response_pattern = "@@miluo";
	}

	bool viewFilter(const QList<const Card*> &cards, const Card*) const
	{
		return cards.length()<2;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		Card*dc = new MiluoCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("MiluoCard")<1;
	}
};

class Miluo : public TriggerSkill
{
public:
	Miluo() : TriggerSkill("miluo")
	{
		events << RoundEnd;
		view_as_skill = new MiluoVs;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==RoundEnd){
			QStringList choices;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if(p->getMark(player->objectName()+"miluoBf_lun")>0){
					foreach(const Card*h, p->getHandcards()){
						if(h->hasTip("miluo")){
							choices << "miluo2="+p->objectName();
							break;
						}
					}
					if(choices.isEmpty()||!choices.last().endsWith(p->objectName()))
						choices << "miluo1="+p->objectName();
				}
			}
			if(choices.length()>0){
				choices << "cancel";
				QString choice = room->askForChoice(player,objectName(),choices.join("+"));
				if(choice!="cancel"){
					player->skillInvoked(this);
					foreach(ServerPlayer*p, room->getAlivePlayers()){
						if(choice.endsWith(p->objectName())){
							if(choice.startsWith("miluo1")){
								room->loseHp(p,1,true,player,objectName());
							}else
								room->recover(p,RecoverStruct(objectName(),player));
						}
					}
				}
			}
		}
		return false;
	}
};

OLJueyanCard::OLJueyanCard()
{
}

bool OLJueyanCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string);
	if(card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool OLJueyanCard::targetFixed() const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string);
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool OLJueyanCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string);
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*OLJueyanCard::validateInResponse(ServerPlayer*user) const
{
	Room*room = user->getRoom();
	room->addPlayerMark(user,"oljueyanUse_lun");
	room->showCard(user,subcards);
	Card*card = Sanguosha->cloneCard(user_string);
	card->setSkillName("oljueyan");
	card->deleteLater();
	return card;
}

const Card*OLJueyanCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	room->addPlayerMark(use.from,"oljueyanUse_lun");
	room->showCard(use.from,subcards);
	Card*card = Sanguosha->cloneCard(user_string);
	card->setSkillName("oljueyan");
	card->deleteLater();
	return card;
}

class OLJueyanVs : public OneCardViewAsSkill
{
public:
	OLJueyanVs() : OneCardViewAsSkill("oljueyan")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		if(to_select->isEquipped()) return false;
		if(to_select->isNDTrick()||(to_select->isKindOf("BasicCard")&&to_select->getSuit()==2)){
			QString pattern = Sanguosha->getCurrentCardUsePattern();
			if(pattern.isEmpty()) return true;
			foreach(QString pc, pattern.split("+")){
				if(to_select->sameNameWith(pc))
					return true;
			}
		}
		return false;
	}

	const Card*viewAs(const Card*original) const
	{
		OLJueyanCard*dc = new OLJueyanCard;
		dc->setUserString(original->objectName());
		dc->addSubcard(original);
		return dc;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(player->getMark("oljueyanUse_lun")>0) return false;
		foreach(QString pc, pattern.split("+")){
			foreach(const Card*h, player->getHandcards()){
				if(h->sameNameWith(pc)&&(h->isNDTrick()||(h->isKindOf("BasicCard")&&h->getSuit()==2)))
					return Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE;
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("oljueyanUse_lun")<1;
	}
};

class OLJueyan : public TriggerSkill
{
public:
	OLJueyan() : TriggerSkill("oljueyan")
	{
		events << ShowCards;
		view_as_skill = new OLJueyanVs;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==ShowCards){
			QList<int> ids = ListS2I(data.toString().split(":").first().split("+"));
			foreach(int id, ids){
				if(player->handCards().contains(id)){
					const Card*c = Sanguosha->getCard(id);
					player->addMark(c->getSuitString()+"oljueyanShow_lun");
					if(player->getMark(c->getSuitString()+"oljueyanShow_lun")==1){
						room->sendCompulsoryTriggerLog(player,objectName());
						player->drawCards(1,objectName());
					}
				}
			}
		}
		return false;
	}
};

class Zhuole : public TriggerSkill
{
public:
	Zhuole() : TriggerSkill("zhuole")
	{
		events << TargetConfirmed;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.contains(player)){
				if(use.card->isDamageCard()){
					if(player->getMark("drank")<1) return false;
					foreach(ServerPlayer*p, room->getAllPlayers()){
						if(p->hasSkill(objectName())&&p->askForSkillInvoke(objectName()+"$-1",player)){
							Card*dc = Sanguosha->cloneCard("peach");
							dc->setSkillName("_zhuole");
							if(dc->isAvailable(player))
								room->useCard(CardUseStruct(dc,player));
							dc->deleteLater();
						}
					}
				}else if(!use.card->getSkillNames().contains(objectName())&&player->hasSkill(objectName())){
					Card*dc = Sanguosha->cloneCard("analeptic");
					dc->setSkillName("_zhuole");
					QList<ServerPlayer*>aps;
					foreach(ServerPlayer*p, room->getAlivePlayers()){
						if(dc->isAvailable(p)) aps << p;
					}
					ServerPlayer*tp = room->askForPlayerChosen(player,aps,objectName()+"$-1","zhuole0",true,true);
					if(tp){
						room->useCard(CardUseStruct(dc,tp));
					}
					dc->deleteLater();
				}
			}
		}
		return false;
	}
};

class Qiaowu : public TriggerSkill
{
public:
	Qiaowu() : TriggerSkill("qiaowu")
	{
		events << CardFinished;
		change_skill = true;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")||use.card->isKindOf("Jink")){
				if(player->getMark("qiaowuUse-Clear")>0||!player->hasTurn()) return false;
				QList<ServerPlayer*>aps;
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->getMark("drank")>0) aps << p;
				}
				if(aps.isEmpty()) return false;
				if(player->askForSkillInvoke(objectName()+"$-1",data)){
					if(player->getChangeSkillState(objectName())==1){
						room->setChangeSkillState(player,objectName(),2);
						room->drawCards(aps,1,objectName());
					}else{
						room->setChangeSkillState(player,objectName(),1);
						foreach(ServerPlayer*p, aps){
							if(p->isDead()) continue;
							room->askForUseCard(p,"Slash","qiaowu0");
						}
					}
				}
			}
		}
		return false;
	}
};

class Jiyun : public TriggerSkill
{
public:
	Jiyun() : TriggerSkill("jiyun")
	{
		events << CardFinished << GameStart << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("JiyunBf")){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(room->getCardOwner(use.card->getEffectiveId())) break;
					if(p->hasSkill(objectName())&&p->getPile("jy_liang").length()<3){
						room->sendCompulsoryTriggerLog(p,objectName());
						p->addToPile("jy_liang",use.card);
					}
				}
			}
		}else if(event==GameStart){
			if(player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				QList<int>ids,ts;
				foreach(int id, room->getDrawPile()){
                    const Card*c = Sanguosha->getCard(id);
					if(ts.contains(c->getTypeId())) continue;
					ts.append(c->getTypeId());
					ids << id;
				}
				player->addToPile("jy_liang",ids);
			}
		}else if(event==EventPhaseStart){
			if(player->getPhase()==Player::Play&&player->hasSkill(objectName())){
				int n = qMax(1,player->getPile("jy_liang").length());
				QList<ServerPlayer*>aps = room->getAlivePlayers();
				aps = room->askForPlayersChosen(player,aps,objectName()+"$-1",0,n,"jiyun",true,true);
				foreach(ServerPlayer*p, aps){
					n = room->drawCardsList(p,1,objectName()).last();
					if(p->hasCard(n)){
						room->setCardFlag(n,"JiyunBf");
						room->setCardTip(n,objectName());
					}
				}
			}
		}
		return false;
	}
};

OLShuliangCard::OLShuliangCard()
{
	will_throw = false;
}

bool OLShuliangCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
	return targets.length()<subcardsLength();
}

bool OLShuliangCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return targets.length()==subcardsLength();
}

void OLShuliangCard::onUse(Room*room, CardUseStruct &card_use) const
{
	room->setTag("olshuliangUse",QVariant::fromValue(card_use));
	SkillCard::onUse(room, card_use);
}

void OLShuliangCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	int i = 0;
	CardUseStruct use = room->getTag("olshuliangUse").value<CardUseStruct>();
	foreach(ServerPlayer*p, use.to){
		room->giveCard(source,p,Sanguosha->getCard(subcards[i]),"olshuliang",true);
		i++;
	}
}

class OLShuliangVs : public ViewAsSkill
{
public:
	OLShuliangVs() : ViewAsSkill("olshuliang")
	{
		expand_pile = "jy_liang";
	}

	bool viewFilter(const QList<const Card*> &cards, const Card*c) const
	{
		if(Self->getPile(expand_pile).contains(c->getId())){
			QString pattern = Sanguosha->getCurrentCardUsePattern();
			if(pattern.isEmpty()) return cards.isEmpty()&&c->isAvailable(Self);
			if(pattern=="@@olshuliang") return true;
			foreach(QString pc, pattern.split("+")){
				if(c->sameNameWith(pc))
					return cards.isEmpty();
			}
		}
		return false;
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		if(Sanguosha->getCurrentCardUsePattern()=="@@olshuliang"){
			Card*dc = new OLShuliangCard;
			dc->addSubcards(cards);
			return dc;
		}
		return cards[0];
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		foreach(QString pc, pattern.split("+")){
			foreach(int id, player->getPile(expand_pile)){
				if(Sanguosha->getCard(id)->sameNameWith(pc))
					return player->getMark("olshuliangBan-Clear")<1&&player->hasTurn();
			}
		}
		return pattern=="@@olshuliang";
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		foreach(int id, player->getPile(expand_pile)){
			if(Sanguosha->getCard(id)->isAvailable(player))
				return player->getMark("olshuliangBan-Clear")<1&&player->hasTurn();
		}
		return false;
	}
};

class OLShuliang : public TriggerSkill
{
public:
	OLShuliang() : TriggerSkill("olshuliang")
	{
		events << PreCardUsed << EventPhaseStart;
		view_as_skill = new OLShuliangVs;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(player->getPile("jy_liang").contains(use.card->getId())){
				room->addPlayerMark(player,"olshuliangBan-Clear");
				player->peiyin(objectName());
			}
		}else if(player->getPhase()==Player::Finish){
			QList<int>ids = player->getPile("jy_liang");
			if(ids.isEmpty()||!player->askForSkillInvoke(objectName())) return false;
			player->peiyin(objectName());
			player->assignmentCards(ids,"olshuliang|olshuliang0",room->getAlivePlayers(),-1,1);
		}
		return false;
	}
};

class Tingji : public TriggerSkill
{
public:
	Tingji() : TriggerSkill("tingji")
	{
		events << CardUsed << HpChanged << MaxHpChanged << EventPhaseChanging << EventLoseSkill << EventAcquireSkill;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasFlag("CurrentPlayer")&&p->hasSkill(objectName())){
						QStringList has = p->getTag("tingjiRange").toStringList();
						if(has.isEmpty()) continue;
						use.no_respond_list << has;
						room->sendCompulsoryTriggerLog(p,this);
						data.setValue(use);
					}
				}
			}
		}else if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive){
				QStringList has = player->getTag("tingjiRange").toStringList();
				player->removeTag("tingjiRange");
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(has.contains(p->objectName()))
						room->removeAttackRangePair(player,p);
				}
			}else if(change.to==Player::RoundStart&&player->hasSkill(objectName())){
				QStringList has;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(player==p||p->isWounded()) continue;
					room->insertAttackRangePair(player,p);
					has << p->objectName();
				}
				player->setTag("tingjiRange", has);
			}
		}else if(event==EventLoseSkill){
			if(data.toString()==objectName()){
				QStringList has = player->getTag("tingjiRange").toStringList();
				player->removeTag("tingjiRange");
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(has.contains(p->objectName()))
						room->removeAttackRangePair(player,p);
				}
			}
		}else if(event==EventAcquireSkill){
			if(data.toString()==objectName()&&player->hasSkill(objectName())){
				QStringList has;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(player==p||p->isWounded()) continue;
					room->insertAttackRangePair(player,p);
					has << p->objectName();
				}
				player->setTag("tingjiRange", has);
			}
		}else{
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if(player!=p&&p->hasFlag("CurrentPlayer")){
					QStringList has = p->getTag("tingjiRange").toStringList();
					if(has.contains(player->objectName())){
						if(player->isWounded()){
							has.removeOne(player->objectName());
							room->removeAttackRangePair(p,player);
						}
					}else if(!player->isWounded()&&p->hasSkill(objectName())){
						has.append(player->objectName());
						room->insertAttackRangePair(p,player);
					}
					p->setTag("tingjiRange", has);
				}
			}
		}
		return false;
	}
};

class Xuanliu : public TriggerSkill
{
public:
	Xuanliu() : TriggerSkill("xuanliu")
	{
		events << EventPhaseEnd << CardUsed;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				player->addMark(use.card->objectName()+"xuanliuUse-PlayClear");
			}
		}else if(event==EventPhaseEnd){
			if(player->getPhase()==Player::Play&&player->getMark("damage_point_play_phase")>0&&player->hasSkill(objectName())){
				ServerPlayer*tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName()+"$-1","xuanliu0",true,true);
				while(tp&&tp->isAlive()){
					tp->drawCards(1,objectName());
					const Card*c = room->askForUseCard(tp,".|.|.|hand","xuanliu1");
					if(c==nullptr||player->getMark(c->objectName()+"xuanliuUse-PlayClear")<1)
						break;
				}
			}
		}
		return false;
	}
};

class Shaying : public TriggerSkill
{
public:
	Shaying() : TriggerSkill("shaying")
	{
		events << EventPhaseStart << RoundEnd;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
    bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==RoundEnd){
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if(player->getMark("&shaying+#"+p->objectName()+"_lun")>0){
					room->removeAttackRangePair(player,p);
					room->removeAttackRangePair(p,player);
				}
			}
		}else if(event==EventPhaseStart){
			if(player->getPhase()==Player::Play&&player->hasSkill(objectName())){
				QList<ServerPlayer*>tps;
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->isAllNude()) continue;
					tps << p;
				}
				ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName()+"$-1","shaying0",true,true);
				if(tp){
					int id = room->askForCardChosen(player,tp,"hej",objectName());
					if(id>=0){
						room->obtainCard(player,id,false);
						if(tp->inMyAttackRange(player)){
                            room->askForUseSlashTo(tp,player,"shaying1:"+player->objectName());
							return false;
						}
						room->setPlayerMark(tp,"&shaying+#"+player->objectName()+"_lun",1);
						room->insertAttackRangePair(player,tp);
						room->insertAttackRangePair(tp,player);
					}
				}
			}
		}
		return false;
	}
};

class Dongxin : public TriggerSkill
{
public:
	Dongxin() : TriggerSkill("dongxin")
	{
		events << Damage << Damaged;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(player->getMark(QString("dongxinUse%1-Clear").arg(event))>0) return false;
		DamageStruct damage = data.value<DamageStruct>();
		int x = damage.to->getLostHp()+1;
		if(damage.to->getCardCount()>=x&&player->canDiscard(damage.to,"he")){
			player->setTag("dongxinData", data);
			QList<int>ids;
			if(player==damage.to){
				Card*dc = room->askForDiscard(player,objectName(),x,x,true,true,"dongxin0:"+QString::number(x),".",objectName()+"$-1");
				if(dc==nullptr) return false;
				player->addMark(QString("dongxinUse%1-Clear").arg(event));
				ids = dc->getSubcards();
			}else{
				if(player->askForSkillInvoke(objectName()+"$-1",damage.to)){
					player->addMark(QString("dongxinUse%1-Clear").arg(event));
					for (int i = 0; i < x; i++) {
						int id = room->askForCardChosen(player,damage.to,"he",objectName(),false,Card::MethodDiscard,ids);
						if(id<0) break;
						ids << id;
					}
					room->throwCard(ids,objectName(),player,damage.to);
				}else
					return false;
			}
			if(player->isDead()) return false;
			if(damage.card) ids << damage.card->getSubcards();
			if(damage.from&&damage.from->isAlive())
				room->damage(DamageStruct(objectName(),player,damage.from));
			if(player->isDead()) return false;
			Card*dc = dummyCard();
			foreach(int id, ids){
				if(Sanguosha->getCard(id)->isKindOf("Slash")&&!room->getCardOwner(id))
					dc->addSubcard(id);
			}
			player->obtainCard(dc);
			foreach(int id, dc->getSubcards()){
				if(player->hasCard(id))
					room->setCardTip(id,objectName());
			}
		}
		return false;
	}
};

ZhitianCard::ZhitianCard()
{
	will_throw = false;
	target_fixed = true;
}

void ZhitianCard::onUse(Room*room, CardUseStruct &card_use) const
{
	QList<int>ids;
    int n = 7-card_use.from->getMark("zhijueNum");
	foreach(int id, room->getDrawPile()){
		ids << id;
		if(ids.length()>=n) break;
	}
	room->fillAG(ids,card_use.from);
	room->askForAG(card_use.from,ids,true,"zhijue");
	room->clearAG(card_use.from);
	room->returnToTopDrawPile(ids);
}

class ZhitianVs : public ZeroCardViewAsSkill
{
public:
	ZhitianVs() : ZeroCardViewAsSkill("zhitian")
	{
	}

	const Card*viewAs() const
	{
		return new ZhitianCard;
	}

	bool isEnabledAtPlay(const Player*) const
	{
		return true;
	}
};

class Zhitian : public TriggerSkill
{
public:
	Zhitian() : TriggerSkill("zhitian")
	{
		events << CardEffected;
		view_as_skill = new ZhitianVs;
	}
	int getPriority(TriggerEvent) const
	{
		return 0;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardEffected){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->isKindOf("FireAttack")&&effect.from->hasSkill(objectName())){
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
					const Card*c = room->askForCardShow(player, effect.from, objectName());
					room->showCard(player,c->getId());
					bool has = false;
					if(effect.from->askForSkillInvoke(objectName()+"$-1",data)){
						QList<int>hids,ids = room->getNCards(7-player->getMark("zhijueNum"),false);
						room->fillAG(ids,effect.from);
						foreach(int id, ids){
							const Card*dc = Sanguosha->getCard(id);
							if(dc->getSuit()==c->getSuit()) hids << id;
						}
						int id = room->askForAG(effect.from,hids,true,objectName());
						room->clearAG(effect.from);
						room->returnToTopDrawPile(ids);
						if(id>=0){
							room->throwCard(id,objectName(),effect.from);
							has = true;
						}
					}
					QString suit_str = c->getSuitString();
					QString suit_str_png = suit_str;
					if (c->hasSuit()) suit_str_png = "<img src='image/system/cardsuit/" + suit_str + ".png' height=17/>";
					QString prompt = QString("@fire-attack:%1::%2").arg(player->objectName()).arg(suit_str_png);
					if(has||room->askForCard(effect.from,QString(".%1").arg(suit_str.at(0).toUpper()),prompt,data)){
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

class Wujing : public TriggerSkill
{
public:
	Wujing() : TriggerSkill("wujing")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
                if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE
				&&player->hasFlag("CurrentPlayer")&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					room->setPlayerMark(player,"&wujing-Clear",1);
				}
				if(player->getMark("wujingNoHand-Clear")<1){
					for (int i = 0; i < move.from_places.length(); i++) {
						if(move.from_places.at(i)!=Player::PlaceHand){
                            if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE
								&&move.reason.m_useStruct.m_isHandcard)
								break;
							player->addMark("wujingNoHand-Clear");
							if(player->hasSkill(objectName())){
								room->sendCompulsoryTriggerLog(player,this);
								player->drawCards(1,objectName());
							}
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

ZhijueCard::ZhijueCard()
{
	//will_throw = false;
	target_fixed = true;
}

void ZhijueCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	QStringList choices;
	room->setChangeSkillState(source,"zhijue",1);
	room->addPlayerMark(source,getColorString()+"zhijueColor-Clear");
	foreach(int id, subcards){
		const Card*c = Sanguosha->getCard(id);
		if(c->isKindOf("BasicCard")||c->isNDTrick()){
			Card*dc = Sanguosha->cloneCard(c->objectName());
			dc->setSkillName("_zhijue");
			if(dc->isAvailable(source))
				choices << c->objectName();
			dc->deleteLater();
		}
	}
	if(choices.isEmpty()) return;
	QString cn = room->askForChoice(source,"zhijue",choices.join("+"));
	room->setPlayerProperty(source,"zhijueCn",cn);
	room->askForUseCard(source,"@@zhijue","zhijue0:"+cn);
}

class ZhijueVs : public ViewAsSkill
{
public:
	ZhijueVs() : ViewAsSkill("zhijue")
	{
		response_pattern = "@@zhijue";
	}

	bool viewFilter(const QList<const Card*> &cards, const Card*c) const
	{
		if(Self->getChangeSkillState(objectName())==1||Sanguosha->getCurrentCardUsePattern()=="@@zhijue"){
			return false;
		}
		return (cards.isEmpty()||c->getColor()==cards.first()->getColor())
			&&Self->getMark(c->getColorString()+"zhijueColor-Clear")<1&&!Self->isJilei(c);
	}

	const Card*viewAs(const QList<const Card*> &cards) const
	{
		if(Sanguosha->getCurrentCardUsePattern()=="@@zhijue"){
			Card*dc = Sanguosha->cloneCard(Self->property("zhijueCn").toString());
			dc->setSkillName("_"+objectName());
			return dc;
		}
		if(Self->getChangeSkillState(objectName())!=1){
			if(cards.isEmpty()) return nullptr;
			int n = 0;
			foreach(const Card*h, Self->getHandcards()){
				if(h->getColor()==cards.first()->getColor())
					n++;
			}
			if(cards.length()<n) return nullptr;
			Card*dc = new ZhijueCard;
			dc->addSubcards(cards);
			return dc;
		}
		Card*dc = Sanguosha->cloneCard("fire_attack");
		dc->addSubcard(Self->getMark("zhijueId"));
		dc->setSkillName(objectName());
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getChangeSkillState(objectName())!=1||player->getMark("zhijueId")>=0;
	}
};

class Zhijue : public TriggerSkill
{
public:
	Zhijue() : TriggerSkill("zhijue")
	{
		events << CardFinished << CardsMoveOneTime << PreCardUsed;
		view_as_skill = new ZhijueVs;
		change_skill = true;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->getSkillNames().contains(objectName())&&!use.card->hasFlag("DamageDone")){
				if(player->getMark("zhijueNum")<6)
					player->addMark("zhijueNum");
				player->drawCards(2,objectName());
			}
		}else if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("FireAttack")&&use.card->getSkillNames().contains(objectName())){
				room->setChangeSkillState(player,objectName(),2);
			}
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DrawPile||move.from_places.contains(Player::DrawPile)){
				room->setPlayerMark(player,"zhijueId",-1);
				foreach(int id, room->getDrawPile()){
					room->setPlayerMark(player,"zhijueId",id);
					break;
				}
			}
		}
		return false;
	}
};

class OLDuoduan : public TriggerSkill
{
public:
	OLDuoduan() : TriggerSkill("olduduan")
	{
		events << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::RoundStart){
				if(player->hasSkill(objectName())&&player->askForSkillInvoke(objectName()+"$-1",data)){
					player->drawCards(1,objectName());
					QStringList choices,choices2;
					QString pc = QString("olduduanPhase%1").arg(Player::Judge);
					if(player->getMark(pc)<1) choices << pc;
					choices2 << pc;
					pc = QString("olduduanPhase%1").arg(Player::Draw);
					if(player->getMark(pc)<1) choices << pc;
					choices2 << pc;
					pc = QString("olduduanPhase%1").arg(Player::Play);
					if(player->getMark(pc)<1) choices << pc;
					choices2 << pc;
					pc = QString("olduduanPhase%1").arg(Player::Discard);
					if(player->getMark(pc)<1) choices << pc;
					choices2 << pc;
					if(choices.isEmpty()) return false;
					QString choice = room->askForChoice(player,objectName(),choices.join("+"),data,"","olduduan0");
					int x = QString(choice.at(13)).toInt();
					player->skip((Player::Phase)x);
					choices2.removeOne(choice);
					player->addMark(choice);
					choice = room->askForChoice(player,objectName(),choices2.join("+"),data,"","olduduan1");
					choices2.removeOne(choice);
					pc = room->askForChoice(player,objectName(),choices2.join("+"),data,"","olduduan1");
					player->addMark(choice+":"+pc+":-Clear");
				}
			}else{
				QString pc = QString("olduduanPhase%1").arg(change.to);
				foreach(QString m, player->getMarkNames()){
					if(m.contains(":olduduanPhase")&&m.contains(pc)){
						QStringList ms = m.split(":");
						if(ms.at(0)==pc){
							change.to = (Player::Phase)QString(ms.at(1).at(13)).toInt();
						}else if(ms.at(1)==pc)
							change.to = (Player::Phase)QString(ms.at(0).at(13)).toInt();
						data.setValue(change);
					}
				}
			}
		}
		return false;
	}
};

YinglveCard::YinglveCard()
{
}

bool YinglveCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*) const
{
	return targets.isEmpty();
}

void YinglveCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		QStringList choices;
		if(source->getMark("1yinglve-PlayClear")<1) choices << "1";
		if(source->getMark("2yinglve-PlayClear")<1) choices << "2";
		QString choice = room->askForChoice(source,"yinglve",choices.join("+"),QVariant::fromValue(p));
		room->addPlayerMark(source,choice+"yinglve-PlayClear");
		if(choice=="1"){
			room->loseHp(p,1,true,source,"yinglve");
		}else{
			p->drawCards(2,"yinglve");
		}
		room->setPlayerMark(p,"&yinglve:"+choice,1);
	}
}

class YinglveVs : public ZeroCardViewAsSkill
{
public:
	YinglveVs() : ZeroCardViewAsSkill("yinglve")
	{
	}

	const Card*viewAs() const
	{
		return new YinglveCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getMark("1yinglve-PlayClear")<1||player->getMark("2yinglve-PlayClear")<1;
	}
};

class Yinglve : public TriggerSkill
{
public:
	Yinglve() : TriggerSkill("yinglve")
	{
		events << DrawNCards << EventPhaseProceeding;
		view_as_skill = new YinglveVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseProceeding){
			if(player->getPhase()==Player::Discard&&player->getMark("&yinglve:2")>0){
				room->setPlayerMark(player,"&yinglve:2",0);
				room->addMaxCards(player,-2);
			}
		}else if(event==DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase"||player->getMark("&yinglve:1")<1) return false;
			room->setPlayerMark(player,"&yinglve:1",0);
			draw.num += 2;
			data.setValue(draw);
		}
		return false;
	}
};

MengshiCard::MengshiCard()
{
	will_throw = false;
}

bool MengshiCard::targetFilter(const QList<const Player*> &targets, const Player*to, const Player*Self) const
{
	return targets.length()<2&&to!=Self;
}

bool MengshiCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	return targets.length()==2;
}

void MengshiCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &tos) const
{
	room->removePlayerMark(source,"@mengshi");
	room->doSuperLightbox(source,"mengshi");
	int x = qAbs(tos.first()->getHp()-tos.last()->getHp());
	int m = 0,n = 99;
	foreach(ServerPlayer*p, tos){
		room->setPlayerMark(p,"&mengshi+#"+source->objectName(),1);
		m = qMax(p->getHp(),m);
		n = qMin(p->getHp(),n);
	}
	if(x>0){
		room->loseHp(source,x,true,source,"mengshi");
		foreach(ServerPlayer*p, tos){
			if(p->getHp()<=n){
				room->recover(p,RecoverStruct("mengshi",source,x));
				tos.removeOne(p);
			}
		}
		foreach(ServerPlayer*p, tos){
			if(p->getHp()>=m)
				room->loseHp(p,x,true,source,"mengshi");
		}
	}
}

class MengshiVs : public ZeroCardViewAsSkill
{
public:
	MengshiVs() : ZeroCardViewAsSkill("mengshi")
	{
		response_pattern = "@@mengshi";
	}

	const Card*viewAs() const
	{
		return new MengshiCard;
	}
};

class Mengshi : public TriggerSkill
{
public:
	Mengshi() : TriggerSkill("mengshi")
	{
		frequency = Limited;
		limit_mark = "@mengshi";
		events << EventPhaseChanging << EventPhaseStart << Death;
		view_as_skill = new MengshiVs;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					foreach(ServerPlayer*q, room->getAlivePlayers()){
						if(q->getMark("&mengshi+#"+p->objectName())>0){
							p->drawCards(1,objectName());
							room->recover(p,RecoverStruct(objectName(),p));
							break;
						}
					}
				}
			}
		}else if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who->getMark("&mengshi+#"+player->objectName())>0){
				foreach(ServerPlayer*p, room->getAlivePlayers())
					room->setPlayerMark(p,"&mengshi+#"+player->objectName(),0);
			}
		}else if(player->getPhase()==Player::Finish&&player->getMark("@mengshi")>0&&player->hasSkill(objectName())){
			room->askForUseCard(player,"@@mengshi","mengshi0");
		}
		return false;
	}
};

HuanpeiCard::HuanpeiCard()
{
}

bool HuanpeiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+")[0]);
	if(card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool HuanpeiCard::targetFixed() const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+")[0]);
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool HuanpeiCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+")[0]);
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*HuanpeiCard::validateInResponse(ServerPlayer*user) const
{
	Room*room = user->getRoom();
	user->skillInvoked("huanpei");
	int x = user->getHandcardNum()-user->getHp();
	if(x>0) room->askForDiscard(user,"huanpei",x,x);
	else user->drawCards(-x,"huanpei");
	room->addPlayerMark(user,"huanpeiUse_lun");
	QString pc = user_string;
	if(pc.contains("+"))
		pc = room->askForChoice(user,"huanpei",pc);
	Card*card = Sanguosha->cloneCard(pc);
	card->setSkillName("_huanpei");
	card->deleteLater();
	return card;
}

const Card*HuanpeiCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	use.from->skillInvoked("huanpei");
	int x = use.from->getHandcardNum()-use.from->getHp();
	if(x>0) room->askForDiscard(use.from,"huanpei",x,x);
	else use.from->drawCards(-x,"huanpei");
	room->addPlayerMark(use.from,"huanpeiUse_lun");
	QString pc = user_string;
	if(pc.contains("+"))
		pc = room->askForChoice(use.from,"huanpei",pc);
	Card*card = Sanguosha->cloneCard(pc);
	card->setSkillName("_huanpei");
	card->deleteLater();
	return card;
}

class Huanpei : public ZeroCardViewAsSkill
{
public:
	Huanpei() : ZeroCardViewAsSkill("huanpei")
	{
	}

	const Card*viewAs() const
	{
        QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card*c = Self->getTag("huanpei").value<const Card*>();
			if(c==nullptr) return nullptr;
			pattern = c->objectName();
		}
		SkillCard*dc = new HuanpeiCard;
		dc->setUserString(pattern);
		return dc;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(player->getMark("huanpeiUse_lun")>0||player->getHandcardNum()==player->getHp()
		||Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
		foreach(QString pc, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc==nullptr) continue;
			dc->deleteLater();
			if(dc->isKindOf("BasicCard"))
				return true;
		}
		return false;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		if(player->getMark("huanpeiUse_lun")>0||player->getHandcardNum()==player->getHp()) return false;
		foreach(QString pc, Sanguosha->getCardNames("BasicCard")){
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc==nullptr) continue;
			dc->deleteLater();
			if(dc->isAvailable(player))
				return true;
		}
		return false;
	}

	QDialog*getDialog() const
	{
		return GuhuoDialog::getInstance(objectName(), true, false);
	}
};

class Wenyi : public TriggerSkill
{
public:
	Wenyi() : TriggerSkill("wenyi")
	{
		events << HpChanged << MaxHpChanged;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive()&&target->getHp()==1;
	}
    bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if(p->getMark("wenyiNum")>=0&&p->hasSkill(objectName())){
				QString str = "Peach,EquipCard";
				Card*dc = Sanguosha->cloneCard("peach");
				if(p->isProhibited(player,dc)||!p->isWounded()) str = "EquipCard";
				const Card*c = room->askForCard(p,str,"wenyi0:"+player->objectName(),QVariant::fromValue(player),Card::MethodUse,nullptr,true);
				dc->deleteLater();
				if(c){
					p->skillInvoked(objectName());
					p->addMark("wenyiNum",-1);
					player->setSkillDescriptionSwap("wenyi","%arg",QString::number(player->getMark("wenyiNum")+1));
					room->changeTranslation(player,"wenyi");
					if(c->isKindOf("EquipCard")){
						room->giveCard(p,player,c,objectName());
						if(player->hasCard(c)&&c->isAvailable(player))
							room->useCard(CardUseStruct(c,player));
						if(player->isChained())
							room->setPlayerChained(player);
						if(player->faceUp()) continue;
						player->turnOver();
					}else{
						room->useCard(CardUseStruct(c,p,player));
					}
				}
			}
		}
		return false;
	}
};

class Miaoxin : public TriggerSkill
{
public:
	Miaoxin() : TriggerSkill("miaoxin")
	{
		events << Damaged;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			for (int i = 0; i < damage.damage; i++) {
				room->sendCompulsoryTriggerLog(player,this);
				if(player->getMark("wenyiNum")>=0)
					player->drawCards(2,objectName());
				else{
					player->drawCards(1,objectName());
					player->addMark("wenyiNum");
					player->setSkillDescriptionSwap("wenyi","%arg",QString::number(player->getMark("wenyiNum")+1));
					room->changeTranslation(player,"wenyi");
				}
			}
		}
		return false;
	}
};

class Bachao : public TriggerSkill
{
public:
	Bachao() : TriggerSkill("bachao")
	{
		events << EventPhaseStart;
		waked_skills = "#olmorumo";
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Play&&player->askForSkillInvoke(objectName()+"$-1")){
				foreach(ServerPlayer*p, room->getOtherPlayers(player))
					room->doAnimate(1,player->objectName(),p->objectName());
				QList<ServerPlayer*>tps;
				QHash<ServerPlayer*,const Card*>p2c;
				foreach(ServerPlayer*p, room->getOtherPlayers(player)){
					if(p->getCardCount()<1) continue;
					p2c[p] = room->askForCard(p,"..","bachao0:"+player->objectName(),QVariant::fromValue(player),Card::MethodNone);
					if(p2c[p]&&p2c[p]->getTypeId()>1) continue;
					tps << p;
				}
				tps << player;
                foreach(ServerPlayer*p, p2c.keys()){
                    if(p2c[p]) room->giveCard(p,player,p2c[p],objectName());
					if(player->isDead()) return false;
				}
				ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName(),"bachao1",true);
				if(tp){
					room->damage(DamageStruct(objectName(),player,tp));
					if(tp==player&&player->isAlive()){
						room->setPlayerMark(player,"&olmorumo",1);
						foreach(int id, player->handCards())
							room->setCardTip(id,"bachao-PlayClear");
					}
				}
			}
		}
		return false;
	}
};

class Fuzhaivs : public OneCardViewAsSkill
{
public:
	Fuzhaivs() : OneCardViewAsSkill("fuzhai")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		return to_select->isKindOf("EquipCard");
	}

	const Card*viewAs(const Card*original) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card*c = Self->getTag(objectName()).value<const Card*>();
			if(c==nullptr) return nullptr;
			pattern = c->objectName();
		}
		if(pattern.contains("ex_nihilo"))
			pattern = "ex_nihilo";
		else
			pattern = "collateral";
		Card*dc = Sanguosha->cloneCard(pattern);
		dc->setSkillName(objectName());
		dc->addSubcard(original);
		return dc;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		return Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		&&player->getCardCount()>0&&(pattern.contains("ex_nihilo")||pattern.contains("collateral"));
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0;
	}
};

class Fuzhai : public TriggerSkill
{
public:
	Fuzhai() : TriggerSkill("fuzhai")
	{
		view_as_skill = new Fuzhaivs;
		waked_skills = "#FuzhaiBf";
		events << HpChanged << MaxHpChanged << CardsMoveOneTime;
		frequency = Compulsory;
	}
	int getEffectIndex(const ServerPlayer*, const Card*) const
	{
		return qrand()%3+1;
	}
	QDialog*getDialog() const
	{
		return JuguanDialog::getInstance(objectName(), "collateral,ex_nihilo");
	}
	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &) const
	{
		if(player->hasEquip()) return false;
		int x = player->property("FuzhaiHp").toInt();
		if(x!=player->getHp()){
			player->peiyin(objectName(),qrand()%2+4);
			x = player->getHp();
			room->safeSetPlayerProperty(player,"FuzhaiHp",x);
			QStringList esn;
			QList<int>ids = Sanguosha->getRandomCards();
			if(player->hasEquipArea(0)){
				foreach(int id, ids){
					const Card*c = Sanguosha->getCard(id);
					if(c->isKindOf("Weapon")){
						if(((const Weapon*)c->getRealCard())->getRange()==x){
							esn << c->objectName();
							break;
						}
					}
				}
				if(esn.isEmpty()){
					foreach(int id, ids){
						const Card*c = Sanguosha->getCard(id);
						if(c->isKindOf("Weapon")){
							esn << c->objectName();
							break;
						}
					}
				}
			}
			if(player->hasEquipArea(1)){
				foreach(int id, ids){
					const Card*c = Sanguosha->getCard(id);
					if(c->isKindOf("Armor")&&c->nameLength()==x){
						esn << c->objectName();
						ids.clear();
						break;
					}
				}
				foreach(int id, ids){
					const Card*c = Sanguosha->getCard(id);
					if(c->isKindOf("Armor")){
						esn << c->objectName();
						break;
					}
				}
			}
			room->setPlayerProperty(player,"FuzhaiVe",esn.join(","));
			foreach(QString m, player->getMarkNames()){
				if(m.contains("&fuzhai+:+"))
					room->setPlayerMark(player,m,0);
			}
			room->setPlayerMark(player,"&fuzhai+:+"+esn.join("+,+"),1);
		}
		return false;
	}
};

class FuzhaiBf : public ViewAsEquipSkill
{
public:
	FuzhaiBf() : ViewAsEquipSkill("#FuzhaiBf")
	{
	}

	QString viewAsEquip(const Player*target) const
	{
		if(!target->hasEquip()&&target->hasSkill("fuzhai"))
			return target->property("FuzhaiVe").toString();
		return "";
	}
};

DinglunCard::DinglunCard()
{
}

bool DinglunCard::targetFilter(const QList<const Player*> &targets, const Player*, const Player*Self) const
{
	return targets.length()<Self->getAliveSiblings(true).length()/2;
}

bool DinglunCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	return targets.length()>=Self->getAliveSiblings(true).length()/2;
}

void DinglunCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
    int n = 0;
	foreach(ServerPlayer*p, targets){
		n += p->getHandcardNum();
	}
	foreach(ServerPlayer*p, room->getAlivePlayers()){
		if(targets.contains(p)) continue;
		n -= p->getHandcardNum();
	}
	if(n<1) return;
	foreach(ServerPlayer*p, targets){
		if(p->hasSkill("olquxi",true)) continue;
		p->addMark("dinglunBf"+source->objectName());
		room->acquireSkill(p,"olquxi");
		p->drawCards(1,"dinglun");
	}
}

class DinglunVs : public ZeroCardViewAsSkill
{
public:
	DinglunVs() : ZeroCardViewAsSkill("dinglun")
	{
	}

	const Card*viewAs() const
	{
		return new DinglunCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("DinglunCard")<1;
	}
};

class Dinglun : public TriggerSkill
{
public:
	Dinglun() : TriggerSkill("dinglun")
	{
		events << EventPhaseStart << Death;
		view_as_skill = new DinglunVs;
		waked_skills = "olquxi";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start){
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if(p->getMark("dinglunBf"+player->objectName())>0){
						p->setMark("dinglunBf"+player->objectName(),0);
						room->detachSkillFromPlayer(p,"olquxi");
					}
				}
			}
		}else if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who!=player) return false;
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if(p->getMark("dinglunBf"+player->objectName())>0){
					p->setMark("dinglunBf"+player->objectName(),0);
					room->detachSkillFromPlayer(p,"olquxi");
				}
			}
		}
		return false;
	}
};

class Jieli : public TriggerSkill
{
public:
	Jieli() : TriggerSkill("jieli")
	{
		events << TargetConfirming;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()==1&&player->getMark("jieliUse_lun")<1){
				ServerPlayer*tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName()+"$-1","jieli0",true,true);
				if(tp){
					player->addMark("jieliUse_lun");
					if(room->getCardTargets(use.from,use.card,use.to).contains(tp)
					&&room->askForChoice(player,objectName(),"jieli1+jieli2",data)=="jieli1"){
						use.to.removeOne(player);
						use.to.append(tp);
						data.setValue(use);
					}else{
                        room->doGongxin(player,tp,QList<int>(),objectName());
						Card*dc = dummyCard();
						foreach(const Card*h, tp->getHandcards()){
							if(h->getSuit()==use.card->getSuit())
								dc->addSubcard(h);
						}
						player->obtainCard(dc);
					}
				}
			}
		}
		return false;
	}
};

class OLQuxivs : public OneCardViewAsSkill
{
public:
	OLQuxivs() : OneCardViewAsSkill("olquxi")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		if(to_select->isKindOf("Jink")) return Self->getMark("olquxiBanJink-PlayClear")<1;
		return to_select->isKindOf("Peach")&&Self->getMark("olquxiBanPeach-PlayClear")<1;
	}

	const Card*viewAs(const Card*original) const
	{
		QString pattern = "snatch";
		if(original->isKindOf("Jink"))
			pattern = "dismantlement";
		Card*dc = Sanguosha->cloneCard(pattern);
		dc->setSkillName(objectName());
		dc->addSubcard(original);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getHandcardNum()>0;
	}
};

class OLQuxi : public TriggerSkill
{
public:
	OLQuxi() : TriggerSkill("olquxi")
	{
		view_as_skill = new OLQuxivs;
		events << PreCardUsed;
	}
	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->getSkillName()==objectName()){
			if(use.card->isKindOf("Snatch"))
				room->addPlayerMark(player,"olquxiBanPeach-PlayClear");
			else
				room->addPlayerMark(player,"olquxiBanJink-PlayClear");
		}
		return false;
	}
};

class Xinchuan : public TriggerSkill
{
public:
	Xinchuan() : TriggerSkill("xinchuan")
	{
		events << CardsMoveOneTime;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				foreach(int id, move.card_ids){
					player->addMark(QString("%1xinchuanId-Clear").arg(id));
				}
                if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE
				&&move.reason.m_useStruct.card->isKindOf("TrickCard")
				&&move.reason.m_playerId==player->objectName()&&player->hasSkill(objectName())){
					QStringList suits;
					foreach(int id, room->getDiscardPile()){
						if(player->getMark(QString("%1xinchuanId-Clear").arg(id))<1) continue;
						QString suit = Sanguosha->getCard(id)->getSuitString();
						if(suits.contains(suit)) continue;
						suits.append(suit);
					}
					if(suits.length()>3) return false;
					ServerPlayer*tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName()+"$-1","xinchuan0",true,true);
					if(tp){
						for (int i = 0; i < 4-suits.length(); i++) {
							tp->drawCards(1,objectName());
							if(tp->isDead()) return false;
							if(tp->getHandcardNum()==tp->getHp())
								tp = tp->getNextAlive();
							room->getThread()->delay();
							suits.clear();
							foreach(int id, room->getDiscardPile()){
								if(tp->getMark(QString("%1xinchuanId-Clear").arg(id))<1) continue;
								QString suit = Sanguosha->getCard(id)->getSuitString();
								if(suits.contains(suit)) continue;
								suits.append(suit);
							}
						}
						for (int i = 0; i < 4-suits.length(); i++) {
							room->askForDiscard(tp,objectName(),1,1,false,true);
							if(tp->isDead()) return false;
							if(tp->getHandcardNum()==tp->getHp())
								tp = tp->getNextAlive();
							room->getThread()->delay();
							suits.clear();
							foreach(int id, room->getDiscardPile()){
								if(tp->getMark(QString("%1xinchuanId-Clear").arg(id))<1) continue;
								QString suit = Sanguosha->getCard(id)->getSuitString();
								if(suits.contains(suit)) continue;
								suits.append(suit);
							}
						}
					}
				}
			}
		}
		return false;
	}
};

OLJinjinCard::OLJinjinCard()
{
}

bool OLJinjinCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+")[0]);
	if(card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool OLJinjinCard::targetFixed() const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card*card = Sanguosha->cloneCard(user_string.split("+")[0]);
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool OLJinjinCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+")[0]);
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card*OLJinjinCard::validateInResponse(ServerPlayer*user) const
{
	Room*room = user->getRoom();
	user->skillInvoked("oljinjin");
	QList<int>ids,breaks;
	foreach(int id, room->getDiscardPile()){
		if(user->getMark(QString("%1oljinjinId-Clear").arg(id))<1) continue;
		ids << id;
	}
	room->fillAG(ids,user);
	bool can = false;
	while(ids.length()>0){
		int id = room->askForAG(user,ids,can,"oljinjin","oljinjin0");
		can = true;
		if(id<0) break;
		ids.removeAll(id);
		breaks << id;
		room->takeAG(user,id,false,QList<ServerPlayer*>()<<user);
		foreach(int cid, ids){
			if(Sanguosha->getCard(id)->getSuit()==Sanguosha->getCard(cid)->getSuit()){
				room->takeAG(nullptr,cid,false,QList<ServerPlayer*>()<<user);
				ids.removeAll(cid);
			}
		}
	}
	room->clearAG(user);
	user->setTag("oljinjin_break", ListI2V(breaks));
	room->breakCard(breaks,user);
	room->detachSkillFromPlayer(user,"oljinjin");
	QString pc = user_string;
	if(pc.contains("+"))
		pc = room->askForChoice(user,"oljinjin",pc);
	Card*card = Sanguosha->cloneCard(pc);
	card->setSkillName("_oljinjin");
	card->deleteLater();
	return card;
}

const Card*OLJinjinCard::validate(CardUseStruct &use) const
{
	Room*room = use.from->getRoom();
	use.from->skillInvoked("oljinjin");
	QList<int>ids,breaks;
	foreach(int id, room->getDiscardPile()){
		if(use.from->getMark(QString("%1oljinjinId-Clear").arg(id))<1) continue;
		ids << id;
	}
	room->fillAG(ids,use.from);
	bool can = false;
	while(ids.length()>0){
		int id = room->askForAG(use.from,ids,can,"oljinjin","oljinjin0");
		can = true;
		if(id<0) break;
		ids.removeAll(id);
		breaks << id;
		room->takeAG(use.from,id,false,QList<ServerPlayer*>()<<use.from);
		foreach(int cid, ids){
			if(Sanguosha->getCard(id)->getSuit()==Sanguosha->getCard(cid)->getSuit()){
				room->takeAG(nullptr,cid,false,QList<ServerPlayer*>()<<use.from);
				ids.removeAll(cid);
			}
		}
	}
	room->clearAG(use.from);
	use.from->setTag("oljinjin_break", ListI2V(breaks));
	room->breakCard(breaks,use.from);
	room->detachSkillFromPlayer(use.from,"oljinjin");
	QString pc = user_string;
	if(pc.contains("+"))
		pc = room->askForChoice(use.from,"oljinjin",pc);
	Card*card = Sanguosha->cloneCard(pc);
	card->setSkillName("_oljinjin");
	card->deleteLater();
	return card;
}

class OLJinjinvs : public ZeroCardViewAsSkill
{
public:
	OLJinjinvs() : ZeroCardViewAsSkill("oljinjin")
	{
	}

	const Card*viewAs() const
	{
        QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card*c = Self->getTag("oljinjin").value<const Card*>();
			if(c==nullptr) return nullptr;
			pattern = c->objectName();
		}
		SkillCard*dc = new OLJinjinCard;
		dc->setUserString(pattern);
		return dc;
	}

	bool isEnabledAtResponse(const Player*player, const QString &pattern) const
	{
		if(Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
		bool ban = true;
		foreach(QString m, player->getMarkNames()){
			if(m.contains("oljinjinId-Clear")&&player->getMark(m)>0){
				ban = false;
				break;
			}
		}
		if(ban) return false;
		foreach(QString pc, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc==nullptr) continue;
			dc->deleteLater();
			if(dc->isNDTrick())
				return true;
		}
		return false;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		bool ban = true;
		foreach(QString m, player->getMarkNames()){
			if(m.contains("oljinjinId-Clear")&&player->getMark(m)>0){
				ban = false;
				break;
			}
		}
		if(ban) return false;
		foreach(QString pc, Sanguosha->getCardNames("TrickCard+^DelayedTrick")){
			Card*dc = Sanguosha->cloneCard(pc);
			if(dc==nullptr) continue;
			dc->setSkillName(objectName());
			dc->deleteLater();
			if(dc->isAvailable(player))
				return true;
		}
		return false;
	}

};

class OLJinjin : public TriggerSkill
{
public:
	OLJinjin() : TriggerSkill("oljinjin")
	{
		events << CardsMoveOneTime << EventPhaseChanging << DamageDone;
		view_as_skill = new OLJinjinvs;
	}
	QDialog*getDialog() const
	{
		return GuhuoDialog::getInstance(objectName(), false, true);
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				foreach(int id, move.card_ids){
					if(room->getCardPlace(id)!=move.to_place) continue;
					room->setPlayerMark(player,QString("%1oljinjinId-Clear").arg(id),1);
				}
			}else if(move.from_places.contains(Player::DiscardPile)){
				foreach(int id, move.card_ids){
					if(room->getCardPlace(id)==Player::DiscardPile) continue;
					room->setPlayerMark(player,QString("%1oljinjinId-Clear").arg(id),0);
				}
			}
		}else if(event==DamageDone){
			if(player->getTag("oljinjin_break").isValid()){
				player->addMark("oljinjinGive-Clear");
			}
		}else{
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->getMark("oljinjinGive-Clear")>0){
						ServerPlayer*tp = room->askForPlayerChosen(p,room->getAlivePlayers(),objectName(),"oljinjin1");
						if(tp){
							p->peiyin(objectName());
							room->doAnimate(1,p->objectName(),tp->objectName());
							Card*dc = dummyCard(ListV2I(p->getTag("oljinjin_break").toList()));
							p->removeTag("oljinjin_break");
							tp->obtainCard(dc);
							room->acquireSkill(tp,objectName());
						}
					}
				}
			}
		}
		return false;
	}
};

class Quanyu : public TriggerSkill
{
public:
	Quanyu() : TriggerSkill("quanyu")
	{
		events << TargetSpecifying << RoundStart << ConfirmDamage;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.to.length()==1&&player->hasSkill(objectName())){
				QString has = player->property("quanyuBf").toString();
				if(has.contains("quanyu1"))
					room->setCardFlag(use.card,"quanyu1");
				if(has.contains("quanyu2")){
                    player->setTag("quanyu2_data", data);
					QList<ServerPlayer*>tps = room->getCardTargets(player,use.card,use.to);
					ServerPlayer*tp = room->askForPlayerChosen(player,tps,"quanyu2","quanyu20",true);
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						use.to << tp;
						room->sortByActionOrder(use.to);
					}
				}
				if(has.contains("quanyu3"))
					player->addQinggangTag(use.card);
				if(has.contains("quanyu4"))
					use.no_respond_list << "_ALL_TARGETS";
				if(has.contains("quanyu5"))
					use.extra_use++;
				data.setValue(use);
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("quanyu1"))
				player->damageRevises(data,1);
		}else if(player->hasSkill(objectName())){
			int n = -1;
			foreach(ServerPlayer*p, room->getAllPlayers()){
				QStringList choices,has = p->property("quanyuBf").toString().split("+");
				for (int i = 1; i <= 6; i++) {
					QString m = QString("quanyu%1").arg(i);
					if(has.contains(m)) continue;
					choices << m;
				}
				if(choices.isEmpty()) continue;
				if(has.first().isEmpty()) has.takeFirst();
				if(n<0) room->sendCompulsoryTriggerLog(player,this);
				has << room->askForChoice(p,objectName(),choices.join("+"));
				room->setPlayerProperty(p,"quanyuBf",has.join("+"));
				p->addMark("quanyuBf_lun");
				n = 0;
			}
			if(n<0) return false;
			QString bf = player->property("quanyuBf").toString().split("+").last();
			foreach(ServerPlayer*p, room->getAlivePlayers()){
				if(p->property("quanyuBf").toString().endsWith(bf))
					n++;
				foreach(QString m, p->getMarkNames()){
					if(m.endsWith("+#quanyuBf"))
						room->setPlayerMark(p,m,0);
				}
				room->setPlayerMark(p,"&"+p->property("quanyuBf").toString()+"+#quanyuBf",1);
			}
			if(n>1)
				player->drawCards(qMin(n-1,3),objectName());
		}
		return false;
	}
};

class Tianen : public TriggerSkill
{
public:
	Tianen() : TriggerSkill("tianen")
	{
		events << TargetSpecified;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.length()==1
			&&player->getMark("quanyuBf_lun")>0&&use.to.last()->getMark("quanyuBf_lun")>0){
				QStringList has = player->property("quanyuBf").toString().split("+");
				ServerPlayer*tp = use.to.last();
				if(tp->property("quanyuBf").toString().endsWith(has.last())){
					if(player->getMark("tianen1-Clear")>0) return false;
					player->addMark("tianen1-Clear");
					room->sendCompulsoryTriggerLog(player,this);
					foreach(int id, room->getDrawPile()){
						if(Sanguosha->getCard(id)->isKindOf("Slash")){
							room->obtainCard(player,id);
							break;
						}
					}
				}else{
					if(player->getMark("tianen2-Clear")>0) return false;
					player->addMark("tianen2-Clear");
					room->sendCompulsoryTriggerLog(player,this);
					QList<int>ids = tp->handCards()+tp->getEquipsId();
					qShuffle(ids);
					foreach(int id, ids){
						if(player->canDiscard(tp,id)){
							room->throwCard(id,objectName(),tp,player);
							break;
						}
					}
					if(tp->isAlive()){
						QStringList choices,has2 = tp->property("quanyuBf").toString().split("+");
						for (int i = 1; i <= 6; i++) {
							QString m = QString("quanyu%1").arg(i);
							if(has2.contains(m)) continue;
							choices << m;
						}
						if(choices.isEmpty()) return false;;
						room->sendCompulsoryTriggerLog(player,"quanyu",true,true);
						has2 << room->askForChoice(tp,"quanyu",choices.join("+"));
						room->setPlayerProperty(tp,"quanyuBf",has2.join("+"));
						tp->addMark("quanyuBf_lun");
						if(has2.last()==has.last())
							player->drawCards(2,"quanyu");
					}
				}
			}
		}
		return false;
	}
};

QiangangCard::QiangangCard()
{
	target_fixed = true;
}

void QiangangCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	//room->doSuperLightbox(source,"qiangang");
	room->detachSkillFromPlayer(source,"tianen");
	room->setPlayerMark(source,"&olmorumo",1);
	room->setPlayerMark(source,"qiangangUse",1);
}

class QiangangVs : public ZeroCardViewAsSkill
{
public:
	QiangangVs() : ZeroCardViewAsSkill("qiangang")
	{
	}

	const Card*viewAs() const
	{
		return new QiangangCard;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->hasSkill("tianen",true);
	}
};

class Qiangang : public TriggerSkill
{
public:
	Qiangang() : TriggerSkill("qiangang")
	{
		events << TargetSpecifying << ConfirmDamage;
		view_as_skill = new QiangangVs;
		waked_skills = "#olmorumo";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.to.length()==1&&player->getMark("qiangangUse")>0&&player->hasSkill(objectName())){
				QString has = use.to.last()->property("quanyuBf").toString();
				if(has.contains("quanyu1"))
					room->setCardFlag(use.card,"qiangang_quanyu1");
				if(has.contains("quanyu2")){
					QList<ServerPlayer*>tps = room->getCardTargets(player,use.card,use.to);
					ServerPlayer*tp = room->askForPlayerChosen(player,tps,"quanyu2","quanyu20",true);
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						use.to << tp;
						room->sortByActionOrder(use.to);
					}
				}
				if(has.contains("quanyu3"))
					player->addQinggangTag(use.card);
				if(has.contains("quanyu4"))
					use.no_respond_list << "_ALL_TARGETS";
				if(has.contains("quanyu5"))
					use.extra_use++;
				data.setValue(use);
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("qiangang_quanyu1"))
				player->damageRevises(data,1);
		}
		return false;
	}
};

class Qianfuvs : public OneCardViewAsSkill
{
public:
	Qianfuvs() : OneCardViewAsSkill("qianfu")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		return Self->getMark("qianfuBf")<1||(to_select->getSuit()<6&&Self->getMark(to_select->getColorString()+"qianfuUse-PlayClear")<1);
	}

	const Card*viewAs(const Card*original) const
	{
		QString pattern = "fire_attack";
		if(original->isBlack()) pattern = "dismantlement";
		Card*dc = Sanguosha->cloneCard(pattern);
		dc->setSkillName(objectName());
		dc->addSubcard(original);
		return dc;
	}

	bool isEnabledAtPlay(const Player*player) const
	{
		return player->getCardCount()>0;
	}
};

class Qianfu : public TriggerSkill
{
public:
	Qianfu() : TriggerSkill("qianfu")
	{
		change_skill = true;
		view_as_skill = new Qianfuvs;
		events << PreCardUsed << CardFinished << CardsMoveOneTime << ChoiceMade;
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillName()==objectName()){
				if(use.card->isKindOf("Dismantlement")){
					room->setChangeSkillState(player,objectName(),2);
				}else
					room->setChangeSkillState(player,objectName(),1);
				room->addPlayerMark(player,use.card->getColorString()+"qianfuUse-PlayClear");
				player->setMark("qianfuId",-1);
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				int id = player->getMark("qianfuId");
				if(id<0||room->getCardOwner(id)) return false;
				if(player->askForSkillInvoke(objectName(),"0:"))
					room->moveCardsInToDrawpile(player,id,objectName());
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&move.reason.m_skillName=="dismantlement"&&&move.reason.m_playerId==player->objectName()){
				player->setMark("qianfuId",move.card_ids.last());
			}
		}else if(event==ChoiceMade){
			QString str = data.toString();
			if(str.contains("cardResponded:")&&!str.endsWith(":")){
				player->setMark("qianfuId",str.split(":").last().toInt());
			}
		}
		return false;
	}
};

class Yushi : public TriggerSkill
{
public:
	Yushi() : TriggerSkill("yushi")
	{
		events << Damage << Damaged << CardsMoveOneTime << MarkChanged;
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.is_last_handcard&&player->getMark("qianfuBf")>0){
				player->setMark("qianfuBf",0);
				room->detachSkillFromPlayer(player,"qianfu",false,true);
			}
		}else if(event==MarkChanged){
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name.endsWith("_num")&&mark.name.contains("+")&&mark.gain>0){
				foreach(ServerPlayer*p, room->getAllPlayers()){
					if(p->hasSkill(objectName())&&p->getMark("yushiUse_lun")<p->getMaxHp()){
						room->sendCompulsoryTriggerLog(p,objectName());
						p->addMark("yushiUse_lun");
						p->drawCards(1,objectName());
					}
				}
			}
		}else if(player->hasSkill(objectName())){
			ServerPlayer*tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName()+"$-1","yushi0",true,true);
			if(tp){
				int n = 0;
				foreach(const Skill*s, tp->getVisibleSkillList()){
					if(s->isChangeSkill()){
						n = tp->getChangeSkillState(s->objectName())+1;
						if(n>2) n = 1;
						room->setChangeSkillState(tp,s->objectName(),n);
					}
				}
				if(n<1){
					room->acquireSkill(tp,"qianfu");
					room->setPlayerMark(tp,"qianfuBf",1);
				}
			}
		}
		return false;
	}
};

class Fenchao : public TriggerSkill
{
public:
	Fenchao() : TriggerSkill("fenchao")
	{
		events << EventPhaseStart << ConfirmDamage;
		frequency = Limited;
		limit_mark = "@fenchao";
	}
	bool triggerable(const ServerPlayer*target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Finish&&player->getMark(limit_mark)>0&&player->hasSkill(objectName())){
				QList<int>ids;
				foreach(int id, room->getDiscardPile()){
					Card*c = Sanguosha->getCard(id);
					if(c->isDamageCard()) ids << id;
				}
				if(ids.isEmpty()) return false;
				room->fillAG(ids,player);
				ServerPlayer*tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName()+"$-1","fenchao0",true,true);
				room->clearAG(player);
				if(tp){
					room->removePlayerMark(player,limit_mark);
					room->doSuperLightbox(player,objectName());
					int n = qMin(ids.length(),room->getAlivePlayers().length());
					room->fillAG(ids,tp);
					Card*dc = dummyCard();
					for (int i = 0; i < n; i++) {
						int id = room->askForAG(tp,ids,false,objectName());
						if(id<0) break;
						ids.removeOne(id);
						dc->addSubcard(id);
						room->takeAG(player,id,false,QList<ServerPlayer*>()<<tp);
					}
					room->clearAG(tp);
					tp->obtainCard(dc);
					foreach(int id, dc->getSubcards()){
						if(tp->hasCard(id)){
							room->setCardFlag(id,"fenchaoBf");
							room->setCardTip(id,"fenchao");
						}
					}
				}
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("fenchaoBf")){
				damage.nature = DamageStruct::Fire;
				data.setValue(damage);
			}
		}
		return false;
	}
};









OLCcxhPackage::OLCcxhPackage()
: Package("ol_ccxh")
{



	General*liuxie = new General(this, "liuxie*xh_tianji", "qun", 3);
	liuxie->addSkill("tianming");
	liuxie->addSkill("mizhao");

	General*new_liuxie = new General(this, "new_liuxie", "qun", 3);
	new_liuxie->addSkill(new NewTianming);
	new_liuxie->addSkill("mizhao");

	General*caoang = new General(this, "caoang*xh_tianji", "wei"); // SP 026
	caoang->addSkill(new Kangkai);

	General*ol_sunhao = new General(this, "ol_sunhao$*xh_tianji", "wu", 5);
	ol_sunhao->addSkill("tenyearcanshi");
	ol_sunhao->addSkill("chouhai");
	ol_sunhao->addSkill("guiming");

	General*newsp_hetaihou = new General(this, "newsp_hetaihou", "qun", 3, false);
	newsp_hetaihou->addSkill(new NewZhendu);
	newsp_hetaihou->addSkill(new NewQiluan);

	General*sunluyu = new General(this, "sunluyu*xh_tianji", "wu", 3, false); // SP 034
	sunluyu->addSkill(new Meibu);
	sunluyu->addSkill(new Mumu);

	General*ol_sunluyu = new General(this, "ol_sunluyu", "wu", 3, false);
	ol_sunluyu->addSkill(new OLMeibu);
	ol_sunluyu->addSkill(new OLMumu);
	addMetaObject<OLMumuCard>();
	addMetaObject<OLMumu2Card>();
	skills << new MeibuFilter("olmeibu") << new MeibuFilter("olzhixi") << new OLZhixi;

	General*ol2_sunluyu = new General(this, "ol2_sunluyu", "wu", 3, false);
	ol2_sunluyu->addSkill(new OLMeibu2);
	ol2_sunluyu->addSkill(new OLMumu2);
	ol2_sunluyu->addRelateSkill("olzhixi");

	General*caosong = new General(this, "caosong*xh_tianji", "wei", 3);
	caosong->addSkill(new Lilu);
	caosong->addSkill(new Yizhengc);
	caosong->addSkill(new YizhengcEffect);
	related_skills.insertMulti("yizhengc", "#yizhengc");
	addMetaObject<LiluCard>();

	General*liuqi = new General(this, "liuqi*xh_tianji", "qun", 3);
	liuqi->addSkill(new Wenji);
	liuqi->addSkill(new Tunjiang);

	General*bianfuren = new General(this, "bianfuren*xh_tianji", "wei", 3, false);
	bianfuren->addSkill(new Wanwei);
	bianfuren->addSkill(new Yuejian);

	General*qinghegongzhu = new General(this, "qinghegongzhu*xh_tianji", "wei", 3, false);
	qinghegongzhu->addSkill(new Zengou);
	qinghegongzhu->addSkill(new Zhangji);
	related_skills.insertMulti("zhangji", "#zhongzuo-record");
	qinghegongzhu->addSkill("#zhongzuo-record");

	General*liuhong = new General(this, "liuhong*xh_tianji", "qun", 4);
	liuhong->addSkill(new Yujue);
	liuhong->addSkill(new Tuxing);
	liuhong->addRelateSkill("zhihu");
	addMetaObject<YujueCard>();

	General*tengfanglan = new General(this, "tengfanglan*xh_tianji", "wu", 3, false);
	tengfanglan->addSkill(new Luochong);
	tengfanglan->addSkill(new Aicheng);

	General*ruiji = new General(this, "ruiji*xh_tianji", "wu", 3, false);
	ruiji->addSkill(new Qiaoli);
	ruiji->addSkill(new Qingliang);
	
	General*yangxiu = new General(this, "yangxiu*xh_sibi", "wei", 3); // SP 001
	yangxiu->addSkill(new Jilei);
	yangxiu->addSkill(new JileiClear);
	yangxiu->addSkill(new Danlao);
	related_skills.insertMulti("jilei", "#jilei-clear");

	General*chengyu = new General(this, "chengyu*xh_sibi", "wei", 3);
	chengyu->addSkill(new Shefu);
	chengyu->addSkill(new Benyu);
	addMetaObject<ShefuCard>();

	General*sp_zhugejin = new General(this, "sp_zhugejin*xh_sibi", "wu", 3, true); // SP 027
	sp_zhugejin->addSkill("hongyuan");
	sp_zhugejin->addSkill("huanshi");
	sp_zhugejin->addSkill("mingzhe");

	General*chenlin = new General(this, "chenlin*xh_sibi", "wei", 3); // SP 020
	chenlin->addSkill(new Bifa);
	chenlin->addSkill(new Songci);
	addMetaObject<BifaCard>();
	addMetaObject<SongciCard>();

	General*ol_shixie = new General(this, "ol_shixie*xh_sibi", "qun", 3);
	ol_shixie->addSkill(new OLBiluan);
	ol_shixie->addSkill(new OLBiluanDist);
	ol_shixie->addSkill(new OLLixia);

	General*new_maliang = new General(this, "new_maliang*xh_sibi", "shu", 3);
	new_maliang->addSkill(new Zishu);
	new_maliang->addSkill(new Yingyuan);

	General*simalang = new General(this, "simalang*xh_sibi", "wei", 3); // SP 040
	simalang->addSkill(new Quji);
	simalang->addSkill(new Junbing);
	addMetaObject<QujiCard>();

	General*buzhi = new General(this, "buzhi*xh_sibi", "wu", 3);
	buzhi->addSkill(new Hongde);
	buzhi->addSkill(new Dingpan);
	addMetaObject<DingpanCard>();

	General*dongyun = new General(this, "dongyun*xh_sibi", "shu", 3);
	dongyun->addSkill(new Bingzheng);
	dongyun->addSkill(new Sheyan);

	General*kanze = new General(this, "kanze*xh_sibi", "wu", 3);
	kanze->addSkill(new Xiashu);
	kanze->addSkill(new Kuanshi("kuanshi"));
	kanze->addSkill(new KuanshiMark("kuanshi"));
	kanze->addSkill(new KuanshiEffect);
	related_skills.insertMulti("kuanshi", "#kuanshi-mark");
	related_skills.insertMulti("kuanshi", "#kuanshi-effect");
	skills << new Kuanshi("tenyearkuanshi") << new KuanshiMark("tenyearkuanshi");
	related_skills.insertMulti("tenyearkuanshi", "#tenyearkuanshi-mark");

	General*xizhicai = new General(this, "xizhicai*xh_sibi", "wei", 3);
	xizhicai->addSkill("tiandu");
	xizhicai->addSkill(new Xianfu);
	xizhicai->addSkill(new Chouce);
	related_skills.insertMulti("xianfu", "#xianfu-target");
	related_skills.insertMulti("chouce", "#chouce-judge");

	General*sunqian = new General(this, "sunqian*xh_sibi", "shu", 3);
	sunqian->addSkill(new Qianya);
	sunqian->addSkill(new Shuomeng);

	General*wangcan = new General(this, "wangcan*xh_sibi", "qun", 3);
	wangcan->addSkill(new Sanwen);
	wangcan->addSkill(new Qiai);
	wangcan->addSkill(new Denglou);
	addMetaObject<DenglouCard>();

	General*lvqian = new General(this, "lvqian*xh_sibi", "wei", 4);
	lvqian->addSkill(new Weilu);
	lvqian->addSkill(new Zengdao);
	addMetaObject<ZengdaoCard>();
	addMetaObject<ZengdaoRemoveCard>();

	General*shenpei = new General(this, "shenpei*xh_sibi", "qun", 3);
	shenpei->addSkill(new Gangzhi);
	shenpei->addSkill(new Beizhan);
	shenpei->addSkill(new BeizhanPro);
	related_skills.insertMulti("beizhan", "#beizhan-prohibit");

	General*xunchen = new General(this, "xunchen*xh_sibi", "qun", 3);
	xunchen->addSkill(new Fenglve);
	xunchen->addSkill(new Moushi);
	addMetaObject<FenglveCard>();
	addMetaObject<MoushiCard>();


	General*sunshao = new General(this, "sunshao*xh_sibi", "wu", 3);
	sunshao->addSkill(new Bizheng);
	sunshao->addSkill(new Yidian);


	General*yanjun = new General(this, "yanjun*xh_sibi", "wu", 3);
	yanjun->addSkill(new Guanchao);
	yanjun->addSkill(new Xunxian);

	General*panjun = new General(this, "panjun*xh_sibi", "wu", 3);
	panjun->addSkill(new Guanwei);
	panjun->addSkill(new Gongqing);

	General*duxi = new General(this, "duxi*xh_sibi", "wei", 3);
	duxi->addSkill(new Quxi);
	duxi->addSkill(new QuxiDraw);
	duxi->addSkill(new Bixiong);
	duxi->addSkill(new BixiongClear);
	duxi->addSkill(new BixiongProhibit);
	related_skills.insertMulti("quxi", "#quxi");
	related_skills.insertMulti("bixiong", "#bixiong-clear");
	related_skills.insertMulti("bixiong", "#bixiong-prohibit");
	addMetaObject<QuxiCard>();

	General*ol_wangyun = new General(this, "ol_wangyun*xh_sibi", "qun", 4);
	ol_wangyun->addSkill(new OLLianji);
	ol_wangyun->addSkill(new OLMoucheng);
	ol_wangyun->addSkill(new OLMouchengUse);
	ol_wangyun->addRelateSkill("tenyearjingong");
	related_skills.insertMulti("olmoucheng", "#olmoucheng-use");
	addMetaObject<OLLianjiCard>();

	General*second_ol_wangyun = new General(this, "second_ol_wangyun", "qun", 4);
	second_ol_wangyun->addSkill("ollianji");
	second_ol_wangyun->addSkill(new SecondOLMoucheng);
	second_ol_wangyun->addSkill(new SecondOLMouchengDamage);
	second_ol_wangyun->addRelateSkill("tenyearjingong");
	related_skills.insertMulti("secondolmoucheng", "#secondolmoucheng-damage");

	General*ol_yangyi = new General(this, "ol_yangyi*xh_sibi", "shu", 3);
	ol_yangyi->addSkill(new Juanxia("juanxia"));
	ol_yangyi->addSkill(new JuanxiaSlash("juanxia"));
	ol_yangyi->addSkill(new Dingcuo);
	related_skills.insertMulti("juanxia", "#juanxia-slash");
	skills << new Juanxia("tenyearjuanxia") << new JuanxiaSlash("tenyearjuanxia");
	related_skills.insertMulti("tenyearjuanxia", "#tenyearjuanxia-slash");

	General*xinpi = new General(this, "xinpi*xh_sibi", "wei", 3);
	xinpi->addSkill(new Chijie);
	xinpi->addSkill(new Yinju);
	addMetaObject<YinjuCard>();

	General*ol_chendeng = new General(this, "ol_chendeng*xh_sibi", "qun", 4);
	ol_chendeng->addSkill(new OLFengji);
	ol_chendeng->addSkill(new OLFengjiDraw);
	related_skills.insertMulti("olfengji", "#olfengji-draw");

	General*jin_yanghu = new General(this, "jin_yanghu*xh_sibi", "jin", 4);
	jin_yanghu->addSkill(new JinHuaiyuan);
	jin_yanghu->addSkill(new JinHuaiyuanDeath);
	jin_yanghu->addSkill(new JinHuaiyuanKeep);
	jin_yanghu->addSkill(new JinHuaiyuanAttack);
	jin_yanghu->addSkill(new JinChongxin);
	jin_yanghu->addSkill(new JinDezhang);
	related_skills.insertMulti("jinhuaiyuan", "#jinhuaiyuan");
	related_skills.insertMulti("jinhuaiyuan", "#jinhuaiyuan-keep");
	related_skills.insertMulti("jinhuaiyuan", "#jinhuaiyuan-attack");
	addMetaObject<JinChongxinCard>();
	skills << new JinWeishu;

	General*dongzhao = new General(this, "dongzhao*xh_sibi", "wei", 3);
	dongzhao->addSkill(new Xianlve);
	dongzhao->addSkill(new XianlveEffect);
	dongzhao->addSkill(new Zaowang);
	related_skills.insertMulti("xianlve", "#xianlve-effect");
	addMetaObject<ZaowangCard>();


	General*jin_zhanghua = new General(this, "jin_zhanghua*xh_sibi", "jin", 3);
	jin_zhanghua->addSkill(new JinBihun);
	jin_zhanghua->addSkill(new JinJianhe);
	jin_zhanghua->addSkill(new JinChuanwu);
	jin_zhanghua->addSkill(new JinChuanwuSkill);
	addMetaObject<JinJianheCard>();

	General*sp_panfeng = new General(this, "sp_panfeng*xh_tianzhu", "qun", 4, true); // SP 029
	sp_panfeng->addSkill("kuangfu");

	General*ol_zhugedan = new General(this, "ol_zhugedan*xh_tianzhu", "wei", 4);
	ol_zhugedan->addSkill("gongao");
	ol_zhugedan->addSkill(new OLJuyi);
	skills << new OLWeizhong;

	General*yanbaihu = new General(this, "yanbaihu*xh_tianzhu", "qun", 4);
	yanbaihu->addSkill(new Zhidao);
	yanbaihu->addSkill(new ZhidaoPro);
	yanbaihu->addSkill(new SpJili);
	related_skills.insertMulti("zhidao", "#zhidao-pro");


	General*ol_fanchou = new General(this, "ol_fanchou*xh_tianzhu", "qun", 4);
	ol_fanchou->addSkill(new Xingluan("olxingluan"));

	General*ol_tadun = new General(this, "ol_tadun*xh_tianzhu", "qun", 4);
	ol_tadun->addSkill(new OLLuanzhan);
	ol_tadun->addSkill(new OLLuanzhanTargetMod);
	related_skills.insertMulti("olluanzhan", "#olluanzhan-target");
	related_skills.insertMulti("olluanzhan", "#luanzhan-mark");
	addMetaObject<OLLuanzhanCard>();
	ol_tadun->addSkill("#luanzhan-mark");

	General*wutugu = new General(this, "wutugu*xh_tianzhu", "qun", 15);
	wutugu->addSkill(new Ranshang);
	wutugu->addSkill(new Hanyong);


	General*huangzu = new General(this, "huangzu*xh_tianzhu", "qun", 4);
	huangzu->addSkill(new Wangong);
	huangzu->addSkill(new WangongRecord);
	related_skills.insertMulti("wangong", "#wangong-record");

	General*gaogan = new General(this, "gaogan*xh_tianzhu", "qun", 4);
	gaogan->addSkill(new Juguan);
	addMetaObject<JuguanCard>();


	General*fanjiangzhangda = new General(this, "fanjiangzhangda*xh_tianzhu", "wu", 4);
	fanjiangzhangda->addSkill(new Yuanchou);
	fanjiangzhangda->addSkill(new Juesheng);


	General*ahuinan = new General(this, "ahuinan*xh_tianzhu", "qun", 4);
	ahuinan->addSkill(new Jueman);

	General*sp_lingju = new General(this, "sp_lingju", "qun", 3, false);
	sp_lingju->addSkill("jieyuan");
	sp_lingju->addSkill(new SpFenxin);

	General*ol_xingcai = new General(this, "ol_xingcai", "shu", 3, false);
	ol_xingcai->addSkill(new OLShenxian);
	ol_xingcai->addSkill("qiangwu");

	General*mayl = new General(this, "mayunlu", "shu", 4, false);
	mayl->addSkill("mashu");
	mayl->addSkill(new Fengpo);

	General*ol_erqiao = new General(this, "ol_erqiao*xh_nvshi", "wu", 3, false);
	ol_erqiao->addSkill(new OLXingwu("olxingwu"));
	ol_erqiao->addSkill("olluoyan");
	addMetaObject<OLXingwuCard>();
	addMetaObject<TenyearXingwuCard>();
	skills << new OLXingwu("tenyearxingwu");

	General*dongbai = new General(this, "dongbai*xh_nvshi", "qun", 3, false);
	dongbai->addSkill(new Lianzhu("lianzhu"));
	dongbai->addSkill(new Xiahui("xiahui"));
	dongbai->addSkill(new XiahuiClear("xiahui"));
	related_skills.insertMulti("xiahui", "#xiahui-clear");
	addMetaObject<LianzhuCard>();
	addMetaObject<TenyearLianzhuCard>();
	skills << new Lianzhu("tenyearlianzhu") << new Xiahui("tenyearxiahui") << new XiahuiClear("tenyearxiahui");
	related_skills.insertMulti("tenyearxiahui", "#tenyearxiahui-clear");

	General*ol_zhaoxiang = new General(this, "ol_zhaoxiang*xh_nvshi", "shu", 4, false);
	ol_zhaoxiang->addSkill("olfanghun");
	ol_zhaoxiang->addSkill("olfuhan");

	General*second_huaman = new General(this, "second_huaman*xh_nvshi", "shu", 3, false);
	second_huaman->addSkill("spmanyi");
	second_huaman->addSkill(new SecondMansi);
	second_huaman->addSkill(new SecondSouying);
	second_huaman->addSkill(new SecondZhanyuan);
	second_huaman->addSkill("#secondzhanyuan");
	second_huaman->addRelateSkill("secondxili");
	related_skills.insertMulti("secondzhanyuan", "#secondzhanyuan");
	addMetaObject<SecondMansiCard>();
	skills << new SecondXili;


	General*jin_guohuai = new General(this, "jin_guohuai*xh_nvshi", "jin", 3, false);
	jin_guohuai->addSkill(new JinZhefu);
	jin_guohuai->addSkill(new JinYidu);

	General*zhanglu = new General(this, "zhanglu*xh_shaowei", "qun", 3);
	zhanglu->addSkill(new Yishe);
	zhanglu->addSkill(new Bushi);
	zhanglu->addSkill(new Midao);
	addMetaObject<MidaoCard>();
	addMetaObject<BushiCard>();

	General*new_zhangbao = new General(this, "new_zhangbao*xh_shaowei", "qun", 3);
	new_zhangbao->addSkill(new NewZhoufu("newzhoufu"));
	new_zhangbao->addSkill(new NewYingbing);
	addMetaObject<NewZhoufuCard>();
	addMetaObject<TenyearZhoufuCard>();
	skills << new NewZhoufu("tenyearzhoufu");

	General*beimihu = new General(this, "beimihu*xh_shaowei", "qun", 3, false);
	beimihu->addSkill(new Zongkui);
	beimihu->addSkill(new Guju);
	beimihu->addSkill(new Baijia);
	beimihu->addSkill(new BaijiaRecord);
	beimihu->addRelateSkill("spcanshi");
	related_skills.insertMulti("baijia", "#baijia");
	addMetaObject<SpCanshiCard>();
	skills << new SpCanshi;

	General*xujing = new General(this, "xujing*xh_shaowei", "shu", 3);
	xujing->addSkill(new Yuxu);
	xujing->addSkill(new Shijian);

	General*zhugeguo = new General(this, "zhugeguo*xh_shaowei", "shu", 3, false);
	zhugeguo->addSkill(new OLYuhua);
	zhugeguo->addSkill(new OLQirang);

	General*simahui = new General(this, "simahui*xh_shaowei", "qun", 3);
	simahui->addSkill(new Jianjie);
	simahui->addSkill(new Chenghao);
	simahui->addSkill(new Yinshi);
	simahui->addRelateSkill("jianjiehuoji");
	simahui->addRelateSkill("jianjielianhuan");
	simahui->addRelateSkill("jianjieyeyan");
	addMetaObject<JianjieCard>();
	addMetaObject<JianjieHuojiCard>();
	addMetaObject<JianjieLianhuanCard>();
	addMetaObject<JianjieYeyanCard>();
	addMetaObject<SmallJianjieYeyanCard>();
	addMetaObject<GreatJianjieYeyanCard>();
	skills << new JianjieHuoji << new JianjieLianhuan << new JianjieYeyan;

	General*zhangling = new General(this, "zhangling*xh_shaowei", "qun", 3);
	zhangling->addSkill(new Huqi);
	zhangling->addSkill(new HuqiDistance);
	zhangling->addSkill(new Shoufu);
	zhangling->addSkill(new ShoufuLimit);
	related_skills.insertMulti("huqi", "#huqi-distance");
	related_skills.insertMulti("shoufu", "#shoufu-limit");
	addMetaObject<ShoufuCard>();
	addMetaObject<ShoufuPutCard>();

	General*huangchengyan = new General(this, "huangchengyan*xh_shaowei", "qun", 3);
	huangchengyan->addSkill(new Guanxu);
	huangchengyan->addSkill(new Yashi);
	huangchengyan->addSkill(new YashiClear);
	huangchengyan->addSkill(new YashiInvalidity);
	related_skills.insertMulti("yashi", "#yashi");
	related_skills.insertMulti("yashi", "#yashi-invalidity");
	addMetaObject<GuanxuCard>();
	addMetaObject<GuanxuChooseCard>();
	addMetaObject<GuanxuDiscardCard>();

	General*caohong = new General(this, "caohong*xh_huben", "wei"); // SP 013
	caohong->addSkill(new Yuanhu);
	addMetaObject<YuanhuCard>();

	General*xiahouba = new General(this, "xiahouba*xh_huben", "shu"); // SP 019
	xiahouba->addSkill(new Baobian);
	xiahouba->addRelateSkill("tiaoxin");
	xiahouba->addRelateSkill("paoxiao");
	xiahouba->addRelateSkill("shensu");

	General*sp_yuejin = new General(this, "sp_yuejin*xh_huben", "wei", 4, true); // SP 024
	sp_yuejin->addSkill("xiaoguo");

	General*lingcao = new General(this, "lingcao*xh_huben", "wu", 4);
	lingcao->addSkill(new Dujin);

	General*ol_dingfeng = new General(this, "ol_dingfeng", "wu", 4);
	ol_dingfeng->addSkill(new OLDuanbing);
	ol_dingfeng->addSkill(new OLFenxun);
	ol_dingfeng->addSkill(new OLFenxunBf);
	related_skills.insertMulti("olfenxun", "#olfenxunbf");
	addMetaObject<OLFenxunCard>();

	General*zumao = new General(this, "zumao*xh_huben", "wu"); // SP 030
	zumao->addSkill(new Yinbing);
	zumao->addSkill(new Juedi);
	addMetaObject<YinbingCard>();

	General*sp_wenpin = new General(this, "sp_wenpin*xh_huben", "wei"); // SP 039
	sp_wenpin->addSkill(new SpZhenwei);

	General*litong = new General(this, "litong*xh_huben", "wei", 4);
	litong->addSkill(new Tuifeng);

	General*zhugeke = new General(this, "zhugeke*xh_huben", "wu", 3); // OL 002
	zhugeke->addSkill(new Aocai);
	zhugeke->addSkill(new Duwu);
	addMetaObject<AocaiCard>();
	addMetaObject<DuwuCard>();

	General*ol_heqi = new General(this, "ol_heqi*xh_huben", "wu", 4);
	ol_heqi->addSkill("olqizhou");
	ol_heqi->addSkill("#olqizhou-lose");
	ol_heqi->addSkill("shanxi");
	ol_heqi->addRelateSkill("olduanbing");
	ol_heqi->addRelateSkill("fenwei");

	General*huangquan = new General(this, "huangquan*xh_huben", "shu", 3);
	huangquan->addSkill(new Dianhu);
	huangquan->addSkill(new DianhuTarget);
	huangquan->addSkill(new Jianji);
	related_skills.insertMulti("dianhu*xh_huben", "#dianhu-target");
	addMetaObject<JianjiCard>();

	General*quyi = new General(this, "quyi*xh_huben", "qun", 4);
	quyi->addSkill(new Fuqi);
	quyi->addSkill(new Jiaozi);

	General*sp_luzhi = new General(this, "sp_luzhi*xh_huben", "wei", 3);
	sp_luzhi->addSkill(new Qingzhong);
	sp_luzhi->addSkill(new Weijing);

	General*tangzi = new General(this, "tangzi*xh_huben", "wei", 4);
	tangzi->addSkill(new Xingzhao("xingzhao"));
	tangzi->addSkill(new XingzhaoXunxun("xingzhao"));
	tangzi->addRelateSkill("xunxun");
	related_skills.insertMulti("xingzhao", "#xingzhao-xunxun");
	related_skills.insertMulti("tenyearxingzhao", "#tenyearxingzhao-xunxun");
	skills << new Xingzhao("tenyearxingzhao") << new XingzhaoXunxun("tenyearxingzhao");

	General*sufei = new General(this, "sufei*xh_huben", "wu", 4);
	sufei->addSkill(new Lianpian);

	General*second_gaolan = new General(this, "second_gaolan*xh_huben", "qun", 4);
	second_gaolan->addSkill(new Xiying("secondxiying"));

	General*gaolan = new General(this, "gaolan*xh_huben", "qun", 4);
	gaolan->addSkill(new Xiying("xiying"));

	General*zhoufang = new General(this, "zhoufang*xh_huben", "wu", 3);
	zhoufang->addSkill(new SpYoudi);
	zhoufang->addSkill(new Duanfa);
	addMetaObject<DuanfaCard>();


	General*lvkuanglvxiang = new General(this, "lvkuanglvxiang*xh_huben", "qun", 4);
	lvkuanglvxiang->addSkill(new Qigong);
	lvkuanglvxiang->addSkill(new Liehou);
	addMetaObject<LiehouCard>();

	General*sp_wuyan = new General(this, "sp_wuyan*xh_huben", "wu", 4);
	sp_wuyan->addSkill(new Lanjiang);

	General*ol_zhuling = new General(this, "ol_zhuling*xh_huben", "wei", 4);
	ol_zhuling->addSkill(new JixianZL);
	ol_zhuling->addSkill(new JixianZLEffect);
	related_skills.insertMulti("jixianzl", "#jixianzl");

	General*tianyu = new General(this, "tianyu*xh_huben", "wei", 4);
	tianyu->addSkill(new Saodi);
	tianyu->addSkill(new Zhuitao);
	tianyu->addSkill(new ZhuitaoDistance);
	related_skills.insertMulti("zhuitao", "#zhuitao");

	General*zhaoyanw = new General(this, "zhaoyanw*xh_huben", "wei", 4);
	zhaoyanw->addSkill(new Tongxie);
	zhaoyanw->addSkill(new TongxieEffect);
	zhaoyanw->addSkill(new TongxieTargetMod);
	related_skills.insertMulti("tongxie", "#tongxie");
	related_skills.insertMulti("tongxie", "#tongxie-target");

	General*dengzhong = new General(this, "dengzhong*xh_huben", "wei", 4);
	dengzhong->addSkill(new KanpoDZ);
	dengzhong->addSkill(new KanpoDZMark);
	dengzhong->addSkill(new Gengzhan);
	dengzhong->addSkill(new GengzhanBuff);

	General*huojun = new General(this, "huojun*xh_huben", "shu", 4);
	huojun->addSkill(new Qiongshou);
	huojun->addSkill(new QiongshouKeep);
	huojun->addSkill(new Fenrui);


	General*ol_furong = new General(this, "ol_furong*xh_huben", "shu", 4);
	ol_furong->addSkill(new Xiaosi);
	ol_furong->addSkill(new XiaosiTargetMod);
	addMetaObject<XiaosiCard>();

	General*luoxian = new General(this, "luoxian*xh_huben", "shu", 4);
	luoxian->addSkill(new Daili);
	luoxian->addSkill(new DailiRecord);

	General*mizhu = new General(this, "mizhu*leisi", "shu", 3);
	mizhu->addSkill(new Ziyuan);
	mizhu->addSkill(new Jugu);
	mizhu->addSkill(new JuguMax);
	related_skills.insertMulti("jugu", "#jugu-max");
	addMetaObject<ZiyuanCard>();

	General*weizi = new General(this, "weizi*leisi", "qun", 3);
	weizi->addSkill(new Yuanzi);
	weizi->addSkill(new YuanziDraw);
	weizi->addSkill(new Liejie);
	related_skills.insertMulti("yuanzi", "#yuanzi");

	General*zhangshiping = new General(this, "zhangshiping*leisi", "shu", 3);
	zhangshiping->addSkill(new Hongji);
	zhangshiping->addSkill(new Xinggu);
	addMetaObject<XingguCard>();

	General*ol_huban = new General(this, "ol_huban", "wei");
	ol_huban->addSkill(new Huiyun);

	General*caoyu = new General(this, "caoyu", "wei", 3);
	caoyu->addSkill(new Gongjie);
	caoyu->addSkill(new Xiangxu);
	caoyu->addSkill(new Xiangzuo);

	General*wangguan = new General(this, "wangguan", "wei", 3);
	wangguan->addSkill(new Miuyan);
	wangguan->addSkill(new Shilu);

	General*ol_guotu = new General(this, "ol_guotu", "qun", 3);
	ol_guotu->addSkill(new Qushi);
	ol_guotu->addSkill(new Weijie);
	addMetaObject<QushiCard>();
	addMetaObject<WeijieCard>();

	General*ol_lukai = new General(this, "ol_lukai", "wu", 3);
	ol_lukai->addSkill(new Xuanzhu);
	ol_lukai->addSkill(new Jiane);
	ol_lukai->addSkill(new JianePro);
	addMetaObject<XuanzhuCard>();
	addMetaObject<Xuanzhu2Card>();

	General*ol_liyi = new General(this, "ol_liyi", "wu", 4);
	ol_liyi->addSkill(new Chanshuang);
	ol_liyi->addSkill(new Zhanjin);
	addMetaObject<ChanshuangCard>();

	General*ol_liupi = new General(this, "ol_liupi", "qun", 4);
	ol_liupi->addSkill(new olYicheng);
	addMetaObject<olYichengCard>();
	addMetaObject<olYicheng2Card>();

	General*yadan = new General(this, "yadan", "qun", 4);
	yadan->addSkill(new Qingya);
	yadan->addSkill(new Tielun);
	yadan->addSkill(new TielunDist);

	General*macheng = new General(this, "macheng", "qun", 4);
	macheng->addSkill("mashu");
	macheng->addSkill(new Chenglie);
	macheng->addSkill(new ChenglieMod);
	addMetaObject<ChenglieCard>();

	General*ol_hujinding = new General(this, "ol_hujinding", "shu", 3, false);
	ol_hujinding->addSkill(new Qingyuan);
	ol_hujinding->addSkill(new Zhongshen);

	General*ol_luyusheng = new General(this, "ol_luyusheng", "wu", 3, false);
	ol_luyusheng->addSkill(new Changxin);
	ol_luyusheng->addSkill(new Runwei);

	General*ol_liwan = new General(this, "ol_liwan", "wei", 3, false);
	ol_liwan->addSkill(new Lianju);
	ol_liwan->addSkill(new Shilv);

	General*ol_qianzhao = new General(this, "ol_qianzhao", "wei", 4);
	ol_qianzhao->addSkill(new Weifu);
	ol_qianzhao->addSkill(new WeifuMod);
	ol_qianzhao->addSkill(new Kuansai);
	addMetaObject<WeifuCard>();

	General*liupan = new General(this, "liupan", "qun", 4);
	liupan->addSkill(new Pijing);

	General*caimao = new General(this, "caimao", "wei", 4);
	caimao->addSkill(new Zuolian);
	caimao->addSkill(new Jingzhou);
	addMetaObject<ZuolianCard>();

	General*lvboshe = new General(this, "lvboshe", "qun", 4);
	lvboshe->addSkill(new Fushi);
	lvboshe->addSkill(new Dongdao);
	addMetaObject<FushiCard>();

	General*dongtuna = new General(this, "dongtuna", "qun", 4);
	dongtuna->addSkill(new Jianman);

	General*ol_peixiu = new General(this, "ol_peixiu", "wei", 4);
	ol_peixiu->addSkill(new Maozhu);
	ol_peixiu->addSkill(new MaozhuMax);
	ol_peixiu->addSkill(new Jinlan);
	addMetaObject<JinlanCard>();

	General*mawan = new General(this, "mawan", "qun", 4);
	mawan->addSkill("mashu");
	mawan->addSkill(new Hunjiang);
	addMetaObject<HunjiangCard>();

	General*budugen = new General(this, "budugen", "qun", 4);
	budugen->addSkill(new Kouchao);
	addMetaObject<KouchaoCard>();

	General*caoteng = new General(this, "caoteng", "qun", 3);
	caoteng->addSkill(new Yongzu);
	caoteng->addSkill(new Qingliu);

	General*ol_sunru = new General(this, "ol_sunru", "wu", 3, false);
	ol_sunru->addSkill(new Chishi);
	ol_sunru->addSkill(new Weimian);
	addMetaObject<WeimianCard>();

	General*lvkai = new General(this, "lvkai*xh_sibi", "shu", 3);
	lvkai->addSkill(new Tunan);
	lvkai->addSkill(new Bijing);
	addMetaObject<TunanCard>();

	General*ol_yuanji = new General(this, "ol_yuanji", "wu", 3,false);
	ol_yuanji->addSkill(new Jieyan);
	ol_yuanji->addSkill(new Jinghua);
	ol_yuanji->addSkill(new Shuiyue);

	General*ol_kebineng = new General(this, "ol_kebineng", "qun", 4);
	ol_kebineng->addSkill(new Pingduan);
	addMetaObject<PingduanCard>();

	General*kongshu = new General(this, "kongshu", "qun", 3,false);
	kongshu->addSkill(new Leiluan);
	kongshu->addSkill(new Fuchao);

	General*wangkuang = new General(this, "wangkuang", "qun", 4);
	wangkuang->addSkill(new Renxian);
	addMetaObject<RenxianCard>();

	General*ol_dongjie = new General(this, "ol_dongjie", "qun", 5,false,false,false,3);
	ol_dongjie->addSkill(new Jiaowei);
	ol_dongjie->addSkill(new JiaoweiLimit);
	ol_dongjie->addSkill(new Bianyu);
	ol_dongjie->addSkill(new OLFengyao);

	General*chenggongying = new General(this, "chenggongying", "qun", 4);
	chenggongying->addSkill(new Kuangxiang);

	General*ol_niufu = new General(this, "ol_niufu", "qun", 4);
	ol_niufu->addSkill(new Shisuan);
	ol_niufu->addSkill(new Zonglve);

	General*ol_hanfu = new General(this, "ol_hanfu", "qun", 4);
	ol_hanfu->addSkill(new Shuzi);
	ol_hanfu->addSkill(new Kuangshou);
	addMetaObject<ShuziCard>();

	General*ol_qinlang = new General(this, "ol_qinlang", "wei", 3);
	ol_qinlang->addSkill(new Xianying);
	addMetaObject<XianyingCard>();

	General*ol_wuanguo = new General(this, "ol_wuanguo", "qun", 4);
	ol_wuanguo->addSkill(new OLLiyong);
	addMetaObject<OLLiyongCard>();

	General*ol_taoqian = new General(this, "ol_taoqian", "qun", 3);
	ol_taoqian->addSkill(new OLZhaohuo);
	ol_taoqian->addSkill(new Wenren);
	ol_taoqian->addSkill(new Zongluan);
	addMetaObject<WenrenCard>();

	General*ol_xingdaorong = new General(this, "ol_xingdaorong", "qun", 4);
	ol_xingdaorong->addSkill(new OLXuhe);

	General*ol_guozhao = new General(this, "ol_guozhao", "wei", 3, false);
	ol_guozhao->addSkill(new Jiaoyu);
	ol_guozhao->addSkill(new Neixun);

	General*ol_xuelingyun = new General(this, "ol_xuelingyun", "wei", 3, false);
	ol_xuelingyun->addSkill(new Siqi);
	ol_xuelingyun->addSkill(new Qiaozhi);
	addMetaObject<SiqiCard>();
	addMetaObject<QiaozhiCard>();

	General*ol_liuzhang = new General(this, "ol_liuzhang", "qun", 3);
	ol_liuzhang->addSkill(new Fengwei);
	ol_liuzhang->addSkill(new Zonghu);
	addMetaObject<ZonghuCard>();

	General*ol_yuanhuan = new General(this, "ol_yuanhuan", "qun", 3);
	ol_yuanhuan->addSkill(new Deru);
	ol_yuanhuan->addSkill(new Linjie);
	addMetaObject<DeruCard>();

	General*ol_yangfeng = new General(this, "ol_yangfeng", "qun", 4);
	ol_yangfeng->addSkill(new Jiawei);
	addMetaObject<JiaweiCard>();

	General*ol_peiyuanshao = new General(this, "ol_peiyuanshao", "qun", 4);
	ol_peiyuanshao->addSkill(new Fulve);

	General*ol_zhaozhong = new General(this, "ol_zhaozhong", "qun", 3);
	ol_zhaozhong->addSkill(new Pengbi);
	ol_zhaozhong->addSkill(new Dici);
	addMetaObject<DiciCard>();
	skills << new Yintian << new Biri;

	General*ol_yangfu = new General(this, "ol_yangfu", "wei", 3);
	ol_yangfu->addSkill(new Pingzhong);
	ol_yangfu->addSkill(new Suyi);
	addMetaObject<SuyiCard>();

	General*ol_guanhai = new General(this, "ol_guanhai", "qun", 4);
	ol_guanhai->addSkill(new Xiewei);
	ol_guanhai->addSkill(new Youque);
	addMetaObject<XieweiCard>();
	addMetaObject<YouqueCard>();

	General*ol_zhangmancheng = new General(this, "ol_zhangmancheng", "qun", 4);
	ol_zhangmancheng->addSkill(new Kuangxin);
	ol_zhangmancheng->addSkill(new Leishi);

	General*fanshi = new General(this, "fanshi", "qun", 3, false);
	fanshi->addSkill(new Zhuole);
	fanshi->addSkill(new Qiaowu);

	General*ol_lifeng = new General(this, "ol_lifeng", "shu", 3);
	ol_lifeng->addSkill(new Jiyun);
	ol_lifeng->addSkill(new OLShuliang);
	addMetaObject<OLShuliangCard>();

	General*qiaoxuan = new General(this, "qiaoxuan", "qun", 3);
	qiaoxuan->addSkill(new Tingji);
	qiaoxuan->addSkill(new Xuanliu);

	General*sunyuan = new General(this, "sunyuan", "wu", 3, false);
	sunyuan->addSkill(new Shaying);
	sunyuan->addSkill(new Dongxin);

	General*huanshujun = new General(this, "huanshujun", "wei", 3, false);
	huanshujun->addSkill(new Huanpei);
	huanshujun->addSkill(new Wenyi);
	huanshujun->addSkill(new Miaoxin);
	addMetaObject<HuanpeiCard>();

}
ADD_PACKAGE(OLCcxh)

OLDemonPackage::OLDemonPackage()
	: Package("ol_demon")
{
	General*olmo_lvbu = new General(this, "olmo_lvbu", "qun", 4);
	olmo_lvbu->addSkill(new Duoqi);
	olmo_lvbu->addSkill(new Kuanmo);
	addMetaObject<KuanmoCard>();
	olmo_lvbu->addSkill(new Gangqian);
	addMetaObject<GangqianCard>();

	General*olmo_diaochan = new General(this, "olmo_diaochan", "qun", 3,false);
	olmo_diaochan->addSkill(new Huanhuo);
	addMetaObject<HuanhuoCard>();
	olmo_diaochan->addSkill(new OLQingshi);
	addMetaObject<OLQingshiCard>();

	General*olmo_caocao = new General(this, "olmo_caocao", "wei", 4);
	olmo_caocao->addSkill(new Bachao);
	olmo_caocao->addSkill(new Fuzhai);
	olmo_caocao->addSkill(new FuzhaiBf);

	General*olmo_sunquan = new General(this, "olmo_sunquan", "wu", 4);
	olmo_sunquan->addSkill(new Quanyu);
	olmo_sunquan->addSkill(new Tianen);
	olmo_sunquan->addSkill(new Qiangang);
	addMetaObject<QiangangCard>();


}
ADD_PACKAGE(OLDemon)

OLMouPackage::OLMouPackage()
: Package("ol_mou")
{
	General*olmou_pangtong = new General(this, "olmou_pangtong", "shu", 3);
	olmou_pangtong->addSkill(new Hongtu);
	olmou_pangtong->addSkill(new HongtuMax);
	olmou_pangtong->addSkill(new Xiwu);
	addMetaObject<HongtuCard>();

	General*olmou_kongrong = new General(this, "olmou_kongrong", "qun", 4);
	olmou_kongrong->addSkill(new Liwen);
	olmou_kongrong->addSkill(new Zhengyi);

	General*olmou_yuanshu = new General(this, "olmou_yuanshu$", "qun", 4);
	olmou_yuanshu->addSkill(new Jinming);
	olmou_yuanshu->addSkill(new Xiaoshi);
	olmou_yuanshu->addSkill(new Yanliang);
	addMetaObject<YanliangCard>();
	skills << new YanliangVs;

	General*olmou_huaxiong = new General(this, "olmou_huaxiong", "qun", 6);
	olmou_huaxiong->addSkill(new Bojue);
	olmou_huaxiong->addSkill(new OLYangwei);
	addMetaObject<BojueCard>();

	General*olmou_dongzhuo = new General(this, "olmou_dongzhuo$", "qun", 5);
	olmou_dongzhuo->addSkill(new Guanbian);
	olmou_dongzhuo->addSkill(new GuanbianMax);
	olmou_dongzhuo->addSkill(new GuanbianDist);
	olmou_dongzhuo->addSkill(new Xiongni);
	olmou_dongzhuo->addSkill(new Fengshang);
	olmou_dongzhuo->addSkill(new Zhibing);
	addMetaObject<FengshangCard>();

	General*olmou_dengai = new General(this, "olmou_dengai", "wei", 4);
	olmou_dengai->addSkill(new Jigu);
	olmou_dengai->addSkill(new Jiewan);
	addMetaObject<JiguCard>();
	addMetaObject<JiewanCard>();

	General*olmou_jvshou = new General(this, "olmou_jvshou", "qun", 3);
	olmou_jvshou->addSkill(new Guliang);
	olmou_jvshou->addSkill(new Xutu);

	General*olmou_zhangfei = new General(this, "olmou_zhangfei", "shu", 4);
	olmou_zhangfei->addSkill(new Jingxian);
	olmou_zhangfei->addSkill(new Xiayong);
	addMetaObject<JingxianCard>();
	addMetaObject<XiayongCard>();

	General*olmou_zhaoyun = new General(this, "olmou_zhaoyun", "shu", 4);
	olmou_zhaoyun->addSkill(new Nilan);
	olmou_zhaoyun->addSkill(new Jueya);

	General*olmou_huangyueying = new General(this, "olmou_huangyueying", "shu", 3, false);
	olmou_huangyueying->addSkill(new Bingcai);
	olmou_huangyueying->addSkill(new Lixian);
	olmou_huangyueying->addSkill(new LixianLimit);

	General*olmou_zhangxiu = new General(this, "olmou_zhangxiu", "qun", 4);
	olmou_zhangxiu->addSkill(new Choulie);
	olmou_zhangxiu->addSkill(new Zhuijiao);

	General*olmou_wenchou = new General(this, "olmou_wenchou", "qun", 4);
	olmou_wenchou->addSkill(new Lunzhan);
	olmou_wenchou->addSkill(new OLJuejue);
	addMetaObject<LunzhanCard>();

	General*olmou_zhangrang = new General(this, "olmou_zhangrang", "qun", 3);
	olmou_zhangrang->addSkill(new Lucun);
	olmou_zhangrang->addSkill(new Tuisheng);
	addMetaObject<LucunCard>();

	General*olmou_luzhi = new General(this, "olmou_luzhi", "qun", 4);
	olmou_luzhi->addSkill(new Sibing);
	olmou_luzhi->addSkill(new Liance);
	addMetaObject<SibingCard>();

	General*olmou_xiaoqiao = new General(this, "olmou_xiaoqiao", "wu", 3,false);
	olmou_xiaoqiao->addSkill(new Miluo);
	olmou_xiaoqiao->addSkill(new OLJueyan);
	addMetaObject<MiluoCard>();
	addMetaObject<OLJueyanCard>();

	General*olmou_zhugeliang = new General(this, "olmou_zhugeliang", "shu", 3);
	olmou_zhugeliang->addSkill(new Zhitian);
	olmou_zhugeliang->addSkill(new Wujing);
	olmou_zhugeliang->addSkill(new Zhijue);
	addMetaObject<ZhitianCard>();
	addMetaObject<ZhijueCard>();

	General*olmou_lusu = new General(this, "olmou_lusu", "wu", 3);
	olmou_lusu->addSkill(new OLDuoduan);
	olmou_lusu->addSkill(new Yinglve);
	olmou_lusu->addSkill(new Mengshi);
	addMetaObject<YinglveCard>();
	addMetaObject<MengshiCard>();

	General*olmou_guojia = new General(this, "olmou_guojia", "wei", 3);
	olmou_guojia->addSkill(new Dinglun);
	olmou_guojia->addSkill(new Jieli);
	addMetaObject<DinglunCard>();
	skills << new OLQuxi;

	General*olmou_xizhicai = new General(this, "olmou_xizhicai", "wei", 3);
	olmou_xizhicai->addSkill(new Xinchuan);
	olmou_xizhicai->addSkill(new OLJinjin);
	addMetaObject<OLJinjinCard>();

	General*olmou_xuyou = new General(this, "olmou_xuyou", "qun", 3);
	olmou_xuyou->addSkill(new Qianfu);
	olmou_xuyou->addSkill(new Yushi);
	olmou_xuyou->addSkill(new Fenchao);


}
ADD_PACKAGE(OLMou)



class Chuyuan : public MasochismSkill
{
public:
	Chuyuan() : MasochismSkill("chuyuan")
	{
		frequency = Frequent;
	}

	bool triggerable(const ServerPlayer*target) const
	{
		return target && target->isAlive();
	}

	void onDamaged(ServerPlayer*player, const DamageStruct &) const
	{
		Room*room = player->getRoom();
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (player->isDead()) return;
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			if (!p->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
			room->broadcastSkillInvoke(objectName());
			player->drawCards(1, objectName());
			if (player->isDead()) return;
			if (player->isKongcheng()) continue;
			const Card*c = room->askForExchange(player, objectName(), 1, 1, false, "@chuyuan-put:" + p->objectName());
			p->addToPile("cychu", c);
		}
	}
};

class ChuyuanKeep : public MaxCardsSkill
{
public:
	ChuyuanKeep() : MaxCardsSkill("#chuyuan-keep")
	{
		frequency = Frequent;
	}

	int getExtra(const Player*target) const
	{
		if (target->hasSkill("chuyuan"))
			return target->getPile("cychu").length();
		return 0;
	}
};

class Dengji : public PhaseChangeSkill
{
public:
	Dengji() : PhaseChangeSkill("dengji")
	{
		frequency = Wake;
		waked_skills = "tianxing";
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*target, Room*room) const
	{
		if (target->getPile("cychu").length() >= 3){
			LogMessage log;
			log.type = "#DengjiWake";
			log.from = target;
			log.arg = QString::number(target->getPile("cychu").length());
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!target->canWake("dengji"))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(target, objectName());
		room->doSuperLightbox(target, "dengji");

		room->setPlayerMark(target, "dengji", 1);
		if (room->changeMaxHpForAwakenSkill(target, -1, objectName())){
			LogMessage log;
			log.type = "$KuangbiGet";
			log.from = target;
			log.arg = "cychu";
			log.card_str = ListI2S(target->getPile("cychu")).join("+");
			room->sendLog(log);
			DummyCard*dummy = new DummyCard(target->getPile("cychu"));
			room->obtainCard(target, dummy);
			delete dummy;
			room->handleAcquireDetachSkills(target, "tenyearjianxiong|tianxing");
		}
		return false;
	}
};

class Tianxing : public PhaseChangeSkill
{
public:
	Tianxing() : PhaseChangeSkill("tianxing")
	{
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer*player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer*target, Room*room) const
	{
		if (target->getPile("cychu").length() >= 3){
			LogMessage log;
			log.type = "#DengjiWake";
			log.from = target;
			log.arg = QString::number(target->getPile("cychu").length());
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!target->canWake("tianxing"))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(target, objectName());
		room->doSuperLightbox(target, "tianxing");

		room->setPlayerMark(target, "tianxing", 1);
		if (room->changeMaxHpForAwakenSkill(target, -1, objectName())){
			LogMessage log;
			log.type = "$KuangbiGet";
			log.from = target;
			log.arg = "cychu";
			log.card_str = ListI2S(target->getPile("cychu")).join("+");
			room->sendLog(log);
			DummyCard*dummy = new DummyCard(target->getPile("cychu"));
			room->obtainCard(target, dummy);
			delete dummy;
			QStringList skills;
			if (target->hasSkill("chuyuan", true))
				room->handleAcquireDetachSkills(target, "-chuyuan");
			if (!target->hasSkill("tenyearrende", true))
				skills << "tenyearrende";
			if (!target->hasSkill("tenyearzhiheng", true))
				skills << "tenyearzhiheng";
			if (!target->hasSkill("olluanji", true))
				skills << "olluanji";
			if (!target->hasSkill("olfangquan", true))
				skills << "olfangquan";
			if (skills.isEmpty()) return false;
			QString skill = room->askForChoice(target, objectName(), skills.join("+"));
            if (target->hasSkill(skill, true)) return false;
			room->handleAcquireDetachSkills(target, skill);
		}
		return false;
	}
};

class Shenfu : public TriggerSkill
{
public:
	Shenfu() : TriggerSkill("shenfu")
	{
		events << EventPhaseChanging;
	}

	bool trigger(TriggerEvent, Room*room, ServerPlayer*player, QVariant &data) const
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		int hand = player->getHandcardNum();
		if (hand % 2 == 0){
			while (true){
				QList<ServerPlayer*> targets;
				foreach(ServerPlayer*p, room->getAlivePlayers()){
					if (p->getMark("shenfu-Clear") > 0) continue;
					targets << p;
				}
				ServerPlayer*target = room->askForPlayerChosen(player, targets, "shenfu_ou", "@shenfu-ou", true, true);
				if (!target) break;
				room->broadcastSkillInvoke(objectName());
				room->addPlayerMark(target, "shenfu-Clear");
				QStringList choices;
				choices << "draw";
				if (player->canDiscard(target, "h"))
					choices << "discard";
				QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target));
				if (choice == "draw")
					target->drawCards(1, objectName());
				else {
					int id = room->askForCardChosen(player, target, "h", objectName(), false, Card::MethodDiscard);
					room->throwCard(id, target, player);
				}
				if (player->isDead() || target->getHandcardNum() != target->getHp()) break;
			}
		} else {
			while (true){
				ServerPlayer*target = room->askForPlayerChosen(player, room->getOtherPlayers(player), "shenfu_ji", "@shenfu-ji", true, true);
				if (!target) break;
				room->broadcastSkillInvoke(objectName());
				room->damage(DamageStruct("shenfu", player, target, 1, DamageStruct::Thunder));
				if (target->isAlive() || target->getDeathReason() != objectName()) break;
			}
		}
		return false;
	}
};

QiexieWeapon::QiexieWeapon(Card::Suit suit, int number)
    : Weapon(suit, number, 1)
{
}

void QiexieWeapon::onInstall(ServerPlayer *player) const
{
    Weapon::onInstall(player);

    Room *room = player->getRoom();
    QString weapon_name = objectName();
    QString tag_key = weapon_name;

    QVariantMap weapon_data = room->getTag(tag_key).toMap();

    if (weapon_data.isEmpty()) {
        return;
    }

    int range = weapon_data["range"].toInt();
    const_cast<QiexieWeapon *>(this)->setRange(range);
    const_cast<QiexieWeapon *>(this)->setSkillName("qiexie");

    QStringList skills = weapon_data["skills"].toStringList();
    QString general_name = weapon_data["general"].toString();

    Weapon *w = Sanguosha->findChild<Weapon*>(weapon_name);
    if (w) w->setRange(range);

    QString skill_names_str;
    QString skill_descs_str;

    foreach (const QString &skill_name, skills) {
        if (!skill_names_str.isEmpty()) {
            skill_names_str += "|";
            skill_descs_str += "|";
        }
        skill_names_str += skill_name;
        skill_descs_str += ":" + skill_name;
    }

    QVariantMap placeholders;
    placeholders["%src"] = QString::number(range);
    placeholders["%skill_name%"] = skill_names_str;
    placeholders["%skill_desc%"] = skill_descs_str;
    placeholders["%general_name%"] = general_name;
    room->updateCardDescription(weapon_name, placeholders);

    foreach (const QString &skill_name, skills) {
        room->acquireSkill(player, skill_name);
    }

    int weapon_id = getEffectiveId();
    QString player_tag_key = QString("qiexie_equipped_skills_%1").arg(weapon_id);
    player->setTag(player_tag_key, skills);
}

void QiexieWeapon::onUninstall(ServerPlayer *player) const
{
    Room *room = player->getRoom();

    QString weapon_name = objectName();
    int weapon_id = getEffectiveId();

    QString player_tag_key = QString("qiexie_equipped_skills_%1").arg(weapon_id);
    QStringList equipped_skills = player->getTag(player_tag_key).toStringList();

    if (!equipped_skills.isEmpty()) {
        foreach (const QString &skill_name, equipped_skills) {
            room->detachSkillFromPlayer(player, skill_name);
        }
        player->setTag(player_tag_key, QVariant());
    } else {
        QVariantMap weapon_data = room->getTag(weapon_name).toMap();
        if (!weapon_data.isEmpty()) {
            QStringList skills = weapon_data["skills"].toStringList();
            foreach (const QString &skill_name, skills) {
                room->detachSkillFromPlayer(player, skill_name);
            }
        }
    }

    Weapon::onUninstall(player);
}

class Qixian : public MaxCardsSkill
{
public:
	Qixian() : MaxCardsSkill("qixian")
	{
	}

	int getFixed(const Player*target) const
	{
		if (target->hasSkill("qixian"))
			return 7;
		return -1;
	}
};

class Juanjia : public GameStartSkill
{
public:
    Juanjia() : GameStartSkill("juanjia")
    {
        frequency = Compulsory;
    }

    void onGameStart(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        room->sendCompulsoryTriggerLog(player, this);
        player->throwEquipArea(1);
        player->addEquipArea(0);
    }
};

CuijueCard::CuijueCard()
{
}

bool CuijueCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	if(targets.isEmpty()&&to_select->getMark("cuijueTo-Clear")<1){
		int n = 1;
		foreach(const Player*p,Self->getAliveSiblings()){
			if(Self->inMyAttackRange(p))
				n = qMax(n,Self->distanceTo(p));
		}
		return Self->distanceTo(to_select)==n;
	}
	return false;
}

void CuijueCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets){
		room->addPlayerMark(p,"cuijueTo-Clear");
		room->damage(DamageStruct("cuijue",source,p));
	}
}

class Cuijue : public OneCardViewAsSkill
{
public:
	Cuijue() : OneCardViewAsSkill("cuijue")
	{
	}

	bool viewFilter(const Card*to_select) const
	{
		return !Self->isJilei(to_select);
	}

	const Card*viewAs(const Card*original) const
	{
		CuijueCard*dc = new CuijueCard;
		dc->addSubcard(original);
		return dc;
	}

bool isEnabledAtPlay(const Player*player) const
	{
		return player->usedTimes("CuijueCard")<1;
	}
};

class Qiexie : public TriggerSkill
{
public:
    Qiexie() : TriggerSkill("qiexie")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Start)
            return false;

        QList<const General *> all_generals = Sanguosha->findChildren<const General *>();
        QStringList valid_generals;

        foreach (const General *general, all_generals) {
            if (general->isHidden() || general->isTotallyHidden())
                continue;

            QList<const Skill *> skills = general->getVisibleSkillList();
            bool has_slash_skill = false;

            foreach (const Skill *skill, skills) {
                if (skill->getFrequency() == Skill::Wake) continue;
                if (skill->getFrequency() == Skill::Limited) continue;
                if (skill->isLordSkill()) continue;

                QString desc = skill->getDescription();
                if (desc.contains("【杀】")) {
                    has_slash_skill = true;
                    break;
                }
            }

            if (has_slash_skill) {
                valid_generals << general->objectName();
            }
        }

        if (valid_generals.isEmpty())
            return false;

        QStringList selected;
        for (int i = 0; i < 5 && !valid_generals.isEmpty(); ++i) {
            int index = qrand() % valid_generals.size();
            selected << valid_generals.at(index);
            valid_generals.removeAt(index);
        }

        if (selected.isEmpty())
            return false;

        QVariantList weapon_ids_variant = player->getTag("qiexie_weapon_ids").toList();
        QList<int> qiexie_weapon_ids;

        if (weapon_ids_variant.isEmpty()) {
            int weapon1_id = player->getDerivativeCard("_qiexie_weapon1", Player::PlaceTable);
            int weapon2_id = player->getDerivativeCard("_qiexie_weapon2", Player::PlaceTable);

            if (weapon1_id >= 0)
                qiexie_weapon_ids << weapon1_id;
            if (weapon2_id >= 0)
                qiexie_weapon_ids << weapon2_id;

            QVariantList ids_to_save;
            foreach (int id, qiexie_weapon_ids)
                ids_to_save << id;
            player->setTag("qiexie_weapon_ids", ids_to_save);
        } else {
            foreach (const QVariant &v, weapon_ids_variant)
                qiexie_weapon_ids << v.toInt();
        }

        foreach (int weapon_id, qiexie_weapon_ids) {
            const Card *card = Sanguosha->getCard(weapon_id);
            Player::Place current_place = room->getCardPlace(weapon_id);

            if (current_place == Player::PlaceEquip) {
                room->moveCardTo(card, player, NULL, Player::DiscardPile,
                    CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), "qiexie", QString()));
            }
        }

        foreach (int weapon_id, qiexie_weapon_ids) {
            const Card *card = Sanguosha->getCard(weapon_id);
            Player::Place current_place = room->getCardPlace(weapon_id);

            if (current_place != Player::PlaceTable) {
                room->moveCardTo(card, NULL, Player::PlaceTable,
                    CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), "qiexie", QString()));
            }
        }

        for (int i = 0; i < qiexie_weapon_ids.size(); ++i) {
            if (selected.isEmpty())
                break;

            int weapon_id = qiexie_weapon_ids[i];
            const Card *weapon_card = Sanguosha->getCard(weapon_id);
            QString weapon_name = weapon_card->objectName();

            QString choice = room->askForGeneral(player, selected.join("+"), QString(), "qiexie");
            if (choice.isEmpty())
                break;

            selected.removeOne(choice);

            const General *chosen_general = Sanguosha->getGeneral(choice);
            if (!chosen_general)
                continue;

            QStringList weapon_skills;
            foreach (const Skill *skill, chosen_general->getVisibleSkillList()) {
                if (skill->getFrequency() == Skill::Wake) continue;
                if (skill->getFrequency() == Skill::Limited) continue;
                if (skill->isLordSkill()) continue;

                QString desc = skill->getDescription();
                if (desc.contains("【杀】")) {
                    weapon_skills << skill->objectName();
                }
            }

            if (weapon_skills.isEmpty())
                continue;

            int weapon_range = chosen_general->getMaxHp();
            QString tag_key = weapon_name;

            QVariantMap weapon_data;
            weapon_data["range"] = weapon_range;
            weapon_data["skills"] = weapon_skills;
            weapon_data["general"] = choice;
            room->setTag(tag_key, weapon_data);

            CardUseStruct use;
            use.card = weapon_card;
            use.from = player;
            room->useCard(use);
        }

        return false;
    }
};

GodPackage::GodPackage()
	: Package("ol_god")
{
	General*shencaopi = new General(this, "shencaopi", "god", 5);
	shencaopi->addSkill(new Chuyuan);
	shencaopi->addSkill(new ChuyuanKeep);
	shencaopi->addSkill(new Dengji);
	shencaopi->addRelateSkill("tianxing");
	shencaopi->addRelateSkill("tenyearrende");
	shencaopi->addRelateSkill("tenyearzhiheng");
	shencaopi->addRelateSkill("olluanji");
	shencaopi->addRelateSkill("olfangquan");
	related_skills.insertMulti("chuyuan", "#chuyuan-keep");

	General*shenzhenji = new General(this, "shenzhenji", "god", 3, false);
	shenzhenji->addSkill(new Shenfu);
	shenzhenji->addSkill(new Qixian);

	General*shendianwei = new General(this, "shendianwei", "god");
	shendianwei->addSkill(new Juanjia);
	shendianwei->addSkill(new Qiexie);
	shendianwei->addSkill(new Cuijue);
	addMetaObject<CuijueCard>();
	addMetaObject<QiexieWeapon>();

	QiexieWeapon *weapon1 = new QiexieWeapon(Card::NoSuit, 0);
	weapon1->setObjectName("_qiexie_weapon1");
	weapon1->setParent(this);

	QiexieWeapon *weapon2 = new QiexieWeapon(Card::NoSuit, 0);
	weapon2->setObjectName("_qiexie_weapon2");
	weapon2->setParent(this);

	skills << new Tianxing;
}
ADD_PACKAGE(God)
