#include "yjcm2023.h"
#include "engine.h"
#include "maneuvering.h"
#include "wrapped-card.h"
#include "room.h"
#include "clientplayer.h"

static QString zhizheEquipObjectNameByArea(int area)
{
	if (area == 0) return "_zhizhe_weapon";
	if (area == 1) return "_zhizhe_armor";
	if (area == 2) return "_zhizhe_defensivehorse";
	if (area == 3) return "_zhizhe_offensivehorse";
	if (area == 4) return "_zhizhe_treasure";
	return QString();
}

static bool safeTurnCardToEquip(Room *room, ServerPlayer *source, int cardId, const QString &equipObjectName, const QString &skillName)
{
	if (!room || !source || equipObjectName.isEmpty())
		return false;
	if (room->getCardPlace(cardId) != Player::PlaceHand)
		return false;

	const Card *rawCard = Sanguosha->getCard(cardId);
	if (!rawCard)
		return false;

	Card *equipCard = Sanguosha->cloneCard(equipObjectName, rawCard->getSuit(), rawCard->getNumber());
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
	wrapped->takeOver(equipCard);
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




class Zhitu : public TriggerSkill
{
public:
	Zhitu() : TriggerSkill("zhitu")
	{
		events << PreCardUsed;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("BasicCard")||(use.card->isNDTrick()&&use.card->isSingleTargetCard())) {
				if(use.to.length()==1){
					int n = player->distanceTo(use.to.last());
					QList<ServerPlayer *>tos = room->getCardTargets(player,use.card,use.to);
					foreach (ServerPlayer *p, tos){
						if(player->distanceTo(p)!=n)
							tos.removeOne(p);
					}
					room->setTag("zhituData",data);
					tos = room->askForPlayersChosen(player,tos,objectName(),0,9,"zhitu0:"+use.card->objectName(),true,true);
					if(tos.length()>0){
						player->peiyin(this);
						use.to << tos;
						room->sortByActionOrder(use.to);
						data.setValue(use);
					}
				}
			}
		}
		return false;
	}
};

FujueCard::FujueCard()
{
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

class Fujue : public ZeroCardViewAsSkill
{
public:
	Fujue() : ZeroCardViewAsSkill("fujue")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("FujueCard");
	}

	const Card *viewAs() const
	{
		return new FujueCard;
	}
};

GongqiaoCard::GongqiaoCard()
{
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
	QStringList info;
	info << equipObjectName << getSuitString() << QString::number(getNumber());
	room->setTag("ZhizheFilter_" + QString::number(getEffectiveId()), info.join("+"));
	info = room->getTag("gongqiaoEquip").toStringList();
	info << QString::number(getEffectiveId());
	room->setTag("gongqiaoEquip", info);

    foreach (ServerPlayer *q, room->getAlivePlayers())
		room->acquireSkill(q, "#zhizhe");
}

class GongqiaoVs : public OneCardViewAsSkill
{
public:
	GongqiaoVs() : OneCardViewAsSkill("gongqiao")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Card*dc = new GongqiaoCard;
		dc->addSubcard(originalCard);
		return dc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->isKongcheng()&&player->hasEquipArea()
		&&!player->hasUsed("GongqiaoCard");
	}
};

class Gongqiao : public TriggerSkill
{
public:
	Gongqiao() : TriggerSkill("gongqiao")
	{
		events << PreHpRecover << ConfirmDamage << CardFinished << CardsMoveOneTime;
		view_as_skill = new GongqiaoVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==PreHpRecover){
			RecoverStruct recover = data.value<RecoverStruct>();
			if(recover.card&&recover.card->isKindOf("BasicCard")&&player->hasSkill(objectName())){
				foreach (int id, player->getEquipsId()){
					const Card*c = Sanguosha->getEngineCard(id);
					if(c->isKindOf("BasicCard")){
						room->sendCompulsoryTriggerLog(player,objectName());
						recover.recover++;
						data.setValue(recover);
						break;
					}
				}
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceEquip)&&move.to_place!=Player::PlaceEquip&&move.from==player){
				QStringList info = room->getTag("gongqiaoEquip").toStringList();
				if(info.isEmpty()) return false;
				foreach (int id, move.card_ids){
					if(info.contains(QString::number(id))){
						info.removeOne(QString::number(id));
						room->setTag("gongqiaoEquip",info);
						room->removeTag("ZhizheFilter_"+QString::number(id));
					}
				}
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("BasicCard")&&player->hasSkill(objectName())){
				foreach (int id, player->getEquipsId()){
					const Card*c = Sanguosha->getEngineCard(id);
					if(c->isKindOf("BasicCard")){
						room->sendCompulsoryTriggerLog(player,objectName());
						player->damageRevises(data,1);
						break;
					}
				}
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				player->addMark(use.card->getType()+"gongqiaoUse-Clear");
				if(player->getMark(use.card->getType()+"gongqiaoUse-Clear")==1&&player->hasSkill(objectName())){
					foreach (int id, player->getEquipsId()){
						const Card*c = Sanguosha->getEngineCard(id);
						if(c->isKindOf("TrickCard")){
							room->sendCompulsoryTriggerLog(player,objectName());
							player->drawCards(1,objectName());
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

class Jingqiao : public TriggerSkill
{
public:
	Jingqiao() : TriggerSkill("jingqiao")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceEquip&&move.to==player&&player->hasTurn()){
				foreach(int id, move.card_ids){
					const Card*c = Sanguosha->getCard(id);
					int n = qobject_cast<const EquipCard *>(c->getRealCard())->location();
					QString ard = QString("%1jingqiao-Clear").arg(n);
					player->addMark(ard);
					if(player->getMark(ard)==1){
						room->sendCompulsoryTriggerLog(player,this);
						player->drawCards(player->getEquips().length(),objectName());
						room->askForDiscard(player,objectName(),2,2,false,true);
					}
				}
			}
		}
		return false;
	}
};

BeiyuCard::BeiyuCard()
{
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
	//qShuffle(ids);
	room->moveCardsToEndOfDrawpile(source,ids,"beiyu");
}

class Beiyu : public ZeroCardViewAsSkill
{
public:
	Beiyu() : ZeroCardViewAsSkill("beiyu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BeiyuCard")<1;
	}

	const Card *viewAs() const
	{
		return new BeiyuCard;
	}
};

class Duchi : public TriggerSkill
{
public:
	Duchi() :TriggerSkill("duchi")
	{
		events << TargetConfirmed;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetConfirmed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.contains(player)&&player!=use.from&&player->hasTurn()
			&&player->getMark("duchiUse-Clear")<1&&player->askForSkillInvoke(objectName()+"$-1",data)){
				player->addMark("duchiUse-Clear");
				player->drawCards(1,objectName(),false);
				room->showAllCards(player);
				QList<const Card*>hs = player->getHandcards();
				foreach (const Card*c, hs){
					if(c->getColor()!=hs.last()->getColor()) return false;
				}
				use.nullified_list << player->objectName();
				data.setValue(use);
			}
		}
		return false;
	}
};

ThQimeiCard::ThQimeiCard()
{
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

class ThQimei : public ViewAsSkill
{
public:
	ThQimei() : ViewAsSkill("thqimei")
	{
		expand_pile = "#tyqimei";
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
	{
		return cards.isEmpty()&&Self->getPileName(to_select->getId())=="#tyqimei"
		&&to_select->isAvailable(Self);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()){
			if(Sanguosha->getCurrentCardUsePattern().isEmpty())
				return new ThQimeiCard();
			return nullptr;
		}
		return cards.first();
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ThQimeiCard")<1;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("@@thzhuiji");
	}
};

class ThZhuiji : public TriggerSkill
{
public:
	ThZhuiji() :TriggerSkill("thzhuiji")
	{
		events << Death << CardsMoveOneTime;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who==player&&player->hasSkill(objectName())){
				ServerPlayer *tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"thzhuiji0",true,true);
				if(tp){
					player->peiyin(this);
					for (int i = 0; i < 5; i++) {
						if(tp->hasEquipArea(i)&&!tp->getEquip(i)){
							QList<int>ids = room->getDrawPile()+room->getDiscardPile();
							qShuffle(ids);
							foreach (int id, ids){
								const Card*tc = Sanguosha->getCard(id);
								if(tc->isKindOf("EquipCard")){
									const EquipCard *equip = qobject_cast<const EquipCard *>(tc->getRealCard());
									if(equip->location()==i&&tc->isAvailable(tp)){
										tp->addMark(tc->toString()+"thzhuijiEquip");
										room->useCard(CardUseStruct(tc,tp));
										break;
									}
								}
							}
						}
					}
				}
			}
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceHand)&&player==move.from){
				foreach (int id, move.card_ids){
					if(player->getMark(QString::number(id)+"thzhuijiEquip")>0){
						const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
						player->removeMark(equip->toString()+"thzhuijiEquip");
						player->throwEquipArea(equip->location());
					}
				}
			}
		}
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
	: Package("YJCM2023")
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
	th_majun->addSkill(new Jingqiao);
	addMetaObject<GongqiaoCard>();



	/*QList<Card *> cards;
	cards << new Wangmeizhike(Card::NoSuit, 0);
	foreach(Card *card, cards)
		card->setParent(this);*/
}
ADD_PACKAGE(YJCM2023)
